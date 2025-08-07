// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_AUTO_SPANIFICATION_HELPER_H_
#define BASE_CONTAINERS_AUTO_SPANIFICATION_HELPER_H_

#include <array>

#include "base/containers/span.h"
#include "base/numerics/checked_math.h"

namespace base {

// SpanificationSizeofForStdArray was introduced temporarily in order to help
// the auto spanification tool (//tools/clang/spanify), and not meant to be
// used widely.
//
// Note that it's *not* guaranteed by the C++ standard that
//     sizeof(arr) == arr.size() * sizeof(arr[0])
// and it's possible that std::array has additional data and/or padding.
template <typename Element, size_t N>
constexpr size_t SpanificationSizeofForStdArray(const std::array<Element, N>&) {
  return sizeof(Element) * N;
}

// This helper is used to rewrite code that passes the address of a single
// variable or object member (e.g. `&my_var` or `&obj.member`) to a function
// that expects a `span` representing a single element.
//
// WARNING: This function should only be used by the auto-spanification tool.
// Do not use this helper outside of the tool.
template <typename T>
span<T, 1> SpanFromSingleElement(T& ref) {
  // This is a single element and the address is always valid as long as the
  // reference is valid.
  return UNSAFE_TODO(span<T, 1u>(&ref, 1u));
}

// Modifies the input span by removing its first element (if not empty)
// and returns the modified span.
// Used to rewrite pre-increment (++ptr).
// WARNING: This helper is intended to be used only by the auto spanification
// tool. Do not use this helper outside of the tool. Usage should usually be
// replaced with `base::span::(const_)iterator`.
template <typename T>
span<T> PreIncrementSpan(span<T>& span_ref) {
  static_assert(
      span<T>::extent == dynamic_extent,
      "PreIncrementSpan requires a dynamic-extent span (base::span<T>)");
  // An iterator that is at the end is expressed as an empty span and it shall
  // not be incremented.
  CHECK(!span_ref.empty());
  span_ref = span_ref.template subspan<1u>();
  return span_ref;
}

// Returns a copy of the input span *before* modification, and then
// modifies the input span by removing its first element (if not empty).
// Used to rewrite post-increment (ptr++).
// WARNING: This helper is intended to be used only by the auto spanification
// tool. Do not use this helper outside of the tool. Usage should usually be
// replaced with `base::span::(const_)iterator`.
template <typename T>
span<T> PostIncrementSpan(span<T>& span_ref) {
  static_assert(
      span<T>::extent == dynamic_extent,
      "PostIncrementSpan requires a dynamic-extent span (base::span<T>)");
  // An iterator that is at the end is expressed as an empty span and it shall
  // not be incremented.
  CHECK(!span_ref.empty());
  span<T> original_span = span_ref;
  span_ref = span_ref.template subspan<1u>();
  return original_span;
}

}  // namespace base

namespace base::spanification_internal {

// ToPointer is a helper function that converts either of a `T&` or a `T*` to a
// pointer `T*`.
//
// Example) Given the following two cases of spanification rewriting,
//     obj.method(arg...)  ==> MACRO(obj, arg...)
//     ptr->method(arg...) ==> MACRO(ptr, arg...)
// MACRO takes either of an optionally-const T& or T* value as the receiver
// object argument. ToPointer(obj) / ToPointer(ptr) converts them to a pointer
// type value. This helps avoiding implementing two versions of the macro.
//
// Note: This helper is intended to be used only in the following macros. Do not
// use this helper outside of them.

template <typename T>
inline const T* ToPointer(const T* value) {
  return value;
}

template <typename T>
inline T* ToPointer(T* value) {
  return value;
}

template <typename T>
inline const T* ToPointer(const T& value) {
  return &value;
}

template <typename T>
inline T* ToPointer(T& value) {
  return &value;
}

// If the value is a smart pointer type value, returns just the value.

template <typename T>
  requires requires(T t) { t.operator->(); }
inline const T& ToPointer(const T& value) {
  return value;
}

template <typename T>
  requires requires(T t) { t.operator->(); }
inline T& ToPointer(T& value) {
  return value;
}

}  // namespace base::spanification_internal

// The following helper macros are introduced temporarily in order to help the
// auto spanification tool (//tools/clang/spanify). The macros wrap third-party
// API calls which should return a base::span for safety but actually not. In
// the future, these macro calls should be replaced with new spanified APIs.
//
// The helper macros are macros because this header (in base/) cannot depend on
// non-base (especially third-party) libraries. The call sites must include
// necessary headers on their side.
//
// In the following macro definitions, a temporary lambda expression is used in
// order to not evaluate arguments multiple times. It also introduces a C++ code
// block where we can define temporary variables.

// https://source.chromium.org/chromium/chromium/src/+/main:third_party/skia/include/core/SkBitmap.h;drc=f72bd467feb15edd9323e46eab1b74ab6025bc5b;l=936
#define UNSAFE_SKBITMAP_GETADDR32(arg_self, arg_x, arg_y) \
  ([](auto&& self, int x, int y) {                        \
    uint32_t* row = self->getAddr32(x, y);                \
    ::base::CheckedNumeric<size_t> width = self->width(); \
    size_t size = (width - x).ValueOrDie();               \
    return UNSAFE_TODO(base::span<uint32_t>(row, size));  \
  }(::base::spanification_internal::ToPointer(arg_self), arg_x, arg_y))

// https://source.chromium.org/chromium/chromium/src/+/main:third_party/boringssl/src/include/openssl/pool.h;drc=c76e4f83a8c5786b463c3e55c070a21ac751b96b;l=81
#define UNSAFE_CRYPTO_BUFFER_DATA(arg_buf)                    \
  ([](const CRYPTO_BUFFER* buf) {                             \
    const uint8_t* data = CRYPTO_BUFFER_data(buf);            \
    size_t len = CRYPTO_BUFFER_len(buf);                      \
    return UNSAFE_TODO(base::span<const uint8_t>(data, len)); \
  }(arg_buf))

// https://source.chromium.org/chromium/chromium/src/+/main:third_party/harfbuzz-ng/src/src/hb-buffer.h;drc=ea6a172f84f2cbcfed803b5ae71064c7afb6b5c2;l=647
#define UNSAFE_HB_BUFFER_GET_GLYPH_INFOS(arg_buffer, arg_length)     \
  ([](hb_buffer_t* buffer, unsigned int* length) {                   \
    unsigned int len;                                                \
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &len); \
    if (length)                                                      \
      *length = len;                                                 \
    return UNSAFE_TODO(base::span<hb_glyph_info_t>(info, len));      \
  }(arg_buffer, arg_length))

// https://source.chromium.org/chromium/chromium/src/+/main:third_party/harfbuzz-ng/src/src/hb-buffer.h;drc=c76e4f83a8c5786b463c3e55c070a21ac751b96b;l=651
#define UNSAFE_HB_BUFFER_GET_GLYPH_POSITIONS(arg_buffer, arg_length)        \
  ([](hb_buffer_t* buffer, unsigned int* length) {                          \
    unsigned int len;                                                       \
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, &len); \
    if (length)                                                             \
      *length = len;                                                        \
    /* It's not clear whether the length is guaranteed to be 0 when !pos.   \
       Explicitly set the length to 0 just in case. */                      \
    if (!pos)                                                               \
      return UNSAFE_TODO(base::span<hb_glyph_position_t>(pos, 0u));         \
    return UNSAFE_TODO(base::span<hb_glyph_position_t>(pos, len));          \
  }(arg_buffer, arg_length))

// https://source.chromium.org/chromium/chromium/src/+/main:remoting/host/xsession_chooser_linux.cc;drc=fca90714b3949f0f4c27f26ef002fe8d33f3cb73;l=274
// https://web.mit.edu/barnowl/share/gtk-doc/html/glib/glib-Miscellaneous-Utility-Functions.html#g-get-system-data-dirs
#define UNSAFE_G_GET_SYSTEM_DATA_DIRS()                             \
  ([]() {                                                           \
    const gchar* const* dirs = g_get_system_data_dirs();            \
    size_t count = 0;                                               \
    while (UNSAFE_TODO(dirs[count]))                                \
      ++count;                                                      \
    /* It's okay to access the null-terminator at the end. */       \
    size_t size = count + 1;                                        \
    return UNSAFE_TODO(base::span<const gchar* const>(dirs, size)); \
  }())

#endif  // BASE_CONTAINERS_AUTO_SPANIFICATION_HELPER_H_
