// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// VP9 streaming extensions for DevTools PageHandler
// This patch extends the existing PageHandler with VP9 streaming capabilities

#ifndef CONTENT_BROWSER_DEVTOOLS_PROTOCOL_PAGE_HANDLER_VP9_PATCH_H_
#define CONTENT_BROWSER_DEVTOOLS_PROTOCOL_PAGE_HANDLER_VP9_PATCH_H_

#include <memory>
#include <optional>

#include "content/browser/devtools/protocol/page_handler.h"
#include "content/browser/devtools/protocol/vp9_frame_streamer.h"
#include "content/browser/devtools/protocol/vp9_stream.pb.h"

namespace content {
namespace protocol {

// Extended PageHandler with VP9 streaming capabilities
class PageHandlerVP9Extension {
 public:
  explicit PageHandlerVP9Extension(PageHandler* page_handler);
  ~PageHandlerVP9Extension();

  // New CDP methods for VP9 streaming
  Response StartVP9Screencast(
      std::optional<int> fps,
      std::optional<int> quality,
      std::optional<int> max_width,
      std::optional<int> max_height,
      std::optional<int> keyframe_interval,
      std::optional<double> change_threshold);

  Response StopVP9Screencast();
  
  Response GetVP9StreamConfig(
      std::unique_ptr<VP9StreamConfig>* config);
  
  Response UpdateVP9StreamConfig(
      std::unique_ptr<VP9StreamConfig> config);

 private:
  // VP9 streaming callbacks
  void OnVP9Frame(const VP9StreamFrame& frame);
  void OnScreenshotCaptured(const SkBitmap& bitmap);

  // Helper methods
  void InitializeVP9Streamer();
  void SendVP9FrameToFrontend(const VP9StreamFrame& frame);
  
  // Convert VP9StreamFrame to base64 for transport
  std::string EncodeVP9FrameToBase64(const VP9StreamFrame& frame);

  raw_ptr<PageHandler> page_handler_;
  std::unique_ptr<VP9FrameStreamer> vp9_streamer_;
  bool is_vp9_streaming_;
  
  base::WeakPtrFactory<PageHandlerVP9Extension> weak_factory_{this};
};

}  // namespace protocol
}  // namespace content

// Protocol definitions for VP9 streaming
namespace content {
namespace protocol {
namespace Page {

// VP9 screencast frame event
class VP9ScreencastFrame {
 public:
  static std::unique_ptr<VP9ScreencastFrame> Create() {
    return std::make_unique<VP9ScreencastFrame>();
  }

  VP9ScreencastFrame& SetData(const std::string& data) {
    data_ = data;
    return *this;
  }

  VP9ScreencastFrame& SetFrameNumber(uint64_t frame_number) {
    frame_number_ = frame_number;
    return *this;
  }

  VP9ScreencastFrame& SetFrameType(const std::string& frame_type) {
    frame_type_ = frame_type;
    return *this;
  }

  VP9ScreencastFrame& SetTimestamp(double timestamp) {
    timestamp_ = timestamp;
    return *this;
  }

  VP9ScreencastFrame& SetMetadata(std::unique_ptr<ScreencastFrameMetadata> metadata) {
    metadata_ = std::move(metadata);
    return *this;
  }

  std::unique_ptr<VP9ScreencastFrame> Build() {
    return std::unique_ptr<VP9ScreencastFrame>(this);
  }

 private:
  std::string data_;
  uint64_t frame_number_;
  std::string frame_type_;
  double timestamp_;
  std::unique_ptr<ScreencastFrameMetadata> metadata_;
};

}  // namespace Page
}  // namespace protocol
}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_PROTOCOL_PAGE_HANDLER_VP9_PATCH_H_