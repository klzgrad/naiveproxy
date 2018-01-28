// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_TEST_TOOLS_QUIC_HTTP_FRAME_PARTS_COLLECTOR_H_
#define NET_QUIC_HTTP_TEST_TOOLS_QUIC_HTTP_FRAME_PARTS_COLLECTOR_H_

// QuicHttpFramePartsCollector is a base class for QuicHttpFrameDecoderListener
// implementations that create one QuicHttpFrameParts instance for each decoded
// frame.

#include <stddef.h>
#include <memory>
#include <vector>

#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener_test_util.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/http/test_tools/quic_http_frame_parts.h"

namespace net {
namespace test {

class QuicHttpFramePartsCollector : public FailingQuicHttpFrameDecoderListener {
 public:
  QuicHttpFramePartsCollector();
  ~QuicHttpFramePartsCollector() override;

  // Toss out the collected data.
  void Reset();

  // Returns true if has started recording the info for a frame and has not yet
  // finished doing so.
  bool IsInProgress() const { return current_frame_ != nullptr; }

  // Returns the QuicHttpFrameParts instance into which we're currently
  // recording callback info if IsInProgress, else nullptr.
  const QuicHttpFrameParts* current_frame() const {
    return current_frame_.get();
  }

  // Returns the number of completely collected QuicHttpFrameParts instances.
  size_t size() const { return collected_frames_.size(); }

  // Returns the n'th frame, where 0 is the oldest of the collected frames,
  // and n==size() is the frame currently being collected, if there is one.
  // Returns nullptr if the requested index is not valid.
  const QuicHttpFrameParts* frame(size_t n) const;

 protected:
  // In support of OnFrameHeader, set the header that we expect to be used in
  // the next call.
  // TODO(jamessynge): Remove ExpectFrameHeader et al. once done with supporting
  // SpdyFramer's exact states.
  void ExpectFrameHeader(const QuicHttpFrameHeader& header);

  // For use in implementing On*Start methods of QuicHttpFrameDecoderListener,
  // returns a QuicHttpFrameParts instance, which will be newly created if
  // IsInProgress==false (which the caller should ensure), else will be the
  // current_frame(); never returns nullptr.
  // If called when IsInProgress==true, a test failure will be recorded.
  QuicHttpFrameDecoderListener* StartFrame(const QuicHttpFrameHeader& header);

  // For use in implementing On* callbacks, such as OnPingAck, that are the only
  // call expected for the frame being decoded; not for On*Start methods.
  // Returns a QuicHttpFrameParts instance, which will be newly created if
  // IsInProgress==false (which the caller should ensure), else will be the
  // current_frame(); never returns nullptr.
  // If called when IsInProgress==true, a test failure will be recorded.
  QuicHttpFrameDecoderListener* StartAndEndFrame(
      const QuicHttpFrameHeader& header);

  // If IsInProgress==true, returns the QuicHttpFrameParts into which the
  // current frame is being recorded; else records a test failure and returns
  // failing_listener_, which will record a test failure when any of its
  // On* methods is called.
  QuicHttpFrameDecoderListener* CurrentFrame();

  // For use in implementing On*End methods, pushes the current frame onto
  // the vector of completed frames, and returns a pointer to it for recording
  // the info in the final call. If IsInProgress==false, records a test failure
  // and returns failing_listener_, which will record a test failure when any
  // of its On* methods is called.
  QuicHttpFrameDecoderListener* EndFrame();

  // For use in implementing OnPaddingTooLong and OnFrameSizeError, is
  // equivalent to EndFrame() if IsInProgress==true, else equivalent to
  // StartAndEndFrame().
  QuicHttpFrameDecoderListener* FrameError(const QuicHttpFrameHeader& header);

 private:
  // Returns the mutable QuicHttpFrameParts instance into which we're currently
  // recording callback info if IsInProgress, else nullptr.
  QuicHttpFrameParts* current_frame() { return current_frame_.get(); }

  // If expected header is set, verify that it matches the header param.
  // TODO(jamessynge): Remove TestExpectedHeader et al. once done
  // with supporting SpdyFramer's exact states.
  void TestExpectedHeader(const QuicHttpFrameHeader& header);

  std::unique_ptr<QuicHttpFrameParts> current_frame_;
  std::vector<std::unique_ptr<QuicHttpFrameParts>> collected_frames_;
  FailingQuicHttpFrameDecoderListener failing_listener_;

  // TODO(jamessynge): Remove expected_header_ et al. once done with supporting
  // SpdyFramer's exact states.
  QuicHttpFrameHeader expected_header_;
  bool expected_header_set_ = false;
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_HTTP_TEST_TOOLS_QUIC_HTTP_FRAME_PARTS_COLLECTOR_H_
