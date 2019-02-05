// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides support for basic in-memory tracing of short events. We
// keep a static circular buffer where we store the last traced events, so we
// can review the cache recent behavior should we need it.

#ifndef NET_DISK_CACHE_BLOCKFILE_TRACE_H_
#define NET_DISK_CACHE_BLOCKFILE_TRACE_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"

namespace disk_cache {

// Create and destroy the tracing buffer.
void InitTrace(void);
void DestroyTrace(void);

// Simple class to handle the trace buffer lifetime. Any object interested in
// tracing should keep a reference to the object returned by GetTraceObject().
class TraceObject : public base::RefCountedThreadSafe<TraceObject> {
  friend class base::RefCountedThreadSafe<TraceObject>;

 public:
  static TraceObject* GetTraceObject();
  void EnableTracing(bool enable);

 private:
  TraceObject();
  ~TraceObject();
  DISALLOW_COPY_AND_ASSIGN(TraceObject);
};

// Traces to the internal buffer.
NET_EXPORT_PRIVATE void Trace(const char* format, ...);

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_BLOCKFILE_TRACE_H_
