// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "net/socket/socket_test_util.h"

#include <inttypes.h>  // For SCNx64
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/notreached.h"
#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/address_family.h"
#include "net/base/address_list.h"
#include "net/base/auth.h"
#include "net/base/completion_once_callback.h"
#include "net/base/hex_utils.h"
#include "net/base/ip_address.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_server.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"
#include "net/socket/connect_job.h"
#include "net/socket/socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/websocket_endpoint_lock_manager.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "net/ssl/ssl_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/strings/ascii.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/build_info.h"
#endif

#define NET_TRACE(level, s) VLOG(level) << s << __FUNCTION__ << "() "

namespace net {
namespace {

inline char AsciifyHigh(char x) {
  char nybble = static_cast<char>((x >> 4) & 0x0F);
  return nybble + ((nybble < 0x0A) ? '0' : 'A' - 10);
}

inline char AsciifyLow(char x) {
  char nybble = static_cast<char>((x >> 0) & 0x0F);
  return nybble + ((nybble < 0x0A) ? '0' : 'A' - 10);
}

inline char Asciify(char x) {
  return absl::ascii_isprint(static_cast<unsigned char>(x)) ? x : '.';
}

void DumpData(const char* data, int data_len) {
  if (logging::LOGGING_INFO < logging::GetMinLogLevel()) {
    return;
  }
  DVLOG(1) << "Length:  " << data_len;
  const char* pfx = "Data:    ";
  if (!data || (data_len <= 0)) {
    DVLOG(1) << pfx << "<None>";
  } else {
    int i;
    for (i = 0; i <= (data_len - 4); i += 4) {
      DVLOG(1) << pfx << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
               << AsciifyHigh(data[i + 1]) << AsciifyLow(data[i + 1])
               << AsciifyHigh(data[i + 2]) << AsciifyLow(data[i + 2])
               << AsciifyHigh(data[i + 3]) << AsciifyLow(data[i + 3]) << "  '"
               << Asciify(data[i + 0]) << Asciify(data[i + 1])
               << Asciify(data[i + 2]) << Asciify(data[i + 3]) << "'";
      pfx = "         ";
    }
    // Take care of any 'trailing' bytes, if data_len was not a multiple of 4.
    switch (data_len - i) {
      case 3:
        DVLOG(1) << pfx << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
                 << AsciifyHigh(data[i + 1]) << AsciifyLow(data[i + 1])
                 << AsciifyHigh(data[i + 2]) << AsciifyLow(data[i + 2])
                 << "    '" << Asciify(data[i + 0]) << Asciify(data[i + 1])
                 << Asciify(data[i + 2]) << " '";
        break;
      case 2:
        DVLOG(1) << pfx << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
                 << AsciifyHigh(data[i + 1]) << AsciifyLow(data[i + 1])
                 << "      '" << Asciify(data[i + 0]) << Asciify(data[i + 1])
                 << "  '";
        break;
      case 1:
        DVLOG(1) << pfx << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
                 << "        '" << Asciify(data[i + 0]) << "   '";
        break;
    }
  }
}

template <MockReadWriteType type>
void DumpMockReadWrite(const MockReadWrite<type>& r) {
  if (logging::LOGGING_INFO < logging::GetMinLogLevel()) {
    return;
  }
  DVLOG(1) << "Async:   " << (r.mode == ASYNC) << "\nResult:  " << r.result;
  DumpData(r.data, r.data_len);
  const char* stop = (r.sequence_number & MockRead::STOPLOOP) ? " (STOP)" : "";
  DVLOG(1) << "Stage:   " << (r.sequence_number & ~MockRead::STOPLOOP) << stop;
}

void RunClosureIfNonNull(base::OnceClosure closure) {
  if (!closure.is_null()) {
    std::move(closure).Run();
  }
}

}  // namespace

MockConnectCompleter::MockConnectCompleter() = default;

MockConnectCompleter::~MockConnectCompleter() = default;

void MockConnectCompleter::SetCallback(CompletionOnceCallback callback) {
  CHECK(!callback_);
  callback_ = std::move(callback);
}

void MockConnectCompleter::Complete(int result) {
  CHECK(callback_);
  std::move(callback_).Run(result);
}

MockConnect::MockConnect() : mode(ASYNC), result(OK) {
  peer_addr = IPEndPoint(IPAddress(192, 0, 2, 33), 0);
}

MockConnect::MockConnect(IoMode io_mode, int r) : mode(io_mode), result(r) {
  peer_addr = IPEndPoint(IPAddress(192, 0, 2, 33), 0);
}

MockConnect::MockConnect(IoMode io_mode, int r, IPEndPoint addr)
    : mode(io_mode), result(r), peer_addr(addr) {}

MockConnect::MockConnect(IoMode io_mode,
                         int r,
                         IPEndPoint addr,
                         bool first_attempt_fails)
    : mode(io_mode),
      result(r),
      peer_addr(addr),
      first_attempt_fails(first_attempt_fails) {}

MockConnect::MockConnect(MockConnectCompleter* completer)
    : mode(ASYNC), result(OK), completer(completer) {}

MockConnect::~MockConnect() = default;

MockConfirm::MockConfirm() : mode(SYNCHRONOUS), result(OK) {}

MockConfirm::MockConfirm(IoMode io_mode, int r) : mode(io_mode), result(r) {}

MockConfirm::~MockConfirm() = default;

bool SocketDataProvider::IsIdle() const {
  return true;
}

void SocketDataProvider::Initialize(AsyncSocket* socket) {
  CHECK(!socket_);
  CHECK(socket);
  socket_ = socket;
  Reset();
}

void SocketDataProvider::DetachSocket() {
  CHECK(socket_);
  socket_ = nullptr;
}

SocketDataProvider::SocketDataProvider() = default;

SocketDataProvider::~SocketDataProvider() {
  if (socket_)
    socket_->OnDataProviderDestroyed();
}

StaticSocketDataHelper::StaticSocketDataHelper(
    base::span<const MockRead> reads,
    base::span<const MockWrite> writes)
    : reads_(reads), writes_(writes) {}

StaticSocketDataHelper::~StaticSocketDataHelper() = default;

const MockRead& StaticSocketDataHelper::PeekRead() const {
  CHECK(!AllReadDataConsumed());
  return reads_[read_index_];
}

const MockWrite& StaticSocketDataHelper::PeekWrite() const {
  CHECK(!AllWriteDataConsumed());
  return writes_[write_index_];
}

const MockRead& StaticSocketDataHelper::AdvanceRead() {
  CHECK(!AllReadDataConsumed());
  return reads_[read_index_++];
}

const MockWrite& StaticSocketDataHelper::AdvanceWrite() {
  CHECK(!AllWriteDataConsumed());
  return writes_[write_index_++];
}

void StaticSocketDataHelper::Reset() {
  read_index_ = 0;
  write_index_ = 0;
}

bool StaticSocketDataHelper::VerifyWriteData(const std::string& data,
                                             SocketDataPrinter* printer) {
  CHECK(!AllWriteDataConsumed());
  // Check that the actual data matches the expectations, skipping over any
  // pause events.
  const MockWrite& next_write = PeekRealWrite();
  if (!next_write.data)
    return true;

  // Note: Partial writes are supported here.  If the expected data
  // is a match, but shorter than the write actually written, that is legal.
  // Example:
  //   Application writes "foobarbaz" (9 bytes)
  //   Expected write was "foo" (3 bytes)
  //   This is a success, and the function returns true.
  std::string expected_data(next_write.data, next_write.data_len);
  std::string actual_data(data.substr(0, next_write.data_len));
  if (printer) {
    EXPECT_TRUE(actual_data == expected_data)
        << "Actual formatted write data:\n"
        << printer->PrintWrite(data) << "Expected formatted write data:\n"
        << printer->PrintWrite(expected_data) << "Actual raw write data:\n"
        << HexDump(data) << "Expected raw write data:\n"
        << HexDump(expected_data);
  } else {
    EXPECT_TRUE(actual_data == expected_data)
        << "Actual write data:\n"
        << HexDump(data) << "Expected write data:\n"
        << HexDump(expected_data);
  }
  return expected_data == actual_data;
}

void StaticSocketDataHelper::ExpectAllReadDataConsumed(
    SocketDataPrinter* printer) const {
  if (AllReadDataConsumed()) {
    return;
  }

  std::ostringstream msg;
  if (read_index_ < read_count()) {
    msg << "Unconsumed reads:\n";
    for (size_t i = read_index_; i < read_count(); i++) {
      msg << (reads_[i].mode == ASYNC ? "ASYNC" : "SYNC") << " MockRead seq "
          << reads_[i].sequence_number << ":\n";
      if (reads_[i].result != OK) {
        msg << "Result: " << reads_[i].result << "\n";
      }
      if (reads_[i].data) {
        std::string data(reads_[i].data, reads_[i].data_len);
        if (printer) {
          msg << printer->PrintWrite(data);
        }
        msg << HexDump(data);
      }
    }
  }
  EXPECT_TRUE(AllReadDataConsumed()) << msg.str();
}

void StaticSocketDataHelper::ExpectAllWriteDataConsumed(
    SocketDataPrinter* printer) const {
  if (AllWriteDataConsumed()) {
    return;
  }

  std::ostringstream msg;
  if (write_index_ < write_count()) {
    msg << "Unconsumed writes:\n";
    for (size_t i = write_index_; i < write_count(); i++) {
      msg << (writes_[i].mode == ASYNC ? "ASYNC" : "SYNC") << " MockWrite seq "
          << writes_[i].sequence_number << ":\n";
      if (writes_[i].result != OK) {
        msg << "Result: " << writes_[i].result << "\n";
      }
      if (writes_[i].data) {
        std::string data(writes_[i].data, writes_[i].data_len);
        if (printer) {
          msg << printer->PrintWrite(data);
        }
        msg << HexDump(data);
      }
    }
  }
  EXPECT_TRUE(AllWriteDataConsumed()) << msg.str();
}

const MockWrite& StaticSocketDataHelper::PeekRealWrite() const {
  for (size_t i = write_index_; i < write_count(); i++) {
    if (writes_[i].mode != ASYNC || writes_[i].result != ERR_IO_PENDING)
      return writes_[i];
  }

  NOTREACHED() << "No write data available.";
}

StaticSocketDataProvider::StaticSocketDataProvider()
    : StaticSocketDataProvider(base::span<const MockRead>(),
                               base::span<const MockWrite>()) {}

StaticSocketDataProvider::StaticSocketDataProvider(
    base::span<const MockRead> reads,
    base::span<const MockWrite> writes)
    : helper_(reads, writes) {}

StaticSocketDataProvider::~StaticSocketDataProvider() = default;

void StaticSocketDataProvider::Pause() {
  paused_ = true;
}

void StaticSocketDataProvider::Resume() {
  paused_ = false;
}

MockRead StaticSocketDataProvider::OnRead() {
  if (AllReadDataConsumed()) {
    const net::MockRead pending_read(net::SYNCHRONOUS, net::ERR_IO_PENDING);
    return pending_read;
  }

  return helper_.AdvanceRead();
}

MockWriteResult StaticSocketDataProvider::OnWrite(const std::string& data) {
  if (helper_.write_count() == 0) {
    // Not using mock writes; succeed synchronously.
    return MockWriteResult(SYNCHRONOUS, data.length());
  }
  if (printer_) {
    EXPECT_FALSE(helper_.AllWriteDataConsumed())
        << "No more mock data to match write:\nFormatted write data:\n"
        << printer_->PrintWrite(data) << "Raw write data:\n"
        << HexDump(data);
  } else {
    EXPECT_FALSE(helper_.AllWriteDataConsumed())
        << "No more mock data to match write:\nRaw write data:\n"
        << HexDump(data);
  }
  if (helper_.AllWriteDataConsumed()) {
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);
  }

  // Check that what we are writing matches the expectation.
  // Then give the mocked return value.
  if (!helper_.VerifyWriteData(data, printer_))
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);

  const MockWrite& next_write = helper_.AdvanceWrite();
  // In the case that the write was successful, return the number of bytes
  // written. Otherwise return the error code.
  int result =
      next_write.result == OK ? next_write.data_len : next_write.result;
  return MockWriteResult(next_write.mode, result);
}

bool StaticSocketDataProvider::AllReadDataConsumed() const {
  return paused_ || helper_.AllReadDataConsumed();
}

bool StaticSocketDataProvider::AllWriteDataConsumed() const {
  return helper_.AllWriteDataConsumed();
}

void StaticSocketDataProvider::Reset() {
  helper_.Reset();
}

SSLSocketDataProvider::SSLSocketDataProvider(IoMode mode, int result)
    : connect(mode, result),
      expected_ssl_version_min(kDefaultSSLVersionMin),
      expected_ssl_version_max(kDefaultSSLVersionMax) {
  SSLConnectionStatusSetVersion(SSL_CONNECTION_VERSION_TLS1_3,
                                &ssl_info.connection_status);
  // Set to TLS_CHACHA20_POLY1305_SHA256
  SSLConnectionStatusSetCipherSuite(0x1301, &ssl_info.connection_status);
}

SSLSocketDataProvider::SSLSocketDataProvider(MockConnectCompleter* completer)
    : connect(completer),
      expected_ssl_version_min(kDefaultSSLVersionMin),
      expected_ssl_version_max(kDefaultSSLVersionMax) {
  SSLConnectionStatusSetVersion(SSL_CONNECTION_VERSION_TLS1_3,
                                &ssl_info.connection_status);
  // Set to TLS_CHACHA20_POLY1305_SHA256
  SSLConnectionStatusSetCipherSuite(0x1301, &ssl_info.connection_status);
}

SSLSocketDataProvider::SSLSocketDataProvider(
    const SSLSocketDataProvider& other) = default;

SSLSocketDataProvider::~SSLSocketDataProvider() = default;

SequencedSocketData::SequencedSocketData()
    : SequencedSocketData(base::span<const MockRead>(),
                          base::span<const MockWrite>()) {}

SequencedSocketData::SequencedSocketData(base::span<const MockRead> reads,
                                         base::span<const MockWrite> writes)
    : helper_(reads, writes) {
  // Check that reads and writes have a contiguous set of sequence numbers
  // starting from 0 and working their way up, with no repeats and skipping
  // no values.
  int next_sequence_number = 0;
  bool last_event_was_pause = false;

  auto next_read = reads.begin();
  auto next_write = writes.begin();
  while (next_read != reads.end() || next_write != writes.end()) {
    if (next_read != reads.end() &&
        next_read->sequence_number == next_sequence_number) {
      // Check if this is a pause.
      if (next_read->mode == ASYNC && next_read->result == ERR_IO_PENDING) {
        CHECK(!last_event_was_pause)
            << "Two pauses in a row are not allowed: " << next_sequence_number;
        last_event_was_pause = true;
      } else if (last_event_was_pause) {
        CHECK_EQ(ASYNC, next_read->mode)
            << "A sync event after a pause makes no sense: "
            << next_sequence_number;
        CHECK_NE(ERR_IO_PENDING, next_read->result)
            << "A pause event after a pause makes no sense: "
            << next_sequence_number;
        last_event_was_pause = false;
      }

      ++next_read;
      ++next_sequence_number;
      continue;
    }
    if (next_write != writes.end() &&
        next_write->sequence_number == next_sequence_number) {
      // Check if this is a pause.
      if (next_write->mode == ASYNC && next_write->result == ERR_IO_PENDING) {
        CHECK(!last_event_was_pause)
            << "Two pauses in a row are not allowed: " << next_sequence_number;
        last_event_was_pause = true;
      } else if (last_event_was_pause) {
        CHECK_EQ(ASYNC, next_write->mode)
            << "A sync event after a pause makes no sense: "
            << next_sequence_number;
        CHECK_NE(ERR_IO_PENDING, next_write->result)
            << "A pause event after a pause makes no sense: "
            << next_sequence_number;
        last_event_was_pause = false;
      }

      ++next_write;
      ++next_sequence_number;
      continue;
    }
    if (next_write != writes.end()) {
      NOTREACHED() << "Sequence number " << next_write->sequence_number
                   << " not found where expected: " << next_sequence_number;
    }
    NOTREACHED() << "Too few writes, next expected sequence number: "
                 << next_sequence_number;
  }

  // Last event must not be a pause.  For the final event to indicate the
  // operation never completes, it should be SYNCHRONOUS and return
  // ERR_IO_PENDING.
  CHECK(!last_event_was_pause);

  CHECK(next_read == reads.end());
  CHECK(next_write == writes.end());
}

SequencedSocketData::SequencedSocketData(const MockConnect& connect,
                                         base::span<const MockRead> reads,
                                         base::span<const MockWrite> writes)
    : SequencedSocketData(reads, writes) {
  set_connect_data(connect);
}
MockRead SequencedSocketData::OnRead() {
  CHECK_EQ(IoState::kIdle, read_state_);
  CHECK(!helper_.AllReadDataConsumed())
      << "Application tried to read but there is no read data left";

  NET_TRACE(1, " *** ") << "sequence_number: " << sequence_number_;
  const MockRead& next_read = helper_.PeekRead();
  NET_TRACE(1, " *** ") << "next_read: " << next_read.sequence_number;
  CHECK_GE(next_read.sequence_number, sequence_number_);

  if (next_read.sequence_number <= sequence_number_) {
    if (next_read.mode == SYNCHRONOUS) {
      NET_TRACE(1, " *** ") << "Returning synchronously";
      DumpMockReadWrite(next_read);
      helper_.AdvanceRead();
      ++sequence_number_;
      MaybePostWriteCompleteTask();
      return next_read;
    }

    // If the result is ERR_IO_PENDING, then pause.
    if (next_read.result == ERR_IO_PENDING) {
      NET_TRACE(1, " *** ") << "Pausing read at: " << sequence_number_;
      read_state_ = IoState::kPaused;
      if (run_until_paused_run_loop_)
        run_until_paused_run_loop_->Quit();
      return MockRead(SYNCHRONOUS, ERR_IO_PENDING);
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SequencedSocketData::OnReadComplete,
                                  weak_factory_.GetWeakPtr()));
    CHECK_NE(IoState::kCompleting, write_state_);
    read_state_ = IoState::kCompleting;
  } else if (next_read.mode == SYNCHRONOUS) {
    ADD_FAILURE() << "Unable to perform synchronous IO while stopped";
    return MockRead(SYNCHRONOUS, ERR_UNEXPECTED);
  } else {
    NET_TRACE(1, " *** ") << "Waiting for write to trigger read";
    read_state_ = IoState::kPending;
  }

  return MockRead(SYNCHRONOUS, ERR_IO_PENDING);
}

MockWriteResult SequencedSocketData::OnWrite(const std::string& data) {
  CHECK_EQ(IoState::kIdle, write_state_);
  if (printer_) {
    CHECK(!helper_.AllWriteDataConsumed())
        << "\nNo more mock data to match write:\nFormatted write data:\n"
        << printer_->PrintWrite(data) << "Raw write data:\n"
        << HexDump(data);
  } else {
    CHECK(!helper_.AllWriteDataConsumed())
        << "\nNo more mock data to match write:\nRaw write data:\n"
        << HexDump(data);
  }

  NET_TRACE(1, " *** ") << "sequence_number: " << sequence_number_;
  const MockWrite& next_write = helper_.PeekWrite();
  NET_TRACE(1, " *** ") << "next_write: " << next_write.sequence_number;
  CHECK_GE(next_write.sequence_number, sequence_number_);

  if (!helper_.VerifyWriteData(data, printer_))
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);

  if (next_write.sequence_number <= sequence_number_) {
    if (next_write.mode == SYNCHRONOUS) {
      helper_.AdvanceWrite();
      ++sequence_number_;
      MaybePostReadCompleteTask();
      // In the case that the write was successful, return the number of bytes
      // written. Otherwise return the error code.
      int rv =
          next_write.result != OK ? next_write.result : next_write.data_len;
      NET_TRACE(1, " *** ") << "Returning synchronously";
      return MockWriteResult(SYNCHRONOUS, rv);
    }

    // If the result is ERR_IO_PENDING, then pause.
    if (next_write.result == ERR_IO_PENDING) {
      NET_TRACE(1, " *** ") << "Pausing write at: " << sequence_number_;
      write_state_ = IoState::kPaused;
      if (run_until_paused_run_loop_)
        run_until_paused_run_loop_->Quit();
      return MockWriteResult(SYNCHRONOUS, ERR_IO_PENDING);
    }

    NET_TRACE(1, " *** ") << "Posting task to complete write";
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SequencedSocketData::OnWriteComplete,
                                  weak_factory_.GetWeakPtr()));
    CHECK_NE(IoState::kCompleting, read_state_);
    write_state_ = IoState::kCompleting;
  } else if (next_write.mode == SYNCHRONOUS) {
    ADD_FAILURE() << "Unable to perform synchronous IO while stopped";
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);
  } else {
    NET_TRACE(1, " *** ") << "Waiting for read to trigger write";
    write_state_ = IoState::kPending;
  }

  return MockWriteResult(SYNCHRONOUS, ERR_IO_PENDING);
}

bool SequencedSocketData::AllReadDataConsumed() const {
  return helper_.AllReadDataConsumed();
}

void SequencedSocketData::CancelPendingRead() {
  DCHECK_EQ(IoState::kPending, read_state_);

  read_state_ = IoState::kIdle;
}

bool SequencedSocketData::AllWriteDataConsumed() const {
  return helper_.AllWriteDataConsumed();
}

void SequencedSocketData::ExpectAllReadDataConsumed() const {
  helper_.ExpectAllReadDataConsumed(printer_.get());
}

void SequencedSocketData::ExpectAllWriteDataConsumed() const {
  helper_.ExpectAllWriteDataConsumed(printer_.get());
}

bool SequencedSocketData::IsIdle() const {
  // If |busy_before_sync_reads_| is not set, always considered idle.  If
  // no reads left, or the next operation is a write, also consider it idle.
  if (!busy_before_sync_reads_ || helper_.AllReadDataConsumed() ||
      helper_.PeekRead().sequence_number != sequence_number_) {
    return true;
  }

  // If the next operation is synchronous read, treat the socket as not idle.
  if (helper_.PeekRead().mode == SYNCHRONOUS)
    return false;
  return true;
}

bool SequencedSocketData::IsPaused() const {
  // Both states should not be paused.
  DCHECK(read_state_ != IoState::kPaused || write_state_ != IoState::kPaused);
  return write_state_ == IoState::kPaused || read_state_ == IoState::kPaused;
}

void SequencedSocketData::Resume() {
  if (!IsPaused()) {
    ADD_FAILURE() << "Unable to Resume when not paused.";
    return;
  }

  sequence_number_++;
  if (read_state_ == IoState::kPaused) {
    read_state_ = IoState::kPending;
    helper_.AdvanceRead();
  } else {  // write_state_ == IoState::kPaused
    write_state_ = IoState::kPending;
    helper_.AdvanceWrite();
  }

  if (!helper_.AllWriteDataConsumed() &&
      helper_.PeekWrite().sequence_number == sequence_number_) {
    // The next event hasn't even started yet.  Pausing isn't really needed in
    // that case, but may as well support it.
    if (write_state_ != IoState::kPending)
      return;
    write_state_ = IoState::kCompleting;
    OnWriteComplete();
    return;
  }

  CHECK(!helper_.AllReadDataConsumed());

  // The next event hasn't even started yet.  Pausing isn't really needed in
  // that case, but may as well support it.
  if (read_state_ != IoState::kPending)
    return;
  read_state_ = IoState::kCompleting;
  OnReadComplete();
}

void SequencedSocketData::RunUntilPaused() {
  CHECK(!run_until_paused_run_loop_);

  if (IsPaused())
    return;

  run_until_paused_run_loop_ = std::make_unique<base::RunLoop>();
  run_until_paused_run_loop_->Run();
  run_until_paused_run_loop_.reset();
  DCHECK(IsPaused());
}

void SequencedSocketData::MaybePostReadCompleteTask() {
  NET_TRACE(1, " ****** ") << " current: " << sequence_number_;
  // Only trigger the next read to complete if there is already a read pending
  // which should complete at the current sequence number.
  if (read_state_ != IoState::kPending ||
      helper_.PeekRead().sequence_number != sequence_number_) {
    return;
  }

  // If the result is ERR_IO_PENDING, then pause.
  if (helper_.PeekRead().result == ERR_IO_PENDING) {
    NET_TRACE(1, " *** ") << "Pausing read at: " << sequence_number_;
    read_state_ = IoState::kPaused;
    if (run_until_paused_run_loop_)
      run_until_paused_run_loop_->Quit();
    return;
  }

  NET_TRACE(1, " ****** ") << "Posting task to complete read: "
                           << sequence_number_;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SequencedSocketData::OnReadComplete,
                                weak_factory_.GetWeakPtr()));
  CHECK_NE(IoState::kCompleting, write_state_);
  read_state_ = IoState::kCompleting;
}

void SequencedSocketData::MaybePostWriteCompleteTask() {
  NET_TRACE(1, " ****** ") << " current: " << sequence_number_;
  // Only trigger the next write to complete if there is already a write pending
  // which should complete at the current sequence number.
  if (write_state_ != IoState::kPending ||
      helper_.PeekWrite().sequence_number != sequence_number_) {
    return;
  }

  // If the result is ERR_IO_PENDING, then pause.
  if (helper_.PeekWrite().result == ERR_IO_PENDING) {
    NET_TRACE(1, " *** ") << "Pausing write at: " << sequence_number_;
    write_state_ = IoState::kPaused;
    if (run_until_paused_run_loop_)
      run_until_paused_run_loop_->Quit();
    return;
  }

  NET_TRACE(1, " ****** ") << "Posting task to complete write: "
                           << sequence_number_;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SequencedSocketData::OnWriteComplete,
                                weak_factory_.GetWeakPtr()));
  CHECK_NE(IoState::kCompleting, read_state_);
  write_state_ = IoState::kCompleting;
}

void SequencedSocketData::Reset() {
  helper_.Reset();
  sequence_number_ = 0;
  read_state_ = IoState::kIdle;
  write_state_ = IoState::kIdle;
  weak_factory_.InvalidateWeakPtrs();
}

void SequencedSocketData::OnReadComplete() {
  CHECK_EQ(IoState::kCompleting, read_state_);
  NET_TRACE(1, " *** ") << "Completing read for: " << sequence_number_;

  MockRead data = helper_.AdvanceRead();
  DCHECK_EQ(sequence_number_, data.sequence_number);
  sequence_number_++;
  read_state_ = IoState::kIdle;

  // The result of this read completing might trigger the completion
  // of a pending write. If so, post a task to complete the write later.
  // Since the socket may call back into the SequencedSocketData
  // from socket()->OnReadComplete(), trigger the write task to be posted
  // before calling that.
  MaybePostWriteCompleteTask();

  if (!socket()) {
    NET_TRACE(1, " *** ") << "No socket available to complete read";
    return;
  }

  NET_TRACE(1, " *** ") << "Completing socket read for: "
                        << data.sequence_number;
  DumpMockReadWrite(data);
  socket()->OnReadComplete(data);
  NET_TRACE(1, " *** ") << "Done";
}

void SequencedSocketData::OnWriteComplete() {
  CHECK_EQ(IoState::kCompleting, write_state_);
  NET_TRACE(1, " *** ") << " Completing write for: " << sequence_number_;

  const MockWrite& data = helper_.AdvanceWrite();
  DCHECK_EQ(sequence_number_, data.sequence_number);
  sequence_number_++;
  write_state_ = IoState::kIdle;
  int rv = data.result == OK ? data.data_len : data.result;

  // The result of this write completing might trigger the completion
  // of a pending read. If so, post a task to complete the read later.
  // Since the socket may call back into the SequencedSocketData
  // from socket()->OnWriteComplete(), trigger the write task to be posted
  // before calling that.
  MaybePostReadCompleteTask();

  if (!socket()) {
    NET_TRACE(1, " *** ") << "No socket available to complete write";
    return;
  }

  NET_TRACE(1, " *** ") << " Completing socket write for: "
                        << data.sequence_number;
  socket()->OnWriteComplete(rv);
  NET_TRACE(1, " *** ") << "Done";
}

SequencedSocketData::~SequencedSocketData() = default;

MockClientSocketFactory::MockClientSocketFactory() = default;

MockClientSocketFactory::~MockClientSocketFactory() = default;

void MockClientSocketFactory::AddSocketDataProvider(SocketDataProvider* data) {
  mock_data_.Add(data);
}

void MockClientSocketFactory::AddTcpSocketDataProvider(
    SocketDataProvider* data) {
  mock_tcp_data_.Add(data);
}

void MockClientSocketFactory::AddSSLSocketDataProvider(
    SSLSocketDataProvider* data) {
  mock_ssl_data_.Add(data);
}

void MockClientSocketFactory::ResetNextMockIndexes() {
  mock_data_.ResetNextIndex();
  mock_ssl_data_.ResetNextIndex();
}

std::unique_ptr<DatagramClientSocket>
MockClientSocketFactory::CreateDatagramClientSocket(
    DatagramSocket::BindType bind_type,
    NetLog* net_log,
    const NetLogSource& source) {
  NET_TRACE(1, " *** ") << "mock_data_index: " << mock_data_.next_index();
  SocketDataProvider* data_provider = mock_data_.GetNext();
  auto socket = std::make_unique<MockUDPClientSocket>(data_provider, net_log);
  if (bind_type == DatagramSocket::RANDOM_BIND)
    socket->set_source_port(static_cast<uint16_t>(base::RandInt(1025, 65535)));
  udp_client_socket_ports_.push_back(socket->source_port());
  return std::move(socket);
}

std::unique_ptr<TransportClientSocket>
MockClientSocketFactory::CreateTransportClientSocket(
    const AddressList& addresses,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    NetworkQualityEstimator* network_quality_estimator,
    NetLog* net_log,
    const NetLogSource& source) {
  SocketDataProvider* data_provider = mock_tcp_data_.GetNextWithoutAsserting();
  if (data_provider) {
    NET_TRACE(1, " *** ") << "mock_tcp_data_index: "
                          << (mock_tcp_data_.next_index() - 1);
  } else {
    NET_TRACE(1, " *** ") << "mock_data_index: " << mock_data_.next_index();
    data_provider = mock_data_.GetNext();
  }
  auto socket =
      std::make_unique<MockTCPClientSocket>(addresses, net_log, data_provider);
  if (enable_read_if_ready_)
    socket->set_enable_read_if_ready(enable_read_if_ready_);
  return std::move(socket);
}

std::unique_ptr<SSLClientSocket> MockClientSocketFactory::CreateSSLClientSocket(
    SSLClientContext* context,
    std::unique_ptr<StreamSocket> stream_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config) {
  NET_TRACE(1, " *** ") << "mock_ssl_data_index: "
                        << mock_ssl_data_.next_index();
  SSLSocketDataProvider* next_ssl_data = mock_ssl_data_.GetNext();
  if (next_ssl_data->next_protos_expected_in_ssl_config.has_value()) {
    EXPECT_TRUE(std::ranges::equal(
        next_ssl_data->next_protos_expected_in_ssl_config.value(),
        ssl_config.alpn_protos));
  }
  if (next_ssl_data->expected_application_settings) {
    EXPECT_EQ(*next_ssl_data->expected_application_settings,
              ssl_config.application_settings);
  }

  // The protocol version used is a combination of the per-socket SSLConfig and
  // the SSLConfigService.
  EXPECT_EQ(
      next_ssl_data->expected_ssl_version_min,
      ssl_config.version_min_override.value_or(context->config().version_min));
  EXPECT_EQ(
      next_ssl_data->expected_ssl_version_max,
      ssl_config.version_max_override.value_or(context->config().version_max));

  if (next_ssl_data->expected_early_data_enabled) {
    EXPECT_EQ(*next_ssl_data->expected_early_data_enabled,
              ssl_config.early_data_enabled);
  }

  if (next_ssl_data->expected_send_client_cert) {
    // Client certificate preferences come from |context|.
    scoped_refptr<X509Certificate> client_cert;
    scoped_refptr<SSLPrivateKey> client_private_key;
    bool send_client_cert = context->GetClientCertificate(
        host_and_port, &client_cert, &client_private_key);

    EXPECT_EQ(*next_ssl_data->expected_send_client_cert, send_client_cert);
    // Note |send_client_cert| may be true while |client_cert| is null if the
    // socket is configured to continue without a certificate, as opposed to
    // surfacing the certificate challenge.
    EXPECT_EQ(!!next_ssl_data->expected_client_cert, !!client_cert);
    if (next_ssl_data->expected_client_cert && client_cert) {
      EXPECT_TRUE(next_ssl_data->expected_client_cert->EqualsIncludingChain(
          client_cert.get()));
    }
  }
  if (next_ssl_data->expected_host_and_port) {
    EXPECT_EQ(*next_ssl_data->expected_host_and_port, host_and_port);
  }
  if (next_ssl_data->expected_ignore_certificate_errors) {
    EXPECT_EQ(*next_ssl_data->expected_ignore_certificate_errors,
              ssl_config.ignore_certificate_errors);
  }
  if (next_ssl_data->expected_network_anonymization_key) {
    EXPECT_EQ(*next_ssl_data->expected_network_anonymization_key,
              ssl_config.network_anonymization_key);
  }
  if (next_ssl_data->expected_ech_config_list) {
    EXPECT_EQ(*next_ssl_data->expected_ech_config_list,
              ssl_config.ech_config_list);
  }
  return std::make_unique<MockSSLClientSocket>(
      std::move(stream_socket), host_and_port, ssl_config, next_ssl_data);
}

MockClientSocket::MockClientSocket(const NetLogWithSource& net_log)
    : net_log_(net_log) {
  local_addr_ = IPEndPoint(IPAddress(192, 0, 2, 33), 123);
  peer_addr_ = IPEndPoint(IPAddress(192, 0, 2, 33), 0);
}

int MockClientSocket::SetReceiveBufferSize(int32_t size) {
  return OK;
}

int MockClientSocket::SetSendBufferSize(int32_t size) {
  return OK;
}

int MockClientSocket::Bind(const net::IPEndPoint& local_addr) {
  local_addr_ = local_addr;
  return net::OK;
}

bool MockClientSocket::SetNoDelay(bool no_delay) {
  return true;
}

bool MockClientSocket::SetKeepAlive(bool enable, int delay) {
  return true;
}

void MockClientSocket::Disconnect() {
  connected_ = false;
}

bool MockClientSocket::IsConnected() const {
  return connected_;
}

bool MockClientSocket::IsConnectedAndIdle() const {
  return connected_;
}

int MockClientSocket::GetPeerAddress(IPEndPoint* address) const {
  if (!IsConnected())
    return ERR_SOCKET_NOT_CONNECTED;
  *address = peer_addr_;
  return OK;
}

int MockClientSocket::GetLocalAddress(IPEndPoint* address) const {
  *address = local_addr_;
  return OK;
}

const NetLogWithSource& MockClientSocket::NetLog() const {
  return net_log_;
}

NextProto MockClientSocket::GetNegotiatedProtocol() const {
  return NextProto::kProtoUnknown;
}

MockClientSocket::~MockClientSocket() = default;

void MockClientSocket::RunCallbackAsync(CompletionOnceCallback callback,
                                        int result) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MockClientSocket::RunCallback, weak_factory_.GetWeakPtr(),
                     std::move(callback), result));
}

void MockClientSocket::RunCallback(CompletionOnceCallback callback,
                                   int result) {
  std::move(callback).Run(result);
}

MockTCPClientSocket::MockTCPClientSocket(const AddressList& addresses,
                                         net::NetLog* net_log,
                                         SocketDataProvider* data)
    : MockClientSocket(
          NetLogWithSource::Make(net_log, NetLogSourceType::SOCKET)),
      addresses_(addresses),
      data_(data),
      read_data_(SYNCHRONOUS, ERR_UNEXPECTED) {
  DCHECK(data_);
  peer_addr_ = data->connect_data().peer_addr;
  data_->Initialize(this);
  if (data_->expected_addresses()) {
    EXPECT_EQ(*data_->expected_addresses(), addresses);
  }
}

MockTCPClientSocket::~MockTCPClientSocket() {
  if (data_)
    data_->DetachSocket();
}

int MockTCPClientSocket::Read(IOBuffer* buf,
                              int buf_len,
                              CompletionOnceCallback callback) {
  // If the buffer is already in use, a read is already in progress!
  DCHECK(!pending_read_buf_);
  // Use base::Unretained() is safe because MockClientSocket::RunCallbackAsync()
  // takes a weak ptr of the base class, MockClientSocket.
  int rv = ReadIfReadyImpl(
      buf, buf_len,
      base::BindOnce(&MockTCPClientSocket::RetryRead, base::Unretained(this)));
  if (rv == ERR_IO_PENDING) {
    DCHECK(callback);

    pending_read_buf_ = buf;
    pending_read_buf_len_ = buf_len;
    pending_read_callback_ = std::move(callback);
  }
  return rv;
}

int MockTCPClientSocket::ReadIfReady(IOBuffer* buf,
                                     int buf_len,
                                     CompletionOnceCallback callback) {
  DCHECK(!pending_read_if_ready_callback_);

  if (!enable_read_if_ready_)
    return ERR_READ_IF_READY_NOT_IMPLEMENTED;
  return ReadIfReadyImpl(buf, buf_len, std::move(callback));
}

int MockTCPClientSocket::CancelReadIfReady() {
  DCHECK(pending_read_if_ready_callback_);

  pending_read_if_ready_callback_.Reset();
  data_->CancelPendingRead();
  return OK;
}

int MockTCPClientSocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& /* traffic_annotation */) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);

  if (!connected_ || !data_)
    return ERR_UNEXPECTED;

  std::string data(buf->data(), buf_len);
  MockWriteResult write_result = data_->OnWrite(data);

  was_used_to_convey_data_ = true;

  if (write_result.result == ERR_CONNECTION_CLOSED) {
    // This MockWrite is just a marker to instruct us to set
    // peer_closed_connection_.
    peer_closed_connection_ = true;
  }
  // ERR_IO_PENDING is a signal that the socket data will call back
  // asynchronously later.
  if (write_result.result == ERR_IO_PENDING) {
    pending_write_callback_ = std::move(callback);
    return ERR_IO_PENDING;
  }

  if (write_result.mode == ASYNC) {
    RunCallbackAsync(std::move(callback), write_result.result);
    return ERR_IO_PENDING;
  }

  return write_result.result;
}

int MockTCPClientSocket::SetReceiveBufferSize(int32_t size) {
  if (!connected_)
    return net::ERR_UNEXPECTED;
  data_->set_receive_buffer_size(size);
  return data_->set_receive_buffer_size_result();
}

int MockTCPClientSocket::SetSendBufferSize(int32_t size) {
  if (!connected_)
    return net::ERR_UNEXPECTED;
  data_->set_send_buffer_size(size);
  return data_->set_send_buffer_size_result();
}

bool MockTCPClientSocket::SetNoDelay(bool no_delay) {
  if (!connected_)
    return false;
  data_->set_no_delay(no_delay);
  return data_->set_no_delay_result();
}

bool MockTCPClientSocket::SetKeepAlive(bool enable, int delay) {
  if (!connected_)
    return false;
  data_->set_keep_alive(enable, delay);
  return data_->set_keep_alive_result();
}

void MockTCPClientSocket::SetBeforeConnectCallback(
    const BeforeConnectCallback& before_connect_callback) {
  DCHECK(!before_connect_callback_);
  DCHECK(!connected_);

  before_connect_callback_ = before_connect_callback;
}

int MockTCPClientSocket::Connect(CompletionOnceCallback callback) {
  if (!data_)
    return ERR_UNEXPECTED;

  if (connected_)
    return OK;

  // Setting socket options fails if not connected, so need to set this before
  // calling |before_connect_callback_|.
  connected_ = true;

  if (before_connect_callback_) {
    for (size_t index = 0; index < addresses_.size(); index++) {
      int result = before_connect_callback_.Run();
      if (data_->connect_data().first_attempt_fails && index == 0) {
        continue;
      }
      DCHECK_NE(result, ERR_IO_PENDING);
      if (result != net::OK) {
        connected_ = false;
        return result;
      }
      break;
    }
  }

  peer_closed_connection_ = false;

  if (data_->connect_data().completer) {
    data_->connect_data().completer->SetCallback(std::move(callback));
    return ERR_IO_PENDING;
  }

  int result = data_->connect_data().result;
  IoMode mode = data_->connect_data().mode;
  if (mode == SYNCHRONOUS)
    return result;

  DCHECK(callback);

  if (result == ERR_IO_PENDING)
    pending_connect_callback_ = std::move(callback);
  else
    RunCallbackAsync(std::move(callback), result);
  return ERR_IO_PENDING;
}

void MockTCPClientSocket::Disconnect() {
  MockClientSocket::Disconnect();
  pending_connect_callback_.Reset();
  pending_read_callback_.Reset();
}

bool MockTCPClientSocket::IsConnected() const {
  if (!data_)
    return false;
  return connected_ && !peer_closed_connection_;
}

bool MockTCPClientSocket::IsConnectedAndIdle() const {
  if (!data_)
    return false;
  return IsConnected() && data_->IsIdle();
}

int MockTCPClientSocket::GetPeerAddress(IPEndPoint* address) const {
  if (addresses_.empty())
    return MockClientSocket::GetPeerAddress(address);

  if (data_->connect_data().first_attempt_fails) {
    DCHECK_GE(addresses_.size(), 2U);
    *address = addresses_[1];
  } else {
    *address = addresses_[0];
  }
  return OK;
}

bool MockTCPClientSocket::WasEverUsed() const {
  return was_used_to_convey_data_;
}

bool MockTCPClientSocket::GetSSLInfo(SSLInfo* ssl_info) {
  return false;
}

void MockTCPClientSocket::OnReadComplete(const MockRead& data) {
  // If |data_| has been destroyed, safest to just do nothing.
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(pending_read_if_ready_callback_);
  // You can't complete a read with another ERR_IO_PENDING status code.
  DCHECK_NE(ERR_IO_PENDING, data.result);
  // Since we've been waiting for data, need_read_data_ should be true.
  DCHECK(need_read_data_);

  read_data_ = data;
  need_read_data_ = false;

  // The caller is simulating that this IO completes right now.  Don't
  // let CompleteRead() schedule a callback.
  read_data_.mode = SYNCHRONOUS;
  RunCallback(std::move(pending_read_if_ready_callback_),
              read_data_.result > 0 ? OK : read_data_.result);
}

void MockTCPClientSocket::OnWriteComplete(int rv) {
  // If |data_| has been destroyed, safest to just do nothing.
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(!pending_write_callback_.is_null());
  RunCallback(std::move(pending_write_callback_), rv);
}

void MockTCPClientSocket::OnConnectComplete(const MockConnect& data) {
  // If |data_| has been destroyed, safest to just do nothing.
  if (!data_)
    return;

  RunCallback(std::move(pending_connect_callback_), data.result);
}

void MockTCPClientSocket::OnDataProviderDestroyed() {
  data_ = nullptr;
}

void MockTCPClientSocket::RetryRead(int rv) {
  DCHECK(pending_read_callback_);
  DCHECK(pending_read_buf_.get());
  DCHECK_LT(0, pending_read_buf_len_);

  if (rv == OK) {
    rv = ReadIfReadyImpl(pending_read_buf_.get(), pending_read_buf_len_,
                         base::BindOnce(&MockTCPClientSocket::RetryRead,
                                        base::Unretained(this)));
    if (rv == ERR_IO_PENDING)
      return;
  }
  pending_read_buf_ = nullptr;
  pending_read_buf_len_ = 0;
  RunCallback(std::move(pending_read_callback_), rv);
}

int MockTCPClientSocket::ReadIfReadyImpl(IOBuffer* buf,
                                         int buf_len,
                                         CompletionOnceCallback callback) {
  if (!connected_ || !data_)
    return ERR_UNEXPECTED;

  DCHECK(!pending_read_if_ready_callback_);

  if (need_read_data_) {
    read_data_ = data_->OnRead();
    if (read_data_.result == ERR_CONNECTION_CLOSED) {
      // This MockRead is just a marker to instruct us to set
      // peer_closed_connection_.
      peer_closed_connection_ = true;
    }
    if (read_data_.result == ERR_TEST_PEER_CLOSE_AFTER_NEXT_MOCK_READ) {
      // This MockRead is just a marker to instruct us to set
      // peer_closed_connection_.  Skip it and get the next one.
      read_data_ = data_->OnRead();
      peer_closed_connection_ = true;
    }
    // ERR_IO_PENDING means that the SocketDataProvider is taking responsibility
    // to complete the async IO manually later (via OnReadComplete).
    if (read_data_.result == ERR_IO_PENDING) {
      // We need to be using async IO in this case.
      DCHECK(!callback.is_null());
      pending_read_if_ready_callback_ = std::move(callback);
      return ERR_IO_PENDING;
    }
    need_read_data_ = false;
  }

  int result = read_data_.result;
  DCHECK_NE(ERR_IO_PENDING, result);
  if (read_data_.mode == ASYNC) {
    DCHECK(!callback.is_null());
    read_data_.mode = SYNCHRONOUS;
    pending_read_if_ready_callback_ = std::move(callback);
    // base::Unretained() is safe here because RunCallbackAsync will wrap it
    // with a callback associated with a weak ptr.
    RunCallbackAsync(
        base::BindOnce(&MockTCPClientSocket::RunReadIfReadyCallback,
                       base::Unretained(this)),
        result);
    return ERR_IO_PENDING;
  }

  was_used_to_convey_data_ = true;
  if (read_data_.data) {
    if (read_data_.data_len - read_offset_ > 0) {
      result = std::min(buf_len, read_data_.data_len - read_offset_);
      memcpy(buf->data(), read_data_.data + read_offset_, result);
      read_offset_ += result;
      if (read_offset_ == read_data_.data_len) {
        need_read_data_ = true;
        read_offset_ = 0;
      }
    } else {
      result = 0;  // EOF
    }
  }
  return result;
}

void MockTCPClientSocket::RunReadIfReadyCallback(int result) {
  // If ReadIfReady is already canceled, do nothing.
  if (!pending_read_if_ready_callback_)
    return;
  std::move(pending_read_if_ready_callback_).Run(result);
}

// static
void MockSSLClientSocket::ConnectCallback(
    MockSSLClientSocket* ssl_client_socket,
    CompletionOnceCallback callback,
    int rv) {
  if (rv == OK)
    ssl_client_socket->connected_ = true;
  std::move(callback).Run(rv);
}

MockSSLClientSocket::MockSSLClientSocket(
    std::unique_ptr<StreamSocket> stream_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    SSLSocketDataProvider* data)
    : net_log_(stream_socket->NetLog()),
      stream_socket_(std::move(stream_socket)),
      data_(data) {
  DCHECK(data_);
  peer_addr_ = data->connect.peer_addr;
}

MockSSLClientSocket::~MockSSLClientSocket() {
  Disconnect();
}

int MockSSLClientSocket::Read(IOBuffer* buf,
                              int buf_len,
                              CompletionOnceCallback callback) {
  return stream_socket_->Read(buf, buf_len, std::move(callback));
}

int MockSSLClientSocket::ReadIfReady(IOBuffer* buf,
                                     int buf_len,
                                     CompletionOnceCallback callback) {
  return stream_socket_->ReadIfReady(buf, buf_len, std::move(callback));
}

int MockSSLClientSocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  if (!data_->is_confirm_data_consumed)
    data_->write_called_before_confirm = true;
  return stream_socket_->Write(buf, buf_len, std::move(callback),
                               traffic_annotation);
}

int MockSSLClientSocket::CancelReadIfReady() {
  return stream_socket_->CancelReadIfReady();
}

int MockSSLClientSocket::Connect(CompletionOnceCallback callback) {
  DCHECK(stream_socket_->IsConnected());
  data_->is_connect_data_consumed = true;
  if (data_->connect.completer) {
    data_->connect.completer->SetCallback(std::move(callback));
    return ERR_IO_PENDING;
  }
  if (data_->connect.result == OK)
    connected_ = true;
  RunClosureIfNonNull(std::move(data_->connect_callback));
  if (data_->connect.mode == ASYNC) {
    RunCallbackAsync(std::move(callback), data_->connect.result);
    return ERR_IO_PENDING;
  }
  return data_->connect.result;
}

void MockSSLClientSocket::Disconnect() {
  if (stream_socket_ != nullptr)
    stream_socket_->Disconnect();
}

void MockSSLClientSocket::RunConfirmHandshakeCallback(
    CompletionOnceCallback callback,
    int result) {
  DCHECK(in_confirm_handshake_);
  in_confirm_handshake_ = false;
  data_->is_confirm_data_consumed = true;
  std::move(callback).Run(result);
}

int MockSSLClientSocket::ConfirmHandshake(CompletionOnceCallback callback) {
  DCHECK(stream_socket_->IsConnected());
  DCHECK(!in_confirm_handshake_);
  if (data_->is_confirm_data_consumed)
    return data_->confirm.result;
  RunClosureIfNonNull(std::move(data_->confirm_callback));
  if (data_->confirm.mode == ASYNC) {
    in_confirm_handshake_ = true;
    RunCallbackAsync(
        base::BindOnce(&MockSSLClientSocket::RunConfirmHandshakeCallback,
                       base::Unretained(this), std::move(callback)),
        data_->confirm.result);
    return ERR_IO_PENDING;
  }
  data_->is_confirm_data_consumed = true;
  if (data_->confirm.result == ERR_IO_PENDING) {
    // `MockConfirm(SYNCHRONOUS, ERR_IO_PENDING)` means `ConfirmHandshake()`
    // never completes.
    in_confirm_handshake_ = true;
  }
  return data_->confirm.result;
}

bool MockSSLClientSocket::IsConnected() const {
  return stream_socket_->IsConnected();
}

bool MockSSLClientSocket::IsConnectedAndIdle() const {
  return stream_socket_->IsConnectedAndIdle();
}

bool MockSSLClientSocket::WasEverUsed() const {
  return stream_socket_->WasEverUsed();
}

int MockSSLClientSocket::GetLocalAddress(IPEndPoint* address) const {
  *address = IPEndPoint(IPAddress(192, 0, 2, 33), 123);
  return OK;
}

int MockSSLClientSocket::GetPeerAddress(IPEndPoint* address) const {
  return stream_socket_->GetPeerAddress(address);
}

NextProto MockSSLClientSocket::GetNegotiatedProtocol() const {
  return data_->next_proto;
}

std::optional<std::string_view>
MockSSLClientSocket::GetPeerApplicationSettings() const {
  return data_->peer_application_settings;
}

bool MockSSLClientSocket::GetSSLInfo(SSLInfo* requested_ssl_info) {
  *requested_ssl_info = data_->ssl_info;
  return true;
}

void MockSSLClientSocket::ApplySocketTag(const SocketTag& tag) {
  return stream_socket_->ApplySocketTag(tag);
}

const NetLogWithSource& MockSSLClientSocket::NetLog() const {
  return net_log_;
}

int64_t MockSSLClientSocket::GetTotalReceivedBytes() const {
  NOTIMPLEMENTED();
  return 0;
}

int64_t MockClientSocket::GetTotalReceivedBytes() const {
  NOTIMPLEMENTED();
  return 0;
}

int MockSSLClientSocket::SetReceiveBufferSize(int32_t size) {
  return OK;
}

int MockSSLClientSocket::SetSendBufferSize(int32_t size) {
  return OK;
}

void MockSSLClientSocket::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) const {
  DCHECK(cert_request_info);
  if (data_->cert_request_info) {
    cert_request_info->host_and_port = data_->cert_request_info->host_and_port;
    cert_request_info->is_proxy = data_->cert_request_info->is_proxy;
    cert_request_info->cert_authorities =
        data_->cert_request_info->cert_authorities;
    cert_request_info->signature_algorithms =
        data_->cert_request_info->signature_algorithms;
  } else {
    cert_request_info->Reset();
  }
}

int MockSSLClientSocket::ExportKeyingMaterial(
    std::string_view label,
    std::optional<base::span<const uint8_t>> context,
    base::span<uint8_t> out) {
  std::ranges::fill(out, 'A');
  return OK;
}

std::vector<uint8_t> MockSSLClientSocket::GetECHRetryConfigs() {
  return data_->ech_retry_configs;
}

void MockSSLClientSocket::RunCallbackAsync(CompletionOnceCallback callback,
                                           int result) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MockSSLClientSocket::RunCallback,
                     weak_factory_.GetWeakPtr(), std::move(callback), result));
}

void MockSSLClientSocket::RunCallback(CompletionOnceCallback callback,
                                      int result) {
  std::move(callback).Run(result);
}

void MockSSLClientSocket::OnReadComplete(const MockRead& data) {
  NOTIMPLEMENTED();
}

void MockSSLClientSocket::OnWriteComplete(int rv) {
  NOTIMPLEMENTED();
}

void MockSSLClientSocket::OnConnectComplete(const MockConnect& data) {
  NOTIMPLEMENTED();
}

MockUDPClientSocket::MockUDPClientSocket(SocketDataProvider* data,
                                         net::NetLog* net_log)
    : data_(data),
      read_data_(SYNCHRONOUS, ERR_UNEXPECTED),
      source_host_(IPAddress(192, 0, 2, 33)),
      net_log_(NetLogWithSource::Make(net_log,
                                      NetLogSourceType::UDP_CLIENT_SOCKET)) {
  if (data_) {
    data_->Initialize(this);
    peer_addr_ = data->connect_data().peer_addr;
  }
}

MockUDPClientSocket::~MockUDPClientSocket() {
  if (data_)
    data_->DetachSocket();
}

int MockUDPClientSocket::Read(IOBuffer* buf,
                              int buf_len,
                              CompletionOnceCallback callback) {
  DCHECK(callback);

  if (!connected_ || !data_)
    return ERR_UNEXPECTED;
  data_transferred_ = true;

  // If the buffer is already in use, a read is already in progress!
  DCHECK(!pending_read_buf_);

  // Store our async IO data.
  pending_read_buf_ = buf;
  pending_read_buf_len_ = buf_len;
  pending_read_callback_ = std::move(callback);

  if (need_read_data_) {
    read_data_ = data_->OnRead();
    last_tos_ = read_data_.tos;
    // ERR_IO_PENDING means that the SocketDataProvider is taking responsibility
    // to complete the async IO manually later (via OnReadComplete).
    if (read_data_.result == ERR_IO_PENDING) {
      // We need to be using async IO in this case.
      DCHECK(!pending_read_callback_.is_null());
      return ERR_IO_PENDING;
    }
    need_read_data_ = false;
  }

  return CompleteRead();
}

int MockUDPClientSocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& /* traffic_annotation */) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);
  DCHECK(callback);

  if (!connected_ || !data_)
    return ERR_UNEXPECTED;
  data_transferred_ = true;

  std::string data(buf->data(), buf_len);
  MockWriteResult write_result = data_->OnWrite(data);

  // ERR_IO_PENDING is a signal that the socket data will call back
  // asynchronously.
  if (write_result.result == ERR_IO_PENDING) {
    pending_write_callback_ = std::move(callback);
    return ERR_IO_PENDING;
  }
  if (write_result.mode == ASYNC) {
    RunCallbackAsync(std::move(callback), write_result.result);
    return ERR_IO_PENDING;
  }
  return write_result.result;
}

int MockUDPClientSocket::SetReceiveBufferSize(int32_t size) {
  return OK;
}

int MockUDPClientSocket::SetSendBufferSize(int32_t size) {
  return OK;
}

int MockUDPClientSocket::SetDoNotFragment() {
  return OK;
}

int MockUDPClientSocket::SetRecvTos() {
  return OK;
}

int MockUDPClientSocket::SetTos(DiffServCodePoint dscp, EcnCodePoint ecn) {
  return OK;
}

void MockUDPClientSocket::Close() {
  connected_ = false;
}

int MockUDPClientSocket::GetPeerAddress(IPEndPoint* address) const {
  if (!data_)
    return ERR_UNEXPECTED;

  *address = peer_addr_;
  return OK;
}

int MockUDPClientSocket::GetLocalAddress(IPEndPoint* address) const {
  *address = IPEndPoint(source_host_, source_port_);
  return OK;
}

void MockUDPClientSocket::UseNonBlockingIO() {}

int MockUDPClientSocket::SetMulticastInterface(uint32_t interface_index) {
  return OK;
}

const NetLogWithSource& MockUDPClientSocket::NetLog() const {
  return net_log_;
}

int MockUDPClientSocket::Connect(const IPEndPoint& address) {
  if (!data_)
    return ERR_UNEXPECTED;
  DCHECK_NE(data_->connect_data().result, ERR_IO_PENDING);
  connected_ = true;
  peer_addr_ = address;
  return data_->connect_data().result;
}

int MockUDPClientSocket::ConnectUsingNetwork(handles::NetworkHandle network,
                                             const IPEndPoint& address) {
  DCHECK(!connected_);
  if (!data_)
    return ERR_UNEXPECTED;
  DCHECK_NE(data_->connect_data().result, ERR_IO_PENDING);
  network_ = network;
  connected_ = true;
  peer_addr_ = address;
  return data_->connect_data().result;
}

int MockUDPClientSocket::ConnectUsingDefaultNetwork(const IPEndPoint& address) {
  DCHECK(!connected_);
  if (!data_)
    return ERR_UNEXPECTED;
  DCHECK_NE(data_->connect_data().result, ERR_IO_PENDING);
  network_ = kDefaultNetworkForTests;
  connected_ = true;
  peer_addr_ = address;
  return data_->connect_data().result;
}

int MockUDPClientSocket::ConnectAsync(const IPEndPoint& address,
                                      CompletionOnceCallback callback) {
  DCHECK(callback);
  if (!data_) {
    return ERR_UNEXPECTED;
  }
  connected_ = true;
  peer_addr_ = address;
  int result = data_->connect_data().result;
  IoMode mode = data_->connect_data().mode;
  if (data_->connect_data().completer) {
    data_->connect_data().completer->SetCallback(std::move(callback));
    return ERR_IO_PENDING;
  }
  if (mode == SYNCHRONOUS) {
    return result;
  }
  RunCallbackAsync(std::move(callback), result);
  return ERR_IO_PENDING;
}

int MockUDPClientSocket::ConnectUsingNetworkAsync(
    handles::NetworkHandle network,
    const IPEndPoint& address,
    CompletionOnceCallback callback) {
  DCHECK(callback);
  DCHECK(!connected_);
  if (!data_)
    return ERR_UNEXPECTED;
  network_ = network;
  connected_ = true;
  peer_addr_ = address;
  int result = data_->connect_data().result;
  IoMode mode = data_->connect_data().mode;
  if (data_->connect_data().completer) {
    data_->connect_data().completer->SetCallback(std::move(callback));
    return ERR_IO_PENDING;
  }
  if (mode == SYNCHRONOUS) {
    return result;
  }
  RunCallbackAsync(std::move(callback), result);
  return ERR_IO_PENDING;
}

int MockUDPClientSocket::ConnectUsingDefaultNetworkAsync(
    const IPEndPoint& address,
    CompletionOnceCallback callback) {
  DCHECK(!connected_);
  if (!data_)
    return ERR_UNEXPECTED;
  network_ = kDefaultNetworkForTests;
  connected_ = true;
  peer_addr_ = address;
  int result = data_->connect_data().result;
  IoMode mode = data_->connect_data().mode;
  if (data_->connect_data().completer) {
    data_->connect_data().completer->SetCallback(std::move(callback));
    return ERR_IO_PENDING;
  }
  if (mode == SYNCHRONOUS) {
    return result;
  }
  RunCallbackAsync(std::move(callback), result);
  return ERR_IO_PENDING;
}

handles::NetworkHandle MockUDPClientSocket::GetBoundNetwork() const {
  return network_;
}

void MockUDPClientSocket::ApplySocketTag(const SocketTag& tag) {
  tagged_before_data_transferred_ &= !data_transferred_ || tag == tag_;
  tag_ = tag;
}

DscpAndEcn MockUDPClientSocket::GetLastTos() const {
  return TosToDscpAndEcn(last_tos_);
}

void MockUDPClientSocket::OnReadComplete(const MockRead& data) {
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(pending_read_buf_.get());
  DCHECK(pending_read_callback_);
  // You can't complete a read with another ERR_IO_PENDING status code.
  DCHECK_NE(ERR_IO_PENDING, data.result);
  // Since we've been waiting for data, need_read_data_ should be true.
  DCHECK(need_read_data_);

  read_data_ = data;
  last_tos_ = data.tos;
  need_read_data_ = false;

  // The caller is simulating that this IO completes right now.  Don't
  // let CompleteRead() schedule a callback.
  read_data_.mode = SYNCHRONOUS;

  CompletionOnceCallback callback = std::move(pending_read_callback_);
  int rv = CompleteRead();
  RunCallback(std::move(callback), rv);
}

void MockUDPClientSocket::OnWriteComplete(int rv) {
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(!pending_write_callback_.is_null());
  RunCallback(std::move(pending_write_callback_), rv);
}

void MockUDPClientSocket::OnConnectComplete(const MockConnect& data) {
  NOTIMPLEMENTED();
}

void MockUDPClientSocket::OnDataProviderDestroyed() {
  data_ = nullptr;
}

int MockUDPClientSocket::CompleteRead() {
  DCHECK(pending_read_buf_.get());
  DCHECK(pending_read_buf_len_ > 0);

  // Save the pending async IO data and reset our |pending_| state.
  scoped_refptr<IOBuffer> buf = pending_read_buf_;
  int buf_len = pending_read_buf_len_;
  CompletionOnceCallback callback = std::move(pending_read_callback_);
  pending_read_buf_ = nullptr;
  pending_read_buf_len_ = 0;

  int result = read_data_.result;
  DCHECK(result != ERR_IO_PENDING);

  if (read_data_.data) {
    if (read_data_.data_len - read_offset_ > 0) {
      result = std::min(buf_len, read_data_.data_len - read_offset_);
      memcpy(buf->data(), read_data_.data + read_offset_, result);
      read_offset_ += result;
      if (read_offset_ == read_data_.data_len) {
        need_read_data_ = true;
        read_offset_ = 0;
      }
    } else {
      result = 0;  // EOF
    }
  }

  if (read_data_.mode == ASYNC) {
    DCHECK(!callback.is_null());
    RunCallbackAsync(std::move(callback), result);
    return ERR_IO_PENDING;
  }
  return result;
}

void MockUDPClientSocket::RunCallbackAsync(CompletionOnceCallback callback,
                                           int result) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MockUDPClientSocket::RunCallback,
                     weak_factory_.GetWeakPtr(), std::move(callback), result));
}

void MockUDPClientSocket::RunCallback(CompletionOnceCallback callback,
                                      int result) {
  std::move(callback).Run(result);
}

TestSocketRequest::TestSocketRequest(
    std::vector<raw_ptr<TestSocketRequest, VectorExperimental>>* request_order,
    size_t* completion_count)
    : request_order_(request_order), completion_count_(completion_count) {
  DCHECK(request_order);
  DCHECK(completion_count);
}

TestSocketRequest::~TestSocketRequest() = default;

void TestSocketRequest::OnComplete(int result) {
  SetResult(result);
  (*completion_count_)++;
  request_order_->push_back(this);
}

// static
const int ClientSocketPoolTest::kIndexOutOfBounds = -1;

// static
const int ClientSocketPoolTest::kRequestNotFound = -2;

ClientSocketPoolTest::ClientSocketPoolTest() = default;
ClientSocketPoolTest::~ClientSocketPoolTest() = default;

int ClientSocketPoolTest::GetOrderOfRequest(size_t index) const {
  index--;
  if (index >= requests_.size())
    return kIndexOutOfBounds;

  for (size_t i = 0; i < request_order_.size(); i++)
    if (requests_[index].get() == request_order_[i])
      return i + 1;

  return kRequestNotFound;
}

bool ClientSocketPoolTest::ReleaseOneConnection(KeepAlive keep_alive) {
  for (std::unique_ptr<TestSocketRequest>& it : requests_) {
    if (it->handle()->is_initialized()) {
      if (keep_alive == NO_KEEP_ALIVE)
        it->handle()->socket()->Disconnect();
      it->handle()->Reset();
      base::RunLoop().RunUntilIdle();
      return true;
    }
  }
  return false;
}

void ClientSocketPoolTest::ReleaseAllConnections(KeepAlive keep_alive) {
  bool released_one;
  do {
    released_one = ReleaseOneConnection(keep_alive);
  } while (released_one);
}

MockTransportClientSocketPool::MockConnectJob::MockConnectJob(
    std::unique_ptr<StreamSocket> socket,
    ClientSocketHandle* handle,
    const SocketTag& socket_tag,
    CompletionOnceCallback callback,
    RequestPriority priority)
    : socket_(std::move(socket)),
      handle_(handle),
      socket_tag_(socket_tag),
      user_callback_(std::move(callback)),
      priority_(priority) {}

MockTransportClientSocketPool::MockConnectJob::~MockConnectJob() = default;

int MockTransportClientSocketPool::MockConnectJob::Connect() {
  socket_->ApplySocketTag(socket_tag_);
  int rv = socket_->Connect(
      base::BindOnce(&MockConnectJob::OnConnect, base::Unretained(this)));
  if (rv != ERR_IO_PENDING) {
    user_callback_.Reset();
    OnConnect(rv);
  }
  return rv;
}

bool MockTransportClientSocketPool::MockConnectJob::CancelHandle(
    const ClientSocketHandle* handle) {
  if (handle != handle_)
    return false;
  socket_.reset();
  handle_ = nullptr;
  user_callback_.Reset();
  return true;
}

void MockTransportClientSocketPool::MockConnectJob::OnConnect(int rv) {
  if (!socket_.get())
    return;
  if (rv == OK) {
    handle_->SetSocket(std::move(socket_));

    // Needed for socket pool tests that layer other sockets on top of mock
    // sockets.
    LoadTimingInfo::ConnectTiming connect_timing;
    base::TimeTicks now = base::TimeTicks::Now();
    connect_timing.domain_lookup_start = now;
    connect_timing.domain_lookup_end = now;
    connect_timing.connect_start = now;
    connect_timing.connect_end = now;
    handle_->set_connect_timing(connect_timing);
  } else {
    socket_.reset();

    // Needed to test copying of ConnectionAttempts in SSL ConnectJob.
    ConnectionAttempts attempts;
    attempts.push_back(ConnectionAttempt(IPEndPoint(), rv));
    handle_->set_connection_attempts(attempts);
  }

  handle_ = nullptr;

  if (!user_callback_.is_null()) {
    std::move(user_callback_).Run(rv);
  }
}

MockTransportClientSocketPool::MockTransportClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    const CommonConnectJobParams* common_connect_job_params)
    : TransportClientSocketPool(
          max_sockets,
          max_sockets_per_group,
          base::Seconds(10) /* unused_idle_socket_timeout */,
          ProxyChain::Direct(),
          false /* is_for_websockets */,
          common_connect_job_params),
      client_socket_factory_(common_connect_job_params->client_socket_factory) {
}

MockTransportClientSocketPool::~MockTransportClientSocketPool() = default;

int MockTransportClientSocketPool::RequestSocket(
    const ClientSocketPool::GroupId& group_id,
    scoped_refptr<ClientSocketPool::SocketParams> socket_params,
    const std::optional<NetworkTrafficAnnotationTag>& proxy_annotation_tag,
    RequestPriority priority,
    const SocketTag& socket_tag,
    RespectLimits respect_limits,
    ClientSocketHandle* handle,
    CompletionOnceCallback callback,
    const ProxyAuthCallback& on_auth_callback,
    bool fail_if_alias_requires_proxy_override,
    const NetLogWithSource& net_log) {
  last_request_priority_ = priority;
  std::unique_ptr<StreamSocket> socket =
      client_socket_factory_->CreateTransportClientSocket(
          AddressList(), nullptr, nullptr, net_log.net_log(), NetLogSource());
  auto job = std::make_unique<MockConnectJob>(
      std::move(socket), handle, socket_tag, std::move(callback), priority);
  auto* job_ptr = job.get();
  job_list_.push_back(std::move(job));
  handle->set_group_generation(1);
  return job_ptr->Connect();
}

void MockTransportClientSocketPool::SetPriority(
    const ClientSocketPool::GroupId& group_id,
    ClientSocketHandle* handle,
    RequestPriority priority) {
  for (auto& job : job_list_) {
    if (job->handle() == handle) {
      job->set_priority(priority);
      return;
    }
  }
  NOTREACHED();
}

void MockTransportClientSocketPool::CancelRequest(
    const ClientSocketPool::GroupId& group_id,
    ClientSocketHandle* handle,
    bool cancel_connect_job) {
  for (std::unique_ptr<MockConnectJob>& it : job_list_) {
    if (it->CancelHandle(handle)) {
      cancel_count_++;
      break;
    }
  }
}

void MockTransportClientSocketPool::ReleaseSocket(
    const ClientSocketPool::GroupId& group_id,
    std::unique_ptr<StreamSocket> socket,
    int64_t generation) {
  EXPECT_EQ(1, generation);
  release_count_++;
}

WrappedStreamSocket::WrappedStreamSocket(
    std::unique_ptr<StreamSocket> transport)
    : transport_(std::move(transport)) {}
WrappedStreamSocket::~WrappedStreamSocket() = default;

int WrappedStreamSocket::Bind(const net::IPEndPoint& local_addr) {
  NOTREACHED();
}

int WrappedStreamSocket::Connect(CompletionOnceCallback callback) {
  return transport_->Connect(std::move(callback));
}

void WrappedStreamSocket::Disconnect() {
  transport_->Disconnect();
}

bool WrappedStreamSocket::IsConnected() const {
  return transport_->IsConnected();
}

bool WrappedStreamSocket::IsConnectedAndIdle() const {
  return transport_->IsConnectedAndIdle();
}

int WrappedStreamSocket::GetPeerAddress(IPEndPoint* address) const {
  return transport_->GetPeerAddress(address);
}

int WrappedStreamSocket::GetLocalAddress(IPEndPoint* address) const {
  return transport_->GetLocalAddress(address);
}

const NetLogWithSource& WrappedStreamSocket::NetLog() const {
  return transport_->NetLog();
}

bool WrappedStreamSocket::WasEverUsed() const {
  return transport_->WasEverUsed();
}

NextProto WrappedStreamSocket::GetNegotiatedProtocol() const {
  return transport_->GetNegotiatedProtocol();
}

bool WrappedStreamSocket::GetSSLInfo(SSLInfo* ssl_info) {
  return transport_->GetSSLInfo(ssl_info);
}

int64_t WrappedStreamSocket::GetTotalReceivedBytes() const {
  return transport_->GetTotalReceivedBytes();
}

void WrappedStreamSocket::ApplySocketTag(const SocketTag& tag) {
  transport_->ApplySocketTag(tag);
}

int WrappedStreamSocket::Read(IOBuffer* buf,
                              int buf_len,
                              CompletionOnceCallback callback) {
  return transport_->Read(buf, buf_len, std::move(callback));
}

int WrappedStreamSocket::ReadIfReady(IOBuffer* buf,
                                     int buf_len,
                                     CompletionOnceCallback callback) {
  return transport_->ReadIfReady(buf, buf_len, std::move((callback)));
}

int WrappedStreamSocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  return transport_->Write(buf, buf_len, std::move(callback),
                           TRAFFIC_ANNOTATION_FOR_TESTS);
}

int WrappedStreamSocket::SetReceiveBufferSize(int32_t size) {
  return transport_->SetReceiveBufferSize(size);
}

int WrappedStreamSocket::SetSendBufferSize(int32_t size) {
  return transport_->SetSendBufferSize(size);
}

int MockTaggingStreamSocket::Connect(CompletionOnceCallback callback) {
  connected_ = true;
  return WrappedStreamSocket::Connect(std::move(callback));
}

void MockTaggingStreamSocket::ApplySocketTag(const SocketTag& tag) {
  tagged_before_connected_ &= !connected_ || tag == tag_;
  tag_ = tag;
  transport_->ApplySocketTag(tag);
}

std::unique_ptr<TransportClientSocket>
MockTaggingClientSocketFactory::CreateTransportClientSocket(
    const AddressList& addresses,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    NetworkQualityEstimator* network_quality_estimator,
    NetLog* net_log,
    const NetLogSource& source) {
  auto socket = std::make_unique<MockTaggingStreamSocket>(
      MockClientSocketFactory::CreateTransportClientSocket(
          addresses, std::move(socket_performance_watcher),
          network_quality_estimator, net_log, source));
  tcp_socket_ = socket.get();
  return std::move(socket);
}

std::unique_ptr<DatagramClientSocket>
MockTaggingClientSocketFactory::CreateDatagramClientSocket(
    DatagramSocket::BindType bind_type,
    NetLog* net_log,
    const NetLogSource& source) {
  std::unique_ptr<DatagramClientSocket> socket(
      MockClientSocketFactory::CreateDatagramClientSocket(bind_type, net_log,
                                                          source));
  udp_socket_ = static_cast<MockUDPClientSocket*>(socket.get());
  return socket;
}

const char kSOCKS4TestHost[] = "127.0.0.1";
const int kSOCKS4TestPort = 80;

const char kSOCKS4OkRequestLocalHostPort80[] = {0x04, 0x01, 0x00, 0x50, 127,
                                                0,    0,    1,    0};
const int kSOCKS4OkRequestLocalHostPort80Length =
    std::size(kSOCKS4OkRequestLocalHostPort80);

const char kSOCKS4OkReply[] = {0x00, 0x5A, 0x00, 0x00, 0, 0, 0, 0};
const int kSOCKS4OkReplyLength = std::size(kSOCKS4OkReply);

const char kSOCKS5TestHost[] = "host";
const int kSOCKS5TestPort = 80;

const char kSOCKS5GreetRequest[] = {0x05, 0x01, 0x00};
const int kSOCKS5GreetRequestLength = std::size(kSOCKS5GreetRequest);

const char kSOCKS5GreetResponse[] = {0x05, 0x00};
const int kSOCKS5GreetResponseLength = std::size(kSOCKS5GreetResponse);

const char kSOCKS5OkRequest[] = {0x05, 0x01, 0x00, 0x03, 0x04, 'h',
                                 'o',  's',  't',  0x00, 0x50};
const int kSOCKS5OkRequestLength = std::size(kSOCKS5OkRequest);

const char kSOCKS5OkResponse[] = {0x05, 0x00, 0x00, 0x01, 127,
                                  0,    0,    1,    0x00, 0x50};
const int kSOCKS5OkResponseLength = std::size(kSOCKS5OkResponse);

int64_t CountReadBytes(base::span<const MockRead> reads) {
  int64_t total = 0;
  for (const MockRead& read : reads)
    total += read.data_len;
  return total;
}

int64_t CountWriteBytes(base::span<const MockWrite> writes) {
  int64_t total = 0;
  for (const MockWrite& write : writes)
    total += write.data_len;
  return total;
}

#if BUILDFLAG(IS_ANDROID)
bool CanGetTaggedBytes() {
  // In Android P, /proc/net/xt_qtaguid/stats is no longer guaranteed to be
  // present, and has been replaced with eBPF Traffic Monitoring in netd. See:
  // https://source.android.com/devices/tech/datausage/ebpf-traffic-monitor
  //
  // To read traffic statistics from netd, apps should use the API
  // NetworkStatsManager.queryDetailsForUidTag(). But this API does not provide
  // statistics for local traffic, only mobile and WiFi traffic, so it would not
  // work in tests that spin up a local server. So for now, GetTaggedBytes is
  // only supported on Android releases older than P.
  return base::android::BuildInfo::GetInstance()->sdk_int() <
         base::android::SDK_VERSION_P;
}

uint64_t GetTaggedBytes(int32_t expected_tag) {
  EXPECT_TRUE(CanGetTaggedBytes());

  // To determine how many bytes the system saw with a particular tag read
  // the /proc/net/xt_qtaguid/stats file which contains the kernel's
  // dump of all the UIDs and their tags sent and received bytes.
  uint64_t bytes = 0;
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(
      base::FilePath::FromUTF8Unsafe("/proc/net/xt_qtaguid/stats"), &contents));
  for (size_t i = contents.find('\n');  // Skip first line which is headers.
       i != std::string::npos && i < contents.length();) {
    uint64_t tag, rx_bytes;
    uid_t uid;
    int n;
    // Parse out the numbers we care about. For reference here's the column
    // headers:
    // idx iface acct_tag_hex uid_tag_int cnt_set rx_bytes rx_packets tx_bytes
    // tx_packets rx_tcp_bytes rx_tcp_packets rx_udp_bytes rx_udp_packets
    // rx_other_bytes rx_other_packets tx_tcp_bytes tx_tcp_packets tx_udp_bytes
    // tx_udp_packets tx_other_bytes tx_other_packets
    EXPECT_EQ(sscanf(contents.c_str() + i,
                     "%*d %*s 0x%" SCNx64 " %d %*d %" SCNu64
                     " %*d %*d %*d %*d %*d %*d %*d %*d "
                     "%*d %*d %*d %*d %*d %*d %*d%n",
                     &tag, &uid, &rx_bytes, &n),
              3);
    // If this line matches our UID and |expected_tag| then add it to the total.
    if (uid == getuid() && (int32_t)(tag >> 32) == expected_tag) {
      bytes += rx_bytes;
    }
    // Move |i| to the next line.
    i += n + 1;
  }
  return bytes;
}
#endif

}  // namespace net
