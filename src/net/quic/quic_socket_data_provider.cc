// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_socket_data_provider.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/hex_utils.h"
#include "net/socket/socket_test_util.h"
#include "net/third_party/quiche/src/quiche/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quiche/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quiche/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quiche/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quiche/quic/tools/quic_simple_server_session.h"

namespace quic {

namespace {

class HttpStreamPrinter : public Http3DebugVisitor {
 public:
  explicit HttpStreamPrinter(std::ostream* output) : output_(output) {}

  void OnControlStreamCreated(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnControlStreamCreated: " << stream_id << "\n";
    }
  }

  void OnQpackEncoderStreamCreated(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnQpackEncoderStreamCreated: " << stream_id << "\n";
    }
  }

  void OnQpackDecoderStreamCreated(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnQpackDecoderStreamCreated: " << stream_id << "\n";
    }
  }

  void OnPeerControlStreamCreated(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnPeerControlStreamCreated: " << stream_id << "\n";
    }
  }

  void OnPeerQpackEncoderStreamCreated(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnPeerQpackEncoderStreamCreated: " << stream_id << "\n";
    }
  }

  void OnPeerQpackDecoderStreamCreated(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnPeerQpackDecoderStreamCreated: " << stream_id << "\n";
    }
  }

  void OnSettingsFrameReceived(const SettingsFrame& frame) override {
    if (output_) {
      *output_ << "OnSettingsFrameReceived: " << frame << "\n";
    }
  }

  void OnGoAwayFrameReceived(const GoAwayFrame& frame) override {
    if (output_) {
      *output_ << "OnGoAwayFrameReceived" << "\n";
    }
  }

  void OnPriorityUpdateFrameReceived(
      const PriorityUpdateFrame& frame) override {
    if (output_) {
      *output_ << "OnPriorityUpdateFrameReceived: " << frame << "\n";
    }
  }

  void OnDataFrameReceived(QuicStreamId stream_id,
                           QuicByteCount payload_length) override {
    if (output_) {
      *output_ << "OnDataFrameReceived: " << stream_id << ", "
               << "received: " << payload_length << " bytes" << "\n";
    }
  }

  void OnHeadersFrameReceived(
      QuicStreamId stream_id,
      QuicByteCount compressed_headers_length) override {
    if (output_) {
      *output_ << "OnHeadersFrameReceived: " << stream_id << ", "
               << "received: " << compressed_headers_length << " bytes" << "\n";
    }
  }

  void OnHeadersDecoded(QuicStreamId stream_id,
                        QuicHeaderList headers) override {
    if (output_) {
      *output_ << "OnHeadersDecoded: " << stream_id << ", "
               << "received: " << headers.DebugString() << "\n";
    }
  }

  void OnUnknownFrameReceived(QuicStreamId stream_id,
                              uint64_t frame_type,
                              QuicByteCount payload_length) override {
    if (output_) {
      *output_ << "OnUnknownFrameReceived: " << stream_id << ", "
               << "frame_type: " << frame_type << ","
               << "received: " << payload_length << "bytes " << "\n ";
    }
  }

  void OnSettingsFrameSent(const SettingsFrame& frame) override {
    if (output_) {
      *output_ << "OnSettingsFrameSent: " << frame << "\n";
    }
  }

  void OnSettingsFrameResumed(const SettingsFrame& frame) override {
    if (output_) {
      *output_ << "OnSettingsFrameResumed: " << frame << "\n";
    }
  }

  void OnGoAwayFrameSent(QuicStreamId stream_id) override {
    if (output_) {
      *output_ << "OnGoAwayFrameSent: " << stream_id << "\n";
    }
  }

  void OnPriorityUpdateFrameSent(const PriorityUpdateFrame& frame) override {
    if (output_) {
      *output_ << "OnPriorityUpdateFrameSent: " << frame << "\n";
    }
  }

  void OnDataFrameSent(QuicStreamId stream_id,
                       QuicByteCount payload_length) override {
    if (output_) {
      *output_ << "OnDataFrameSent: " << stream_id << ", "
               << "sent: " << payload_length << " bytes" << "\n";
    }
  }

  void OnHeadersFrameSent(
      QuicStreamId stream_id,
      const quiche::HttpHeaderBlock& header_block) override {
    if (output_) {
      *output_ << "OnHeadersFrameSent: " << stream_id << ", "
               << "sent: " << header_block.DebugString() << "\n";
    }
  }

 private:
  mutable raw_ptr<std::ostream> output_ = nullptr;
};
}  // namespace

}  // namespace quic

namespace net::test {

QuicSimpleServerSessionForTest::~QuicSimpleServerSessionForTest() = default;

// Always return true if CryptoStream is created, so that the pending stream can
// handle initial setting frames for tests.
bool QuicSimpleServerSessionForTest::IsEncryptionEstablished() const {
  return GetCryptoStream() != nullptr;
}

QuicSocketDataProvider::Expectation::Expectation(
    std::string name,
    Type type,
    int rv,
    std::unique_ptr<quic::QuicEncryptedPacket> packet)
    : name_(std::move(name)),
      type_(type),
      rv_(rv),
      packet_(std::move(packet)) {}

QuicSocketDataProvider::Expectation::Expectation(
    QuicSocketDataProvider::Expectation&&) = default;

QuicSocketDataProvider::Expectation::~Expectation() = default;

QuicSocketDataProvider::Expectation& QuicSocketDataProvider::Expectation::After(
    std::string name) {
  after_.insert(std::move(name));
  return *this;
}

std::string QuicSocketDataProvider::Expectation::TypeToString(
    QuicSocketDataProvider::Expectation::Type type) {
  switch (type) {
    case Expectation::Type::READ:
      return "READ";
    case Expectation::Type::WRITE:
      return "WRITE";
    case Expectation::Type::PAUSE:
      return "PAUSE";
  }
  NOTREACHED();
}

void QuicSocketDataProvider::Expectation::Consume() {
  CHECK(!consumed_);
  VLOG(1) << "Consuming " << TypeToString(type_) << " expectation " << name_;
  consumed_ = true;
}

QuicSocketDataProvider::QuicSocketDataProvider(quic::ParsedQuicVersion version)
    : printer_(version),
      crypto_config_(quic::QuicCryptoServerConfig::TESTING,
                     quic::QuicRandom::GetInstance(),
                     quic::test::crypto_test_utils::ProofSourceForTesting(),
                     quic::KeyExchangeSource::Default()),
      compressed_certs_cache_(
          quic::QuicCompressedCertsCache::kQuicCompressedCertsCacheSize) {
  session_for_actual_ = GenSimpleServerSession();
  session_for_expected_ = GenSimpleServerSession();
}

QuicSocketDataProvider::~QuicSocketDataProvider() = default;

std::unique_ptr<quic::QuicSimpleServerSession>
QuicSocketDataProvider::GenSimpleServerSession() {
  quic::test::MockQuicConnection* connection =
      new quic::test::MockQuicConnection(  // IN-TEST
          &helper_, &alarm_factory_, quic::Perspective::IS_SERVER);
  connection->AdvanceTime(quic::QuicTime::Delta::FromSeconds(1));
  connection->SetEncrypter(
      quic::ENCRYPTION_FORWARD_SECURE,
      std::make_unique<quic::NullEncrypter>(connection->perspective()));

  std::unique_ptr<QuicSimpleServerSessionForTest> session =
      std::make_unique<QuicSimpleServerSessionForTest>(
          config_, quic::CurrentSupportedVersions(), connection, &owner_,
          &stream_helper_, &crypto_config_, &compressed_certs_cache_,
          &memory_cache_backend_);
  session->Initialize();
  return session;
}

std::string QuicSocketDataProvider::PrintWithQuicSession(
    quic::QuicSimpleServerSession* session,
    std::string data) {
  std::ostringstream output;
  quic::HttpStreamPrinter printer(&output);
  session->set_debug_visitor(&printer);
  std::string res = printer_.PrintWithQuicSession(data, output, session);
  session->set_debug_visitor(nullptr);
  return res;
}

QuicSocketDataProvider::Expectation& QuicSocketDataProvider::AddRead(
    std::string name,
    std::unique_ptr<quic::QuicEncryptedPacket> packet) {
  expectations_.push_back(Expectation(std::move(name), Expectation::Type::READ,
                                      OK, std::move(packet)));
  return expectations_.back();
}

QuicSocketDataProvider::Expectation& QuicSocketDataProvider::AddRead(
    std::string name,
    std::unique_ptr<quic::QuicReceivedPacket> packet) {
  uint8_t tos_byte = static_cast<uint8_t>(packet->ecn_codepoint());
  return AddRead(std::move(name),
                 static_cast<std::unique_ptr<quic::QuicEncryptedPacket>>(
                     std::move(packet)))
      .TosByte(tos_byte);
}

QuicSocketDataProvider::Expectation& QuicSocketDataProvider::AddReadError(
    std::string name,
    int rv) {
  CHECK_NE(rv, OK);
  CHECK_NE(rv, ERR_IO_PENDING);
  expectations_.push_back(
      Expectation(std::move(name), Expectation::Type::READ, rv, nullptr));
  return expectations_.back();
}

QuicSocketDataProvider::Expectation& QuicSocketDataProvider::AddWrite(
    std::string name,
    std::unique_ptr<quic::QuicEncryptedPacket> packet,
    int rv) {
  expectations_.push_back(Expectation(std::move(name), Expectation::Type::WRITE,
                                      rv, std::move(packet)));
  return expectations_.back();
}

QuicSocketDataProvider::Expectation& QuicSocketDataProvider::AddWriteError(
    std::string name,
    int rv) {
  CHECK_NE(rv, OK);
  CHECK_NE(rv, ERR_IO_PENDING);
  expectations_.push_back(
      Expectation(std::move(name), Expectation::Type::WRITE, rv, nullptr));
  return expectations_.back();
}

QuicSocketDataProvider::PausePoint QuicSocketDataProvider::AddPause(
    std::string name) {
  expectations_.push_back(
      Expectation(std::move(name), Expectation::Type::PAUSE, OK, nullptr));
  return expectations_.size() - 1;
}

bool QuicSocketDataProvider::AllDataConsumed() const {
  return std::all_of(
      expectations_.begin(), expectations_.end(),
      [](const Expectation& expectation) { return expectation.consumed(); });
}

void QuicSocketDataProvider::RunUntilPause(
    QuicSocketDataProvider::PausePoint pause_point) {
  if (!paused_at_.has_value()) {
    run_until_run_loop_ = std::make_unique<base::RunLoop>();
    run_until_run_loop_->Run();
    run_until_run_loop_.reset();
  }
  CHECK(paused_at_.has_value() && *paused_at_ == pause_point)
      << "Did not pause at '" << expectations_[pause_point].name() << "'.";
}

void QuicSocketDataProvider::Resume() {
  CHECK(paused_at_.has_value());
  VLOG(1) << "Resuming from pause point " << expectations_[*paused_at_].name();
  expectations_[*paused_at_].Consume();
  paused_at_ = std::nullopt;
  ExpectationConsumed();
}

void QuicSocketDataProvider::RunUntilAllConsumed() {
  if (!AllDataConsumed()) {
    run_until_run_loop_ = std::make_unique<base::RunLoop>();
    run_until_run_loop_->Run();
    run_until_run_loop_.reset();
  }

  // If that run timed out, then there will still be un-consumed data.
  if (!AllDataConsumed()) {
    std::vector<size_t> unconsumed;
    for (size_t i = 0; i < expectations_.size(); i++) {
      if (!expectations_[i].consumed()) {
        unconsumed.push_back(i);
      }
    }
    FAIL() << "All expectations were not consumed; remaining: "
           << ExpectationList(unconsumed);
  }
}

MockRead QuicSocketDataProvider::OnRead() {
  CHECK(!read_pending_);
  read_pending_ = true;
  std::optional<MockRead> next_read = ConsumeNextRead();
  if (!next_read.has_value()) {
    return MockRead(ASYNC, ERR_IO_PENDING);
  }

  read_pending_ = false;
  return *next_read;
}

MockWriteResult QuicSocketDataProvider::OnWrite(const std::string& data) {
  CHECK(!write_pending_.has_value());
  write_pending_ = data;
  std::optional<MockWriteResult> next_write = ConsumeNextWrite();
  if (!next_write.has_value()) {
    // If Write() was called when no corresponding expectation exists, that's
    // an error unless execution is currently paused, in which case it's just
    // pending. This rarely occurs because the only other type of expectation
    // that might be blocking a WRITE is a READ, and QUIC implementations
    // typically eagerly consume READs.
    if (paused_at_.has_value()) {
      return MockWriteResult(ASYNC, ERR_IO_PENDING);
    } else {
      ADD_FAILURE() << "Write call when none is expected:\n"
                    << printer_.PrintWrite(data);
      return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);
    }
  }

  write_pending_ = std::nullopt;
  return *next_write;
}

bool QuicSocketDataProvider::AllReadDataConsumed() const {
  return AllDataConsumed();
}

bool QuicSocketDataProvider::AllWriteDataConsumed() const {
  return AllDataConsumed();
}

void QuicSocketDataProvider::CancelPendingRead() {
  read_pending_ = false;
}

void QuicSocketDataProvider::Reset() {
  // Note that `Reset` is a parent-class method with a confusing name. It is
  // used to initialize the socket data provider before it is used.

  // Map names to index, and incidentally check for duplicate names.
  std::map<std::string, size_t> names;
  for (size_t i = 0; i < expectations_.size(); i++) {
    Expectation& expectation = expectations_[i];
    auto [_, inserted] = names.insert({expectation.name(), i});
    CHECK(inserted) << "Another expectation named " << expectation.name()
                    << " exists.";
  }

  // Calculate `dependencies_` mapping indices in `expectations_` to indices
  // of the expectations they depend on.
  dependencies_.clear();
  for (size_t i = 0; i < expectations_.size(); i++) {
    Expectation& expectation = expectations_[i];
    if (expectation.after().empty()) {
      // If no other dependencies are given, make the expectation depend on
      // the previous expectation.
      if (i > 0) {
        dependencies_[i].insert(i - 1);
      }
    } else {
      for (auto& after : expectation.after()) {
        const auto dep = names.find(after);
        CHECK(dep != names.end()) << "No expectation named " << after;
        dependencies_[i].insert(dep->second);
      }
    }
  }

  pending_maybe_consume_expectations_ = false;
  read_pending_ = false;
  write_pending_ = std::nullopt;
  MaybeConsumeExpectations();
}

std::optional<size_t> QuicSocketDataProvider::FindReadyExpectations(
    Expectation::Type type) {
  std::vector<size_t> matches;
  for (size_t i = 0; i < expectations_.size(); i++) {
    const Expectation& expectation = expectations_[i];
    if (expectation.consumed() || expectation.type() != type) {
      continue;
    }
    bool found_unconsumed = false;
    for (auto dep : dependencies_[i]) {
      if (!expectations_[dep].consumed_) {
        found_unconsumed = true;
        break;
      }
    }
    if (!found_unconsumed) {
      matches.push_back(i);
    }
  }

  if (matches.size() > 1) {
    std::string exp_type = Expectation::TypeToString(type);
    std::string names = ExpectationList(matches);
    CHECK(matches.size() <= 1)
        << "Multiple expectations of type " << exp_type
        << " are ready: " << names << ". Use .After() to disambiguate.";
  }

  return matches.empty() ? std::nullopt : std::make_optional(matches[0]);
}

std::optional<MockRead> QuicSocketDataProvider::ConsumeNextRead() {
  CHECK(read_pending_);
  std::optional<size_t> ready = FindReadyExpectations(Expectation::Type::READ);
  if (!ready.has_value()) {
    return std::nullopt;
  }

  // If there's exactly one matching expectation, return it.
  Expectation& ready_expectation = expectations_[*ready];
  MockRead read(ready_expectation.mode(), ready_expectation.rv());
  if (ready_expectation.packet()) {
    read.data = ready_expectation.packet()->data();
    read.data_len = ready_expectation.packet()->length();
  }
  read.tos = ready_expectation.tos_byte();
  ready_expectation.Consume();
  ExpectationConsumed();
  return read;
}

std::optional<MockWriteResult> QuicSocketDataProvider::ConsumeNextWrite() {
  CHECK(write_pending_.has_value());
  std::optional<size_t> ready = FindReadyExpectations(Expectation::Type::WRITE);
  if (!ready.has_value()) {
    return std::nullopt;
  }

  // If there's exactly one matching expectation, check if it matches the
  // write and return it.
  Expectation& ready_expectation = expectations_[*ready];
  if (ready_expectation.packet()) {
    if (!VerifyWriteData(ready_expectation)) {
      return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);
    }
  }
  MockWriteResult write(ready_expectation.mode(),
                        ready_expectation.packet()
                            ? ready_expectation.packet()->length()
                            : ready_expectation.rv());
  ready_expectation.Consume();
  ExpectationConsumed();
  return write;
}

void QuicSocketDataProvider::MaybeConsumeExpectations() {
  pending_maybe_consume_expectations_ = false;
  if (read_pending_) {
    std::optional<MockRead> next_read = ConsumeNextRead();
    if (next_read.has_value()) {
      read_pending_ = false;
      if (socket()) {
        socket()->OnReadComplete(*next_read);
      }
    }
  }

  if (write_pending_.has_value()) {
    std::optional<MockWriteResult> next_write = ConsumeNextWrite();
    if (next_write.has_value()) {
      write_pending_ = std::nullopt;
      if (socket()) {
        socket()->OnWriteComplete(next_write->result);
      }
    }
  }

  if (!paused_at_) {
    std::optional<size_t> ready =
        FindReadyExpectations(Expectation::Type::PAUSE);
    if (ready.has_value()) {
      VLOG(1) << "Pausing at " << expectations_[*ready].name();
      paused_at_ = *ready;
      if (run_until_run_loop_) {
        run_until_run_loop_->Quit();
      }
    }
  }

  if (run_until_run_loop_ && AllDataConsumed()) {
    run_until_run_loop_->Quit();
  }
}

void QuicSocketDataProvider::ExpectationConsumed() {
  if (pending_maybe_consume_expectations_) {
    return;
  }
  pending_maybe_consume_expectations_ = true;

  // Call `MaybeConsumeExpectations` in a task. That method may trigger
  // consumption of other expectations, and that consumption must happen
  // _after_ the current call to `Read` or `Write` has finished.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&QuicSocketDataProvider::MaybeConsumeExpectations,
                     weak_factory_.GetWeakPtr()));
}

bool QuicSocketDataProvider::VerifyWriteData(
    QuicSocketDataProvider::Expectation& expectation) {
  std::string expected_data(expectation.packet()->data(),
                            expectation.packet()->length());
  std::string& actual_data = *write_pending_;
  actual_log_.append(
      PrintWithQuicSession(session_for_actual_.get(), actual_data));
  expected_log_.append(
      PrintWithQuicSession(session_for_expected_.get(), expected_data));

  bool write_matches = actual_data == expected_data;
  EXPECT_TRUE(write_matches)
      << "Expectation '" << expectation.name()
      << "' not met. Actual formatted write data:\n"
      << actual_log_ << "But expectation '" << expectation.name()
      << "' expected formatted write data:\n"
      << expected_log_ << "Actual raw write data:\n"
      << HexDump(actual_data) << "Expected raw write data:\n"
      << HexDump(expected_data);
  return write_matches;
}

std::string QuicSocketDataProvider::ExpectationList(
    const std::vector<size_t>& indices) {
  std::ostringstream names;
  bool first = true;
  for (auto i : indices) {
    names << (first ? "" : ", ") << expectations_[i].name();
    first = false;
  }
  return names.str();
}

}  // namespace net::test
