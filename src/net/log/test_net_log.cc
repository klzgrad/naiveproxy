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

TestNetLog::TestNetLog() : NetLog(util::PassKey<TestNetLog>()) {}
TestNetLog::~TestNetLog() = default;

RecordingTestNetLog::RecordingTestNetLog() {
  AddObserver(this, NetLogCaptureMode::kIncludeSensitive);
}

RecordingTestNetLog::~RecordingTestNetLog() {
  RemoveObserver(this);
}

std::vector<NetLogEntry> RecordingTestNetLog::GetEntries() const {
  base::AutoLock lock(lock_);
  std::vector<NetLogEntry> result;
  for (const auto& entry : entry_list_)
    result.push_back(entry.Clone());
  return result;
}

std::vector<NetLogEntry> RecordingTestNetLog::GetEntriesForSource(
    NetLogSource source) const {
  base::AutoLock lock(lock_);
  std::vector<NetLogEntry> result;
  for (const auto& entry : entry_list_) {
    if (entry.source.id == source.id)
      result.push_back(entry.Clone());
  }
  return result;
}

std::vector<NetLogEntry> RecordingTestNetLog::GetEntriesWithType(
    NetLogEventType type) const {
  base::AutoLock lock(lock_);
  std::vector<NetLogEntry> result;
  for (const auto& entry : entry_list_) {
    if (entry.type == type)
      result.push_back(entry.Clone());
  }
  return result;
}

size_t RecordingTestNetLog::GetSize() const {
  base::AutoLock lock(lock_);
  return entry_list_.size();
}

void RecordingTestNetLog::Clear() {
  base::AutoLock lock(lock_);
  entry_list_.clear();
}

void RecordingTestNetLog::OnAddEntry(const NetLogEntry& entry) {
  base::Value params = entry.params.Clone();
  auto time = base::TimeTicks::Now();

  // Only need to acquire the lock when accessing class variables.
  base::AutoLock lock(lock_);
  entry_list_.emplace_back(entry.type, entry.source, entry.phase, time,
                           std::move(params));
}

NetLog::ThreadSafeObserver* RecordingTestNetLog::GetObserver() {
  return this;
}

void RecordingTestNetLog::SetObserverCaptureMode(
    NetLogCaptureMode capture_mode) {
  RemoveObserver(this);
  AddObserver(this, capture_mode);
}

RecordingBoundTestNetLog::RecordingBoundTestNetLog()
    : net_log_(NetLogWithSource::Make(&test_net_log_, NetLogSourceType::NONE)) {
}

RecordingBoundTestNetLog::~RecordingBoundTestNetLog() = default;

std::vector<NetLogEntry> RecordingBoundTestNetLog::GetEntries() const {
  return test_net_log_.GetEntries();
}

std::vector<NetLogEntry> RecordingBoundTestNetLog::GetEntriesForSource(
    NetLogSource source) const {
  return test_net_log_.GetEntriesForSource(source);
}

std::vector<NetLogEntry> RecordingBoundTestNetLog::GetEntriesWithType(
    NetLogEventType type) const {
  return test_net_log_.GetEntriesWithType(type);
}

size_t RecordingBoundTestNetLog::GetSize() const {
  return test_net_log_.GetSize();
}

void RecordingBoundTestNetLog::Clear() {
  test_net_log_.Clear();
}

void RecordingBoundTestNetLog::SetObserverCaptureMode(
    NetLogCaptureMode capture_mode) {
  test_net_log_.SetObserverCaptureMode(capture_mode);
}

}  // namespace net
