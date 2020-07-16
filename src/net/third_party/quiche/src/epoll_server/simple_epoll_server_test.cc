// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Epoll tests which determine that the right things happen in the right order.
// Also lots of testing of individual functions.

#include "net/third_party/quiche/src/epoll_server/simple_epoll_server.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/epoll_server/fake_simple_epoll_server.h"
#include "net/third_party/quiche/src/epoll_server/platform/api/epoll_address_test_utils.h"
#include "net/third_party/quiche/src/epoll_server/platform/api/epoll_expect_bug.h"
#include "net/third_party/quiche/src/epoll_server/platform/api/epoll_ptr_util.h"
#include "net/third_party/quiche/src/epoll_server/platform/api/epoll_test.h"
#include "net/third_party/quiche/src/epoll_server/platform/api/epoll_thread.h"
#include "net/third_party/quiche/src/epoll_server/platform/api/epoll_time.h"

namespace epoll_server {

namespace test {

namespace {

const int kPageSize = 4096;
const int kMaxBufLen = 10000;

// These are used to record what is happening.
enum {
  CREATION,
  REGISTRATION,
  MODIFICATION,
  EVENT,
  UNREGISTRATION,
  SHUTDOWN,
  DESTRUCTION
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

struct RecordEntry {
  RecordEntry() : time(0), instance(nullptr), event_type(0), fd(0), data(0) {}

  RecordEntry(int64_t time, void* instance, int event_type, int fd, int data)
      : time(time),
        instance(instance),
        event_type(event_type),
        fd(fd),
        data(data) {}

  int64_t time;
  void* instance;
  int event_type;
  int fd;
  int data;

  bool IsEqual(const RecordEntry *entry) const {
    bool retval = true;

    if (instance  !=  entry->instance) {
      retval = false;
      EPOLL_LOG(INFO) << " instance (" << instance << ") != entry->instance("
                      << entry->instance << ")";
    }
    if (event_type != entry->event_type) {
      retval = false;
      EPOLL_LOG(INFO) << " event_type (" << event_type
                      << ") != entry->event_type(" << entry->event_type << ")";
    }
    if ( fd != entry->fd ) {
      retval = false;
      EPOLL_LOG(INFO) << " fd (" << fd << ") != entry->fd (" << entry->fd
                      << ")";
    }
    if (data != entry->data) {
      retval = false;
      EPOLL_LOG(INFO) << " data (" << data << ") != entry->data(" << entry->data
                      << ")";
    }
    return retval;
  }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class Recorder {
 public:
  void Record(void* instance, int event_type, int fd, int data) {
    records_.push_back(
        RecordEntry(WallTimeNowInUsec(), instance, event_type, fd, data));
  }

  const std::vector<RecordEntry> *records() const { return &records_; }

  bool IsEqual(const Recorder *recorder) const {
    const std::vector<RecordEntry> *records = recorder->records();

     if (records_.size() != records->size()) {
       EPOLL_LOG(INFO) << "records_.size() (" << records_.size()
                       << ") != records->size() (" << records->size() << ")";
       return false;
     }
     for (size_t i = 0; i < std::min(records_.size(), records->size()); ++i) {
       if (!records_[i].IsEqual(&(*records)[i])) {
         EPOLL_LOG(INFO) << "entry in index: " << i
                         << " differs from recorder.";
         return false;
       }
     }
    return true;
  }

 private:
  std::vector<RecordEntry> records_;
};


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class RecordingCB : public EpollCallbackInterface {
 public:
  RecordingCB() : recorder_(new Recorder()) {
    recorder_->Record(this, CREATION, 0, 0);
  }

  ~RecordingCB() override {
    recorder_->Record(this, DESTRUCTION, 0, 0);
    delete recorder_;
  }

  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
    recorder_->Record(this, REGISTRATION, fd, event_mask);
  }

  void OnModification(int fd, int event_mask) override {
    recorder_->Record(this, MODIFICATION, fd, event_mask);
  }

  void OnEvent(int fd, EpollEvent* event) override {
    recorder_->Record(this, EVENT, fd, event->in_events);
    if (event->in_events & EPOLLIN) {
      const int kLength = 1024;
      char buf[kLength];
      int data_read;
      do {
        data_read = read(fd, &buf, kLength);
      } while (data_read > 0);
    }
  }

  void OnUnregistration(int fd, bool replaced) override {
    recorder_->Record(this, UNREGISTRATION, fd, replaced);
  }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {
    if (fd >= 0) {
      eps->UnregisterFD(fd);
    }
    recorder_->Record(this, SHUTDOWN, fd, 0);
  }

  std::string Name() const override { return "RecordingCB"; }

  const Recorder* recorder() const { return recorder_; }

 protected:
  Recorder* recorder_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// A simple test server that adds some test functions to SimpleEpollServer as
// well as allowing access to protected functions.
class EpollTestServer : public SimpleEpollServer {
 public:
  EpollTestServer() : SimpleEpollServer() {}

  ~EpollTestServer() override {}

  void CheckMapping(int fd, CB* cb) {
    CBAndEventMask tmp;
    tmp.fd = fd;
    FDToCBMap::iterator fd_i = cb_map_.find(tmp);
    CHECK(fd_i != cb_map_.end());  // Chokes CHECK_NE.
    CHECK(fd_i->cb == cb);
  }

  void CheckNotMapped(int fd) {
    CBAndEventMask tmp;
    tmp.fd = fd;
    FDToCBMap::iterator fd_i = cb_map_.find(tmp);
    CHECK(fd_i == cb_map_.end());  // Chokes CHECK_EQ.
  }

  void CheckEventMask(int fd, int event_mask) {
    CBAndEventMask tmp;
    tmp.fd = fd;
    FDToCBMap::iterator fd_i = cb_map_.find(tmp);
    CHECK(cb_map_.end() != fd_i);  // Chokes CHECK_NE.
    CHECK_EQ(fd_i->event_mask, event_mask);
  }

  void CheckNotRegistered(int fd) {
    struct epoll_event ee;
    memset(&ee, 0, sizeof(ee));
    // If the fd is registered, the epoll_ctl call would succeed (return 0) and
    // the CHECK would fail.
    CHECK(epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ee));
  }

  size_t GetNumPendingAlarmsForTest() const { return alarm_map_.size(); }

  bool ContainsAlarm(AlarmCB* ac) {
    return all_alarms_.find(ac) != all_alarms_.end();
  }

  using SimpleEpollServer::WaitForEventsAndCallHandleEvents;
};

class EpollFunctionTest : public EpollTest {
 public:
  EpollFunctionTest()
      : fd_(-1), fd2_(-1), recorder_(nullptr), cb_(nullptr), ep_(nullptr) {
  }

  ~EpollFunctionTest() override {
    delete ep_;
    delete cb_;
  }

  void SetUp() override {
    ep_ = new EpollTestServer();
    cb_ = new RecordingCB();
    // recorder_ is safe to use directly as we know it has the same scope as
    // cb_
    recorder_ = cb_->recorder();

    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
      EPOLL_PLOG(FATAL) << "pipe() failed";
    }
    fd_ = pipe_fds[0];
    fd2_ = pipe_fds[1];
  }

  void TearDown() override {
    close(fd_);
    close(fd2_);
  }

  void DeleteSimpleEpollServer() {
    delete ep_;
    ep_ = nullptr;
  }

  int fd() { return fd_; }
  int fd2() { return fd2_; }
  EpollTestServer*  ep() { return ep_; }
  EpollCallbackInterface* cb() { return cb_; }
  const Recorder* recorder() { return recorder_; }

 private:
  int fd_;
  int fd2_;
  const Recorder *recorder_;
  RecordingCB* cb_;
  EpollTestServer* ep_;
};

TEST_F(EpollFunctionTest, TestUnconnectedSocket) {
  int fd = socket(AddressFamilyUnderTest(), SOCK_STREAM, IPPROTO_TCP);
  ep()->RegisterFD(fd, cb(), EPOLLIN | EPOLLOUT);
  ep()->WaitForEventsAndExecuteCallbacks();

  Recorder tmp;
  tmp.Record(cb(), CREATION, 0, 0);
  tmp.Record(cb(), REGISTRATION, fd, EPOLLIN | EPOLLOUT);
  tmp.Record(cb(), EVENT, fd, EPOLLOUT | EPOLLHUP);
  EXPECT_TRUE(recorder()->IsEqual(&tmp));
}

TEST_F(EpollFunctionTest, TestRegisterFD) {
  // Check that the basic register works.
  ep()->RegisterFD(fd(), cb(), EPOLLIN);

  // Make sure that the fd-CB mapping is there.
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN);

  // Now make sure that if we register again, we stomp the old callback.
  // Also make sure we handle O_NONBLOCK correctly
  RecordingCB cb2;
  ep()->RegisterFD(fd(), &cb2, EPOLLOUT | O_NONBLOCK);
  ep()->CheckMapping(fd(), &cb2);
  ep()->CheckEventMask(fd(), EPOLLOUT | O_NONBLOCK);

  // Clean up.
  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestRegisterFDForWrite) {
  ep()->RegisterFDForWrite(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLOUT);

  // Clean up.
  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestRegisterFDForReadWrite) {
  ep()->RegisterFDForReadWrite(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN | EPOLLOUT);

  // Clean up.
  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestRegisterFDForRead) {
  ep()->RegisterFDForRead(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN);

  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestUnregisterFD) {
  ep()->RegisterFDForRead(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN);

    // Unregister and make sure that it's gone.
  ep()->UnregisterFD(fd());
  ep()->CheckNotMapped(fd());
  ep()->CheckNotRegistered(fd());

  // And make sure that unregistering something a second time doesn't cause
  // crashes.
  ep()->UnregisterFD(fd());
  ep()->CheckNotMapped(fd());
  ep()->CheckNotRegistered(fd());
}

TEST_F(EpollFunctionTest, TestModifyCallback) {
  // Check that nothing terrible happens if we modify an unregistered fd.
  ep()->ModifyCallback(fd(), EPOLLOUT);
  ep()->CheckNotMapped(fd());
  ep()->CheckNotRegistered(fd());

  // Check that the basic register works.
  ep()->RegisterFD(fd(), cb(), EPOLLIN);
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN);

  // Check that adding a signal swaps it out for the first.
  ep()->ModifyCallback(fd(), EPOLLOUT);
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLOUT);

  // Check that modifying from X to X works correctly.
  ep()->ModifyCallback(fd(), EPOLLOUT);
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLOUT);

  // Check that modifying from something to nothing works.
  ep()->ModifyCallback(fd(), 0);
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), 0);

  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestStopRead) {
  ep()->RegisterFDForReadWrite(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN | EPOLLOUT);

  // Unregister and make sure you only lose the read event.
  ep()->StopRead(fd());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLOUT);

  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestStartRead) {
  ep()->RegisterFDForWrite(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLOUT);

  // Make sure that StartRead adds EPOLLIN and doesn't remove other signals.
  ep()->StartRead(fd());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN | EPOLLOUT);

  // Clean up.
  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestStopWrite) {
  ep()->RegisterFDForReadWrite(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN | EPOLLOUT);

  // Unregister write and make sure you only lose the write event.
  ep()->StopWrite(fd());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN);

  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestStartWrite) {
  ep()->RegisterFDForRead(fd(), cb());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN);

  // Make sure that StartWrite adds EPOLLOUT and doesn't remove other
  // signals.
  ep()->StartWrite(fd());
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), EPOLLIN | EPOLLOUT);

  // Clean up.
  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestSet_timeout_in_us) {
  // Check that set works with various values.  There's a separate test below
  // to make sure the values are used properly.
  ep()->set_timeout_in_us(10);
  EXPECT_EQ(10, ep()->timeout_in_us_for_test());

  ep()->set_timeout_in_us(-1);
  EXPECT_EQ(-1, ep()->timeout_in_us_for_test());
}

TEST_F(EpollFunctionTest, TestHandleEvent) {
  const std::vector<RecordEntry> *records = recorder()->records();

  // Test that nothing bad happens if the FD is not in the map.
  ep()->HandleEvent(fd(), EPOLLOUT);
  ep()->CallReadyListCallbacks();

  ep()->RegisterFD(fd(), cb(), 0);
  ep()->CheckMapping(fd(), cb());
  ep()->CheckEventMask(fd(), 0);

  // At this point we should have creation and registration recorded.
  EXPECT_EQ(2u, records->size());

  // Call handle event and make sure something was recorded.
  ep()->HandleEvent(fd(), EPOLLOUT);
  ep()->CallReadyListCallbacks();
  EXPECT_EQ(3u, records->size());

  // Call handle event and make sure something was recorded.
  ep()->HandleEvent(fd(), EPOLLIN | O_NONBLOCK);
  ep()->CallReadyListCallbacks();
  EXPECT_EQ(4u, records->size());

  Recorder tmp;
  tmp.Record(cb(), CREATION, 0, 0);
  tmp.Record(cb(), REGISTRATION, fd(), 0);
  tmp.Record(cb(), EVENT, fd(), EPOLLOUT);
  tmp.Record(cb(), EVENT, fd(), EPOLLIN | O_NONBLOCK);

  EXPECT_TRUE(recorder()->IsEqual(&tmp));
  ep()->UnregisterFD(fd());
}

TEST_F(EpollFunctionTest, TestNumFDsRegistered) {
  EXPECT_EQ(0, ep()->NumFDsRegistered());

  ep()->RegisterFD(fd(), cb(), 0);
  EXPECT_EQ(1, ep()->NumFDsRegistered());

  ep()->RegisterFD(fd2(), cb(), 0);
  EXPECT_EQ(2, ep()->NumFDsRegistered());

  ep()->RegisterFD(fd2(), cb(), 0);
  EXPECT_EQ(2, ep()->NumFDsRegistered());

  ep()->UnregisterFD(fd2());
  EXPECT_EQ(1, ep()->NumFDsRegistered());

  ep()->UnregisterFD(fd());
  EXPECT_EQ(0, ep()->NumFDsRegistered());
}

// Check all of the individual signals and 1-2 combinations.
TEST_F(EpollFunctionTest, TestEventMaskToString) {
  std::string test;

  test = SimpleEpollServer::EventMaskToString(EPOLLIN);
  EXPECT_EQ(test, "EPOLLIN ");

  test = SimpleEpollServer::EventMaskToString(EPOLLOUT);
  EXPECT_EQ(test, "EPOLLOUT ");

  test = SimpleEpollServer::EventMaskToString(EPOLLPRI);
  EXPECT_EQ(test, "EPOLLPRI ");

  test = SimpleEpollServer::EventMaskToString(EPOLLERR);
  EXPECT_EQ(test, "EPOLLERR ");

  test = SimpleEpollServer::EventMaskToString(EPOLLHUP);
  EXPECT_EQ(test, "EPOLLHUP ");

  test = SimpleEpollServer::EventMaskToString(EPOLLHUP | EPOLLIN);
  EXPECT_EQ(test, "EPOLLIN EPOLLHUP ");

  test = SimpleEpollServer::EventMaskToString(EPOLLIN | EPOLLOUT);
  EXPECT_EQ(test, "EPOLLIN EPOLLOUT ");
}

class TestAlarm : public EpollAlarmCallbackInterface {
 public:
  TestAlarm()
    : time_before_next_alarm_(-1),
      was_called_(false),
      num_called_(0),
      absolute_time_(false),
      onshutdown_called_(false),
      has_token_(false),
      eps_(nullptr) {
  }
  ~TestAlarm() override {
  }
  int64_t OnAlarm() override {
    has_token_ = false;
    was_called_ = true;
    ++num_called_;
    if (time_before_next_alarm_ < 0) {
      return 0;
    }
    if (absolute_time_) {
      return time_before_next_alarm_;
    } else {
      return WallTimeNowInUsec() + time_before_next_alarm_;
    }
  }

  void OnShutdown(SimpleEpollServer* eps) override {
    onshutdown_called_ = true;
    has_token_ = false;
  }
  void OnRegistration(const SimpleEpollServer::AlarmRegToken& token,
                      SimpleEpollServer* eps) override {
    has_token_ = true;
    last_token_ = token;
    eps_ = eps;
  }
  void OnUnregistration() override {
    has_token_ = false;
  }

  void UnregisterIfRegistered(SimpleEpollServer* eps) {
    if (has_token_) {
      eps->UnregisterAlarm(last_token_);
    }
  }

  void ReregisterAlarm(int64_t timeout_in_us) {
    CHECK(has_token_);
    eps_->ReregisterAlarm(last_token_, timeout_in_us);
  }

  void Reset() {
    time_before_next_alarm_ = -1;
    was_called_ = false;
    absolute_time_ = false;
  }

  bool was_called() const { return was_called_; }
  int num_called() const { return num_called_; }

  void set_time_before_next_alarm(int64_t time) {
    time_before_next_alarm_ = time;
  }
  void set_absolute_time(bool absolute) {
    absolute_time_ = absolute;
  }
  bool onshutdown_called() { return onshutdown_called_; }

 protected:
  int64_t time_before_next_alarm_;
  bool was_called_;
  int num_called_;
  // Is time_before_next_alarm relative to the current time or absolute?
  bool absolute_time_;
  bool onshutdown_called_;
  bool has_token_;
  SimpleEpollServer::AlarmRegToken last_token_;
  SimpleEpollServer* eps_;
};

class TestChildAlarm;

// This node unregister all other alarms when it receives
// OnShutdown() from any one child.
class TestParentAlarm {
 public:
  void OnShutdown(TestChildAlarm* child, SimpleEpollServer* eps) {
    // Unregister
    for (ChildTokenMap::const_iterator it = child_tokens_.begin();
         it != child_tokens_.end(); ++it) {
      if (it->first != child) {
        eps->UnregisterAlarm(it->second);
      }
    }
    child_tokens_.clear();
  }

  void OnRegistration(TestChildAlarm* child,
                      const SimpleEpollServer::AlarmRegToken& token) {
    child_tokens_[child] = token;
  }

 protected:
  typedef std::unordered_map<TestChildAlarm*, SimpleEpollServer::AlarmRegToken>
      ChildTokenMap;

  ChildTokenMap child_tokens_;
};

class TestChildAlarm : public TestAlarm {
 public:
  void set_parent(TestParentAlarm* tp) { parent_ = tp; }
  void OnShutdown(SimpleEpollServer* eps) override {
    onshutdown_called_ = true;
    // Inform parent of shutdown
    parent_->OnShutdown(this, eps);
  }
  void OnRegistration(const SimpleEpollServer::AlarmRegToken& token,
                      SimpleEpollServer* eps) override {
    parent_->OnRegistration(this, token);
  }

 protected:
  TestParentAlarm* parent_;
};

class TestAlarmThatRegistersAnotherAlarm : public TestAlarm {
 public:
  TestAlarmThatRegistersAnotherAlarm()
      : alarm_(nullptr),
        reg_time_delta_usec_(0),
        eps_to_register_(nullptr),
        has_reg_alarm_(false) {}
  void SetRegisterAlarm(TestAlarm* alarm, int64_t time_delta_usec,
                        SimpleEpollServer* eps) {
    alarm_ = alarm;
    reg_time_delta_usec_ = time_delta_usec;
    has_reg_alarm_ = true;
    eps_to_register_ = eps;
  }
  int64_t OnAlarm() override {
    if (has_reg_alarm_) {
      eps_to_register_->RegisterAlarm(
          eps_to_register_->ApproximateNowInUsec() + reg_time_delta_usec_,
          alarm_);
      has_reg_alarm_ = false;
    }
    return TestAlarm::OnAlarm();
  }

 protected:
  TestAlarm* alarm_;
  int64_t reg_time_delta_usec_;
  SimpleEpollServer* eps_to_register_;
  bool has_reg_alarm_;
};

class TestAlarmThatRegistersAndReregistersAnotherAlarm : public TestAlarm {
 public:
  TestAlarmThatRegistersAndReregistersAnotherAlarm()
      : alarm_(nullptr),
        reg_time_delta_usec_(0),
        reregister_time_delta_usec_(0),
        eps_to_register_(nullptr),
        has_reg_alarm_(false) {}
  void SetRegisterAndReregisterAlarm(TestAlarm* alarm, int64_t time_delta_usec,
                                     int64_t reregister_delta_usec,
                                     SimpleEpollServer* eps) {
    alarm_ = alarm;
    reg_time_delta_usec_ = time_delta_usec;
    reregister_time_delta_usec_ = reregister_delta_usec;
    has_reg_alarm_ = true;
    eps_to_register_ = eps;
  }
  int64_t OnAlarm() override {
    if (has_reg_alarm_) {
      eps_to_register_->RegisterAlarm(
          eps_to_register_->ApproximateNowInUsec() + reg_time_delta_usec_,
          alarm_);
      alarm_->ReregisterAlarm(eps_to_register_->ApproximateNowInUsec() +
                              reregister_time_delta_usec_);
      has_reg_alarm_ = false;
    }
    return TestAlarm::OnAlarm();
  }

 protected:
  TestAlarm* alarm_;
  int64_t reg_time_delta_usec_;
  int64_t reregister_time_delta_usec_;
  SimpleEpollServer* eps_to_register_;
  bool has_reg_alarm_;
};

class TestAlarmThatUnregistersAnotherAlarm : public TestAlarm {
 public:
  TestAlarmThatUnregistersAnotherAlarm()
      : alarm_(nullptr), eps_to_register_(nullptr), has_unreg_alarm_(false) {}
  void SetUnregisterAlarm(TestAlarm* alarm, SimpleEpollServer* eps) {
    alarm_ = alarm;
    has_unreg_alarm_ = true;
    eps_to_register_ = eps;
  }
  int64_t OnAlarm() override {
    if (has_unreg_alarm_) {
      has_unreg_alarm_ = false;
      alarm_->UnregisterIfRegistered(eps_to_register_);
    }
    return TestAlarm::OnAlarm();
  }

 protected:
  TestAlarm* alarm_;
  SimpleEpollServer* eps_to_register_;
  bool has_unreg_alarm_;
};

class TestAlarmUnregister : public TestAlarm {
 public:
  TestAlarmUnregister()
      : onunregistration_called_(false),
        iterator_token_(nullptr) {
  }
  ~TestAlarmUnregister() override {
    delete iterator_token_;
  }

  void OnShutdown(SimpleEpollServer* eps) override {
    onshutdown_called_ = true;
  }

  int64_t OnAlarm() override {
    delete iterator_token_;
    iterator_token_ = nullptr;

    return TestAlarm::OnAlarm();
  }

  void OnRegistration(const SimpleEpollServer::AlarmRegToken& token,
                      SimpleEpollServer* eps) override {
    // Multiple iterator tokens are not maintained by this code,
    // so we should have reset the iterator_token in OnAlarm or
    // OnUnregistration.
    CHECK(iterator_token_ == nullptr);
    iterator_token_ = new SimpleEpollServer::AlarmRegToken(token);
  }
  void OnUnregistration() override {
    delete iterator_token_;
    iterator_token_ = nullptr;
    // Make sure that this alarm was not already unregistered.
    CHECK(onunregistration_called_ == false);
    onunregistration_called_ = true;
  }

  bool onunregistration_called() { return onunregistration_called_; }
  // Returns true if the token has been filled in with the saved iterator
  // and false if it has not.
  bool get_token(SimpleEpollServer::AlarmRegToken* token) {
    if (iterator_token_ != nullptr) {
      *token = *iterator_token_;
      return true;
    } else {
      return false;
    }
  }

 protected:
  bool onunregistration_called_;
  SimpleEpollServer::AlarmRegToken* iterator_token_;
};

void WaitForAlarm(SimpleEpollServer* eps, const TestAlarm& alarm) {
  for (int i = 0; i < 5; ++i) {
    // Ideally we would only have to call this once but it could wake up a bit
    // early and so not call the alarm.  If it wakes up early several times
    // there is something wrong.
    eps->WaitForEventsAndExecuteCallbacks();
    if (alarm.was_called()) {
      break;
    }
  }
}

// Check a couple of alarm times to make sure they're falling within a
// reasonable range.
TEST(SimpleEpollServerTest, TestAlarms) {
  EpollTestServer ep;
  TestAlarm alarm;

  int alarm_time = 10;

  // Register an alarm and make sure we wait long enough to hit it.
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  ep.RegisterAlarm(WallTimeNowInUsec() + alarm_time, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  WaitForAlarm(&ep, alarm);
  EXPECT_TRUE(alarm.was_called());
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  alarm.Reset();

  // Test a different time just to be careful.
  alarm_time = 20;
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  ep.RegisterAlarm(WallTimeNowInUsec() + alarm_time, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  WaitForAlarm(&ep, alarm);
  EXPECT_TRUE(alarm.was_called());
  alarm.Reset();

  // The alarm was a one-time thing.  Make sure that we don't hit it again.
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_FALSE(alarm.was_called());
  alarm.Reset();
}

// Same as above, but using RegisterAlarmApproximateDelta.
TEST(SimpleEpollServerTest, TestRegisterAlarmApproximateDelta) {
  EpollTestServer ep;
  TestAlarm alarm;

  int alarm_time = 10;

  // Register an alarm and make sure we wait long enough to hit it.
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  ep.RegisterAlarmApproximateDelta(alarm_time * 1000, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  WaitForAlarm(&ep, alarm);
  EXPECT_TRUE(alarm.was_called());
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  alarm.Reset();
  int64_t first_now = ep.ApproximateNowInUsec();
  EXPECT_LT(0u, first_now);

  // Test a different time just to be careful.
  alarm_time = 20;
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  ep.RegisterAlarmApproximateDelta(alarm_time * 1000, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  WaitForAlarm(&ep, alarm);
  EXPECT_TRUE(alarm.was_called());
  alarm.Reset();
  int64_t second_now = ep.ApproximateNowInUsec();

  EXPECT_LT(first_now, second_now);


  // The alarm was a one-time thing.  Make sure that we don't hit it again.
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_FALSE(alarm.was_called());
  alarm.Reset();
}

TEST(SimpleEpollServerTest, TestAlarmsWithInfiniteWait) {
  EpollTestServer ep;
  TestAlarm alarm;

  int alarm_time = 10;

  // Register an alarm and make sure we wait long enough to hit it.
  ep.set_timeout_in_us(-1);
  ep.RegisterAlarm(WallTimeNowInUsec() + alarm_time, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  WaitForAlarm(&ep, alarm);
  EXPECT_TRUE(alarm.was_called());
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  alarm.Reset();
}

// In this test we have an alarm that when fires gets re-registered
// at almost the same time at which it fires. Here, we want to make
// sure that when the alarm gets re-registered we do not call OnAlarm()
// on the same Alarm object again, until we have called
// WaitForEventsAndExecuteCallbacks(). A poor implementation of epoll
// server alarm handling can potentially cause OnAlarm() to be called
// multiple times. We make sure that the epoll server is not going in
// an infinite loop by checking that OnAlarm() is called exactly once
// on the alarm object that got registered again.
TEST(SimpleEpollServerTest, TestAlarmsThatGetReRegisteredAreNotCalledTwice) {
  // This alarm would get registered again
  TestAlarm alarm;
  TestAlarm alarm2;
  EpollTestServer ep;
  ep.set_timeout_in_us(-1);

  int64_t alarm_time = 10;
  int64_t abs_time = WallTimeNowInUsec() + alarm_time * 1000;

  // This will make the alarm re-register when OnAlarm is called.
  alarm.set_absolute_time(true);
  alarm.set_time_before_next_alarm(abs_time + 2);

  // Register two alarms and make sure we wait long enough to hit it.
  ep.RegisterAlarm(abs_time, &alarm);
  ep.RegisterAlarm(abs_time, &alarm2);
  EXPECT_EQ(2u, ep.GetNumPendingAlarmsForTest());

  WaitForAlarm(&ep, alarm);

  EXPECT_TRUE(alarm.was_called());
  // Make sure that alarm is called only once.
  EXPECT_EQ(1, alarm.num_called());
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  alarm.Reset();
}

// Here we make sure that when one alarm unregisters another alarm
// (that is supposed to be registered again because its OnAlarm
// returned > 0), the alarm thats supposed to be unregistered does
// actually gets unregistered.
TEST(SimpleEpollServerTest, TestAlarmsOneOnAlarmUnRegistersAnotherAlarm) {
  TestAlarm alarm;
  TestAlarmThatUnregistersAnotherAlarm alarm2;
  EpollTestServer ep;
  ep.set_timeout_in_us(-1);

  int64_t alarm_time = 1;
  int64_t abs_time = WallTimeNowInUsec() + alarm_time * 1000;

  // This will make the alarm re-register when OnAlarm is called.
  alarm.set_absolute_time(true);
  alarm.set_time_before_next_alarm(abs_time + 2);


  // Register two alarms and make sure we wait long enough to hit it.
  ep.RegisterAlarm(abs_time, &alarm);
  // This would cause us to unregister alarm when OnAlarm is called
  // on alarm2.
  alarm2.SetUnregisterAlarm(&alarm, &ep);
  ep.RegisterAlarm(abs_time + 1, &alarm2);
  EXPECT_EQ(2u, ep.GetNumPendingAlarmsForTest());

  WaitForAlarm(&ep, alarm);

  EXPECT_TRUE(alarm.was_called());
  // Make sure that alarm is called only once.
  EXPECT_EQ(1, alarm.num_called());
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  alarm.Reset();
}

// Check a couple of alarm times to make sure they're falling within a
// reasonable range.
TEST(SimpleEpollServerTest, TestRepeatAlarms) {
  EpollTestServer ep;
  TestAlarm alarm;

  int alarm_time = 20;

  // Register an alarm and make sure we wait long enough to hit it.
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  alarm.set_time_before_next_alarm(1000*alarm_time);
  ep.RegisterAlarm(WallTimeNowInUsec() + alarm_time, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());

  WaitForAlarm(&ep, alarm);
  // When we wake up it should be because the Alarm has been called, and has
  // registered itself to be called again.

  // Make sure the first alarm was called properly.
  EXPECT_TRUE(alarm.was_called());

  // Resetting means that the alarm is no longer a recurring alarm.  It will be
  // called once more and then stop.
  alarm.Reset();

  // Make sure the alarm is called one final time.
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  WaitForAlarm(&ep, alarm);

  EXPECT_TRUE(alarm.was_called());
  alarm.Reset();

  // The alarm was a one-time thing.  Make sure that we don't hit it again.
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_FALSE(alarm.was_called());
}

// Verify that an alarm that repeats itself in the past works properly.
TEST(SimpleEpollServerTest, TestRepeatAlarmInPast) {
  EpollTestServer ep;
  TestAlarm alarm;

  int64_t alarm_time = 20;
  int64_t abs_time = WallTimeNowInUsec() + alarm_time * 1000;

  // Make the alarm re-register in the past when OnAlarm is called.
  alarm.set_absolute_time(true);
  alarm.set_time_before_next_alarm(abs_time - 1000);

  // Register the alarm and make sure we wait long enough to hit it.
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  ep.RegisterAlarm(abs_time, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());

  WaitForAlarm(&ep, alarm);
  // When we wake up it should be because the Alarm has been called, and has
  // registered itself to be called again.

  // Make sure the first alarm was called properly.
  EXPECT_TRUE(alarm.was_called());

  // Resetting means that the alarm is no longer a recurring alarm.  It will be
  // called once more and then stop.
  alarm.Reset();

  // Make sure the alarm is called one final time.
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  ep.set_timeout_in_us(alarm_time * 1000 * 2);
  WaitForAlarm(&ep, alarm);

  EXPECT_TRUE(alarm.was_called());
  alarm.Reset();

  // The alarm was a one-time thing.  Make sure that we don't hit it again.
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_FALSE(alarm.was_called());
}

class EpollTestAlarms : public SimpleEpollServer {
 public:
  EpollTestAlarms() : SimpleEpollServer() {}

  inline int64_t NowInUsec() const override { return time_; }

  void CallAndReregisterAlarmEvents() override {
    recorded_now_in_us_ = NowInUsec();
    SimpleEpollServer::CallAndReregisterAlarmEvents();
  }

  void set_time(int64_t time) { time_ = time; }

  size_t GetNumPendingAlarmsForTest() const { return alarm_map_.size(); }

 private:
  int64_t time_;
};

// Test multiple interleaving alarms to make sure they work right.
// Pattern is roughly:
// time:   15    20    30    40
// alarm:   A     B     A'    C
TEST(SimpleEpollServerTest, TestMultipleAlarms) {
  EpollTestAlarms ep;
  TestAlarm alarmA;
  TestAlarm alarmB;
  TestAlarm alarmC;

  ep.set_timeout_in_us(50 * 1000 * 2);
  alarmA.set_time_before_next_alarm(1000 * 30);
  alarmA.set_absolute_time(true);
  ep.RegisterAlarm(15 * 1000, &alarmA);
  ep.RegisterAlarm(20 * 1000, &alarmB);
  ep.RegisterAlarm(40 * 1000, &alarmC);

  ep.set_time(15 * 1000);
  ep.CallAndReregisterAlarmEvents();  // A
  EXPECT_TRUE(alarmA.was_called());
  EXPECT_FALSE(alarmB.was_called());
  EXPECT_FALSE(alarmC.was_called());
  alarmA.Reset();  // Unregister A in the future.

  ep.set_time(20 * 1000);
  ep.CallAndReregisterAlarmEvents();  // B
  EXPECT_FALSE(alarmA.was_called());
  EXPECT_TRUE(alarmB.was_called());
  EXPECT_FALSE(alarmC.was_called());
  alarmB.Reset();

  ep.set_time(30 * 1000);
  ep.CallAndReregisterAlarmEvents();  // A
  EXPECT_TRUE(alarmA.was_called());
  EXPECT_FALSE(alarmB.was_called());
  EXPECT_FALSE(alarmC.was_called());
  alarmA.Reset();

  ep.set_time(40 * 1000);
  ep.CallAndReregisterAlarmEvents();  // C
  EXPECT_FALSE(alarmA.was_called());
  EXPECT_FALSE(alarmB.was_called());
  EXPECT_TRUE(alarmC.was_called());
  alarmC.Reset();

  ep.CallAndReregisterAlarmEvents();  // None.
  EXPECT_FALSE(alarmA.was_called());
  EXPECT_FALSE(alarmB.was_called());
  EXPECT_FALSE(alarmC.was_called());
}

TEST(SimpleEpollServerTest, TestAlarmOnShutdown) {
  TestAlarm alarm1;
  {
    EpollTestServer ep;
    const int64_t now = WallTimeNowInUsec();
    ep.RegisterAlarm(now + 5000, &alarm1);
  }

  EXPECT_TRUE(alarm1.onshutdown_called());
}

// Tests that if we have multiple alarms
// OnShutdown then we handle them properly.
TEST(SimpleEpollServerTest, TestMultipleAlarmOnShutdown) {
  TestAlarm alarm1;
  TestAlarm alarm2;
  TestAlarm alarm3;
  {
    EpollTestServer ep;
    const int64_t now = WallTimeNowInUsec();
    ep.RegisterAlarm(now + 5000, &alarm1);
    ep.RegisterAlarm(now + 9000, &alarm2);
    ep.RegisterAlarm(now + 9000, &alarm3);
  }

  EXPECT_TRUE(alarm1.onshutdown_called());
  EXPECT_TRUE(alarm2.onshutdown_called());
  EXPECT_TRUE(alarm3.onshutdown_called());
}
TEST(SimpleEpollServerTest, TestMultipleAlarmUnregistrationOnShutdown) {
  TestParentAlarm tp;
  TestChildAlarm alarm1;
  TestChildAlarm alarm2;
  alarm1.set_parent(&tp);
  alarm2.set_parent(&tp);
  {
    EpollTestServer ep;
    const int64_t now = WallTimeNowInUsec();
    ep.RegisterAlarm(now + 5000, &alarm1);
    ep.RegisterAlarm(now + 9000, &alarm2);
  }

  EXPECT_TRUE(alarm1.onshutdown_called());
  EXPECT_FALSE(alarm2.onshutdown_called());
}

// Check an alarm set in the past runs right away.
TEST(SimpleEpollServerTest, TestPastAlarm) {
  EpollTestServer ep;
  TestAlarm alarm;

  // Register the alarm and make sure we wait long enough to hit it.
  ep.set_timeout_in_us(1000 * 2);
  ep.RegisterAlarm(WallTimeNowInUsec() - 1000, &alarm);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_TRUE(alarm.was_called());
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  alarm.Reset();
}

// Test Unregistering of Alarms
TEST(SimpleEpollServerTest, TestUnregisterAlarm) {
  EpollTestServer ep;
  SimpleEpollServer::AlarmRegToken temptok;

  TestAlarmUnregister alarm1;
  TestAlarmUnregister alarm2;

  ep.RegisterAlarm(WallTimeNowInUsec() + 5 * 1000, &alarm1);
  ep.RegisterAlarm(WallTimeNowInUsec() + 13 * 1000, &alarm2);

  // Unregister an alarm.
  if (alarm2.get_token(&temptok)) {
    ep.UnregisterAlarm(temptok);
  }
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  EXPECT_TRUE(alarm2.onunregistration_called());

  if (alarm1.get_token(&temptok)) {
    ep.UnregisterAlarm(temptok);
  }
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
  EXPECT_TRUE(alarm1.onunregistration_called());
}

// Test Reregistering of Alarms
TEST(SimpleEpollServerTest, TestReregisterAlarm) {
  EpollTestAlarms ep;
  SimpleEpollServer::AlarmRegToken token;

  TestAlarmUnregister alarm;
  ep.set_time(1000);
  ep.RegisterAlarm(5000, &alarm);

  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  ASSERT_TRUE(alarm.get_token(&token));
  ep.ReregisterAlarm(token, 6000);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());

  ep.set_time(5000);
  ep.set_timeout_in_us(0);
  ep.CallAndReregisterAlarmEvents();
  EXPECT_FALSE(alarm.was_called());

  ep.set_time(6000);
  ep.CallAndReregisterAlarmEvents();
  EXPECT_TRUE(alarm.was_called());
}

TEST(SimpleEpollServerTest, TestReregisterDeferredAlarm) {
  EpollTestAlarms ep;
  ep.set_timeout_in_us(0);

  TestAlarm alarm;
  TestAlarmThatRegistersAndReregistersAnotherAlarm register_alarm;
  // Register the alarm in the past so it is added as a deferred alarm.
  register_alarm.SetRegisterAndReregisterAlarm(&alarm, -500, 500, &ep);
  ep.set_time(1000);
  ep.RegisterAlarm(1000, &register_alarm);
  // Call reregister twice, first to run register_alarm and second to run any
  // scheduled deferred alarms.
  ep.CallAndReregisterAlarmEvents();
  ep.CallAndReregisterAlarmEvents();

  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  EXPECT_FALSE(alarm.was_called());

  ep.set_time(1500);
  ep.CallAndReregisterAlarmEvents();
  EXPECT_TRUE(alarm.was_called());
}

// Check if an alarm fired and got reregistered, you are able to
// unregister the second registration.
TEST(SimpleEpollServerTest, TestFiredReregisteredAlarm) {
  EpollTestAlarms ep;
  TestAlarmUnregister alarmA;

  SimpleEpollServer::AlarmRegToken first_token;
  SimpleEpollServer::AlarmRegToken second_token;
  bool found;

  ep.set_timeout_in_us(50 * 1000 * 2);
  alarmA.set_time_before_next_alarm(1000 * 30);
  alarmA.set_absolute_time(true);

// Alarm A first fires at 15, then 30
  ep.RegisterAlarm(15 * 1000, &alarmA);

  found = alarmA.get_token(&first_token);
  EXPECT_TRUE(found);

  ep.set_time(15 * 1000);
  ep.CallAndReregisterAlarmEvents();  // A
  EXPECT_TRUE(alarmA.was_called());

  alarmA.Reset();

  found = alarmA.get_token(&second_token);
  EXPECT_TRUE(found);
  if (found) {
    ep.UnregisterAlarm(second_token);
  }

  ep.set_time(30 * 1000);
  ep.CallAndReregisterAlarmEvents();  // A

  alarmA.Reset();
}

// Here we make sure that one alarm can unregister another alarm
// in OnShutdown().
TEST(SimpleEpollServerTest, TestAlarmCanUnregistersAnotherAlarmOnShutdown) {
  TestAlarmThatUnregistersAnotherAlarm alarm1;
  TestAlarm alarm2;
  {
    EpollTestServer ep;
    // Register two alarms and make alarm1 is placed in queue in front of alarm2
    // so that when the queue is cleared, alarm1 is processed first.
    const int64_t now = WallTimeNowInUsec();
    ep.RegisterAlarm(now + 5000, &alarm1);
    ep.RegisterAlarm(now + 9000, &alarm2);
    alarm1.SetUnregisterAlarm(&alarm2, &ep);
    EXPECT_EQ(2u, ep.GetNumPendingAlarmsForTest());
  }
}

class TestAlarmRegisterAnotherAlarmShutdown : public TestAlarmUnregister {
 public:
  TestAlarmRegisterAnotherAlarmShutdown(EpollAlarmCallbackInterface* alarm2,
                                        int64_t when)
      : alarm2_(alarm2), when_(when) {}
  void OnShutdown(SimpleEpollServer* eps) override {
    TestAlarmUnregister::OnShutdown(eps);
    eps->RegisterAlarm(when_, alarm2_);
  }

 private:
  EpollAlarmCallbackInterface* alarm2_;
  int64_t when_;
};

// This tests that alarm registers another alarm when shutting down.
// The two cases are: new alarm comes before and after the alarm being
// notified by OnShutdown()
TEST(SimpleEpollServerTest, AlarmRegistersAnotherAlarmOnShutdownBeforeSelf) {
  TestAlarm alarm2;
  int64_t alarm_time = WallTimeNowInUsec() + 5000;
  TestAlarmRegisterAnotherAlarmShutdown alarm1(&alarm2, alarm_time - 1000);
  {
    EpollTestAlarms ep;
    ep.RegisterAlarm(alarm_time, &alarm1);
  }
  EXPECT_TRUE(alarm1.onshutdown_called());
  EXPECT_FALSE(alarm2.onshutdown_called());
}

TEST(SimpleEpollServerTest, AlarmRegistersAnotherAlarmOnShutdownAfterSelf) {
  TestAlarm alarm2;
  int64_t alarm_time = WallTimeNowInUsec() + 5000;
  TestAlarmRegisterAnotherAlarmShutdown alarm1(&alarm2, alarm_time + 1000);
  {
    EpollTestAlarms ep;
    ep.RegisterAlarm(alarm_time, &alarm1);
  }
  EXPECT_TRUE(alarm1.onshutdown_called());
  EXPECT_TRUE(alarm2.onshutdown_called());
}

TEST(SimpleEpollServerTest, TestWrite) {
  SimpleEpollServer ep;
  ep.set_timeout_in_us(1);
  char data[kPageSize] = {0};

  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }
  int read_fd = pipe_fds[0];
  int write_fd = pipe_fds[1];

  RecordingCB recording_cb;
  const Recorder* recorder = recording_cb.recorder();
  const std::vector<RecordEntry> *records = recorder->records();

  // Register to listen to write events.
  ep.RegisterFD(write_fd, &recording_cb, EPOLLOUT | O_NONBLOCK);
  // At this point the recorder should have the creation and registration
  // events.
  EXPECT_EQ(2u, records->size());

  // Fill up the pipe.
  int written = 1;
  for (int i = 0; i < 17 && written > 0 ; ++i) {
    written = write(write_fd, &data, kPageSize);
  }
  EXPECT_LT(written, 0);

  // There should be no new events as the pipe is not available for writing.
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(2u, records->size());

  // Now read data from the pipe to make it writable again.  This time the
  // we should get an EPOLLOUT event.
  int size = read(read_fd, &data, kPageSize);
  EXPECT_EQ(kPageSize, size);
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(3u, records->size());

  // Now unsubscribe from writable events (which adds a modification record)
  // and wait to verify that no event records are added.
  ep.StopWrite(write_fd);
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(4u, records->size());

  // We had the right number of events all along. Make sure they were actually
  // the right events.
  Recorder tmp;
  tmp.Record(&recording_cb, CREATION, 0, 0);
  tmp.Record(&recording_cb, REGISTRATION, write_fd, EPOLLOUT | O_NONBLOCK);
  tmp.Record(&recording_cb, EVENT, write_fd, EPOLLOUT);
  tmp.Record(&recording_cb, MODIFICATION, write_fd, O_NONBLOCK);

  EXPECT_TRUE(recorder->IsEqual(&tmp));
  ep.UnregisterFD(write_fd);

  close(read_fd);
  close(write_fd);
}

TEST(SimpleEpollServerTest, TestReadWrite) {
  SimpleEpollServer ep;
  ep.set_timeout_in_us(1);
  char data[kPageSize] = {0};

  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }
  int read_fd = pipe_fds[0];
  int write_fd = pipe_fds[1];

  RecordingCB recording_cb;
  const Recorder* recorder = recording_cb.recorder();
  const std::vector<RecordEntry> *records = recorder->records();

  // Register to listen to read and write events.
  ep.RegisterFDForReadWrite(read_fd, &recording_cb);
  // At this point the recorder should have the creation and registration
  // events.
  EXPECT_EQ(2u, records->size());

  int written = write(write_fd, &data, kPageSize);
  EXPECT_EQ(kPageSize, written);

  ep.WaitForEventsAndExecuteCallbacks();
  ep.UnregisterFD(read_fd);

  close(read_fd);
  close(write_fd);
}

TEST(SimpleEpollServerTest, TestMultipleFDs) {
  SimpleEpollServer ep;
  ep.set_timeout_in_us(1);
  char data = 'x';

  int pipe_one[2];
  if (pipe(pipe_one) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }
  int pipe_two[2];
  if (pipe(pipe_two) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }

  RecordingCB recording_cb_one;
  const Recorder* recorder_one = recording_cb_one.recorder();
  const std::vector<RecordEntry> *records_one = recorder_one->records();

  RecordingCB recording_cb_two;
  const Recorder* recorder_two = recording_cb_two.recorder();
  const std::vector<RecordEntry> *records_two = recorder_two->records();

  // Register to listen to read events for both pipes
  ep.RegisterFDForRead(pipe_one[0], &recording_cb_one);
  ep.RegisterFDForRead(pipe_two[0], &recording_cb_two);

  EXPECT_EQ(2u, records_one->size());
  EXPECT_EQ(2u, records_two->size());

  EXPECT_EQ(1, write(pipe_one[1], &data, 1));
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(3u, records_one->size());
  EXPECT_EQ(2u, records_two->size());

  EXPECT_EQ(1, write(pipe_two[1], &data, 1));
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(3u, records_one->size());
  EXPECT_EQ(3u, records_two->size());

  EXPECT_EQ(1, write(pipe_one[1], &data, 1));
  EXPECT_EQ(1, write(pipe_two[1], &data, 1));
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(4u, records_one->size());
  EXPECT_EQ(4u, records_two->size());

  ep.WaitForEventsAndExecuteCallbacks();
  ep.UnregisterFD(pipe_one[0]);
  ep.UnregisterFD(pipe_two[0]);
  close(pipe_one[0]);
  close(pipe_one[1]);
  close(pipe_two[0]);
  close(pipe_two[1]);
}

// Check that the SimpleEpollServer calls OnShutdown for any registered FDs.
TEST(SimpleEpollServerTest, TestFDOnShutdown) {
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }
  int read_fd = pipe_fds[0];
  int write_fd = pipe_fds[1];

  RecordingCB recording_cb1;
  RecordingCB recording_cb2;
  const Recorder* recorder1 = recording_cb1.recorder();
  const Recorder* recorder2 = recording_cb2.recorder();

  {
    SimpleEpollServer ep;
    ep.set_timeout_in_us(1);

    // Register to listen to write events.
    ep.RegisterFD(write_fd, &recording_cb1, EPOLLOUT | O_NONBLOCK);
    ep.RegisterFD(read_fd, &recording_cb2, EPOLLIN | O_NONBLOCK);
  }

  // Make sure OnShutdown was called for both callbacks.
  Recorder write_recorder;
  write_recorder.Record(&recording_cb1, CREATION, 0, 0);
  write_recorder.Record(
      &recording_cb1, REGISTRATION, write_fd, EPOLLOUT | O_NONBLOCK);
  write_recorder.Record(&recording_cb1, UNREGISTRATION, write_fd, false);
  write_recorder.Record(&recording_cb1, SHUTDOWN, write_fd, 0);
  EXPECT_TRUE(recorder1->IsEqual(&write_recorder));

  Recorder read_recorder;
  read_recorder.Record(&recording_cb2, CREATION, 0, 0);
  read_recorder.Record(
      &recording_cb2, REGISTRATION, read_fd, EPOLLIN | O_NONBLOCK);
  read_recorder.Record(&recording_cb2, UNREGISTRATION, read_fd, false);
  read_recorder.Record(&recording_cb2, SHUTDOWN, read_fd, 0);
  EXPECT_TRUE(recorder2->IsEqual(&read_recorder));

  close(read_fd);
  close(write_fd);
}

class UnregisterCB : public EpollCallbackInterface {
 public:
  explicit UnregisterCB(int fd)
    : eps_(nullptr), fd_(fd), onshutdown_called_(false) {
  }

  ~UnregisterCB() override {
  }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {
    eps_->UnregisterFD(fd_);
    eps_->UnregisterFD(fd);
    onshutdown_called_ = true;
    eps_ = nullptr;
  }

  void set_epollserver(SimpleEpollServer* eps) { eps_ = eps; }
  bool onshutdown_called() { return onshutdown_called_; }

  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {}
  void OnUnregistration(int fd, bool replaced) override {}

  std::string Name() const override { return "UnregisterCB"; }

 protected:
  SimpleEpollServer* eps_;
  int fd_;
  bool onshutdown_called_;
};

// Check that unregistering fds in OnShutdown works cleanly.
TEST(SimpleEpollServerTest, TestUnregisteringFDsOnShutdown) {
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }
  int read_fd = pipe_fds[0];
  int write_fd = pipe_fds[1];

  UnregisterCB unreg_cb1(read_fd);
  UnregisterCB unreg_cb2(write_fd);

  {
    SimpleEpollServer ep;
    ep.set_timeout_in_us(1);

    unreg_cb1.set_epollserver(&ep);
    unreg_cb2.set_epollserver(&ep);

    // Register to listen to write events.
    ep.RegisterFD(write_fd, &unreg_cb1, EPOLLOUT | O_NONBLOCK);
    ep.RegisterFD(read_fd, &unreg_cb2, EPOLLIN | O_NONBLOCK);
  }

  // Make sure at least one onshutdown was called.
  EXPECT_TRUE(unreg_cb1.onshutdown_called() ||
              unreg_cb2.onshutdown_called());
  // Make sure that both onshutdowns were not called.
  EXPECT_TRUE(!(unreg_cb1.onshutdown_called() &&
              unreg_cb2.onshutdown_called()));

  close(read_fd);
  close(write_fd);
}

TEST(SimpleEpollServerTest, TestFDsAndAlarms) {
  SimpleEpollServer ep;
  ep.set_timeout_in_us(5);
  char data = 'x';

  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }

  RecordingCB recording_cb;
  const Recorder* recorder = recording_cb.recorder();
  const std::vector<RecordEntry> *records = recorder->records();

  TestAlarm alarm;

  ep.RegisterFDForRead(pipe_fds[0], &recording_cb);

  EXPECT_EQ(2u, records->size());
  EXPECT_FALSE(alarm.was_called());

  // Write to the pipe and set a longish alarm so we get a read event followed
  // by an alarm event.
  int written = write(pipe_fds[1], &data, 1);
  EXPECT_EQ(1, written);
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(3u, records->size());
  EXPECT_FALSE(alarm.was_called());
  ep.RegisterAlarm(WallTimeNowInUsec() + 1000, &alarm);
  WaitForAlarm(&ep, alarm);
  EXPECT_EQ(3u, records->size());
  EXPECT_TRUE(alarm.was_called());
  alarm.Reset();

  // Now set a short alarm so the alarm and the read event are called together.
  ep.RegisterAlarm(WallTimeNowInUsec(), &alarm);
  written = write(pipe_fds[1], &data, 1);
  EXPECT_EQ(1, written);
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_TRUE(alarm.was_called());
  EXPECT_EQ(4u, records->size());

  ep.UnregisterFD(pipe_fds[0]);

  close(pipe_fds[0]);
  close(pipe_fds[1]);
}

class EpollReader: public EpollCallbackInterface {
 public:
  explicit EpollReader(int len)
      : len_(0),
        expected_len_(len),
        done_reading_(false) {
    memset(&buf_, 0, kMaxBufLen);
  }

  ~EpollReader() override {}

  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }

  void OnModification(int fd, int event_mask) override {}

  void OnEvent(int fd, EpollEvent* event) override {
    if (event->in_events & EPOLLIN) {
      len_ += read(fd, &buf_ + len_, kMaxBufLen - len_);
    }

    // If we have finished reading...
    if (event->in_events & EPOLLHUP) {
      CHECK_EQ(len_, expected_len_);
      done_reading_ = true;
    }
  }

  void OnUnregistration(int fd, bool replaced) override {}

  void OnShutdown(SimpleEpollServer* eps, int fd) override {
    // None of the current tests involve having active callbacks when the
    // server shuts down.
    EPOLL_LOG(FATAL);
  }

  std::string Name() const override { return "EpollReader"; }

  // Returns true if the data in buf is the same as buf_, false otherwise.
  bool CheckOutput(char* buf, int len) {
    if (len != len_) {
      return false;
    }
    return !memcmp(buf, buf_, len);
  }

  bool done_reading() { return done_reading_; }

 protected:
  int len_;
  int expected_len_;
  char buf_[kMaxBufLen];
  bool done_reading_;
};

void TestPipe(char *test_message, int len) {
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe failed()";
  }
  int reader_pipe = pipe_fds[0];
  int writer_pipe = pipe_fds[1];
  int child_pid;
  memset(test_message, 'x', len);

  switch (child_pid = fork()) {
    case 0: {  // Child will send message.
      const char *message = test_message;
      int size;
      close(reader_pipe);
      while ((size = write(writer_pipe, message, len)) > 0) {
        message += size;
        len -= size;
        if (len == 0) {
          break;
        }
      }
      if (len > 0) {
        EPOLL_PLOG(FATAL) << "write() failed";
      }
      close(writer_pipe);

      _exit(0);
    }
    case -1:
      EPOLL_PLOG(FATAL) << "fork() failed";
      break;
    default: {  // Parent will receive message.
      close(writer_pipe);
      auto ep = EpollMakeUnique<SimpleEpollServer>();
      ep->set_timeout_in_us(1);
      EpollReader reader(len);
      ep->RegisterFD(reader_pipe, &reader, EPOLLIN);

      int64_t start_ms = WallTimeNowInUsec() / 1000;
      // Loop until we're either done reading, or have waited ~10 us.
      while (!reader.done_reading() &&
             (WallTimeNowInUsec() / 1000 - start_ms) < 10000) {
        ep->WaitForEventsAndExecuteCallbacks();
      }
      ep->UnregisterFD(reader_pipe);
      CHECK(reader.CheckOutput(test_message, len));
      break;
    }
  }

  close(reader_pipe);
  close(writer_pipe);
}

TEST(SimpleEpollServerTest, TestSmallPipe) {
  char buf[kMaxBufLen];
  TestPipe(buf, 10);
}

TEST(SimpleEpollServerTest, TestLargePipe) {
  char buf[kMaxBufLen];
  TestPipe(buf, kMaxBufLen);
}

// Tests RegisterFDForRead as well as StopRead.
TEST(SimpleEpollServerTest, TestRead) {
  SimpleEpollServer ep;
  ep.set_timeout_in_us(1);
  int len = 1;

  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }
  int read_fd = pipe_fds[0];
  int write_fd = pipe_fds[1];

  auto reader = EpollMakeUnique<EpollReader>(len);

  // Check that registering a FD for read alerts us when there is data to be
  // read.
  ep.RegisterFDForRead(read_fd, reader.get());
  char data = 'a';
  int size = write(write_fd, &data, 1);
  EXPECT_EQ(1, size);
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_TRUE(reader->CheckOutput(&data, len));

  // Remove the callback for read events, write to the pipe and make sure that
  // we did not read more data.
  ep.StopRead(read_fd);
  size = write(write_fd, &data, len);
  EXPECT_EQ(1, size);
  // The wait will return after timeout.
  ep.WaitForEventsAndExecuteCallbacks();
  EXPECT_TRUE(reader->CheckOutput(&data, len));
  ep.UnregisterFD(read_fd);

  close(read_fd);
  close(write_fd);
}

class EdgeTriggerCB : public EpollCallbackInterface {
 public:
  EdgeTriggerCB(int read_size, int write_size, char write_char, char peer_char)
      : eps_(nullptr),
        read_buf_(read_size),
        write_buf_(write_size, write_char),
        peer_char_(peer_char) {
    Reset();
  }

  ~EdgeTriggerCB() override {}

  void Reset() {
    CHECK(eps_ == nullptr);
    bytes_read_ = 0;
    bytes_written_ = 0;
    can_read_ = false;
    will_read_ = false;
    can_write_ = false;
    will_write_ = false;
    read_closed_ = false;
    write_closed_ = false;
  }

  void ResetByteCounts() {
    bytes_read_ = bytes_written_ = 0;
  }

  void set_will_read(bool will_read) { will_read_ = will_read; }

  void set_will_write(bool will_write) { will_write_ = will_write; }

  bool can_write() const { return can_write_; }

  int bytes_read() const { return bytes_read_; }

  int bytes_written() const { return bytes_written_; }

  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
    EXPECT_TRUE(eps_ == nullptr);
    eps_ = eps;
    Initialize(fd, event_mask);
  }

  void OnModification(int fd, int event_mask) override {
    EXPECT_TRUE(eps_ != nullptr);
    if (event_mask & EPOLLET) {
      Initialize(fd, event_mask);
    } else {
      eps_->SetFDNotReady(fd);
    }
  }

  void OnEvent(int fd, EpollEvent* event) override {
    const int event_mask = event->in_events;
    if (event_mask & (EPOLLHUP | EPOLLERR)) {
      write_closed_ = true;
      return;
    }
    if (will_read_ && event_mask & EPOLLIN) {
      EXPECT_FALSE(read_closed_);
      int read_size = read_buf_.size();
      memset(&read_buf_[0], 0, read_size);
      int len = recv(fd, &read_buf_[0], read_size, MSG_DONTWAIT);
      // Update the readiness states
      can_read_ = (len == read_size);

      if (len > 0) {
        bytes_read_ += len;
        EPOLL_VLOG(1) << "fd: " << fd << ", read " << len
                      << ", total: " << bytes_read_;
        // Now check the bytes read
        EXPECT_TRUE(CheckReadBuffer(len));
      } else if (len < 0) {
        EPOLL_VLOG(1) << "fd: " << fd << " read hit EAGAIN";
        EXPECT_EQ(EAGAIN, errno) << strerror(errno);
        can_read_ = false;
      } else {
        read_closed_ = true;
      }
    }
    if (will_write_ && event_mask & EPOLLOUT) {
      // Write side close/full close can only detected by EPOLLHUP, which is
      // caused by EPIPE.
      EXPECT_FALSE(write_closed_);
      int write_size = write_buf_.size();
      int len = send(fd, &write_buf_[0], write_size, MSG_DONTWAIT);
      can_write_ = (len == write_size);
      if (len > 0) {
        bytes_written_ += len;
        EPOLL_VLOG(1) << "fd: " << fd << ", write " << len
                      << ", total: " << bytes_written_;
      } else {
        EPOLL_VLOG(1) << "fd: " << fd << " write hit EAGAIN";
        EXPECT_EQ(EAGAIN, errno) << strerror(errno);
        can_write_ = false;
      }
    }
    // Since we can only get on the ready list once, wait till we confirm both
    // read and write side continuation state and set the correct event mask
    // for the ready list.
    event->out_ready_mask = can_read_ ? static_cast<int>(EPOLLIN) : 0;
    if (can_write_) {
      event->out_ready_mask |= EPOLLOUT;
    }
  }

  void OnUnregistration(int fd, bool replaced) override {
    EXPECT_TRUE(eps_ != nullptr);
    eps_ = nullptr;
  }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {
    // None of the current tests involve having active callbacks when the
    // server shuts down.
    EPOLL_LOG(FATAL);
  }

  std::string Name() const override { return "EdgeTriggerCB"; }

 private:
  SimpleEpollServer* eps_;
  std::vector<char> read_buf_;
  int bytes_read_;
  std::vector<char> write_buf_;
  int bytes_written_;
  char peer_char_;   // The char we expected to read.
  bool can_read_;
  bool will_read_;
  bool can_write_;
  bool will_write_;
  bool read_closed_;
  bool write_closed_;

  void Initialize(int fd, int event_mask) {
    CHECK(eps_);
    can_read_ = can_write_ = false;
    if (event_mask & EPOLLET) {
      int events = 0;
      if (event_mask & EPOLLIN) {
        events |= EPOLLIN;
        can_read_ = true;
      }
      if (event_mask & EPOLLOUT) {
        events |= EPOLLOUT;
        can_write_ = true;
      }
      eps_->SetFDReady(fd, events);
    }
  }

  bool CheckReadBuffer(int len) const {
    for (int i = 0; i < len; ++i) {
      if (peer_char_ != read_buf_[i]) {
        return false;
      }
    }
    return true;
  }
};

// Test adding and removing from the ready list.
TEST(SimpleEpollServerTest, TestReadyList) {
  SimpleEpollServer ep;
  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    EPOLL_PLOG(FATAL) << "pipe() failed";
  }

  // Just use any CB will do, since we never wait on epoll events.
  EdgeTriggerCB reader1(0, 0, 0, 0);
  EdgeTriggerCB reader2(0, 0, 0, 0);

  ep.RegisterFD(pipe_fds[0], &reader1, EPOLLIN);
  ep.RegisterFD(pipe_fds[1], &reader2, EPOLLOUT);

  // Adding fds that are registered with eps
  EXPECT_FALSE(ep.IsFDReady(pipe_fds[0]));
  EXPECT_FALSE(ep.IsFDReady(pipe_fds[1]));

  ep.SetFDReady(pipe_fds[0], EPOLLIN);
  EXPECT_TRUE(ep.IsFDReady(pipe_fds[0]));
  EXPECT_FALSE(ep.IsFDReady(pipe_fds[1]));
  EXPECT_EQ(1u, ep.ReadyListSize());
  ep.SetFDReady(pipe_fds[1], EPOLLOUT);
  EXPECT_TRUE(ep.IsFDReady(pipe_fds[0]));
  EXPECT_TRUE(ep.IsFDReady(pipe_fds[1]));
  EXPECT_EQ(2u, ep.ReadyListSize());

  // Now check that SetFDNotReady doesn't affect other fds
  ep.SetFDNotReady(pipe_fds[0]);
  EXPECT_FALSE(ep.IsFDReady(pipe_fds[0]));
  EXPECT_TRUE(ep.IsFDReady(pipe_fds[1]));
  EXPECT_EQ(1u, ep.ReadyListSize());

  ep.UnregisterFD(pipe_fds[0]);
  ep.UnregisterFD(pipe_fds[1]);
  EXPECT_EQ(0u, ep.ReadyListSize());

  // Now try adding them when they are not registered, and it shouldn't work.
  ep.SetFDReady(pipe_fds[0], EPOLLIN);
  EXPECT_FALSE(ep.IsFDReady(pipe_fds[0]));
  EXPECT_EQ(0u, ep.ReadyListSize());

  close(pipe_fds[0]);
  close(pipe_fds[1]);
}

class EPSWaitThread : public EpollThread {
 public:
  explicit EPSWaitThread(SimpleEpollServer* eps)
      : EpollThread("EPSWait"), eps_(eps), done_(false) {}

  void Run() override {
    eps_->WaitForEventsAndExecuteCallbacks();
  }

  bool done() { return done_; }
 private:
  SimpleEpollServer* eps_;
  bool done_;
};

TEST(EpollServerTest, TestWake) {
  SimpleEpollServer eps;
  eps.set_timeout_in_us(-1);
  EPSWaitThread eps_thread(&eps);
  eps_thread.Start();

  EXPECT_FALSE(eps_thread.done());
  eps.Wake();
  eps_thread.Join();
}

class UnRegisterWhileProcessingCB: public EpollCallbackInterface {
 public:
  explicit UnRegisterWhileProcessingCB(int fd) : eps_(nullptr), fd_(fd) {}

  ~UnRegisterWhileProcessingCB() override {
  }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {}

  void set_epoll_server(SimpleEpollServer* eps) { eps_ = eps; }
  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {
    // This should cause no problems.
    eps_->UnregisterFD(fd_);
  }
  void OnUnregistration(int fd, bool replaced) override {}
  std::string Name() const override { return "UnRegisterWhileProcessingCB"; }

 protected:
  SimpleEpollServer* eps_;
  int fd_;
};

class RegisterWhileProcessingCB: public EpollCallbackInterface {
 public:
  RegisterWhileProcessingCB(int fd, EpollCallbackInterface* cb)
    : eps_(nullptr), fd_(fd), cb_(cb) {}

  ~RegisterWhileProcessingCB() override {
  }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {}

  void set_epoll_server(SimpleEpollServer* eps) { eps_ = eps; }
  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {
    // This should cause no problems.
    eps_->RegisterFDForReadWrite(fd_, cb_);
  }
  void OnUnregistration(int fd, bool replaced) override {}
  std::string Name() const override { return "RegisterWhileProcessingCB"; }

 protected:
  SimpleEpollServer* eps_;
  int fd_;
  EpollCallbackInterface* cb_;
};

// Nothing bad should happen when we do this. We're -only-
// testing that nothing bad occurs in this test.
TEST(SimpleEpollServerTest, NothingBadWhenUnRegisteringFDWhileProcessingIt) {
  UnRegisterWhileProcessingCB cb(0);
  {
    FakeSimpleEpollServer epoll_server;
    cb.set_epoll_server(&epoll_server);
    epoll_server.RegisterFDForReadWrite(0, &cb);
    epoll_event ee;
    ee.data.fd = 0;
    epoll_server.AddEvent(0, ee);
    epoll_server.AdvanceBy(1);
    epoll_server.WaitForEventsAndExecuteCallbacks();
  }
}

//
// testing that nothing bad occurs in this test.
TEST(SimpleEpollServerTest,
     NoEventsDeliveredForFdsOfUnregisteredCBsWithReRegdFD) {
  // events: fd0, fd1, fd2
  // fd0 -> unreg fd2
  // fd1 -> reg fd2
  // fd2 -> no event should be seen
  RecordingCB recorder_cb;
  UnRegisterWhileProcessingCB unreg_cb(-3);
  RegisterWhileProcessingCB reg_other_cb(-3, &recorder_cb);
  {
    FakeSimpleEpollServer epoll_server;
    unreg_cb.set_epoll_server(&epoll_server);
    reg_other_cb.set_epoll_server(&epoll_server);
    epoll_server.RegisterFDForReadWrite(-1, &unreg_cb);
    epoll_server.RegisterFDForReadWrite(-2, &reg_other_cb);
    epoll_server.RegisterFDForReadWrite(-3, &recorder_cb);

    epoll_event ee;
    ee.events = EPOLLIN;  // asserted for all events for this test.

    // Note that these events are in 'backwards' order in terms of time.
    // Currently, the SimpleEpollServer code invokes the CBs from last delivered
    // to first delivered, so this is to be sure that we invoke the CB for -1
    // before -2, before -3.
    ee.data.fd = -1;
    epoll_server.AddEvent(2, ee);
    ee.data.fd = -2;
    epoll_server.AddEvent(1, ee);
    ee.data.fd = -3;
    epoll_server.AddEvent(0, ee);

    epoll_server.AdvanceBy(5);
    epoll_server.WaitForEventsAndExecuteCallbacks();
  }

  Recorder correct_recorder;
  correct_recorder.Record(&recorder_cb, CREATION, 0, 0);
  correct_recorder.Record(&recorder_cb, REGISTRATION, -3,
                          EPOLLIN | EPOLLOUT);
  correct_recorder.Record(&recorder_cb, UNREGISTRATION, -3, 0);
  correct_recorder.Record(&recorder_cb, REGISTRATION, -3,
                          EPOLLIN | EPOLLOUT);
  correct_recorder.Record(&recorder_cb, SHUTDOWN, -3, 0);

  EXPECT_TRUE(correct_recorder.IsEqual(recorder_cb.recorder()));
}

class ReRegWhileReadyListOnEvent: public EpollCallbackInterface {
 public:
  explicit ReRegWhileReadyListOnEvent(int fd) : eps_(nullptr) {}

  void OnShutdown(SimpleEpollServer* eps, int fd) override {}

  void set_epoll_server(SimpleEpollServer* eps) { eps_ = eps; }
  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {
    // This should cause no problems.
    eps_->UnregisterFD(fd);
    eps_->RegisterFDForReadWrite(fd, this);
    eps_->UnregisterFD(fd);
  }
  void OnUnregistration(int fd, bool replaced) override {}
  std::string Name() const override { return "ReRegWhileReadyListOnEvent"; }

 protected:
  SimpleEpollServer* eps_;
};

// Nothing bad should happen when we do this. We're -only-
// testing that nothing bad occurs in this test.
TEST(SimpleEpollServerTest,
     NothingBadWhenReRegisteringFDWhileProcessingFromReadyList) {
  ReRegWhileReadyListOnEvent cb(0);
  {
    FakeSimpleEpollServer epoll_server;
    cb.set_epoll_server(&epoll_server);
    epoll_server.RegisterFDForReadWrite(0, &cb);
    epoll_event ee;
    ee.data.fd = 0;
    epoll_server.AddEvent(0, ee);
    epoll_server.AdvanceBy(1);
    epoll_server.WaitForEventsAndExecuteCallbacks();
  }
}

class UnRegEverythingReadyListOnEvent: public EpollCallbackInterface {
 public:
  UnRegEverythingReadyListOnEvent() : eps_(nullptr), fd_(0), fd_range_(0) {}

  void set_fd(int fd) { fd_ = fd; }
  void set_fd_range(int fd_range) { fd_range_ = fd_range; }
  void set_num_called(int* num_called) { num_called_ = num_called; }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {}

  void set_epoll_server(SimpleEpollServer* eps) { eps_ = eps; }
  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
    eps->SetFDReady(fd, EPOLLIN);
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {
    // This should cause no problems.
    CHECK(num_called_ != nullptr);
    ++(*num_called_);
    // Note that we're iterating from -fd_range + 1 -> 0.
    // We do this because there is an FD installed into the
    // epollserver somewhere in the low numbers.
    // Using negative FD numbers (which are guaranteed to not
    // exist in the epoll-server) ensures that we will not
    // come in conflict with the preexisting FD.
    for (int i = -fd_range_ + 1; i <= 0; ++i) {
      eps_->UnregisterFD(i);
    }
  }
  void OnUnregistration(int fd, bool replaced) override {}
  std::string Name() const override {
    return "UnRegEverythingReadyListOnEvent";
  }

 protected:
  SimpleEpollServer* eps_;
  int fd_;
  int fd_range_;
  int* num_called_;
};

TEST(SimpleEpollServerTest,
     NothingBadWhenUnRegisteredWhileProcessingFromReadyList) {
  const size_t kNumCallbacks = 32u;
  UnRegEverythingReadyListOnEvent callbacks[kNumCallbacks];
  int num_called = 0;
  {
    FakeSimpleEpollServer epoll_server;
    for (size_t i = 0; i < kNumCallbacks; ++i) {
      callbacks[i].set_fd(-i);
      callbacks[i].set_fd_range(kNumCallbacks);
      callbacks[i].set_num_called(&num_called);
      callbacks[i].set_epoll_server(&epoll_server);
      epoll_server.RegisterFDForReadWrite(0, &callbacks[i]);
      epoll_event ee;
      ee.data.fd = -i;
      epoll_server.AddEvent(0, ee);
    }
    epoll_server.AdvanceBy(1);
    epoll_server.WaitForEventsAndExecuteCallbacks();
    epoll_server.WaitForEventsAndExecuteCallbacks();
  }
  EXPECT_EQ(1, num_called);
}

TEST(SimpleEpollServerTest, TestThatVerifyReadyListWorksWithNothingInList) {
  FakeSimpleEpollServer epoll_server;
  epoll_server.VerifyReadyList();
}

TEST(SimpleEpollServerTest, TestThatVerifyReadyListWorksWithStuffInLists) {
  FakeSimpleEpollServer epoll_server;
  epoll_server.VerifyReadyList();
}

TEST(SimpleEpollServerTest,
     ApproximateNowInUsAccurateOutideOfWaitForEventsAndExecuteCallbacks) {
  FakeSimpleEpollServer epoll_server;
  epoll_server.AdvanceBy(1232);
  EXPECT_EQ(epoll_server.ApproximateNowInUsec(), epoll_server.NowInUsec());
  epoll_server.AdvanceBy(1111);
  EXPECT_EQ(epoll_server.ApproximateNowInUsec(), epoll_server.NowInUsec());
}

class ApproximateNowInUsecTestCB: public EpollCallbackInterface {
 public:
  ApproximateNowInUsecTestCB() : feps_(nullptr), called_(false) {}

  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {
    EXPECT_EQ(feps_->ApproximateNowInUsec(), feps_->NowInUsec());
    feps_->AdvanceBy(1111);
    EXPECT_EQ(1 * 1111 + feps_->ApproximateNowInUsec(), feps_->NowInUsec());
    feps_->AdvanceBy(1111);
    EXPECT_EQ(2 * 1111 + feps_->ApproximateNowInUsec(), feps_->NowInUsec());
    called_ = true;
  }
  void OnUnregistration(int fd, bool replaced) override {}
  void OnShutdown(SimpleEpollServer* eps, int fd) override {}
  std::string Name() const override { return "ApproximateNowInUsecTestCB"; }

  void set_fakeepollserver(FakeSimpleEpollServer* feps) { feps_ = feps; }
  bool called() const { return called_; }

 protected:
  FakeSimpleEpollServer* feps_;
  bool called_;
};

TEST(SimpleEpollServerTest,
     ApproximateNowInUsApproximateInsideOfWaitForEventsAndExecuteCallbacks) {
  int dummy_fd = 11111;
  ApproximateNowInUsecTestCB aniutcb;
  {
    FakeSimpleEpollServer epoll_server;
    aniutcb.set_fakeepollserver(&epoll_server);

    epoll_server.RegisterFD(dummy_fd, &aniutcb, EPOLLIN);
    epoll_event ee;
    ee.data.fd = dummy_fd;
    ee.events = EPOLLIN;
    epoll_server.AddEvent(10242, ee);
    epoll_server.set_timeout_in_us(-1);
    epoll_server.AdvanceByAndWaitForEventsAndExecuteCallbacks(20000);
    EXPECT_TRUE(aniutcb.called());
  }
}

// A mock epoll server that also simulates kernel delay in scheduling epoll
// events.
class FakeEpollServerWithDelay : public FakeSimpleEpollServer {
 public:
  FakeEpollServerWithDelay() : FakeSimpleEpollServer(), delay(0) {}

  int delay;

 protected:
  int epoll_wait_impl(int epfd, struct epoll_event* events, int max_events,
                      int timeout_in_ms) override {
    int out = FakeSimpleEpollServer::epoll_wait_impl(epfd, events, max_events,
                                                     timeout_in_ms);
    AdvanceBy(delay);
    return out;
  }
};

// A callback that records the epoll event's delay.
class RecordDelayOnEvent: public EpollCallbackInterface {
 public:
  RecordDelayOnEvent() : last_delay(-1), eps_(nullptr) {}

  ~RecordDelayOnEvent() override {
  }

  void OnShutdown(SimpleEpollServer* eps, int fd) override {}

  std::string Name() const override { return "RecordDelayOnEvent"; }

  void set_epoll_server(SimpleEpollServer* eps) { eps_ = eps; }
  void OnRegistration(SimpleEpollServer* eps, int fd, int event_mask) override {
  }
  void OnModification(int fd, int event_mask) override {}
  void OnEvent(int fd, EpollEvent* event) override {
    last_delay = eps_->LastDelayInUsec();
  }
  void OnUnregistration(int fd, bool replaced) override {}

  int64_t last_delay;

 protected:
  SimpleEpollServer* eps_;
};

// Tests that an epoll callback sees the correct delay for its event when it
// calls LastDelayInUsec().
TEST(EpollServerTest, TestLastDelay) {
  RecordDelayOnEvent cb;
  FakeEpollServerWithDelay epoll_server;

  cb.set_epoll_server(&epoll_server);

  epoll_server.RegisterFDForReadWrite(0, &cb);
  epoll_event ee;
  ee.data.fd = 0;

  // Inject delay, and confirm that it's reported.
  epoll_server.set_timeout_in_us(5000);
  epoll_server.delay = 6000;
  epoll_server.AddEvent(0, ee);
  epoll_server.AdvanceBy(1);
  epoll_server.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(cb.last_delay, 1000);

  // Fire an event before the timeout ends, and confirm that reported delay
  // isn't negative.
  epoll_server.set_timeout_in_us(5000);
  epoll_server.delay = 0;
  epoll_server.AddEvent(0, ee);
  epoll_server.AdvanceBy(1);
  epoll_server.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(cb.last_delay, 0);

  // Wait forever until an event fires, and confirm there's no reported delay.
  epoll_server.set_timeout_in_us(-1);
  epoll_server.delay = 6000;
  epoll_server.AddEvent(0, ee);
  epoll_server.AdvanceBy(1);
  epoll_server.WaitForEventsAndExecuteCallbacks();
  EXPECT_EQ(cb.last_delay, 0);
}

TEST(SimpleEpollServerAlarmTest, TestShutdown) {
  std::unique_ptr<SimpleEpollServer> eps(new SimpleEpollServer);
  EpollAlarm alarm1;
  EpollAlarm alarm2;

  eps->RegisterAlarmApproximateDelta(10000000, &alarm1);
  eps->RegisterAlarmApproximateDelta(10000000, &alarm2);

  alarm2.UnregisterIfRegistered();
  EXPECT_FALSE(alarm2.registered());
  eps = nullptr;

  EXPECT_FALSE(alarm1.registered());
}

TEST(SimpleEpollServerAlarmTest, TestUnregister) {
  SimpleEpollServer eps;
  EpollAlarm alarm;

  eps.RegisterAlarmApproximateDelta(10000000, &alarm);
  EXPECT_TRUE(alarm.registered());

  alarm.UnregisterIfRegistered();
  EXPECT_FALSE(alarm.registered());

  alarm.UnregisterIfRegistered();
  EXPECT_FALSE(alarm.registered());
}

TEST(SimpleEpollServerAlarmTest, TestUnregisterOnDestruction) {
  EpollTestServer eps;
  std::unique_ptr<EpollAlarm> alarm(new EpollAlarm());
  EpollAlarm* alarm_ptr = alarm.get();

  eps.RegisterAlarmApproximateDelta(10000000, alarm.get());
  EXPECT_TRUE(eps.ContainsAlarm(alarm_ptr));
  alarm = nullptr;
  EXPECT_EQ(0u, eps.GetNumPendingAlarmsForTest());
}

TEST(SimpleEpollServerAlarmTest, TestUnregisterOnAlarm) {
  EpollTestServer eps;
  EpollAlarm alarm;

  eps.RegisterAlarmApproximateDelta(1, &alarm);
  EXPECT_TRUE(eps.ContainsAlarm(&alarm));

  while (alarm.registered()) {
    eps.WaitForEventsAndExecuteCallbacks();
  }
  EXPECT_FALSE(eps.ContainsAlarm(&alarm));
}

TEST(SimpleEpollServerAlarmTest, TestReregisterAlarm) {
  EpollTestAlarms ep;

  EpollAlarm alarm;
  ep.set_time(1000);
  ep.RegisterAlarm(5000, &alarm);

  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());
  alarm.ReregisterAlarm(6000);
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());

  ep.set_time(5000);
  ep.set_timeout_in_us(0);
  ep.CallAndReregisterAlarmEvents();
  EXPECT_EQ(1u, ep.GetNumPendingAlarmsForTest());

  ep.set_time(6000);
  ep.CallAndReregisterAlarmEvents();
  EXPECT_EQ(0u, ep.GetNumPendingAlarmsForTest());
}

TEST(SimpleEpollServerAlarmTest, TestThatSameAlarmCanNotBeRegisteredTwice) {
  TestAlarm alarm;
  SimpleEpollServer epoll_server;
  epoll_server.RegisterAlarm(1, &alarm);
  EXPECT_EPOLL_BUG(epoll_server.RegisterAlarm(1, &alarm),
                   "Alarm already exists");
}

}  // namespace

}  // namespace test

}  // namespace epoll_server
