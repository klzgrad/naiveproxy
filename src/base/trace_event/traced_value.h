// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_TRACED_VALUE_H_
#define BASE_TRACE_EVENT_TRACED_VALUE_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/pickle.h"
#include "base/strings/string_piece.h"
#include "base/trace_event/trace_event_impl.h"

namespace base {

class Value;

namespace trace_event {

class BASE_EXPORT TracedValue : public ConvertableToTraceFormat {
 public:
  TracedValue();
  explicit TracedValue(size_t capacity, bool force_json = false);
  ~TracedValue() override;

  void EndDictionary();
  void EndArray();

  // These methods assume that |name| is a long lived "quoted" string.
  void SetInteger(const char* name, int value);
  void SetDouble(const char* name, double value);
  void SetBoolean(const char* name, bool value);
  void SetString(const char* name, base::StringPiece value);
  void SetValue(const char* name, TracedValue* value);
  void BeginDictionary(const char* name);
  void BeginArray(const char* name);

  // These, instead, can be safely passed a temporary string.
  void SetIntegerWithCopiedName(base::StringPiece name, int value);
  void SetDoubleWithCopiedName(base::StringPiece name, double value);
  void SetBooleanWithCopiedName(base::StringPiece name, bool value);
  void SetStringWithCopiedName(base::StringPiece name, base::StringPiece value);
  void SetValueWithCopiedName(base::StringPiece name, TracedValue* value);
  void BeginDictionaryWithCopiedName(base::StringPiece name);
  void BeginArrayWithCopiedName(base::StringPiece name);

  void AppendInteger(int);
  void AppendDouble(double);
  void AppendBoolean(bool);
  void AppendString(base::StringPiece);
  void BeginArray();
  void BeginDictionary();

  // ConvertableToTraceFormat implementation.
  void AppendAsTraceFormat(std::string* out) const override;
  bool AppendToProto(ProtoAppender* appender) override;

  void EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead) override;

  // A custom serialization class can be supplied by implementing the
  // Writer interface and supplying a factory class to SetWriterFactoryCallback.
  // Primarily used by Perfetto to write TracedValues directly into its proto
  // format, which lets us do a direct memcpy() in AppendToProto() rather than
  // a JSON serialization step in AppendAsTraceFormat.
  class BASE_EXPORT Writer {
   public:
    virtual ~Writer() = default;

    virtual void BeginArray() = 0;
    virtual void BeginDictionary() = 0;
    virtual void EndDictionary() = 0;
    virtual void EndArray() = 0;

    // These methods assume that |name| is a long lived "quoted" string.
    virtual void SetInteger(const char* name, int value) = 0;
    virtual void SetDouble(const char* name, double value) = 0;
    virtual void SetBoolean(const char* name, bool value) = 0;
    virtual void SetString(const char* name, base::StringPiece value) = 0;
    virtual void SetValue(const char* name, Writer* value) = 0;
    virtual void BeginDictionary(const char* name) = 0;
    virtual void BeginArray(const char* name) = 0;

    // These, instead, can be safely passed a temporary string.
    virtual void SetIntegerWithCopiedName(base::StringPiece name,
                                          int value) = 0;
    virtual void SetDoubleWithCopiedName(base::StringPiece name,
                                         double value) = 0;
    virtual void SetBooleanWithCopiedName(base::StringPiece name,
                                          bool value) = 0;
    virtual void SetStringWithCopiedName(base::StringPiece name,
                                         base::StringPiece value) = 0;
    virtual void SetValueWithCopiedName(base::StringPiece name,
                                        Writer* value) = 0;
    virtual void BeginDictionaryWithCopiedName(base::StringPiece name) = 0;
    virtual void BeginArrayWithCopiedName(base::StringPiece name) = 0;

    virtual void AppendInteger(int) = 0;
    virtual void AppendDouble(double) = 0;
    virtual void AppendBoolean(bool) = 0;
    virtual void AppendString(base::StringPiece) = 0;

    virtual void AppendAsTraceFormat(std::string* out) const = 0;

    virtual bool AppendToProto(ProtoAppender* appender);

    virtual void EstimateTraceMemoryOverhead(
        TraceEventMemoryOverhead* overhead) = 0;

    virtual std::unique_ptr<base::Value> ToBaseValue() const = 0;

    virtual bool IsPickleWriter() const = 0;
    virtual bool IsProtoWriter() const = 0;
  };

  typedef std::unique_ptr<Writer> (*WriterFactoryCallback)(size_t capacity);
  static void SetWriterFactoryCallback(WriterFactoryCallback callback);

  // Public for tests only.
  std::unique_ptr<base::Value> ToBaseValue() const;

 private:
  std::unique_ptr<Writer> writer_;

#ifndef NDEBUG
  // In debug builds checks the pairings of {Start,End}{Dictionary,Array}
  std::vector<bool> nesting_stack_;
#endif

  DISALLOW_COPY_AND_ASSIGN(TracedValue);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_TRACED_VALUE_H_
