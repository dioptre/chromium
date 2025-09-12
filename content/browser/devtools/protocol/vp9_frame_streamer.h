// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEVTOOLS_PROTOCOL_VP9_FRAME_STREAMER_H_
#define CONTENT_BROWSER_DEVTOOLS_PROTOCOL_VP9_FRAME_STREAMER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/browser/devtools/protocol/vp9_stream.pb.h"
#include "media/video/vpx_video_encoder.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/size.h"

namespace content {
namespace protocol {

class FrameDifferencer;

// VP9 frame streamer for DevTools Protocol
// Captures screen content and streams it as VP9-encoded frames with
// delta compression to minimize bandwidth usage.
class VP9FrameStreamer {
 public:
  using FrameCallback = base::RepeatingCallback<void(const VP9StreamFrame&)>;
  
  struct Config {
    uint32_t fps = 30;
    uint32_t keyframe_interval = 30;  // One keyframe every second at 30fps
    uint32_t quality = 80;
    uint32_t max_frame_buffer_size = 5;
    float change_threshold = 0.01f;  // 1% change threshold
    gfx::Size max_dimensions{1920, 1080};
    std::string magic_id = "VP9_DEVTOOLS_STREAM";
  };

  explicit VP9FrameStreamer(Config config);
  ~VP9FrameStreamer();

  // Start streaming with the given callback
  void StartStreaming(FrameCallback callback);
  
  // Stop streaming
  void StopStreaming();
  
  // Process a new frame from the screen capture
  void ProcessFrame(const SkBitmap& bitmap, 
                   const gfx::Size& viewport_size,
                   float device_scale_factor,
                   float page_scale_factor,
                   const gfx::PointF& scroll_offset);
  
  // Update streaming configuration
  void UpdateConfig(const Config& new_config);
  
  bool is_streaming() const { return is_streaming_; }

 private:
  // Internal frame representation
  struct CapturedFrame {
    SkBitmap bitmap;
    FrameMetadata metadata;
    base::TimeTicks capture_time;
    uint64_t frame_number;
  };

  // Initialize VP9 encoder
  bool InitializeEncoder(const gfx::Size& frame_size);
  
  // Encode frame using VP9
  std::optional<std::vector<uint8_t>> EncodeFrame(
      const SkBitmap& bitmap, 
      bool is_keyframe);
  
  // Check if frame has significant changes
  bool HasSignificantChanges(const SkBitmap& current_frame,
                           const SkBitmap& previous_frame);
  
  // Generate frame metadata
  FrameMetadata CreateMetadata(const SkBitmap& bitmap,
                             const gfx::Size& viewport_size,
                             float device_scale_factor,
                             float page_scale_factor,
                             const gfx::PointF& scroll_offset);
  
  // Manage frame buffer
  void AddToFrameBuffer(const CapturedFrame& frame);
  std::optional<CapturedFrame> GetReferenceFrame();
  
  // Timer callback for frame processing
  void OnFrameTimer();
  
  // Frame processing queue
  void ProcessPendingFrame();
  
  Config config_;
  FrameCallback frame_callback_;
  
  bool is_streaming_;
  uint64_t current_frame_number_;
  
  // VP9 encoder
  std::unique_ptr<media::VpxVideoEncoder> vp9_encoder_;
  bool encoder_initialized_;
  gfx::Size encoder_frame_size_;
  
  // Frame differencing
  std::unique_ptr<FrameDifferencer> frame_differencer_;
  
  // Frame buffer for reference frames
  base::circular_deque<CapturedFrame> frame_buffer_;
  
  // Timing and capture
  base::RepeatingTimer frame_timer_;
  std::optional<CapturedFrame> pending_frame_;
  base::TimeTicks last_keyframe_time_;
  
  base::WeakPtrFactory<VP9FrameStreamer> weak_factory_{this};
};

// Helper class for detecting frame differences
class FrameDifferencer {
 public:
  FrameDifferencer();
  ~FrameDifferencer();
  
  // Calculate difference percentage between two frames
  float CalculateDifference(const SkBitmap& frame1, const SkBitmap& frame2);
  
  // Get regions that changed between frames
  std::vector<gfx::Rect> GetChangedRegions(const SkBitmap& frame1, 
                                          const SkBitmap& frame2,
                                          float threshold);

 private:
  // Internal implementation details
  std::vector<uint8_t> temp_buffer_;
};

}  // namespace protocol
}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_PROTOCOL_VP9_FRAME_STREAMER_H_