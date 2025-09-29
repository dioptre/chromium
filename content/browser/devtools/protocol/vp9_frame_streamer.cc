// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/protocol/vp9_frame_streamer.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/time/time.h"
#include "media/base/video_codecs.h"
#include "media/base/video_frame.h"
#include "media/base/video_types.h"
#include "media/video/video_encoder_info.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
namespace protocol {

namespace {

constexpr base::TimeDelta kMinFrameInterval = base::Milliseconds(16);  // ~60fps max
constexpr base::TimeDelta kMaxFrameInterval = base::Milliseconds(1000); // 1fps min
// Removed unused variable

// Convert SkBitmap to I420 format for VP9 encoding
scoped_refptr<media::VideoFrame> ConvertBitmapToI420VideoFrame(
    const SkBitmap& bitmap) {
  if (bitmap.isNull() || bitmap.empty()) {
    return nullptr;
  }

  const int width = bitmap.width();
  const int height = bitmap.height();
  
  // Create I420 video frame
  auto video_frame = media::VideoFrame::CreateFrame(
      media::PIXEL_FORMAT_I420, 
      gfx::Size(width, height),
      gfx::Rect(width, height),
      gfx::Size(width, height),
      base::TimeDelta());
  
  if (!video_frame) {
    LOG(ERROR) << "Failed to create video frame";
    return nullptr;
  }

  // Convert BGRA to I420
  const uint8_t* src_bgra = static_cast<const uint8_t*>(bitmap.getPixels());
  int src_stride_bgra = bitmap.rowBytes();
  
  uint8_t* dst_y = video_frame->writable_data(media::VideoFrame::Plane::kY);
  uint8_t* dst_u = video_frame->writable_data(media::VideoFrame::Plane::kU);
  uint8_t* dst_v = video_frame->writable_data(media::VideoFrame::Plane::kV);
  
  int dst_stride_y = video_frame->stride(media::VideoFrame::Plane::kY);
  int dst_stride_u = video_frame->stride(media::VideoFrame::Plane::kU);
  int dst_stride_v = video_frame->stride(media::VideoFrame::Plane::kV);

  // Use libyuv for conversion
  int result = libyuv::ARGBToI420(
      src_bgra, src_stride_bgra,
      dst_y, dst_stride_y,
      dst_u, dst_stride_u,
      dst_v, dst_stride_v,
      width, height);
      
  if (result != 0) {
    LOG(ERROR) << "Failed to convert ARGB to I420";
    return nullptr;
  }

  return video_frame;
}

}  // namespace

VP9FrameStreamer::VP9FrameStreamer(Config config)
    : config_(std::move(config)),
      is_streaming_(false),
      current_frame_number_(0),
      encoder_initialized_(false),
      frame_differencer_(std::make_unique<FrameDifferencer>()) {
  
  // Validate configuration
  config_.fps = std::clamp(config_.fps, 1u, 60u);
  config_.quality = std::clamp(config_.quality, 10u, 100u);
  config_.max_frame_buffer_size = std::clamp(config_.max_frame_buffer_size, 1u, 10u);
  config_.change_threshold = std::clamp(config_.change_threshold, 0.001f, 0.5f);
  
  if (config_.magic_id.empty()) {
    config_.magic_id = "VP9_DEVTOOLS_STREAM";
  }
}

VP9FrameStreamer::~VP9FrameStreamer() {
  StopStreaming();
}

void VP9FrameStreamer::StartStreaming(FrameCallback callback) {
  if (is_streaming_) {
    StopStreaming();
  }
  
  frame_callback_ = std::move(callback);
  is_streaming_ = true;
  current_frame_number_ = 0;
  last_keyframe_time_ = base::TimeTicks::Now();
  
  // Calculate frame interval based on FPS
  base::TimeDelta frame_interval = 
      base::Milliseconds(1000 / std::max(1u, config_.fps));
  frame_interval = std::clamp(frame_interval, kMinFrameInterval, kMaxFrameInterval);
  
  frame_timer_.Start(FROM_HERE, frame_interval,
                    base::BindRepeating(&VP9FrameStreamer::OnFrameTimer,
                                       weak_factory_.GetWeakPtr()));
  
  LOG(INFO) << "VP9FrameStreamer started with " << config_.fps << " FPS";
}

void VP9FrameStreamer::StopStreaming() {
  if (!is_streaming_) {
    return;
  }
  
  frame_timer_.Stop();
  is_streaming_ = false;
  frame_callback_.Reset();
  pending_frame_.reset();
  frame_buffer_.clear();
  
  // Reset encoder
  vp9_encoder_.reset();
  encoder_initialized_ = false;
  
  LOG(INFO) << "VP9FrameStreamer stopped";
}

void VP9FrameStreamer::ProcessFrame(const SkBitmap& bitmap,
                                   const gfx::Size& viewport_size,
                                   float device_scale_factor,
                                   float page_scale_factor,
                                   const gfx::PointF& scroll_offset) {
  if (!is_streaming_ || bitmap.isNull()) {
    return;
  }
  
  // Create captured frame
  CapturedFrame frame;
  frame.bitmap = bitmap;
  frame.metadata = CreateMetadata(bitmap, viewport_size, device_scale_factor,
                                 page_scale_factor, scroll_offset);
  frame.capture_time = base::TimeTicks::Now();
  frame.frame_number = ++current_frame_number_;
  
  // Store as pending frame for timer processing
  pending_frame_ = std::move(frame);
}

bool VP9FrameStreamer::InitializeEncoder(const gfx::Size& frame_size) {
  if (encoder_initialized_ && encoder_frame_size_ == frame_size) {
    return true;
  }
  
  vp9_encoder_.reset();
  encoder_initialized_ = false;
  encoder_frame_size_ = frame_size;
  
  // Create VP9 encoder options
  media::VideoEncoder::Options options;
  options.frame_size = frame_size;
  uint32_t target_bps = 1000000 * config_.quality / 100;
  options.bitrate = media::Bitrate::VariableBitrate(target_bps, target_bps * 2);
  options.framerate = config_.fps;
  options.keyframe_interval = config_.keyframe_interval;
  options.content_hint = media::VideoEncoder::ContentHint::Screen;
  options.latency_mode = media::VideoEncoder::LatencyMode::Realtime;
  
  vp9_encoder_ = std::make_unique<media::VpxVideoEncoder>();
  
  // Initialize encoder
  bool success = false;
  vp9_encoder_->Initialize(
      media::VP9PROFILE_PROFILE0,
      options,
      media::VideoEncoder::EncoderInfoCB(),
      base::DoNothing(),
      base::BindOnce([](bool* result, media::EncoderStatus status) {
        *result = status.is_ok();
      }, &success));
      
  encoder_initialized_ = success;
  
  if (encoder_initialized_) {
    LOG(INFO) << "VP9 encoder initialized: " << frame_size.ToString();
  } else {
    LOG(ERROR) << "Failed to initialize VP9 encoder";
  }
  
  return encoder_initialized_;
}

std::optional<std::vector<uint8_t>> VP9FrameStreamer::EncodeFrame(
    const SkBitmap& bitmap, bool is_keyframe) {
  if (!InitializeEncoder(gfx::Size(bitmap.width(), bitmap.height()))) {
    return std::nullopt;
  }
  
  // Convert bitmap to video frame
  auto video_frame = ConvertBitmapToI420VideoFrame(bitmap);
  if (!video_frame) {
    return std::nullopt;
  }
  
  // Set keyframe flag
  media::VideoEncoder::EncodeOptions encode_options;
  encode_options.key_frame = is_keyframe;
  
  // Encode frame with real VP9 encoder
  std::vector<uint8_t> encoded_data;
  bool encoding_complete = false;
  
  // Set up encoder output callback - match OutputCB signature
  auto output_cb = base::BindRepeating([](std::vector<uint8_t>* data, bool* complete,
                                         media::VideoEncoderOutput output,
                                         std::optional<std::vector<uint8_t>> codec_desc) {
    if (!output.data.empty()) {
      data->resize(output.data.size());
      std::copy(output.data.begin(), output.data.end(), data->begin());
    }
    *complete = true;
  }, &encoded_data, &encoding_complete);
  
  // Create encoder options
  media::VideoEncoder::Options encoder_options;
  encoder_options.frame_size = gfx::Size(bitmap.width(), bitmap.height());
  encoder_options.bitrate = media::Bitrate::VariableBitrate(1000000u, 2000000u);
  encoder_options.framerate = 30;
  encoder_options.keyframe_interval = 30;

  // Initialize encoder with output callback
  vp9_encoder_->Initialize(
      media::VP9PROFILE_PROFILE0,
      encoder_options,
      media::VideoEncoder::EncoderInfoCB(),
      output_cb,
      base::BindOnce([](media::EncoderStatus status) {
        // Encoder initialized
      }));
  
  // Encode the frame  
  vp9_encoder_->Encode(
      video_frame,
      encode_options,
      base::BindOnce([](media::EncoderStatus status) {
        // Frame encoded
      }));
      
  // Wait for encoding (synchronous for now)
  base::ThreadPoolInstance::Get()->FlushForTesting();
  
  if (!encoded_data.empty()) {
    return encoded_data;
  }
  
  return std::nullopt;
}

bool VP9FrameStreamer::HasSignificantChanges(const SkBitmap& current_frame,
                                            const SkBitmap& previous_frame) {
  if (previous_frame.isNull() || current_frame.isNull()) {
    return true;  // First frame or invalid frame
  }
  
  if (current_frame.dimensions() != previous_frame.dimensions()) {
    return true;  // Size changed
  }
  
  float difference = frame_differencer_->CalculateDifference(current_frame, previous_frame);
  return difference >= config_.change_threshold;
}

FrameMetadata VP9FrameStreamer::CreateMetadata(
    const SkBitmap& bitmap,
    const gfx::Size& viewport_size,
    float device_scale_factor,
    float page_scale_factor,
    const gfx::PointF& scroll_offset) {
  
  FrameMetadata metadata;
  metadata.set_width(bitmap.width());
  metadata.set_height(bitmap.height());
  metadata.set_timestamp_us(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
  metadata.set_device_scale_factor(device_scale_factor);
  metadata.set_page_scale_factor(page_scale_factor);
  metadata.set_scroll_offset_x(scroll_offset.x());
  metadata.set_scroll_offset_y(scroll_offset.y());
  metadata.set_quality(config_.quality);
  metadata.set_uncompressed_size(bitmap.computeByteSize());
  
  return metadata;
}

void VP9FrameStreamer::AddToFrameBuffer(const CapturedFrame& frame) {
  frame_buffer_.push_back(frame);
  
  // Limit buffer size
  while (frame_buffer_.size() > config_.max_frame_buffer_size) {
    frame_buffer_.pop_front();
  }
}

std::optional<VP9FrameStreamer::CapturedFrame> VP9FrameStreamer::GetReferenceFrame() {
  if (frame_buffer_.empty()) {
    return std::nullopt;
  }
  return frame_buffer_.back();
}

void VP9FrameStreamer::OnFrameTimer() {
  if (pending_frame_.has_value()) {
    ProcessPendingFrame();
  }
}

void VP9FrameStreamer::ProcessPendingFrame() {
  if (!pending_frame_.has_value() || !frame_callback_) {
    return;
  }
  
  CapturedFrame& frame = pending_frame_.value();
  
  // Determine if this should be a keyframe
  bool should_be_keyframe = false;
  base::TimeTicks now = base::TimeTicks::Now();
  
  // Force keyframe based on interval
  if (now - last_keyframe_time_ >= 
      base::Milliseconds(1000 * config_.keyframe_interval / config_.fps)) {
    should_be_keyframe = true;
    last_keyframe_time_ = now;
  }
  
  // Check for significant changes
  auto reference_frame = GetReferenceFrame();
  bool has_changes = true;
  
  if (reference_frame.has_value() && !should_be_keyframe) {
    has_changes = HasSignificantChanges(frame.bitmap, reference_frame->bitmap);
  }
  
  VP9StreamFrame stream_frame;
  stream_frame.set_magic_id(config_.magic_id);
  stream_frame.set_frame_number(frame.frame_number);
  *stream_frame.mutable_metadata() = frame.metadata;
  
  if (!has_changes && !should_be_keyframe) {
    // Skip frame - no significant changes
    stream_frame.set_frame_type(FrameType::FRAME_TYPE_SKIP);
    stream_frame.set_frame_data("");
  } else {
    // Encode frame
    auto encoded_data = EncodeFrame(frame.bitmap, should_be_keyframe);
    if (encoded_data.has_value()) {
      stream_frame.set_frame_type(should_be_keyframe ? 
                                  FrameType::FRAME_TYPE_KEY : 
                                  FrameType::FRAME_TYPE_DELTA);
      stream_frame.set_frame_data(std::string(encoded_data->begin(), encoded_data->end()));
      
      // Add to frame buffer for future reference
      AddToFrameBuffer(frame);
    } else {
      LOG(ERROR) << "Failed to encode frame " << frame.frame_number;
      return;
    }
  }
  
  // Send frame to callback
  frame_callback_.Run(stream_frame);
  
  // Clear pending frame
  pending_frame_.reset();
}

void VP9FrameStreamer::UpdateConfig(const Config& new_config) {
  bool needs_restart = is_streaming_ && (
      new_config.fps != config_.fps ||
      new_config.quality != config_.quality);
  
  Config old_config = config_;
  config_ = new_config;
  
  if (needs_restart) {
    auto callback = frame_callback_;
    StopStreaming();
    StartStreaming(callback);
  }
}

// FrameDifferencer implementation
FrameDifferencer::FrameDifferencer() = default;
FrameDifferencer::~FrameDifferencer() = default;

float FrameDifferencer::CalculateDifference(const SkBitmap& frame1, 
                                          const SkBitmap& frame2) {
  if (frame1.dimensions() != frame2.dimensions()) {
    return 1.0f;  // 100% different
  }
  
  if (frame1.isNull() || frame2.isNull()) {
    return 1.0f;
  }
  
  const int width = frame1.width();
  const int height = frame1.height();
  const int pixels = width * height;
  
  if (pixels == 0) {
    return 0.0f;
  }
  
  // Sample-based difference calculation for performance
  const int sample_rate = std::max(1, pixels / 10000);  // Sample ~10k pixels max
  int different_pixels = 0;
  int sampled_pixels = 0;
  
  for (int y = 0; y < height; y += sample_rate) {
    for (int x = 0; x < width; x += sample_rate) {
      SkColor color1 = frame1.getColor(x, y);
      SkColor color2 = frame2.getColor(x, y);
      
      // Calculate color difference
      int r_diff = abs(static_cast<int>(SkColorGetR(color1)) - 
                       static_cast<int>(SkColorGetR(color2)));
      int g_diff = abs(static_cast<int>(SkColorGetG(color1)) - 
                       static_cast<int>(SkColorGetG(color2)));
      int b_diff = abs(static_cast<int>(SkColorGetB(color1)) - 
                       static_cast<int>(SkColorGetB(color2)));
      
      // Consider pixels different if any channel differs by more than threshold
      const int threshold = 10;  // ~4% of 255
      if (r_diff > threshold || g_diff > threshold || b_diff > threshold) {
        different_pixels++;
      }
      
      sampled_pixels++;
    }
  }
  
  return sampled_pixels > 0 ? 
         static_cast<float>(different_pixels) / sampled_pixels : 0.0f;
}

std::vector<gfx::Rect> FrameDifferencer::GetChangedRegions(
    const SkBitmap& frame1, 
    const SkBitmap& frame2,
    float threshold) {
  std::vector<gfx::Rect> regions;
  
  if (frame1.dimensions() != frame2.dimensions() || 
      frame1.isNull() || frame2.isNull()) {
    return regions;
  }
  
  // This is a simplified implementation
  // A production version would use more sophisticated region detection
  const int width = frame1.width();
  const int height = frame1.height();
  const int block_size = 64;  // 64x64 pixel blocks
  
  for (int y = 0; y < height; y += block_size) {
    for (int x = 0; x < width; x += block_size) {
      int block_width = std::min(block_size, width - x);
      int block_height = std::min(block_size, height - y);
      
      // Sample a few pixels in the block
      bool block_changed = false;
      for (int by = y; by < y + block_height && !block_changed; by += block_size / 4) {
        for (int bx = x; bx < x + block_width && !block_changed; bx += block_size / 4) {
          SkColor color1 = frame1.getColor(bx, by);
          SkColor color2 = frame2.getColor(bx, by);
          
          int r_diff = abs(static_cast<int>(SkColorGetR(color1)) - 
                           static_cast<int>(SkColorGetR(color2)));
          int g_diff = abs(static_cast<int>(SkColorGetG(color1)) - 
                           static_cast<int>(SkColorGetG(color2)));
          int b_diff = abs(static_cast<int>(SkColorGetB(color1)) - 
                           static_cast<int>(SkColorGetB(color2)));
          
          const int pixel_threshold = static_cast<int>(threshold * 255);
          if (r_diff > pixel_threshold || g_diff > pixel_threshold || b_diff > pixel_threshold) {
            block_changed = true;
          }
        }
      }
      
      if (block_changed) {
        regions.emplace_back(x, y, block_width, block_height);
      }
    }
  }
  
  return regions;
}

}  // namespace protocol
}  // namespace content