// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socket_test_util.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_helpers.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "net/base/address_family.h"
#include "net/base/address_list.h"
#include "net/base/auth.h"
#include "net/base/ip_address.h"
#include "net/base/load_timing_info.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"
#include "net/socket/socket.h"
#include "net/socket/websocket_endpoint_lock_manager.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "net/ssl/ssl_info.h"
#include "testing/gtest/include/gtest/gtest.h"

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
  if ((x < 0) || !isprint(x))
    return '.';
  return x;
}

void DumpData(const char* data, int data_len) {
  if (logging::LOG_INFO < logging::GetMinLogLevel())
    return;
  DVLOG(1) << "Length:  " << data_len;
  const char* pfx = "Data:    ";
  if (!data || (data_len <= 0)) {
    DVLOG(1) << pfx << "<None>";
  } else {
    int i;
    for (i = 0; i <= (data_len - 4); i += 4) {
      DVLOG(1) << pfx
               << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
               << AsciifyHigh(data[i + 1]) << AsciifyLow(data[i + 1])
               << AsciifyHigh(data[i + 2]) << AsciifyLow(data[i + 2])
               << AsciifyHigh(data[i + 3]) << AsciifyLow(data[i + 3])
               << "  '"
               << Asciify(data[i + 0])
               << Asciify(data[i + 1])
               << Asciify(data[i + 2])
               << Asciify(data[i + 3])
               << "'";
      pfx = "         ";
    }
    // Take care of any 'trailing' bytes, if data_len was not a multiple of 4.
    switch (data_len - i) {
      case 3:
        DVLOG(1) << pfx
                 << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
                 << AsciifyHigh(data[i + 1]) << AsciifyLow(data[i + 1])
                 << AsciifyHigh(data[i + 2]) << AsciifyLow(data[i + 2])
                 << "    '"
                 << Asciify(data[i + 0])
                 << Asciify(data[i + 1])
                 << Asciify(data[i + 2])
                 << " '";
        break;
      case 2:
        DVLOG(1) << pfx
                 << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
                 << AsciifyHigh(data[i + 1]) << AsciifyLow(data[i + 1])
                 << "      '"
                 << Asciify(data[i + 0])
                 << Asciify(data[i + 1])
                 << "  '";
        break;
      case 1:
        DVLOG(1) << pfx
                 << AsciifyHigh(data[i + 0]) << AsciifyLow(data[i + 0])
                 << "        '"
                 << Asciify(data[i + 0])
                 << "   '";
        break;
    }
  }
}

template <MockReadWriteType type>
void DumpMockReadWrite(const MockReadWrite<type>& r) {
  if (logging::LOG_INFO < logging::GetMinLogLevel())
    return;
  DVLOG(1) << "Async:   " << (r.mode == ASYNC)
           << "\nResult:  " << r.result;
  DumpData(r.data, r.data_len);
  const char* stop = (r.sequence_number & MockRead::STOPLOOP) ? " (STOP)" : "";
  DVLOG(1) << "Stage:   " << (r.sequence_number & ~MockRead::STOPLOOP) << stop;
}

}  // namespace

MockConnect::MockConnect() : mode(ASYNC), result(OK) {
  peer_addr = IPEndPoint(IPAddress(192, 0, 2, 33), 0);
}

MockConnect::MockConnect(IoMode io_mode, int r) : mode(io_mode), result(r) {
  peer_addr = IPEndPoint(IPAddress(192, 0, 2, 33), 0);
}

MockConnect::MockConnect(IoMode io_mode, int r, IPEndPoint addr) :
    mode(io_mode),
    result(r),
    peer_addr(addr) {
}

MockConnect::~MockConnect() {}

void SocketDataProvider::OnEnableTCPFastOpenIfSupported() {}

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

SocketDataProvider::SocketDataProvider() : socket_(nullptr) {}

SocketDataProvider::~SocketDataProvider() {
  if (socket_)
    socket_->OnDataProviderDestroyed();
}

StaticSocketDataHelper::StaticSocketDataHelper(MockRead* reads,
                                               size_t reads_count,
                                               MockWrite* writes,
                                               size_t writes_count)
    : reads_(reads),
      read_index_(0),
      read_count_(reads_count),
      writes_(writes),
      write_index_(0),
      write_count_(writes_count) {
}

StaticSocketDataHelper::~StaticSocketDataHelper() {
}

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

bool StaticSocketDataHelper::VerifyWriteData(const std::string& data) {
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
  EXPECT_GE(data.length(), expected_data.length());
  EXPECT_EQ(expected_data, actual_data);
  return expected_data == actual_data;
}

const MockWrite& StaticSocketDataHelper::PeekRealWrite() const {
  for (size_t i = write_index_; i < write_count_; i++) {
    if (writes_[i].mode != ASYNC || writes_[i].result != ERR_IO_PENDING)
      return writes_[i];
  }

  CHECK(false) << "No write data available.";
  return writes_[0];  // Avoid warning about unreachable missing return.
}

StaticSocketDataProvider::StaticSocketDataProvider()
    : StaticSocketDataProvider(nullptr, 0, nullptr, 0) {
}

StaticSocketDataProvider::StaticSocketDataProvider(MockRead* reads,
                                                   size_t reads_count,
                                                   MockWrite* writes,
                                                   size_t writes_count)
    : helper_(reads, reads_count, writes, writes_count) {
}

StaticSocketDataProvider::~StaticSocketDataProvider() {
}

MockRead StaticSocketDataProvider::OnRead() {
  CHECK(!helper_.AllReadDataConsumed());
  return helper_.AdvanceRead();
}

MockWriteResult StaticSocketDataProvider::OnWrite(const std::string& data) {
  if (helper_.write_count() == 0) {
    // Not using mock writes; succeed synchronously.
    return MockWriteResult(SYNCHRONOUS, data.length());
  }
  EXPECT_FALSE(helper_.AllWriteDataConsumed());
  if (helper_.AllWriteDataConsumed()) {
    // Show what the extra write actually consists of.
    EXPECT_EQ("<unexpected write>", data);
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);
  }

  // Check that what we are writing matches the expectation.
  // Then give the mocked return value.
  if (!helper_.VerifyWriteData(data))
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);

  const MockWrite& next_write = helper_.AdvanceWrite();
  // In the case that the write was successful, return the number of bytes
  // written. Otherwise return the error code.
  int result =
      next_write.result == OK ? next_write.data_len : next_write.result;
  return MockWriteResult(next_write.mode, result);
}

bool StaticSocketDataProvider::AllReadDataConsumed() const {
  return helper_.AllReadDataConsumed();
}

bool StaticSocketDataProvider::AllWriteDataConsumed() const {
  return helper_.AllWriteDataConsumed();
}

void StaticSocketDataProvider::Reset() {
  helper_.Reset();
}

SSLSocketDataProvider::SSLSocketDataProvider(IoMode mode, int result)
    : connect(mode, result),
      next_proto(kProtoUnknown),
      client_cert_sent(false),
      cert_request_info(NULL),
      cert_status(0),
      channel_id_sent(false),
      connection_status(0),
      token_binding_negotiated(false) {
  SSLConnectionStatusSetVersion(SSL_CONNECTION_VERSION_TLS1_2,
                                &connection_status);
  // Set to TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305
  SSLConnectionStatusSetCipherSuite(0xcca9, &connection_status);
}

SSLSocketDataProvider::SSLSocketDataProvider(
    const SSLSocketDataProvider& other) = default;

SSLSocketDataProvider::~SSLSocketDataProvider() {
}

SequencedSocketData::SequencedSocketData(MockRead* reads,
                                         size_t reads_count,
                                         MockWrite* writes,
                                         size_t writes_count)
    : helper_(reads, reads_count, writes, writes_count),
      sequence_number_(0),
      read_state_(IDLE),
      write_state_(IDLE),
      busy_before_sync_reads_(false),
      is_using_tcp_fast_open_(false),
      weak_factory_(this) {
  // Check that reads and writes have a contiguous set of sequence numbers
  // starting from 0 and working their way up, with no repeats and skipping
  // no values.
  size_t next_read = 0;
  size_t next_write = 0;
  int next_sequence_number = 0;
  bool last_event_was_pause = false;
  while (next_read < reads_count || next_write < writes_count) {
    if (next_read < reads_count &&
        reads[next_read].sequence_number == next_sequence_number) {
      // Check if this is a pause.
      if (reads[next_read].mode == ASYNC &&
          reads[next_read].result == ERR_IO_PENDING) {
        CHECK(!last_event_was_pause) << "Two pauses in a row are not allowed: "
                                     << next_sequence_number;
        last_event_was_pause = true;
      } else if (last_event_was_pause) {
        CHECK_EQ(ASYNC, reads[next_read].mode)
            << "A sync event after a pause makes no sense: "
            << next_sequence_number;
        CHECK_NE(ERR_IO_PENDING, reads[next_read].result)
            << "A pause event after a pause makes no sense: "
            << next_sequence_number;
        last_event_was_pause = false;
      }

      ++next_read;
      ++next_sequence_number;
      continue;
    }
    if (next_write < writes_count &&
        writes[next_write].sequence_number == next_sequence_number) {
      // Check if this is a pause.
      if (writes[next_write].mode == ASYNC &&
          writes[next_write].result == ERR_IO_PENDING) {
        CHECK(!last_event_was_pause) << "Two pauses in a row are not allowed: "
                                     << next_sequence_number;
        last_event_was_pause = true;
      } else if (last_event_was_pause) {
        CHECK_EQ(ASYNC, writes[next_write].mode)
            << "A sync event after a pause makes no sense: "
            << next_sequence_number;
        CHECK_NE(ERR_IO_PENDING, writes[next_write].result)
            << "A pause event after a pause makes no sense: "
            << next_sequence_number;
        last_event_was_pause = false;
      }

      ++next_write;
      ++next_sequence_number;
      continue;
    }
    CHECK(false) << "Sequence number not found where expected: "
                 << next_sequence_number;
    return;
  }

  // Last event must not be a pause.  For the final event to indicate the
  // operation never completes, it should be SYNCHRONOUS and return
  // ERR_IO_PENDING.
  CHECK(!last_event_was_pause);

  CHECK_EQ(next_read, reads_count);
  CHECK_EQ(next_write, writes_count);
}

SequencedSocketData::SequencedSocketData(const MockConnect& connect,
                                         MockRead* reads,
                                         size_t reads_count,
                                         MockWrite* writes,
                                         size_t writes_count)
    : SequencedSocketData(reads, reads_count, writes, writes_count) {
  set_connect_data(connect);
}

MockRead SequencedSocketData::OnRead() {
  CHECK_EQ(IDLE, read_state_);
  CHECK(!helper_.AllReadDataConsumed());

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
      read_state_ = PAUSED;
      if (run_until_paused_run_loop_)
        run_until_paused_run_loop_->Quit();
      return MockRead(SYNCHRONOUS, ERR_IO_PENDING);
    }
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&SequencedSocketData::OnReadComplete,
                              weak_factory_.GetWeakPtr()));
    CHECK_NE(COMPLETING, write_state_);
    read_state_ = COMPLETING;
  } else if (next_read.mode == SYNCHRONOUS) {
    ADD_FAILURE() << "Unable to perform synchronous IO while stopped";
    return MockRead(SYNCHRONOUS, ERR_UNEXPECTED);
  } else {
    NET_TRACE(1, " *** ") << "Waiting for write to trigger read";
    read_state_ = PENDING;
  }

  return MockRead(SYNCHRONOUS, ERR_IO_PENDING);
}

MockWriteResult SequencedSocketData::OnWrite(const std::string& data) {
  CHECK_EQ(IDLE, write_state_);
  CHECK(!helper_.AllWriteDataConsumed());

  NET_TRACE(1, " *** ") << "sequence_number: " << sequence_number_;
  const MockWrite& next_write = helper_.PeekWrite();
  NET_TRACE(1, " *** ") << "next_write: " << next_write.sequence_number;
  CHECK_GE(next_write.sequence_number, sequence_number_);

  if (!helper_.VerifyWriteData(data))
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
      write_state_ = PAUSED;
      if (run_until_paused_run_loop_)
        run_until_paused_run_loop_->Quit();
      return MockWriteResult(SYNCHRONOUS, ERR_IO_PENDING);
    }

    NET_TRACE(1, " *** ") << "Posting task to complete write";
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&SequencedSocketData::OnWriteComplete,
                              weak_factory_.GetWeakPtr()));
    CHECK_NE(COMPLETING, read_state_);
    write_state_ = COMPLETING;
  } else if (next_write.mode == SYNCHRONOUS) {
    ADD_FAILURE() << "Unable to perform synchronous IO while stopped";
    return MockWriteResult(SYNCHRONOUS, ERR_UNEXPECTED);
  } else {
    NET_TRACE(1, " *** ") << "Waiting for read to trigger write";
    write_state_ = PENDING;
  }

  return MockWriteResult(SYNCHRONOUS, ERR_IO_PENDING);
}

bool SequencedSocketData::AllReadDataConsumed() const {
  return helper_.AllReadDataConsumed();
}

bool SequencedSocketData::AllWriteDataConsumed() const {
  return helper_.AllWriteDataConsumed();
}

void SequencedSocketData::OnEnableTCPFastOpenIfSupported() {
  is_using_tcp_fast_open_ = true;
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
  DCHECK(read_state_ != PAUSED || write_state_ != PAUSED);
  return write_state_ == PAUSED || read_state_ == PAUSED;
}

void SequencedSocketData::Resume() {
  if (!IsPaused()) {
    ADD_FAILURE() << "Unable to Resume when not paused.";
    return;
  }

  sequence_number_++;
  if (read_state_ == PAUSED) {
    read_state_ = PENDING;
    helper_.AdvanceRead();
  } else {  // write_state_ == PAUSED
    write_state_ = PENDING;
    helper_.AdvanceWrite();
  }

  if (!helper_.AllWriteDataConsumed() &&
      helper_.PeekWrite().sequence_number == sequence_number_) {
    // The next event hasn't even started yet.  Pausing isn't really needed in
    // that case, but may as well support it.
    if (write_state_ != PENDING)
      return;
    write_state_ = COMPLETING;
    OnWriteComplete();
    return;
  }

  CHECK(!helper_.AllReadDataConsumed());

  // The next event hasn't even started yet.  Pausing isn't really needed in
  // that case, but may as well support it.
  if (read_state_ != PENDING)
    return;
  read_state_ = COMPLETING;
  OnReadComplete();
}

void SequencedSocketData::RunUntilPaused() {
  CHECK(!run_until_paused_run_loop_);

  if (IsPaused())
    return;

  run_until_paused_run_loop_.reset(new base::RunLoop());
  run_until_paused_run_loop_->Run();
  run_until_paused_run_loop_.reset();
  DCHECK(IsPaused());
}

void SequencedSocketData::MaybePostReadCompleteTask() {
  NET_TRACE(1, " ****** ") << " current: " << sequence_number_;
  // Only trigger the next read to complete if there is already a read pending
  // which should complete at the current sequence number.
  if (read_state_ != PENDING ||
      helper_.PeekRead().sequence_number != sequence_number_) {
    return;
  }

  // If the result is ERR_IO_PENDING, then pause.
  if (helper_.PeekRead().result == ERR_IO_PENDING) {
    NET_TRACE(1, " *** ") << "Pausing read at: " << sequence_number_;
    read_state_ = PAUSED;
    if (run_until_paused_run_loop_)
      run_until_paused_run_loop_->Quit();
    return;
  }

  NET_TRACE(1, " ****** ") << "Posting task to complete read: "
                           << sequence_number_;
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&SequencedSocketData::OnReadComplete,
                            weak_factory_.GetWeakPtr()));
  CHECK_NE(COMPLETING, write_state_);
  read_state_ = COMPLETING;
}

bool SequencedSocketData::IsUsingTCPFastOpen() const {
  return is_using_tcp_fast_open_;
}

void SequencedSocketData::MaybePostWriteCompleteTask() {
  NET_TRACE(1, " ****** ") << " current: " << sequence_number_;
  // Only trigger the next write to complete if there is already a write pending
  // which should complete at the current sequence number.
  if (write_state_ != PENDING ||
      helper_.PeekWrite().sequence_number != sequence_number_) {
    return;
  }

  // If the result is ERR_IO_PENDING, then pause.
  if (helper_.PeekWrite().result == ERR_IO_PENDING) {
    NET_TRACE(1, " *** ") << "Pausing write at: " << sequence_number_;
    write_state_ = PAUSED;
    if (run_until_paused_run_loop_)
      run_until_paused_run_loop_->Quit();
    return;
  }

  NET_TRACE(1, " ****** ") << "Posting task to complete write: "
                           << sequence_number_;
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&SequencedSocketData::OnWriteComplete,
                            weak_factory_.GetWeakPtr()));
  CHECK_NE(COMPLETING, read_state_);
  write_state_ = COMPLETING;
}

void SequencedSocketData::Reset() {
  helper_.Reset();
  sequence_number_ = 0;
  read_state_ = IDLE;
  write_state_ = IDLE;
  is_using_tcp_fast_open_ = false;
  weak_factory_.InvalidateWeakPtrs();
}

void SequencedSocketData::OnReadComplete() {
  CHECK_EQ(COMPLETING, read_state_);
  NET_TRACE(1, " *** ") << "Completing read for: " << sequence_number_;

  MockRead data = helper_.AdvanceRead();
  DCHECK_EQ(sequence_number_, data.sequence_number);
  sequence_number_++;
  read_state_ = IDLE;

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
  CHECK_EQ(COMPLETING, write_state_);
  NET_TRACE(1, " *** ") << " Completing write for: " << sequence_number_;

  const MockWrite& data = helper_.AdvanceWrite();
  DCHECK_EQ(sequence_number_, data.sequence_number);
  sequence_number_++;
  write_state_ = IDLE;
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

SequencedSocketData::~SequencedSocketData() {
}

MockClientSocketFactory::MockClientSocketFactory()
    : enable_read_if_ready_(false) {}

MockClientSocketFactory::~MockClientSocketFactory() {}

void MockClientSocketFactory::AddSocketDataProvider(
    SocketDataProvider* data) {
  mock_data_.Add(data);
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
    const RandIntCallback& rand_int_cb,
    NetLog* net_log,
    const NetLogSource& source) {
  SocketDataProvider* data_provider = mock_data_.GetNext();
  std::unique_ptr<MockUDPClientSocket> socket(
      new MockUDPClientSocket(data_provider, net_log));
  if (bind_type == DatagramSocket::RANDOM_BIND)
    socket->set_source_port(
        static_cast<uint16_t>(rand_int_cb.Run(1025, 65535)));
  udp_client_socket_ports_.push_back(socket->source_port());
  return std::move(socket);
}

std::unique_ptr<StreamSocket>
MockClientSocketFactory::CreateTransportClientSocket(
    const AddressList& addresses,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    NetLog* net_log,
    const NetLogSource& source) {
  SocketDataProvider* data_provider = mock_data_.GetNext();
  std::unique_ptr<MockTCPClientSocket> socket(
      new MockTCPClientSocket(addresses, net_log, data_provider));
  if (enable_read_if_ready_)
    socket->set_enable_read_if_ready(enable_read_if_ready_);
  return std::move(socket);
}

std::unique_ptr<SSLClientSocket> MockClientSocketFactory::CreateSSLClientSocket(
    std::unique_ptr<ClientSocketHandle> transport_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    const SSLClientSocketContext& context) {
  SSLSocketDataProvider* next_ssl_data = mock_ssl_data_.GetNext();
  if (!next_ssl_data->next_protos_expected_in_ssl_config.empty()) {
    EXPECT_EQ(next_ssl_data->next_protos_expected_in_ssl_config.size(),
              ssl_config.alpn_protos.size());
    EXPECT_TRUE(
        std::equal(next_ssl_data->next_protos_expected_in_ssl_config.begin(),
                   next_ssl_data->next_protos_expected_in_ssl_config.end(),
                   ssl_config.alpn_protos.begin()));
  }
  return std::unique_ptr<SSLClientSocket>(new MockSSLClientSocket(
      std::move(transport_socket), host_and_port, ssl_config, next_ssl_data));
}

void MockClientSocketFactory::ClearSSLSessionCache() {
}

MockClientSocket::MockClientSocket(const NetLogWithSource& net_log)
    : connected_(false), net_log_(net_log), weak_factory_(this) {
  peer_addr_ = IPEndPoint(IPAddress(192, 0, 2, 33), 0);
}

int MockClientSocket::SetReceiveBufferSize(int32_t size) {
  return OK;
}

int MockClientSocket::SetSendBufferSize(int32_t size) {
  return OK;
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
  *address = IPEndPoint(IPAddress(192, 0, 2, 33), 123);
  return OK;
}

const NetLogWithSource& MockClientSocket::NetLog() const {
  return net_log_;
}

bool MockClientSocket::WasAlpnNegotiated() const {
  return false;
}

NextProto MockClientSocket::GetNegotiatedProtocol() const {
  return kProtoUnknown;
}

void MockClientSocket::GetConnectionAttempts(ConnectionAttempts* out) const {
  out->clear();
}

int64_t MockClientSocket::GetTotalReceivedBytes() const {
  NOTIMPLEMENTED();
  return 0;
}

void MockClientSocket::GetSSLCertRequestInfo(
  SSLCertRequestInfo* cert_request_info) {
}

int MockClientSocket::ExportKeyingMaterial(const base::StringPiece& label,
                                           bool has_context,
                                           const base::StringPiece& context,
                                           unsigned char* out,
                                           unsigned int outlen) {
  memset(out, 'A', outlen);
  return OK;
}

ChannelIDService* MockClientSocket::GetChannelIDService() const {
  NOTREACHED();
  return NULL;
}

Error MockClientSocket::GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                                 TokenBindingType tb_type,
                                                 std::vector<uint8_t>* out) {
  NOTREACHED();
  return ERR_NOT_IMPLEMENTED;
}

crypto::ECPrivateKey* MockClientSocket::GetChannelIDKey() const {
  NOTREACHED();
  return NULL;
}

MockClientSocket::~MockClientSocket() {}

void MockClientSocket::RunCallbackAsync(const CompletionCallback& callback,
                                        int result) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&MockClientSocket::RunCallback,
                            weak_factory_.GetWeakPtr(), callback, result));
}

void MockClientSocket::RunCallback(const CompletionCallback& callback,
                                   int result) {
  if (!callback.is_null())
    callback.Run(result);
}

MockTCPClientSocket::MockTCPClientSocket(const AddressList& addresses,
                                         net::NetLog* net_log,
                                         SocketDataProvider* data)
    : MockClientSocket(NetLogWithSource::Make(net_log, NetLogSourceType::NONE)),
      addresses_(addresses),
      data_(data),
      read_offset_(0),
      read_data_(SYNCHRONOUS, ERR_UNEXPECTED),
      need_read_data_(true),
      peer_closed_connection_(false),
      pending_read_buf_(NULL),
      pending_read_buf_len_(0),
      was_used_to_convey_data_(false),
      enable_read_if_ready_(false) {
  DCHECK(data_);
  peer_addr_ = data->connect_data().peer_addr;
  data_->Initialize(this);
}

MockTCPClientSocket::~MockTCPClientSocket() {
  if (data_)
    data_->DetachSocket();
}

int MockTCPClientSocket::Read(IOBuffer* buf, int buf_len,
                              const CompletionCallback& callback) {
  // If the buffer is already in use, a read is already in progress!
  DCHECK(!pending_read_buf_);
  // Use base::Unretained() is safe because MockClientSocket::RunCallbackAsync()
  // takes a weak ptr of the base class, MockClientSocket.
  int rv = ReadIfReadyImpl(
      buf, buf_len,
      base::Bind(&MockTCPClientSocket::RetryRead, base::Unretained(this)));
  if (rv == ERR_IO_PENDING) {
    pending_read_buf_ = buf;
    pending_read_buf_len_ = buf_len;
    pending_read_callback_ = callback;
  }
  return rv;
}

int MockTCPClientSocket::ReadIfReady(IOBuffer* buf,
                                     int buf_len,
                                     const CompletionCallback& callback) {
  DCHECK(!pending_read_if_ready_callback_);

  if (!enable_read_if_ready_)
    return ERR_READ_IF_READY_NOT_IMPLEMENTED;
  return ReadIfReadyImpl(buf, buf_len, callback);
}

int MockTCPClientSocket::Write(IOBuffer* buf, int buf_len,
                               const CompletionCallback& callback) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);

  if (!connected_ || !data_)
    return ERR_UNEXPECTED;

  std::string data(buf->data(), buf_len);
  MockWriteResult write_result = data_->OnWrite(data);

  was_used_to_convey_data_ = true;

  // ERR_IO_PENDING is a signal that the socket data will call back
  // asynchronously later.
  if (write_result.result == ERR_IO_PENDING) {
    pending_write_callback_ = callback;
    return ERR_IO_PENDING;
  }

  if (write_result.mode == ASYNC) {
    RunCallbackAsync(callback, write_result.result);
    return ERR_IO_PENDING;
  }

  return write_result.result;
}

void MockTCPClientSocket::GetConnectionAttempts(ConnectionAttempts* out) const {
  *out = connection_attempts_;
}

void MockTCPClientSocket::ClearConnectionAttempts() {
  connection_attempts_.clear();
}

void MockTCPClientSocket::AddConnectionAttempts(
    const ConnectionAttempts& attempts) {
  connection_attempts_.insert(connection_attempts_.begin(), attempts.begin(),
                              attempts.end());
}

int MockTCPClientSocket::Connect(const CompletionCallback& callback) {
  if (!data_)
    return ERR_UNEXPECTED;

  if (connected_)
    return OK;
  connected_ = true;
  peer_closed_connection_ = false;

  int result = data_->connect_data().result;
  IoMode mode = data_->connect_data().mode;

  if (result != OK && result != ERR_IO_PENDING) {
    IPEndPoint address;
    if (GetPeerAddress(&address) == OK)
      connection_attempts_.push_back(ConnectionAttempt(address, result));
  }

  if (mode == SYNCHRONOUS)
    return result;

  if (result == ERR_IO_PENDING)
    pending_connect_callback_ = callback;
  else
    RunCallbackAsync(callback, result);
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

  *address = addresses_[0];
  return OK;
}

bool MockTCPClientSocket::WasEverUsed() const {
  return was_used_to_convey_data_;
}

void MockTCPClientSocket::EnableTCPFastOpenIfSupported() {
  EXPECT_FALSE(IsConnected()) << "Can't enable fast open after connect.";

  data_->OnEnableTCPFastOpenIfSupported();
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
  RunCallback(base::ResetAndReturn(&pending_read_if_ready_callback_),
              read_data_.result > 0 ? OK : read_data_.result);
}

void MockTCPClientSocket::OnWriteComplete(int rv) {
  // If |data_| has been destroyed, safest to just do nothing.
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(!pending_write_callback_.is_null());
  CompletionCallback callback = pending_write_callback_;
  RunCallback(callback, rv);
}

void MockTCPClientSocket::OnConnectComplete(const MockConnect& data) {
  // If |data_| has been destroyed, safest to just do nothing.
  if (!data_)
    return;

  CompletionCallback callback = pending_connect_callback_;
  RunCallback(callback, data.result);
}

void MockTCPClientSocket::OnDataProviderDestroyed() {
  data_ = nullptr;
}

void MockTCPClientSocket::RetryRead(int rv) {
  DCHECK(pending_read_callback_);
  DCHECK(pending_read_buf_.get());
  DCHECK_LT(0, pending_read_buf_len_);

  if (rv == OK) {
    rv = ReadIfReadyImpl(
        pending_read_buf_.get(), pending_read_buf_len_,
        base::Bind(&MockTCPClientSocket::RetryRead, base::Unretained(this)));
    if (rv == ERR_IO_PENDING)
      return;
  }
  pending_read_buf_ = nullptr;
  pending_read_buf_len_ = 0;
  RunCallback(base::ResetAndReturn(&pending_read_callback_), rv);
}

int MockTCPClientSocket::ReadIfReadyImpl(IOBuffer* buf,
                                         int buf_len,
                                         const CompletionCallback& callback) {
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
      pending_read_if_ready_callback_ = callback;
      return ERR_IO_PENDING;
    }
    need_read_data_ = false;
  }

  int result = read_data_.result;
  DCHECK_NE(ERR_IO_PENDING, result);
  if (read_data_.mode == ASYNC) {
    DCHECK(!callback.is_null());
    read_data_.mode = SYNCHRONOUS;
    RunCallbackAsync(callback, result);
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

// static
void MockSSLClientSocket::ConnectCallback(
    MockSSLClientSocket* ssl_client_socket,
    const CompletionCallback& callback,
    int rv) {
  if (rv == OK)
    ssl_client_socket->connected_ = true;
  callback.Run(rv);
}

MockSSLClientSocket::MockSSLClientSocket(
    std::unique_ptr<ClientSocketHandle> transport_socket,
    const HostPortPair& host_port_pair,
    const SSLConfig& ssl_config,
    SSLSocketDataProvider* data)
    : MockClientSocket(
          // Have to use the right NetLogWithSource for LoadTimingInfo
          // regression
          // tests.
          transport_socket->socket()->NetLog()),
      transport_(std::move(transport_socket)),
      data_(data) {
  DCHECK(data_);
  peer_addr_ = data->connect.peer_addr;
}

MockSSLClientSocket::~MockSSLClientSocket() {
  Disconnect();
}

int MockSSLClientSocket::Read(IOBuffer* buf, int buf_len,
                              const CompletionCallback& callback) {
  return transport_->socket()->Read(buf, buf_len, callback);
}

int MockSSLClientSocket::ReadIfReady(IOBuffer* buf,
                                     int buf_len,
                                     const CompletionCallback& callback) {
  return transport_->socket()->ReadIfReady(buf, buf_len, callback);
}

int MockSSLClientSocket::Write(IOBuffer* buf, int buf_len,
                               const CompletionCallback& callback) {
  return transport_->socket()->Write(buf, buf_len, callback);
}

int MockSSLClientSocket::Connect(const CompletionCallback& callback) {
  int rv = transport_->socket()->Connect(
      base::Bind(&ConnectCallback, base::Unretained(this), callback));
  if (rv == OK) {
    if (data_->connect.result == OK)
      connected_ = true;
    if (data_->connect.mode == ASYNC) {
      RunCallbackAsync(callback, data_->connect.result);
      return ERR_IO_PENDING;
    }
    return data_->connect.result;
  }
  return rv;
}

void MockSSLClientSocket::Disconnect() {
  MockClientSocket::Disconnect();
  if (transport_->socket() != NULL)
    transport_->socket()->Disconnect();
}

bool MockSSLClientSocket::IsConnected() const {
  return transport_->socket()->IsConnected();
}

bool MockSSLClientSocket::IsConnectedAndIdle() const {
  return transport_->socket()->IsConnectedAndIdle();
}

bool MockSSLClientSocket::WasEverUsed() const {
  return transport_->socket()->WasEverUsed();
}

int MockSSLClientSocket::GetPeerAddress(IPEndPoint* address) const {
  return transport_->socket()->GetPeerAddress(address);
}

bool MockSSLClientSocket::WasAlpnNegotiated() const {
  return data_->next_proto != kProtoUnknown;
}

NextProto MockSSLClientSocket::GetNegotiatedProtocol() const {
  return data_->next_proto;
}

bool MockSSLClientSocket::GetSSLInfo(SSLInfo* ssl_info) {
  ssl_info->Reset();
  ssl_info->cert = data_->cert;
  ssl_info->cert_status = data_->cert_status;
  ssl_info->client_cert_sent = data_->client_cert_sent;
  ssl_info->channel_id_sent = data_->channel_id_sent;
  ssl_info->connection_status = data_->connection_status;
  ssl_info->token_binding_negotiated = data_->token_binding_negotiated;
  ssl_info->token_binding_key_param = data_->token_binding_key_param;
  return true;
}

void MockSSLClientSocket::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) {
  DCHECK(cert_request_info);
  if (data_->cert_request_info) {
    cert_request_info->host_and_port =
        data_->cert_request_info->host_and_port;
    cert_request_info->is_proxy = data_->cert_request_info->is_proxy;
    cert_request_info->cert_authorities =
        data_->cert_request_info->cert_authorities;
    cert_request_info->cert_key_types =
        data_->cert_request_info->cert_key_types;
  } else {
    cert_request_info->Reset();
  }
}

ChannelIDService* MockSSLClientSocket::GetChannelIDService() const {
  return data_->channel_id_service;
}

Error MockSSLClientSocket::GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                                    TokenBindingType tb_type,
                                                    std::vector<uint8_t>* out) {
  out->push_back('A');
  return OK;
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
    : connected_(false),
      data_(data),
      read_offset_(0),
      read_data_(SYNCHRONOUS, ERR_UNEXPECTED),
      need_read_data_(true),
      source_port_(123),
      network_(NetworkChangeNotifier::kInvalidNetworkHandle),
      pending_read_buf_(NULL),
      pending_read_buf_len_(0),
      net_log_(NetLogWithSource::Make(net_log, NetLogSourceType::NONE)),
      weak_factory_(this) {
  DCHECK(data_);
  data_->Initialize(this);
  peer_addr_ = data->connect_data().peer_addr;
}

MockUDPClientSocket::~MockUDPClientSocket() {
  if (data_)
    data_->DetachSocket();
}

int MockUDPClientSocket::Read(IOBuffer* buf,
                              int buf_len,
                              const CompletionCallback& callback) {
  if (!connected_ || !data_)
    return ERR_UNEXPECTED;

  // If the buffer is already in use, a read is already in progress!
  DCHECK(!pending_read_buf_);

  // Store our async IO data.
  pending_read_buf_ = buf;
  pending_read_buf_len_ = buf_len;
  pending_read_callback_ = callback;

  if (need_read_data_) {
    read_data_ = data_->OnRead();
    // ERR_IO_PENDING means that the SocketDataProvider is taking responsibility
    // to complete the async IO manually later (via OnReadComplete).
    if (read_data_.result == ERR_IO_PENDING) {
      // We need to be using async IO in this case.
      DCHECK(!callback.is_null());
      return ERR_IO_PENDING;
    }
    need_read_data_ = false;
  }

  return CompleteRead();
}

int MockUDPClientSocket::Write(IOBuffer* buf, int buf_len,
                               const CompletionCallback& callback) {
  DCHECK(buf);
  DCHECK_GT(buf_len, 0);

  if (!connected_ || !data_)
    return ERR_UNEXPECTED;

  std::string data(buf->data(), buf_len);
  MockWriteResult write_result = data_->OnWrite(data);

  // ERR_IO_PENDING is a signal that the socket data will call back
  // asynchronously.
  if (write_result.result == ERR_IO_PENDING) {
    pending_write_callback_ = callback;
    return ERR_IO_PENDING;
  }
  if (write_result.mode == ASYNC) {
    RunCallbackAsync(callback, write_result.result);
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

void MockUDPClientSocket::Close() {
  connected_ = false;
}

int MockUDPClientSocket::GetPeerAddress(IPEndPoint* address) const {
  *address = peer_addr_;
  return OK;
}

int MockUDPClientSocket::GetLocalAddress(IPEndPoint* address) const {
  *address = IPEndPoint(IPAddress(192, 0, 2, 33), source_port_);
  return OK;
}

void MockUDPClientSocket::UseNonBlockingIO() {}

const NetLogWithSource& MockUDPClientSocket::NetLog() const {
  return net_log_;
}

int MockUDPClientSocket::Connect(const IPEndPoint& address) {
  if (!data_)
    return ERR_UNEXPECTED;
  connected_ = true;
  peer_addr_ = address;
  return data_->connect_data().result;
}

int MockUDPClientSocket::ConnectUsingNetwork(
    NetworkChangeNotifier::NetworkHandle network,
    const IPEndPoint& address) {
  DCHECK(!connected_);
  if (!data_)
    return ERR_UNEXPECTED;
  network_ = network;
  connected_ = true;
  peer_addr_ = address;
  return data_->connect_data().result;
}

int MockUDPClientSocket::ConnectUsingDefaultNetwork(const IPEndPoint& address) {
  DCHECK(!connected_);
  if (!data_)
    return ERR_UNEXPECTED;
  network_ = kDefaultNetworkForTests;
  connected_ = true;
  peer_addr_ = address;
  return data_->connect_data().result;
}

NetworkChangeNotifier::NetworkHandle MockUDPClientSocket::GetBoundNetwork()
    const {
  return network_;
}

void MockUDPClientSocket::OnReadComplete(const MockRead& data) {
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(pending_read_buf_.get());
  // You can't complete a read with another ERR_IO_PENDING status code.
  DCHECK_NE(ERR_IO_PENDING, data.result);
  // Since we've been waiting for data, need_read_data_ should be true.
  DCHECK(need_read_data_);

  read_data_ = data;
  need_read_data_ = false;

  // The caller is simulating that this IO completes right now.  Don't
  // let CompleteRead() schedule a callback.
  read_data_.mode = SYNCHRONOUS;

  CompletionCallback callback = pending_read_callback_;
  int rv = CompleteRead();
  RunCallback(callback, rv);
}

void MockUDPClientSocket::OnWriteComplete(int rv) {
  if (!data_)
    return;

  // There must be a read pending.
  DCHECK(!pending_write_callback_.is_null());
  CompletionCallback callback = pending_write_callback_;
  RunCallback(callback, rv);
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
  CompletionCallback callback = pending_read_callback_;
  pending_read_buf_ = NULL;
  pending_read_buf_len_ = 0;
  pending_read_callback_.Reset();

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
    RunCallbackAsync(callback, result);
    return ERR_IO_PENDING;
  }
  return result;
}

void MockUDPClientSocket::RunCallbackAsync(const CompletionCallback& callback,
                                           int result) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&MockUDPClientSocket::RunCallback,
                            weak_factory_.GetWeakPtr(), callback, result));
}

void MockUDPClientSocket::RunCallback(const CompletionCallback& callback,
                                      int result) {
  if (!callback.is_null())
    callback.Run(result);
}

TestSocketRequest::TestSocketRequest(
    std::vector<TestSocketRequest*>* request_order, size_t* completion_count)
    : request_order_(request_order),
      completion_count_(completion_count),
      callback_(base::Bind(&TestSocketRequest::OnComplete,
                           base::Unretained(this))) {
  DCHECK(request_order);
  DCHECK(completion_count);
}

TestSocketRequest::~TestSocketRequest() {
}

void TestSocketRequest::OnComplete(int result) {
  SetResult(result);
  (*completion_count_)++;
  request_order_->push_back(this);
}

// static
const int ClientSocketPoolTest::kIndexOutOfBounds = -1;

// static
const int ClientSocketPoolTest::kRequestNotFound = -2;

ClientSocketPoolTest::ClientSocketPoolTest() : completion_count_(0) {}
ClientSocketPoolTest::~ClientSocketPoolTest() {}

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
    const CompletionCallback& callback)
    : socket_(std::move(socket)), handle_(handle), user_callback_(callback) {}

MockTransportClientSocketPool::MockConnectJob::~MockConnectJob() {}

int MockTransportClientSocketPool::MockConnectJob::Connect() {
  int rv = socket_->Connect(base::Bind(&MockConnectJob::OnConnect,
                                       base::Unretained(this)));
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
  handle_ = NULL;
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
    connect_timing.dns_start = now;
    connect_timing.dns_end = now;
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

  handle_ = NULL;

  if (!user_callback_.is_null()) {
    CompletionCallback callback = user_callback_;
    user_callback_.Reset();
    callback.Run(rv);
  }
}

MockTransportClientSocketPool::MockTransportClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    ClientSocketFactory* socket_factory)
    : TransportClientSocketPool(max_sockets,
                                max_sockets_per_group,
                                NULL,
                                NULL,
                                NULL,
                                NULL),
      client_socket_factory_(socket_factory),
      last_request_priority_(DEFAULT_PRIORITY),
      release_count_(0),
      cancel_count_(0) {}

MockTransportClientSocketPool::~MockTransportClientSocketPool() {}

int MockTransportClientSocketPool::RequestSocket(
    const std::string& group_name,
    const void* socket_params,
    RequestPriority priority,
    RespectLimits respect_limits,
    ClientSocketHandle* handle,
    const CompletionCallback& callback,
    const NetLogWithSource& net_log) {
  last_request_priority_ = priority;
  std::unique_ptr<StreamSocket> socket =
      client_socket_factory_->CreateTransportClientSocket(
          AddressList(), NULL, net_log.net_log(), NetLogSource());
  MockConnectJob* job = new MockConnectJob(std::move(socket), handle, callback);
  job_list_.push_back(base::WrapUnique(job));
  handle->set_pool_id(1);
  return job->Connect();
}

void MockTransportClientSocketPool::SetPriority(const std::string& group_name,
                                                ClientSocketHandle* handle,
                                                RequestPriority priority) {
  // TODO: Implement.
}

void MockTransportClientSocketPool::CancelRequest(const std::string& group_name,
                                                  ClientSocketHandle* handle) {
  for (std::unique_ptr<MockConnectJob>& it : job_list_) {
    if (it->CancelHandle(handle)) {
      cancel_count_++;
      break;
    }
  }
}

void MockTransportClientSocketPool::ReleaseSocket(
    const std::string& group_name,
    std::unique_ptr<StreamSocket> socket,
    int id) {
  EXPECT_EQ(1, id);
  release_count_++;
}

MockSOCKSClientSocketPool::MockSOCKSClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    TransportClientSocketPool* transport_pool)
    : SOCKSClientSocketPool(max_sockets,
                            max_sockets_per_group,
                            NULL,
                            transport_pool,
                            NULL,
                            NULL),
      transport_pool_(transport_pool) {}

MockSOCKSClientSocketPool::~MockSOCKSClientSocketPool() {}

int MockSOCKSClientSocketPool::RequestSocket(const std::string& group_name,
                                             const void* socket_params,
                                             RequestPriority priority,
                                             RespectLimits respect_limits,
                                             ClientSocketHandle* handle,
                                             const CompletionCallback& callback,
                                             const NetLogWithSource& net_log) {
  return transport_pool_->RequestSocket(group_name, socket_params, priority,
                                        respect_limits, handle, callback,
                                        net_log);
}

void MockSOCKSClientSocketPool::SetPriority(const std::string& group_name,
                                            ClientSocketHandle* handle,
                                            RequestPriority priority) {
  transport_pool_->SetPriority(group_name, handle, priority);
}

void MockSOCKSClientSocketPool::CancelRequest(
    const std::string& group_name,
    ClientSocketHandle* handle) {
  return transport_pool_->CancelRequest(group_name, handle);
}

void MockSOCKSClientSocketPool::ReleaseSocket(
    const std::string& group_name,
    std::unique_ptr<StreamSocket> socket,
    int id) {
  return transport_pool_->ReleaseSocket(group_name, std::move(socket), id);
}

ScopedWebSocketEndpointZeroUnlockDelay::
    ScopedWebSocketEndpointZeroUnlockDelay() {
  old_delay_ =
      WebSocketEndpointLockManager::GetInstance()->SetUnlockDelayForTesting(
          base::TimeDelta());
}

ScopedWebSocketEndpointZeroUnlockDelay::
    ~ScopedWebSocketEndpointZeroUnlockDelay() {
  base::TimeDelta active_delay =
      WebSocketEndpointLockManager::GetInstance()->SetUnlockDelayForTesting(
          old_delay_);
  EXPECT_EQ(active_delay, base::TimeDelta());
}

const char kSOCKS5GreetRequest[] = { 0x05, 0x01, 0x00 };
const int kSOCKS5GreetRequestLength = arraysize(kSOCKS5GreetRequest);

const char kSOCKS5GreetResponse[] = { 0x05, 0x00 };
const int kSOCKS5GreetResponseLength = arraysize(kSOCKS5GreetResponse);

const char kSOCKS5OkRequest[] =
    { 0x05, 0x01, 0x00, 0x03, 0x04, 'h', 'o', 's', 't', 0x00, 0x50 };
const int kSOCKS5OkRequestLength = arraysize(kSOCKS5OkRequest);

const char kSOCKS5OkResponse[] =
    { 0x05, 0x00, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x50 };
const int kSOCKS5OkResponseLength = arraysize(kSOCKS5OkResponse);

int64_t CountReadBytes(const MockRead reads[], size_t reads_size) {
  int64_t total = 0;
  for (const MockRead* read = reads; read != reads + reads_size; ++read)
    total += read->data_len;
  return total;
}

int64_t CountWriteBytes(const MockWrite writes[], size_t writes_size) {
  int64_t total = 0;
  for (const MockWrite* write = writes; write != writes + writes_size; ++write)
    total += write->data_len;
  return total;
}

}  // namespace net
