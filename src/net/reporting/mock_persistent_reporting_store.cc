// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/mock_persistent_reporting_store.h"

#include <algorithm>

namespace net {

MockPersistentReportingStore::Command::Command(
    Type type,
    ReportingClientsLoadedCallback loaded_callback)
    : type(type), loaded_callback(std::move(loaded_callback)) {}

MockPersistentReportingStore::Command::Command(
    Type type,
    const ReportingEndpoint& endpoint)
    : type(type), group_key(endpoint.group_key), url(endpoint.info.url) {}

MockPersistentReportingStore::Command::Command(
    Type type,
    const CachedReportingEndpointGroup& group)
    : type(type), group_key(group.group_key) {}

MockPersistentReportingStore::Command::Command(Type type) : type(type) {}

MockPersistentReportingStore::Command::Command(const Command& other)
    : type(other.type), group_key(other.group_key), url(other.url) {}

MockPersistentReportingStore::Command::Command(Command&& other) = default;

MockPersistentReportingStore::Command::~Command() = default;

bool operator==(const MockPersistentReportingStore::Command& lhs,
                const MockPersistentReportingStore::Command& rhs) {
  if (lhs.type != rhs.type)
    return false;
  bool equal = true;
  switch (lhs.type) {
    // For load and flush, just check the type.
    case MockPersistentReportingStore::Command::Type::LOAD_REPORTING_CLIENTS:
    case MockPersistentReportingStore::Command::Type::FLUSH:
      return true;
    // For endpoint operations, check the url and group key.
    case MockPersistentReportingStore::Command::Type::ADD_REPORTING_ENDPOINT:
    case MockPersistentReportingStore::Command::Type::
        UPDATE_REPORTING_ENDPOINT_DETAILS:
    case MockPersistentReportingStore::Command::Type::DELETE_REPORTING_ENDPOINT:
      equal &= (lhs.url == rhs.url);
      FALLTHROUGH;
    // For endpoint group operations, check the group key only.
    case MockPersistentReportingStore::Command::Type::
        ADD_REPORTING_ENDPOINT_GROUP:
    case MockPersistentReportingStore::Command::Type::
        UPDATE_REPORTING_ENDPOINT_GROUP_ACCESS_TIME:
    case MockPersistentReportingStore::Command::Type::
        UPDATE_REPORTING_ENDPOINT_GROUP_DETAILS:
    case MockPersistentReportingStore::Command::Type::
        DELETE_REPORTING_ENDPOINT_GROUP:
      equal &= (lhs.group_key == rhs.group_key);
  }
  return equal;
}

bool operator!=(const MockPersistentReportingStore::Command& lhs,
                const MockPersistentReportingStore::Command& rhs) {
  return !(lhs == rhs);
}

MockPersistentReportingStore::MockPersistentReportingStore()
    : load_started_(false),
      endpoint_count_(0),
      endpoint_group_count_(0),
      queued_endpoint_count_delta_(0),
      queued_endpoint_group_count_delta_(0) {}

MockPersistentReportingStore::~MockPersistentReportingStore() = default;

void MockPersistentReportingStore::LoadReportingClients(
    ReportingClientsLoadedCallback loaded_callback) {
  DCHECK(!load_started_);
  command_list_.emplace_back(Command::Type::LOAD_REPORTING_CLIENTS,
                             std::move(loaded_callback));
  load_started_ = true;
}

void MockPersistentReportingStore::AddReportingEndpoint(
    const ReportingEndpoint& endpoint) {
  DCHECK(load_started_);
  command_list_.emplace_back(Command::Type::ADD_REPORTING_ENDPOINT, endpoint);
  ++queued_endpoint_count_delta_;
}

void MockPersistentReportingStore::AddReportingEndpointGroup(
    const CachedReportingEndpointGroup& group) {
  DCHECK(load_started_);
  command_list_.emplace_back(Command::Type::ADD_REPORTING_ENDPOINT_GROUP,
                             group);
  ++queued_endpoint_group_count_delta_;
}

void MockPersistentReportingStore::UpdateReportingEndpointGroupAccessTime(
    const CachedReportingEndpointGroup& group) {
  DCHECK(load_started_);
  command_list_.emplace_back(
      Command::Type::UPDATE_REPORTING_ENDPOINT_GROUP_ACCESS_TIME, group);
}

void MockPersistentReportingStore::UpdateReportingEndpointDetails(
    const ReportingEndpoint& endpoint) {
  DCHECK(load_started_);
  command_list_.emplace_back(Command::Type::UPDATE_REPORTING_ENDPOINT_DETAILS,
                             endpoint);
}

void MockPersistentReportingStore::UpdateReportingEndpointGroupDetails(
    const CachedReportingEndpointGroup& group) {
  DCHECK(load_started_);
  command_list_.emplace_back(
      Command::Type::UPDATE_REPORTING_ENDPOINT_GROUP_DETAILS, group);
}

void MockPersistentReportingStore::DeleteReportingEndpoint(
    const ReportingEndpoint& endpoint) {
  DCHECK(load_started_);
  command_list_.emplace_back(Command::Type::DELETE_REPORTING_ENDPOINT,
                             endpoint);
  --queued_endpoint_count_delta_;
}

void MockPersistentReportingStore::DeleteReportingEndpointGroup(
    const CachedReportingEndpointGroup& group) {
  DCHECK(load_started_);
  command_list_.emplace_back(Command::Type::DELETE_REPORTING_ENDPOINT_GROUP,
                             group);
  --queued_endpoint_group_count_delta_;
}

void MockPersistentReportingStore::Flush() {
  // Can be called before |load_started_| is true, if the ReportingCache is
  // destroyed before getting a chance to load.
  command_list_.emplace_back(Command::Type::FLUSH);
  endpoint_count_ += queued_endpoint_count_delta_;
  queued_endpoint_count_delta_ = 0;
  endpoint_group_count_ += queued_endpoint_group_count_delta_;
  queued_endpoint_group_count_delta_ = 0;
}

void MockPersistentReportingStore::SetPrestoredClients(
    std::vector<ReportingEndpoint> endpoints,
    std::vector<CachedReportingEndpointGroup> groups) {
  DCHECK(!load_started_);
  DCHECK_EQ(0, endpoint_count_);
  DCHECK_EQ(0, endpoint_group_count_);
  endpoint_count_ += endpoints.size();
  prestored_endpoints_.swap(endpoints);
  endpoint_group_count_ += groups.size();
  prestored_endpoint_groups_.swap(groups);
}

void MockPersistentReportingStore::FinishLoading(bool load_success) {
  DCHECK(load_started_);
  for (size_t i = 0; i < command_list_.size(); ++i) {
    Command& command = command_list_[i];
    if (command.type == Command::Type::LOAD_REPORTING_CLIENTS) {
      // If load has been initiated, it should be the first operation.
      DCHECK_EQ(0u, i);
      DCHECK(!command.loaded_callback.is_null());
      if (load_success) {
        std::move(command.loaded_callback)
            .Run(std::move(prestored_endpoints_),
                 std::move(prestored_endpoint_groups_));
      } else {
        std::move(command.loaded_callback)
            .Run(std::vector<ReportingEndpoint>(),
                 std::vector<CachedReportingEndpointGroup>());
      }
    }
    if (i > 0) {
      // Load should not have been called twice.
      DCHECK(command.type != Command::Type::LOAD_REPORTING_CLIENTS);
    }
  }
}

bool MockPersistentReportingStore::VerifyCommands(
    const CommandList& expected_commands) const {
  return command_list_ == expected_commands;
}

int MockPersistentReportingStore::CountCommands(Command::Type t) {
  int c = 0;
  for (const auto& cmd : command_list_) {
    if (cmd.type == t)
      ++c;
  }
  return c;
}

MockPersistentReportingStore::CommandList
MockPersistentReportingStore::GetAllCommands() const {
  return command_list_;
}

}  // namespace net
