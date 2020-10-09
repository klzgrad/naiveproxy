// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_TRACE_CONVERSION_HELPER_H_
#define BASE_TRACE_EVENT_TRACE_CONVERSION_HELPER_H_

#include <sstream>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/trace_event/traced_value.h"

namespace base {

namespace trace_event {

// Helpers for base::trace_event::ValueToString.
namespace internal {

// Return std::string representation given by |value|'s ostream operator<<.
template <typename ValueType>
std::string OstreamValueToString(const ValueType& value) {
  std::stringstream ss;
  ss << value;
  return ss.str();
}

// Helper class to mark call priority. Lower number has precedence:
//
// Example:
//   void foo(ValueToStringPriority<1>);
//   void foo(ValueToStringPriority<3>);
//   foo(ValueToStringPriority<0>());  // Calls |foo(ValueToStringPriority<1>)|.
template <int N>
class ValueToStringPriority : public ValueToStringPriority<N + 1> {};
template <>
// The number must be the same as in the fallback version of
// |ValueToStringHelper|.
class ValueToStringPriority<5> {};

// Use SFINAE to decide how to extract a string from the given parameter.

// Check if |value| can be used as a parameter of |base::NumberToString|. If
// std::string is not constructible from the returned value of
// |base::NumberToString| cause compilation error.
//
// |base::NumberToString| does not do locale specific formatting and should be
// faster than using |std::ostream::operator<<|.
template <typename ValueType>
decltype(base::NumberToString(std::declval<const ValueType>()), std::string())
ValueToStringHelper(ValueToStringPriority<0>,
                    const ValueType& value,
                    std::string /* unused */) {
  return base::NumberToString(value);
}

// If there is |ValueType::ToString| whose return value can be used to construct
// |std::string|, use this. Else use other methods.
template <typename ValueType>
decltype(std::string(std::declval<const ValueType>().ToString()))
ValueToStringHelper(ValueToStringPriority<1>,
                    const ValueType& value,
                    std::string /* unused */) {
  return value.ToString();
}

// If |std::ostream::operator<<| can be used, use it. Useful for |void*|.
template <typename ValueType>
decltype(
    std::declval<std::ostream>().operator<<(std::declval<const ValueType>()),
    std::string())
ValueToStringHelper(ValueToStringPriority<2>,
                    const ValueType& value,
                    std::string /* unused */) {
  return OstreamValueToString(value);
}

// Use |ValueType::operator<<| if applicable.
template <typename ValueType>
decltype(operator<<(std::declval<std::ostream&>(),
                    std::declval<const ValueType>()),
         std::string())
ValueToStringHelper(ValueToStringPriority<3>,
                    const ValueType& value,
                    std::string /* unused */) {
  return OstreamValueToString(value);
}

// If there is |ValueType::data| whose return value can be used to construct
// |std::string|, use it.
template <typename ValueType>
decltype(std::string(std::declval<const ValueType>().data()))
ValueToStringHelper(ValueToStringPriority<4>,
                    const ValueType& value,
                    std::string /* unused */) {
  return value.data();
}

// Fallback returns the |fallback_value|. Needs to have |ValueToStringPriority|
// with the highest number (to be called last).
template <typename ValueType>
std::string ValueToStringHelper(ValueToStringPriority<5>,
                                const ValueType& /* unused */,
                                std::string fallback_value) {
  return fallback_value;
}

}  // namespace internal

// The function to be used.
template <typename ValueType>
std::string ValueToString(const ValueType& value,
                          std::string fallback_value = "<value>") {
  return internal::ValueToStringHelper(internal::ValueToStringPriority<0>(),
                                       value, std::move(fallback_value));
}

// ToTracedValue helpers simplify using |AsValueInto| method to capture by
// eliminating the need to create TracedValue manually. Also supports passing
// pointers, including null ones.
template <typename T>
std::unique_ptr<TracedValue> ToTracedValue(T& value) {
  std::unique_ptr<TracedValue> result = std::make_unique<TracedValue>();
  // AsValueInto might not be const-only, so do not use const references.
  value.AsValueInto(result.get());
  return result;
}

template <typename T>
std::unique_ptr<TracedValue> ToTracedValue(T* value) {
  if (!value)
    return TracedValue::Build({{"this", "nullptr"}});
  return ToTracedValue(*value);
}

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_TRACE_CONVERSION_HELPER_H_
