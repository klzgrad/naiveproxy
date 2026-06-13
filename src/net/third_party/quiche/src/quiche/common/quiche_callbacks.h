// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// quiche_callbacks.h provides definitions for the callback types used by
// QUICHE.  Those aliases should be used instead of the function types provided
// by the standard library (std::function) or Abseil (absl::FunctionRef,
// absl::AnyInvocable).
//
// The aliases defined in this class are:
//   - quiche::UnretainedCallback
//   - quiche::SingleUseCallback
//   - quiche::MultiUseCallback
// Each is documented below near its definition.
//
// As a general principle, there are following ways of constructing a callback:
//   - Using a lambda expression (preferred)
//   - Using absl::bind_front
//   - Passing an already defined local function
//
// The following methods, however, should be avoided:
//   - std::bind (<https://abseil.io/tips/108>)
//   - Binding or taking a pointer to a function outside of the current module,
//     especially the methods provided by the C++ standard library
//     (use lambda instead; see go/totw/133 internally for motivation)

#ifndef QUICHE_COMMON_QUICHE_CALLBACKS_H_
#define QUICHE_COMMON_QUICHE_CALLBACKS_H_

#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

namespace callbacks_internal {
template <class Sig>
class QUICHE_EXPORT SignatureChanger {};

template <typename ReturnType, typename... Args>
class QUICHE_NO_EXPORT SignatureChanger<ReturnType(Args...)> {
 public:
  using Rvalue = ReturnType(Args...) &&;
  using Const = ReturnType(Args...) const;
};
}  // namespace callbacks_internal

// UnretainedCallback is the QUICHE alias for absl::FunctionRef.
//
// UnretainedCallback should be used when a function needs another function
// passed into it, but it will not retain any pointers to it long-term.
//
// For example, a QuicSession class may have function:
//   void DoForAllStreams(quiche::UnretainedCallback<void(QuicStream*)>);
//
// Then a method could call it like this:
//   int num_bidi_streams = 0;
//   DoForAllStreams([&num_bidi_streams](QuicStream* stream) {
//     if (stream->is_bidirectional()) {
//       ++num_bidi_streams;
//     }
//   });
//
// Note that similarly to absl::string_view, FunctionRef/UnretainedCallback does
// not own the underlying memory.  This means that the code below will not work:
//
//   quiche::UnretainedCallback f = [&i]() { ++i; }    // <- INVALID CODE
//
// The code above allocates a lambda object that stores a pointer to `i` in it,
// stores a reference to that object inside `f`, and then immediately frees the
// lambda object.  Attempting to compile the code above will fail with an error
// that says "Temporary whose address is used as value of local variable 'f'
// will be destroyed at the end of the full-expression".
template <class T>
using UnretainedCallback = absl::FunctionRef<T>;

// SingleUseCallback<T(...)> is the QUICHE alias for
// absl::AnyInvocable<T(...) &&>.
//
// SingleUseCallback is meant to be used for callbacks that may be called at
// most once.  For instance, a class may have a method that looks like this:
//
//   void SetOnSessionDestroyed(quiche::SingleUseCallback<void()> callback) {
//     on_session_destroyed_callback_ = std::move(callback);
//   }
//
// Then it can execute the callback like this:
//
//   ~Session() {
//     std::move(on_session_destroyed_callback_ )();
//   }
//
// Note that as with other types of similar nature, calling the callback after
// it has been moved is undefined behavior (it will result in an
// ABSL_HARDENING_ASSERT() call).
template <class T>
using SingleUseCallback = absl::AnyInvocable<
    typename callbacks_internal::SignatureChanger<T>::Rvalue>;

static_assert(std::is_same_v<SingleUseCallback<void(int, int &, int &&)>,
                             absl::AnyInvocable<void(int, int &, int &&) &&>>);

// MultiUseCallback<T(...)> is the QUICHE alias for
// absl::AnyInvocable<T(...) const>.
//
// MultiUseCallback is intended for situations where a callback may be invoked
// multiple times.  It is probably the closest equivalent to std::function
// in this file, notable differences being that MultiUseCallback is move-only
// and immutable (meaning that it cannot have an internal state that mutates; it
// can still point to things that are mutable).
template <class T>
using MultiUseCallback =
    absl::AnyInvocable<typename callbacks_internal::SignatureChanger<T>::Const>;

static_assert(
    std::is_same_v<MultiUseCallback<void()>, absl::AnyInvocable<void() const>>);

// Use cases that are intentionally not covered by this header file:
//
// (a) Mutable callbacks.
//
// In C++, it's possible for a lambda to mutate its own state, e.g.:
//
//   absl::AnyInvocable<void()> inc = [i = 0]() mutable {
//     std::cout << (i++) << std::endl;
//   };
//   inc();
//   inc();
//   inc();
//
// The code above will output numbers 0, 1, 2.  The fact that a callback can
// mutate its internal representation is counterintuitive, and thus not
// supported. Note that this limitation can be trivially worked around by
// passing a pointer (e.g., in the example below, `i = 0` could be replaced with
// `i = std::make_unique<int>(0)` to the similar effect).
//
// (b) Copyable callbacks.
//
// In C++, this would typically achieved by using std::function.  This file
// could contain an alias for std::function; it currently does not, since this
// use case is expected to be fairly rare.
//
// (c) noexpect support.
//
// We do not use C++ exceptions in QUICHE.

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_CALLBACKS_H_
