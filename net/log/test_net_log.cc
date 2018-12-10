// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/test_net_log.h"

#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_entry.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"

namespace net {

// TestNetLog::Observer is an implementation of NetLog::ThreadSafeObserver
// that saves messages to a buffer.
class TestNetLog::Observer : public NetLog::ThreadSafeObserver {
 public:
  Observer() = default;
  ~Observer() override = default;

  // Returns the list of all entries in the log.
  void GetEntries(TestNetLogEntry::List* entry_list) const {
    base::AutoLock lock(lock_);
    *entry_list = entry_list_;
  }

  // Fills |entry_list| with all entries in the log from the specified Source.
  void GetEntriesForSource(NetLogSource source,
                           TestNetLogEntry::List* entry_list) const {
    base::AutoLock lock(lock_);
    entry_list->clear();
    for (const auto& entry : entry_list_) {
      if (entry.source.id == source.id)
        entry_list->push_back(entry);
    }
  }

  // Returns the number of entries in the log.
  size_t GetSize() const {
    base::AutoLock lock(lock_);
    return entry_list_.size();
  }

  void Clear() {
    base::AutoLock lock(lock_);
    entry_list_.clear();
  }

 private:
  // ThreadSafeObserver implementation:
  void OnAddEntry(const NetLogEntry& entry) override {
    // Using Dictionaries instead of Values makes checking values a little
    // simpler.
    std::unique_ptr<base::DictionaryValue> param_dict =
        base::DictionaryValue::From(entry.ParametersToValue());

    // Only need to acquire the lock when accessing class variables.
    base::AutoLock lock(lock_);
    entry_list_.push_back(TestNetLogEntry(entry.type(), base::TimeTicks::Now(),
                                          entry.source(), entry.phase(),
                                          std::move(param_dict)));
  }

  // Needs to be "mutable" to use it in GetEntries().
  mutable base::Lock lock_;

  TestNetLogEntry::List entry_list_;

  DISALLOW_COPY_AND_ASSIGN(Observer);
};

TestNetLog::TestNetLog() : observer_(new Observer()) {
  AddObserver(observer_.get(),
              NetLogCaptureMode::IncludeCookiesAndCredentials());
}

TestNetLog::~TestNetLog() {
  RemoveObserver(observer_.get());
}

void TestNetLog::SetCaptureMode(NetLogCaptureMode capture_mode) {
  SetObserverCaptureMode(observer_.get(), capture_mode);
}

void TestNetLog::GetEntries(TestNetLogEntry::List* entry_list) const {
  observer_->GetEntries(entry_list);
}

void TestNetLog::GetEntriesForSource(NetLogSource source,
                                     TestNetLogEntry::List* entry_list) const {
  observer_->GetEntriesForSource(source, entry_list);
}

size_t TestNetLog::GetSize() const {
  return observer_->GetSize();
}

void TestNetLog::Clear() {
  observer_->Clear();
}

NetLog::ThreadSafeObserver* TestNetLog::GetObserver() const {
  return observer_.get();
}

BoundTestNetLog::BoundTestNetLog()
    : net_log_(NetLogWithSource::Make(&test_net_log_, NetLogSourceType::NONE)) {
}

BoundTestNetLog::~BoundTestNetLog() = default;

void BoundTestNetLog::GetEntries(TestNetLogEntry::List* entry_list) const {
  test_net_log_.GetEntries(entry_list);
}

void BoundTestNetLog::GetEntriesForSource(
    NetLogSource source,
    TestNetLogEntry::List* entry_list) const {
  test_net_log_.GetEntriesForSource(source, entry_list);
}

size_t BoundTestNetLog::GetSize() const {
  return test_net_log_.GetSize();
}

void BoundTestNetLog::Clear() {
  test_net_log_.Clear();
}

void BoundTestNetLog::SetCaptureMode(NetLogCaptureMode capture_mode) {
  test_net_log_.SetCaptureMode(capture_mode);
}

}  // namespace net
