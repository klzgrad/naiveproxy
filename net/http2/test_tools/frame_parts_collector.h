// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_TEST_TOOLS_FRAME_PARTS_COLLECTOR_H_
#define NET_HTTP2_TEST_TOOLS_FRAME_PARTS_COLLECTOR_H_

// FramePartsCollector is a base class for Http2FrameDecoderListener
// implementations that create one FrameParts instance for each decoded frame.

#include <stddef.h>

#include <memory>
#include <vector>

#include "net/http2/decoder/http2_frame_decoder_listener.h"
#include "net/http2/decoder/http2_frame_decoder_listener_test_util.h"
#include "net/http2/http2_structures.h"
#include "net/http2/test_tools/frame_parts.h"

namespace net {
namespace test {

class FramePartsCollector : public FailingHttp2FrameDecoderListener {
 public:
  FramePartsCollector();
  ~FramePartsCollector() override;

  // Toss out the collected data.
  void Reset();

  // Returns true if has started recording the info for a frame and has not yet
  // finished doing so.
  bool IsInProgress() const { return current_frame_ != nullptr; }

  // Returns the FrameParts instance into which we're currently recording
  // callback info if IsInProgress, else nullptr.
  const FrameParts* current_frame() const { return current_frame_.get(); }

  // Returns the number of completely collected FrameParts instances.
  size_t size() const { return collected_frames_.size(); }

  // Returns the n'th frame, where 0 is the oldest of the collected frames,
  // and n==size() is the frame currently being collected, if there is one.
  // Returns nullptr if the requested index is not valid.
  const FrameParts* frame(size_t n) const;

 protected:
  // In support of OnFrameHeader, set the header that we expect to be used in
  // the next call.
  // TODO(jamessynge): Remove ExpectFrameHeader et al. once done with supporting
  // SpdyFramer's exact states.
  void ExpectFrameHeader(const Http2FrameHeader& header);

  // For use in implementing On*Start methods of Http2FrameDecoderListener,
  // returns a FrameParts instance, which will be newly created if
  // IsInProgress==false (which the caller should ensure), else will be the
  // current_frame(); never returns nullptr.
  // If called when IsInProgress==true, a test failure will be recorded.
  Http2FrameDecoderListener* StartFrame(const Http2FrameHeader& header);

  // For use in implementing On* callbacks, such as OnPingAck, that are the only
  // call expected for the frame being decoded; not for On*Start methods.
  // Returns a FrameParts instance, which will be newly created if
  // IsInProgress==false (which the caller should ensure), else will be the
  // current_frame(); never returns nullptr.
  // If called when IsInProgress==true, a test failure will be recorded.
  Http2FrameDecoderListener* StartAndEndFrame(const Http2FrameHeader& header);

  // If IsInProgress==true, returns the FrameParts into which the current
  // frame is being recorded; else records a test failure and returns
  // failing_listener_, which will record a test failure when any of its
  // On* methods is called.
  Http2FrameDecoderListener* CurrentFrame();

  // For use in implementing On*End methods, pushes the current frame onto
  // the vector of completed frames, and returns a pointer to it for recording
  // the info in the final call. If IsInProgress==false, records a test failure
  // and returns failing_listener_, which will record a test failure when any
  // of its On* methods is called.
  Http2FrameDecoderListener* EndFrame();

  // For use in implementing OnPaddingTooLong and OnFrameSizeError, is
  // equivalent to EndFrame() if IsInProgress==true, else equivalent to
  // StartAndEndFrame().
  Http2FrameDecoderListener* FrameError(const Http2FrameHeader& header);

 private:
  // Returns the mutable FrameParts instance into which we're currently
  // recording callback info if IsInProgress, else nullptr.
  FrameParts* current_frame() { return current_frame_.get(); }

  // If expected header is set, verify that it matches the header param.
  // TODO(jamessynge): Remove TestExpectedHeader et al. once done
  // with supporting SpdyFramer's exact states.
  void TestExpectedHeader(const Http2FrameHeader& header);

  std::unique_ptr<FrameParts> current_frame_;
  std::vector<std::unique_ptr<FrameParts>> collected_frames_;
  FailingHttp2FrameDecoderListener failing_listener_;

  // TODO(jamessynge): Remove expected_header_ et al. once done with supporting
  // SpdyFramer's exact states.
  Http2FrameHeader expected_header_;
  bool expected_header_set_ = false;
};

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_TEST_TOOLS_FRAME_PARTS_COLLECTOR_H_
