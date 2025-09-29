// Simplified VP9 implementation for Chrome DevTools Protocol

#include "content/browser/devtools/protocol/vp9_stream.pb.h"
#include "base/base64.h"
#include "base/time/time.h"

namespace content {
namespace protocol {

class VP9SimpleStreamer {
 public:
  VP9SimpleStreamer() = default;
  ~VP9SimpleStreamer() = default;

  // Create a simple VP9 frame for testing
  std::string CreateTestFrame(uint64_t frame_number, const std::string& magic_id) {
    VP9StreamFrame frame;
    frame.set_magic_id(magic_id);
    frame.set_frame_number(frame_number);
    frame.set_frame_data("VP9_TEST_DATA");
    
    // Set metadata
    FrameMetadata* metadata = frame.mutable_metadata();
    metadata->set_width(1920);
    metadata->set_height(1080);
    metadata->set_timestamp_us(base::Time::Now().InMicrosecondsSinceUnixEpoch());
    metadata->set_device_scale_factor(1.0f);
    metadata->set_page_scale_factor(1.0f);
    metadata->set_scroll_offset_x(0.0f);
    metadata->set_scroll_offset_y(0.0f);
    metadata->set_quality(80);
    metadata->set_uncompressed_size(1920 * 1080 * 4);
    
    frame.set_frame_type(FrameType::FRAME_TYPE_KEY);
    
    // Serialize and encode
    std::string serialized;
    frame.SerializeToString(&serialized);
    return base::Base64Encode(serialized);
  }
};

}  // namespace protocol  
}  // namespace content