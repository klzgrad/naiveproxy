/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_PROTOVM_ERROR_HANDLING_H_
#define SRC_PROTOVM_ERROR_HANDLING_H_

#include <cstdarg>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"

#if defined(__GNUC__) || defined(__clang__)
// Ignore GCC warning about a missing argument for a variadic macro parameter.
#pragma GCC system_header
#endif

namespace perfetto {
namespace protovm {

using Stacktrace = std::vector<std::string>;

void LogStacktraceMessage(Stacktrace& stacktrace,
                          const char* file_name,
                          int file_line,
                          const char* fmt,
                          ...) PERFETTO_PRINTF_FORMAT(4, 5);

// overload for calls without message args
void LogStacktraceMessage(Stacktrace& stacktrace,
                          const char* file_name,
                          int file_line);

#define PROTOVM_ABORT(fmt, ...)                                               \
  do {                                                                        \
    auto status_abort = StatusOr<void>::Abort();                              \
    LogStacktraceMessage(status_abort.stacktrace(),                           \
                         ::perfetto::base::Basename(__FILE__), __LINE__, fmt, \
                         ##__VA_ARGS__);                                      \
    return status_abort;                                                      \
  } while (0)

#define PROTOVM_RETURN(s, ...)                                             \
  do {                                                                     \
    if (s.IsAbort()) {                                                     \
      LogStacktraceMessage(s.stacktrace(),                                 \
                           ::perfetto::base::Basename(__FILE__), __LINE__, \
                           ##__VA_ARGS__);                                 \
    }                                                                      \
    return s;                                                              \
  } while (0)

#define PROTOVM_RETURN_IF_NOT_OK(s, ...)                                   \
  do {                                                                     \
    if (s.IsAbort()) {                                                     \
      LogStacktraceMessage(s.stacktrace(),                                 \
                           ::perfetto::base::Basename(__FILE__), __LINE__, \
                           ##__VA_ARGS__);                                 \
      return s;                                                            \
    }                                                                      \
    if (!s.IsOk()) {                                                       \
      return s;                                                            \
    }                                                                      \
  } while (0)

enum class Status : uint8_t {
  kOk,
  kError,

  // It causes an abort of the whole VM's program. Typically indicates a
  // fundamental issue with the program and requires developer's intervention.
  kAbort,
};

namespace internal {

template <class T>
constexpr bool is_empty_class_v = std::is_class_v<T> && std::is_empty_v<T>;

template <class T, class Arg, class... Args>
static constexpr bool is_copy_or_move_ctor_arg_v =
    sizeof...(Args) == 0 && std::is_same_v<T, std::decay_t<Arg>>;

template <class T>
inline constexpr std::size_t sizeof_v = sizeof(T);

template <>
inline constexpr std::size_t sizeof_v<void> = 0;

template <class T>
inline constexpr std::size_t alignof_v = alignof(T);

template <>
inline constexpr std::size_t alignof_v<void> = 0;

}  // namespace internal

template <class T>
class StatusOr {
 public:
  // Factory methods to create empty instances (without value)
  static StatusOr Ok() { return StatusOr{Status::kOk}; }
  static StatusOr Error() { return StatusOr{Status::kError}; }
  static StatusOr Abort() { return StatusOr{Status::kAbort}; }

  StatusOr(const StatusOr&) = delete;

  StatusOr(StatusOr&& other) : status_{other.status_} {
    if (IsOk()) {
      if constexpr (!std::is_same_v<T, void>) {
        new (storage_) T(std::move(other.value()));
      }
    } else if (IsAbort()) {
      new (storage_) std::unique_ptr<Stacktrace>(
          std::move(other.GetStacktraceUniquePtr()));
    }
  }

  // Intentionally implicit to allow idiomatic usage (return plain value to be
  // implicitly converted to StatusOr)
  template <
      class... Args,
      // sfinae to avoid clashing with copy/move constructors
      std::enable_if_t<!internal::is_copy_or_move_ctor_arg_v<StatusOr, Args...>,
                       int> = 0>
  StatusOr(Args&&... args) : status_{Status::kOk} {
    if constexpr (!std::is_same_v<T, void>) {
      new (storage_) T(std::forward<Args>(args)...);
    }
  }

  // Intentionally implicit to allow idiomatic usage (e.g. return StatusOr<A> to
  // be implicitely converted to StatusOr<B>).
  // When status is ok, the conversion is only allowed if B = void, otherwise it
  // would require a conversion from A to non-void B.
  template <class U, std::enable_if_t<!std::is_same_v<U, T>, int> = 0>
  StatusOr(StatusOr<U>&& other) : status_{other.status_} {
    if (IsOk()) {
      PERFETTO_DCHECK((std::is_same_v<T, void>));
    } else if (IsAbort()) {
      new (storage_) std::unique_ptr<Stacktrace>(
          std::move(other.GetStacktraceUniquePtr()));
    }
  }

  ~StatusOr() {
    if (IsOk()) {
      if constexpr (!std::is_same_v<T, void>) {
        value().~T();
      }
    }
    if (IsAbort()) {
      GetStacktraceUniquePtr().~unique_ptr();
    }
  }

  bool IsOk() const { return status_ == Status::kOk; }
  bool IsError() const { return status_ == Status::kError; }
  bool IsAbort() const { return status_ == Status::kAbort; }

  template <class U = T>
  std::enable_if_t<!std::is_same_v<U, void>, U&> value() {
    PERFETTO_DCHECK(IsOk());
    return *std::launder(reinterpret_cast<T*>(storage_));
  }

  template <class U = T>
  std::enable_if_t<!std::is_same_v<U, void>, const U&> value() const {
    return const_cast<StatusOr*>(this)->value();
  }

  template <class U = T>
  std::enable_if_t<!std::is_same_v<U, void>, U&> operator*() {
    return value();
  }

  template <class U = T>
  std::enable_if_t<!std::is_same_v<U, void>, const U&> operator*() const {
    return value();
  }

  template <class U = T>
  std::enable_if_t<!std::is_same_v<U, void>, U*> operator->() {
    return &value();
  }

  template <class U = T>
  std::enable_if_t<!std::is_same_v<U, void>, const U*> operator->() const {
    return &value();
  }

  Stacktrace& stacktrace() {
    PERFETTO_DCHECK(IsAbort());
    return **std::launder(
        reinterpret_cast<std::unique_ptr<Stacktrace>*>(storage_));
  }

 private:
  template <class U>
  friend class StatusOr;

  // Intentionally private to encourage the use of factory methods, instead of
  // plain status values. E.g. StatusOr::Abort() instead of Status::kAbort.
  // Plain status value don't include stacktrace information in case of an
  // abort.
  explicit StatusOr(Status status) : status_{status} {
    PERFETTO_DCHECK((std::is_same_v<T, void>) || !IsOk());
    if (IsAbort()) {
      new (storage_) std::unique_ptr<Stacktrace>{new Stacktrace{}};
    }
  }

  std::unique_ptr<Stacktrace>& GetStacktraceUniquePtr() {
    PERFETTO_DCHECK(IsAbort());
    return *std::launder(
        reinterpret_cast<std::unique_ptr<Stacktrace>*>(storage_));
  }

  Status status_;

  static constexpr size_t kStorageAlign =
      std::max(internal::alignof_v<T>, alignof(std::unique_ptr<Stacktrace>));
  static constexpr size_t kStorageSize =
      std::max(internal::sizeof_v<T>, sizeof(std::unique_ptr<Stacktrace>));
  alignas(kStorageAlign) unsigned char storage_[kStorageSize];
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_ERROR_HANDLING_H_
