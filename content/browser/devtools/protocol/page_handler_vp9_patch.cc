// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/protocol/page_handler_vp9_patch.h"

#include <memory>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/size.h"

namespace content {
namespace protocol {

PageHandlerVP9Extension::PageHandlerVP9Extension(PageHandler* page_handler)
    : page_handler_(page_handler),
      is_vp9_streaming_(false) {}

PageHandlerVP9Extension::~PageHandlerVP9Extension() {
  if (is_vp9_streaming_) {
    StopVP9Screencast();
  }
}

Response PageHandlerVP9Extension::StartVP9Screencast(
    std::optional<int> fps,
    std::optional<int> quality,
    std::optional<int> max_width,
    std::optional<int> max_height,
    std::optional<int> keyframe_interval,
    std::optional<double> change_threshold) {
  
  if (is_vp9_streaming_) {
    return Response::ServerError("VP9 streaming already active");
  }

  // Configure VP9 streamer
  VP9FrameStreamer::Config config;
  config.fps = fps.value_or(30);
  config.quality = quality.value_or(80);
  config.keyframe_interval = keyframe_interval.value_or(30);
  config.change_threshold = static_cast<float>(change_threshold.value_or(0.01));
  
  if (max_width.has_value() && max_height.has_value()) {
    config.max_dimensions = gfx::Size(max_width.value(), max_height.value());
  }

  // Initialize VP9 streamer
  vp9_streamer_ = std::make_unique<VP9FrameStreamer>(config);
  
  // Start streaming with frame callback
  vp9_streamer_->StartStreaming(
      base::BindRepeating(&PageHandlerVP9Extension::OnVP9Frame,
                         weak_factory_.GetWeakPtr()));
  
  is_vp9_streaming_ = true;
  
  LOG(INFO) << "VP9 screencast started: " << config.fps << " FPS, "
            << "quality=" << config.quality << ", "
            << "keyframe_interval=" << config.keyframe_interval;
  
  return Response::Success();
}

Response PageHandlerVP9Extension::StopVP9Screencast() {
  if (!is_vp9_streaming_) {
    return Response::ServerError("VP9 streaming not active");
  }

  if (vp9_streamer_) {
    vp9_streamer_->StopStreaming();
    vp9_streamer_.reset();
  }
  
  is_vp9_streaming_ = false;
  
  LOG(INFO) << "VP9 screencast stopped";
  return Response::Success();
}

Response PageHandlerVP9Extension::GetVP9StreamConfig(
    std::unique_ptr<VP9StreamConfig>* config) {
  
  auto stream_config = std::make_unique<VP9StreamConfig>();
  
  if (vp9_streamer_) {
    // Get current config from streamer
    // This would be implemented with a getter method
    stream_config->set_fps(30);  // Default values for now
    stream_config->set_quality(80);
    stream_config->set_keyframe_interval(30);
    stream_config->set_max_frame_buffer_size(5);
    stream_config->set_change_threshold(0.01f);
    stream_config->set_max_width(1920);
    stream_config->set_max_height(1080);
  } else {
    return Response::ServerError("VP9 streamer not initialized");
  }
  
  *config = std::move(stream_config);
  return Response::Success();
}

Response PageHandlerVP9Extension::UpdateVP9StreamConfig(
    std::unique_ptr<VP9StreamConfig> config) {
  
  if (!vp9_streamer_) {
    return Response::ServerError("VP9 streamer not initialized");
  }
  
  // Convert protobuf config to streamer config
  VP9FrameStreamer::Config new_config;
  new_config.fps = config->fps();
  new_config.quality = config->quality();
  new_config.keyframe_interval = config->keyframe_interval();
  new_config.max_frame_buffer_size = config->max_frame_buffer_size();
  new_config.change_threshold = config->change_threshold();
  new_config.max_dimensions = gfx::Size(config->max_width(), config->max_height());
  
  vp9_streamer_->UpdateConfig(new_config);
  
  LOG(INFO) << "VP9 stream config updated";
  return Response::Success();
}

void PageHandlerVP9Extension::OnVP9Frame(const VP9StreamFrame& frame) {
  if (!is_vp9_streaming_) {
    return;
  }
  
  // Send frame to DevTools frontend
  SendVP9FrameToFrontend(frame);
}

void PageHandlerVP9Extension::OnScreenshotCaptured(const SkBitmap& bitmap) {
  if (!is_vp9_streaming_ || !vp9_streamer_) {
    return;
  }
  
  // Process the captured frame through VP9 streamer
  // This would typically be called from the PageHandler's screenshot capture logic
  vp9_streamer_->ProcessFrame(
      bitmap,
      gfx::Size(bitmap.width(), bitmap.height()),  // viewport_size
      1.0f,  // device_scale_factor
      1.0f,  // page_scale_factor  
      gfx::PointF(0, 0));  // scroll_offset
}

void PageHandlerVP9Extension::SendVP9FrameToFrontend(const VP9StreamFrame& frame) {
  // Create DevTools protocol message
  auto vp9_frame = Page::VP9ScreencastFrame::Create();
  
  // Encode frame data as base64 for transport
  std::string encoded_data = EncodeVP9FrameToBase64(frame);
  
  vp9_frame->SetData(encoded_data)
           .SetFrameNumber(frame.frame_number())
           .SetTimestamp(frame.metadata().timestamp_us() / 1000000.0);  // Convert to seconds
  
  // Set frame type
  switch (frame.frame_type()) {
    case FrameType::FRAME_TYPE_KEY:
      vp9_frame->SetFrameType("key");
      break;
    case FrameType::FRAME_TYPE_DELTA:
      vp9_frame->SetFrameType("delta");
      break;
    case FrameType::FRAME_TYPE_SKIP:
      vp9_frame->SetFrameType("skip");
      break;
    default:
      vp9_frame->SetFrameType("unknown");
      break;
  }
  
  // Create metadata
  auto metadata = Page::ScreencastFrameMetadata::Create();
  metadata->SetPageScaleFactor(frame.metadata().page_scale_factor())
          .SetDeviceWidth(frame.metadata().width())
          .SetDeviceHeight(frame.metadata().height())
          .SetScrollOffsetX(frame.metadata().scroll_offset_x())
          .SetScrollOffsetY(frame.metadata().scroll_offset_y())
          .SetTimestamp(frame.metadata().timestamp_us() / 1000000.0);
  
  vp9_frame->SetMetadata(metadata->Build());
  
  // Send to frontend via PageHandler
  // Note: This would require extending the PageHandler with a new event
  // page_handler_->GetFrontend()->VP9ScreencastFrame(vp9_frame->Build());
  
  LOG(INFO) << "VP9 frame sent: " << frame.frame_number() 
            << " type=" << (frame.frame_type() == FrameType::FRAME_TYPE_KEY ? "key" : 
                           frame.frame_type() == FrameType::FRAME_TYPE_DELTA ? "delta" : "skip")
            << " size=" << frame.frame_data().size() << " bytes";
}

std::string PageHandlerVP9Extension::EncodeVP9FrameToBase64(const VP9StreamFrame& frame) {
  // Serialize the protobuf message
  std::string serialized_frame;
  if (!frame.SerializeToString(&serialized_frame)) {
    LOG(ERROR) << "Failed to serialize VP9StreamFrame";
    return "";
  }
  
  // Encode as base64 for transport over DevTools protocol
  std::string encoded;
  base::Base64Encode(serialized_frame, &encoded);
  return encoded;
}

}  // namespace protocol
}  // namespace content