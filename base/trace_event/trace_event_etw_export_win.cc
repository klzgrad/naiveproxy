// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/trace_event_etw_export_win.h"

#include <stddef.h>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_impl.h"

#include <windows.h>

// The GetProcAddress technique is borrowed from
// https://github.com/google/UIforETW/tree/master/ETWProviders
//
// EVNTAPI is used in evntprov.h which is included by chrome_events_win.h.
// We define EVNTAPI without the DECLSPEC_IMPORT specifier so that we can
// implement these functions locally instead of using the import library, and
// can therefore still run on Windows XP.
#define EVNTAPI __stdcall
// Include the event register/write/unregister macros compiled from the manifest
// file. Note that this includes evntprov.h which requires a Vista+ Windows SDK.
//
// In SHARED_INTERMEDIATE_DIR.
#include "base/trace_event/etw_manifest/chrome_events_win.h"  // NOLINT

namespace {
// |kFilteredEventGroupNames| contains the event categories that can be
// exported individually. These categories can be enabled by passing the correct
// keyword when starting the trace. A keyword is a 64-bit flag and we attribute
// one bit per category. We can therefore enable a particular category by
// setting its corresponding bit in the keyword. For events that are not present
// in |kFilteredEventGroupNames|, we have two bits that control their
// behaviour. When bit 61 is enabled, any event that is not disabled by default
// (ie. doesn't start with disabled-by-default-) will be exported. Likewise,
// when bit 62 is enabled, any event that is disabled by default will be
// exported.
//
// Note that bit 63 (MSB) must always be set, otherwise tracing will be disabled
// by ETW. Therefore, the keyword will always be greater than
// 0x8000000000000000.
//
// Examples of passing keywords to the provider using xperf:
// # This exports "benchmark" and "cc" events
// xperf -start chrome -on Chrome:0x8000000000000009
//
// # This exports "gpu", "netlog" and all other events that are not disabled by
// # default
// xperf -start chrome -on Chrome:0xA0000000000000A0
//
// More info about starting a trace and keyword can be obtained by using the
// help section of xperf (xperf -help start). Note that xperf documentation
// refers to keywords as flags and there are two ways to enable them, using
// group names or the hex representation. We only support the latter. Also, we
// ignore the level.
const char* const kFilteredEventGroupNames[] = {
    "benchmark",                                        // 0x1
    "blink",                                            // 0x2
    "browser",                                          // 0x4
    "cc",                                               // 0x8
    "evdev",                                            // 0x10
    "gpu",                                              // 0x20
    "input",                                            // 0x40
    "netlog",                                           // 0x80
    "sequence_manager",                                 // 0x100
    "toplevel",                                         // 0x200
    "v8",                                               // 0x400
    "disabled-by-default-cc.debug",                     // 0x800
    "disabled-by-default-cc.debug.picture",             // 0x1000
    "disabled-by-default-toplevel.flow",                // 0x2000
    "startup",                                          // 0x4000
    "latency"};                                         // 0x8000
const char kOtherEventsGroupName[] = "__OTHER_EVENTS";  // 0x2000000000000000
const char kDisabledOtherEventsGroupName[] =
    "__DISABLED_OTHER_EVENTS";  // 0x4000000000000000
const uint64_t kOtherEventsKeywordBit = 1ULL << 61;
const uint64_t kDisabledOtherEventsKeywordBit = 1ULL << 62;
const size_t kNumberOfCategories = ARRAYSIZE(kFilteredEventGroupNames) + 2U;

}  // namespace

namespace base {
namespace trace_event {

// This object will be created by each process. It's a background (low-priority)
// thread that will monitor the ETW keyword for any changes.
class TraceEventETWExport::ETWKeywordUpdateThread
    : public PlatformThread::Delegate {
 public:
  ETWKeywordUpdateThread() {}
  ~ETWKeywordUpdateThread() override {}

  // Implementation of PlatformThread::Delegate:
  void ThreadMain() override {
    PlatformThread::SetName("ETW Keyword Update Thread");
    TimeDelta sleep_time = TimeDelta::FromMilliseconds(kUpdateTimerDelayMs);
    while (1) {
      PlatformThread::Sleep(sleep_time);
      trace_event::TraceEventETWExport::UpdateETWKeyword();
    }
  }

 private:
  // Time between checks for ETW keyword changes (in milliseconds).
  unsigned int kUpdateTimerDelayMs = 1000;
};


TraceEventETWExport::TraceEventETWExport()
    : etw_export_enabled_(false), etw_match_any_keyword_(0ULL) {
  // Register the ETW provider. If registration fails then the event logging
  // calls will fail.
  EventRegisterChrome();

  // Make sure to initialize the map with all the group names. Subsequent
  // modifications will be made by the background thread and only affect the
  // values of the keys (no key addition/deletion). Therefore, the map does not
  // require a lock for access.
  for (size_t i = 0; i < ARRAYSIZE(kFilteredEventGroupNames); i++)
    categories_status_[kFilteredEventGroupNames[i]] = false;
  categories_status_[kOtherEventsGroupName] = false;
  categories_status_[kDisabledOtherEventsGroupName] = false;
  DCHECK_EQ(kNumberOfCategories, categories_status_.size());
}

TraceEventETWExport::~TraceEventETWExport() {
  EventUnregisterChrome();
}

// static
TraceEventETWExport* TraceEventETWExport::GetInstance() {
  return Singleton<TraceEventETWExport,
                   StaticMemorySingletonTraits<TraceEventETWExport>>::get();
}

// static
void TraceEventETWExport::EnableETWExport() {
  auto* instance = GetInstance();
  if (instance && !instance->etw_export_enabled_) {
    instance->etw_export_enabled_ = true;
    // Sync the enabled categories with ETW by calling UpdateEnabledCategories()
    // that checks the keyword. Then create a thread that will call that same
    // function periodically, to make sure we stay in sync.
    instance->UpdateEnabledCategories();
    if (instance->keyword_update_thread_handle_.is_null()) {
      instance->keyword_update_thread_.reset(new ETWKeywordUpdateThread);
      PlatformThread::CreateWithPriority(
          0, instance->keyword_update_thread_.get(),
          &instance->keyword_update_thread_handle_, ThreadPriority::BACKGROUND);
    }
  }
}

// static
void TraceEventETWExport::DisableETWExport() {
  auto* instance = GetInstance();
  if (instance && instance->etw_export_enabled_)
    instance->etw_export_enabled_ = false;
}

// static
bool TraceEventETWExport::IsETWExportEnabled() {
  auto* instance = GetInstance();
  return (instance && instance->etw_export_enabled_);
}

// static
void TraceEventETWExport::AddEvent(
    char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned long long id,
    int num_args,
    const char* const* arg_names,
    const unsigned char* arg_types,
    const unsigned long long* arg_values,
    const std::unique_ptr<ConvertableToTraceFormat>* convertable_values) {
  // We bail early in case exporting is disabled or no consumer is listening.
  auto* instance = GetInstance();
  if (!instance || !instance->etw_export_enabled_ || !EventEnabledChromeEvent())
    return;

  const char* phase_string = nullptr;
  // Space to store the phase identifier and null-terminator, when needed.
  char phase_buffer[2];
  switch (phase) {
    case TRACE_EVENT_PHASE_BEGIN:
      phase_string = "Begin";
      break;
    case TRACE_EVENT_PHASE_END:
      phase_string = "End";
      break;
    case TRACE_EVENT_PHASE_COMPLETE:
      phase_string = "Complete";
      break;
    case TRACE_EVENT_PHASE_INSTANT:
      phase_string = "Instant";
      break;
    case TRACE_EVENT_PHASE_ASYNC_BEGIN:
      phase_string = "Async Begin";
      break;
    case TRACE_EVENT_PHASE_ASYNC_STEP_INTO:
      phase_string = "Async Step Into";
      break;
    case TRACE_EVENT_PHASE_ASYNC_STEP_PAST:
      phase_string = "Async Step Past";
      break;
    case TRACE_EVENT_PHASE_ASYNC_END:
      phase_string = "Async End";
      break;
    case TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN:
      phase_string = "Nestable Async Begin";
      break;
    case TRACE_EVENT_PHASE_NESTABLE_ASYNC_END:
      phase_string = "Nestable Async End";
      break;
    case TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT:
      phase_string = "Nestable Async Instant";
      break;
    case TRACE_EVENT_PHASE_FLOW_BEGIN:
      phase_string = "Phase Flow Begin";
      break;
    case TRACE_EVENT_PHASE_FLOW_STEP:
      phase_string = "Phase Flow Step";
      break;
    case TRACE_EVENT_PHASE_FLOW_END:
      phase_string = "Phase Flow End";
      break;
    case TRACE_EVENT_PHASE_METADATA:
      phase_string = "Phase Metadata";
      break;
    case TRACE_EVENT_PHASE_COUNTER:
      phase_string = "Phase Counter";
      break;
    case TRACE_EVENT_PHASE_SAMPLE:
      phase_string = "Phase Sample";
      break;
    case TRACE_EVENT_PHASE_CREATE_OBJECT:
      phase_string = "Phase Create Object";
      break;
    case TRACE_EVENT_PHASE_SNAPSHOT_OBJECT:
      phase_string = "Phase Snapshot Object";
      break;
    case TRACE_EVENT_PHASE_DELETE_OBJECT:
      phase_string = "Phase Delete Object";
      break;
    default:
      phase_buffer[0] = phase;
      phase_buffer[1] = 0;
      phase_string = phase_buffer;
      break;
  }

  std::string arg_values_string[3];
  for (int i = 0; i < num_args; i++) {
    if (arg_types[i] == TRACE_VALUE_TYPE_CONVERTABLE) {
      // Temporarily do nothing here. This function consumes 1/3 to 1/2 of
      // *total* process CPU time when ETW tracing, and many of the strings
      // created exceed WPA's 4094 byte limit and are shown as:
      // "Unable to parse data". See crbug.com/488257
      // convertable_values[i]->AppendAsTraceFormat(arg_values_string + i);
    } else {
      TraceEvent::TraceValue trace_event;
      trace_event.as_uint = arg_values[i];
      TraceEvent::AppendValueAsJSON(arg_types[i], trace_event,
                                    arg_values_string + i);
    }
  }

  EventWriteChromeEvent(
      name, phase_string, num_args > 0 ? arg_names[0] : "",
      arg_values_string[0].c_str(), num_args > 1 ? arg_names[1] : "",
      arg_values_string[1].c_str(), num_args > 2 ? arg_names[2] : "",
      arg_values_string[2].c_str());
}

// static
void TraceEventETWExport::AddCompleteEndEvent(const char* name) {
  auto* instance = GetInstance();
  if (!instance || !instance->etw_export_enabled_ || !EventEnabledChromeEvent())
    return;

  EventWriteChromeEvent(name, "Complete End", "", "", "", "", "", "");
}

// static
bool TraceEventETWExport::IsCategoryGroupEnabled(
    StringPiece category_group_name) {
  DCHECK(!category_group_name.empty());
  auto* instance = GetInstance();
  if (instance == nullptr)
    return false;

  if (!instance->IsETWExportEnabled())
    return false;

  CStringTokenizer category_group_tokens(category_group_name.begin(),
                                         category_group_name.end(), ",");
  while (category_group_tokens.GetNext()) {
    StringPiece category_group_token = category_group_tokens.token_piece();
    if (instance->IsCategoryEnabled(category_group_token)) {
      return true;
    }
  }
  return false;
}

bool TraceEventETWExport::UpdateEnabledCategories() {
  if (etw_match_any_keyword_ == CHROME_Context.MatchAnyKeyword)
    return false;

  // If the keyword has changed, update each category.
  // Chrome_Context.MatchAnyKeyword is set by UIforETW (or other ETW trace
  // recording tools) using the ETW infrastructure. This value will be set in
  // all Chrome processes that have registered their ETW provider.
  etw_match_any_keyword_ = CHROME_Context.MatchAnyKeyword;
  for (size_t i = 0; i < ARRAYSIZE(kFilteredEventGroupNames); i++) {
    if (etw_match_any_keyword_ & (1ULL << i)) {
      categories_status_[kFilteredEventGroupNames[i]] = true;
    } else {
      categories_status_[kFilteredEventGroupNames[i]] = false;
    }
  }

  // Also update the two default categories.
  if (etw_match_any_keyword_ & kOtherEventsKeywordBit) {
    categories_status_[kOtherEventsGroupName] = true;
  } else {
    categories_status_[kOtherEventsGroupName] = false;
  }
  if (etw_match_any_keyword_ & kDisabledOtherEventsKeywordBit) {
    categories_status_[kDisabledOtherEventsGroupName] = true;
  } else {
    categories_status_[kDisabledOtherEventsGroupName] = false;
  }

  DCHECK_EQ(kNumberOfCategories, categories_status_.size());

  // Update the categories in TraceLog.
  TraceLog::GetInstance()->UpdateETWCategoryGroupEnabledFlags();

  return true;
}

bool TraceEventETWExport::IsCategoryEnabled(StringPiece category_name) const {
  DCHECK_EQ(kNumberOfCategories, categories_status_.size());
  // Try to find the category and return its status if found
  auto it = categories_status_.find(category_name);
  if (it != categories_status_.end())
    return it->second;

  // Otherwise return the corresponding default status by first checking if the
  // category is disabled by default.
  if (category_name.starts_with("disabled-by-default")) {
    DCHECK(categories_status_.find(kDisabledOtherEventsGroupName) !=
           categories_status_.end());
    return categories_status_.find(kDisabledOtherEventsGroupName)->second;
  } else {
    DCHECK(categories_status_.find(kOtherEventsGroupName) !=
           categories_status_.end());
    return categories_status_.find(kOtherEventsGroupName)->second;
  }
}

// static
void TraceEventETWExport::UpdateETWKeyword() {
  if (!IsETWExportEnabled())
    return;
  auto* instance = GetInstance();
  DCHECK(instance);
  instance->UpdateEnabledCategories();
}
}  // namespace trace_event
}  // namespace base
