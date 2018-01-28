// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the Windows-specific exporting to ETW.
#ifndef BASE_TRACE_EVENT_TRACE_EVENT_ETW_EXPORT_WIN_H_
#define BASE_TRACE_EVENT_TRACE_EVENT_ETW_EXPORT_WIN_H_

#include <stdint.h>

#include <map>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/strings/string_piece.h"
#include "base/trace_event/trace_event_impl.h"

namespace base {

template <typename Type>
struct StaticMemorySingletonTraits;

namespace trace_event {

class BASE_EXPORT TraceEventETWExport {
 public:
  ~TraceEventETWExport();

  // Retrieves the singleton.
  // Note that this may return NULL post-AtExit processing.
  static TraceEventETWExport* GetInstance();

  // Enables/disables exporting of events to ETW. If disabled,
  // AddEvent and AddCustomEvent will simply return when called.
  static void EnableETWExport();
  static void DisableETWExport();

  // Returns true if ETW is enabled. For now, this is true if the command line
  // flag is specified.
  static bool IsETWExportEnabled();

  // Exports an event to ETW. This is mainly used in
  // TraceLog::AddTraceEventWithThreadIdAndTimestamp to export internal events.
  static void AddEvent(
      char phase,
      const unsigned char* category_group_enabled,
      const char* name,
      unsigned long long id,
      int num_args,
      const char* const* arg_names,
      const unsigned char* arg_types,
      const unsigned long long* arg_values,
      const std::unique_ptr<ConvertableToTraceFormat>* convertable_values);

  // Exports an ETW event that marks the end of a complete event.
  static void AddCompleteEndEvent(const char* name);

  // Returns true if any category in the group is enabled.
  static bool IsCategoryGroupEnabled(StringPiece category_group_name);

 private:
  // Ensure only the provider can construct us.
  friend struct StaticMemorySingletonTraits<TraceEventETWExport>;
  // To have access to UpdateKeyword().
  class ETWKeywordUpdateThread;
  TraceEventETWExport();

  // Updates the list of enabled categories by consulting the ETW keyword.
  // Returns true if there was a change, false otherwise.
  bool UpdateEnabledCategories();

  // Returns true if the category is enabled.
  bool IsCategoryEnabled(StringPiece category_name) const;

  // Called back by the update thread to check for potential changes to the
  // keyword.
  static void UpdateETWKeyword();

  // True if ETW is enabled. Allows hiding the exporting behind a flag.
  bool etw_export_enabled_;

  // Maps category names to their status (enabled/disabled).
  std::map<StringPiece, bool> categories_status_;

  // Local copy of the ETW keyword.
  uint64_t etw_match_any_keyword_;

  // Background thread that monitors changes to the ETW keyword and updates
  // the enabled categories when a change occurs.
  std::unique_ptr<ETWKeywordUpdateThread> keyword_update_thread_;
  PlatformThreadHandle keyword_update_thread_handle_;

  DISALLOW_COPY_AND_ASSIGN(TraceEventETWExport);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_TRACE_EVENT_ETW_EXPORT_WIN_H_
