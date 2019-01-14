#include "net/third_party/quic/core/http/http_framer.h"
#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_test.h"

using testing::InSequence;

namespace quic {

class MockVisitor : public HttpFramer::Visitor {
 public:
  virtual ~MockVisitor() = default;

  // Called if an error is detected.
  MOCK_METHOD1(OnError, void(HttpFramer* framer));

  MOCK_METHOD1(OnPriorityFrame, void(const PriorityFrame& frame));
  MOCK_METHOD1(OnCancelPushFrame, void(const CancelPushFrame& frame));
  MOCK_METHOD1(OnMaxPushIdFrame, void(const MaxPushIdFrame& frame));
  MOCK_METHOD1(OnGoAwayFrame, void(const GoAwayFrame& frame));
  MOCK_METHOD1(OnSettingsFrame, void(const SettingsFrame& frame));

  MOCK_METHOD0(OnDataFrameStart, void());
  MOCK_METHOD1(OnDataFramePayload, void(QuicStringPiece payload));
  MOCK_METHOD0(OnDataFrameEnd, void());

  MOCK_METHOD0(OnHeadersFrameStart, void());
  MOCK_METHOD1(OnHeadersFramePayload, void(QuicStringPiece payload));
  MOCK_METHOD0(OnHeadersFrameEnd, void());

  MOCK_METHOD1(OnPushPromiseFrameStart, void(PushId push_id));
  MOCK_METHOD1(OnPushPromiseFramePayload, void(QuicStringPiece payload));
  MOCK_METHOD0(OnPushPromiseFrameEnd, void());
};

class HttpFramerTest : public QuicTest {
 public:
  HttpFramerTest() { framer_.set_visitor(&visitor_); }
  HttpFramer framer_;
  testing::StrictMock<MockVisitor> visitor_;
};

TEST_F(HttpFramerTest, InitialState) {
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, ReservedFramesNoPayload) {
  for (int n = 0; n < 8; ++n) {
    const uint8_t type = 0xB + 0x1F * n;
    char input[] = {// length
                    0x00,
                    // type
                    type};

    EXPECT_EQ(2u, framer_.ProcessInput(input, QUIC_ARRAYSIZE(input))) << n;
    EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
    ASSERT_EQ("", framer_.error_detail());
  }
}

TEST_F(HttpFramerTest, ReservedFramesSmallPayload) {
  for (int n = 0; n < 8; ++n) {
    const uint8_t type = 0xB + 0x1F * n;
    const uint8_t payload_size = 50;
    char input[payload_size + 2] = {// length
                                    payload_size,
                                    // type
                                    type};

    EXPECT_EQ(QUIC_ARRAYSIZE(input),
              framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)))
        << n;
    EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
    ASSERT_EQ("", framer_.error_detail());
  }
}

TEST_F(HttpFramerTest, ReservedFramesLargePayload) {
  for (int n = 0; n < 8; ++n) {
    const uint8_t type = 0xB + 0x1F * n;
    const size_t payload_size = 256;
    char input[payload_size + 3] = {// length
                                    0x40 + 0x01, 0x00,
                                    // type
                                    type};

    EXPECT_EQ(QUIC_ARRAYSIZE(input),
              framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)))
        << n;
    EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
    ASSERT_EQ("", framer_.error_detail());
  }
}

TEST_F(HttpFramerTest, CancelPush) {
  char input[] = {// length
                  0x2,
                  // type (CANCEL_PUSH)
                  0x03,
                  // Push Id
                  0x01};

  // Process the full frame.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, PushPromiseFrame) {
  char input[] = {// length
                  0x8,
                  // type (PUSH_PROMISE)
                  0x05,
                  // Push Id
                  0x01,
                  // Header Block
                  'H', 'e', 'a', 'd', 'e', 'r', 's'};

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("Headers"));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("H"));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("e"));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("a"));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("d"));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("e"));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("r"));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload("s"));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, MaxPushId) {
  char input[] = {// length
                  0x2,
                  // type (MAX_PUSH_ID)
                  0x0D,
                  // Push Id
                  0x01};

  // Process the full frame.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, PriorityFrame) {
  char input[] = {// length
                  0x4,
                  // type (PRIORITY)
                  0x2,
                  // request stream, request stream, exclusive
                  0x01,
                  // prioritized_element_id
                  0x03,
                  // element_dependency_id
                  0x04,
                  // weight
                  0xFF};

  PriorityFrame frame;
  frame.prioritized_type = REQUEST_STREAM;
  frame.dependency_type = REQUEST_STREAM;
  frame.exclusive = true;
  frame.prioritized_element_id = 0x03;
  frame.element_dependency_id = 0x04;
  frame.weight = 0xFF;

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPriorityFrame(frame));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  /*
  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnPriorityFrame(frame));
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
  */
}

TEST_F(HttpFramerTest, SettingsFrame) {
  char input[] = {
      // length
      0x17,
      // type (SETTINGS)
      0x04,
      // identifier (SETTINGS_NUM_PLACEHOLDERS)
      0x00, 0x03,
      // length
      0x02,
      // content
      0x00, 0x02,
      // identifier (SETTINGS_MAX_HEADER_LIST_SIZE)
      0x00, 0x06,
      // length
      0x04,
      // content
      0x00, 0x00, 0x00, 0x05,
      // identifier (unknown)
      0x00, 0x05,
      // length
      0x08,
      // content
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  };

  SettingsFrame frame;
  frame.values[3] = 2;
  frame.values[6] = 5;

  // Process the full frame.
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, DataFrame) {
  char input[] = {// length
                  0x05,
                  // type (DATA)
                  0x00,
                  // data
                  'D', 'a', 't', 'a', '!'};

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnDataFrameStart());
  EXPECT_CALL(visitor_, OnDataFramePayload("Data!"));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnDataFrameStart());
  EXPECT_CALL(visitor_, OnDataFramePayload("D"));
  EXPECT_CALL(visitor_, OnDataFramePayload("a"));
  EXPECT_CALL(visitor_, OnDataFramePayload("t"));
  EXPECT_CALL(visitor_, OnDataFramePayload("a"));
  EXPECT_CALL(visitor_, OnDataFramePayload("!"));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, GoAway) {
  char input[] = {// length
                  0x1,
                  // type (GOAWAY)
                  0x07,
                  // StreamId
                  0x01};

  // Process the full frame.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

TEST_F(HttpFramerTest, HeadersFrame) {
  char input[] = {// length
                  0x07,
                  // type (HEADERS)
                  0x01,
                  // headers
                  'H', 'e', 'a', 'd', 'e', 'r', 's'};

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnHeadersFrameStart());
  EXPECT_CALL(visitor_, OnHeadersFramePayload("Headers"));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            framer_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnHeadersFrameStart());
  EXPECT_CALL(visitor_, OnHeadersFramePayload("H"));
  EXPECT_CALL(visitor_, OnHeadersFramePayload("e"));
  EXPECT_CALL(visitor_, OnHeadersFramePayload("a"));
  EXPECT_CALL(visitor_, OnHeadersFramePayload("d"));
  EXPECT_CALL(visitor_, OnHeadersFramePayload("e"));
  EXPECT_CALL(visitor_, OnHeadersFramePayload("r"));
  EXPECT_CALL(visitor_, OnHeadersFramePayload("s"));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, framer_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, framer_.error());
  EXPECT_EQ("", framer_.error_detail());
}

}  // namespace quic
