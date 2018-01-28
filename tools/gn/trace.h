// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_TRACE_H_
#define TOOLS_GN_TRACE_H_

#include <string>

#include "base/macros.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"

class Label;

namespace base {
class CommandLine;
class FilePath;
}

class TraceItem {
 public:
  enum Type {
    TRACE_SETUP,
    TRACE_FILE_LOAD,
    TRACE_FILE_PARSE,
    TRACE_FILE_EXECUTE,
    TRACE_FILE_WRITE,
    TRACE_IMPORT_LOAD,
    TRACE_IMPORT_BLOCK,
    TRACE_SCRIPT_EXECUTE,
    TRACE_DEFINE_TARGET,
    TRACE_ON_RESOLVED,
    TRACE_CHECK_HEADER,   // One file.
    TRACE_CHECK_HEADERS,  // All files.
  };

  TraceItem(Type type,
            const std::string& name,
            base::PlatformThreadId thread_id);
  ~TraceItem();

  Type type() const { return type_; }
  const std::string& name() const { return name_; }
  base::PlatformThreadId thread_id() const { return thread_id_; }

  base::TimeTicks begin() const { return begin_; }
  void set_begin(base::TimeTicks b) { begin_ = b; }
  base::TimeTicks end() const { return end_; }
  void set_end(base::TimeTicks e) { end_ = e; }

  base::TimeDelta delta() const { return end_ - begin_; }

  // Optional toolchain label.
  const std::string& toolchain() const { return toolchain_; }
  void set_toolchain(const std::string& t) { toolchain_ = t; }

  // Optional command line.
  const std::string& cmdline() const { return cmdline_; }
  void set_cmdline(const std::string& c) { cmdline_ = c; }

 private:
  Type type_;
  std::string name_;
  base::PlatformThreadId thread_id_;

  base::TimeTicks begin_;
  base::TimeTicks end_;

  std::string toolchain_;
  std::string cmdline_;
};

class ScopedTrace {
 public:
  ScopedTrace(TraceItem::Type t, const std::string& name);
  ScopedTrace(TraceItem::Type t, const Label& label);
  ~ScopedTrace();

  void SetToolchain(const Label& label);
  void SetCommandLine(const base::CommandLine& cmdline);

  void Done();

 private:
  TraceItem* item_;
  bool done_;
};

// Call to turn tracing on. It's off by default.
void EnableTracing();

// Returns whether tracing is enabled.
bool TracingEnabled();

// Adds a trace event to the log. Takes ownership of the pointer.
void AddTrace(TraceItem* item);

// Returns a summary of the current traces, or the empty string if tracing is
// not enabled.
std::string SummarizeTraces();

// Saves the current traces to the given filename in JSON format.
void SaveTraces(const base::FilePath& file_name);

#endif  // TOOLS_GN_TRACE_H_
