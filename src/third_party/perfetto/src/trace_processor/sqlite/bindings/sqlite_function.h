/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_BINDINGS_SQLITE_FUNCTION_H_
#define SRC_TRACE_PROCESSOR_SQLITE_BINDINGS_SQLITE_FUNCTION_H_

#include <sqlite3.h>  // IWYU pragma: export
#include <memory>

namespace perfetto::trace_processor::sqlite {

// Prototype for a function which can be registered with SQLite.
//
// See https://www.sqlite.org/c3ref/create_function.html for details on how
// to implement the methods of this class.
template <typename Impl>
class Function {
 public:
  // The type of the context object which will be passed to the function.
  // Can be redefined in any sub-classes to override the context.
  using UserData = void;

  // The type of the auxilary object which can be read from and written to
  // by the function.
  using AuxData = void;

  // The xStep function which will be executed by SQLite to add a row of values
  // to the current window.
  //
  // Implementations MUST define this function themselves; this function is
  // declared but *not* defined so linker errors will be thrown if not defined.
  static void Step(sqlite3_context*, int argc, sqlite3_value** argv);

  // Returns the pointer to the user data structure which is passed when
  // creating the function.
  static auto GetUserData(sqlite3_context* ctx) {
    return static_cast<typename Impl::UserData*>(sqlite3_user_data(ctx));
  }

  // Returns the pointer to the auxilary data structure which is passed from
  // SQLite.
  //
  // See https://sqlite.org/c3ref/get_auxdata.html for details on how to
  // use this function.
  static auto GetAuxData(sqlite3_context* ctx, int N) {
    return static_cast<typename Impl::AuxData*>(sqlite3_get_auxdata(ctx, N));
  }

  // Sets the pointer to the auxilary data structure which is passed to SQLite.
  //
  // See https://sqlite.org/c3ref/get_auxdata.html for details on how to
  // use this function.
  template <typename I = Impl>
  static void SetAuxData(sqlite3_context* ctx,
                         int N,
                         std::unique_ptr<typename I::AuxData> data) {
    SetAuxData(ctx, N, data.release(), [](void* ptr) {
      delete static_cast<typename Impl::AuxData*>(ptr);
    });
  }
  template <typename I = Impl>
  static void SetAuxData(sqlite3_context* ctx,
                         int N,
                         typename I::AuxData* data,
                         void (*destructor)(void*)) {
    sqlite3_set_auxdata(ctx, N, data, destructor);
  }
};

}  // namespace perfetto::trace_processor::sqlite

#endif  // SRC_TRACE_PROCESSOR_SQLITE_BINDINGS_SQLITE_FUNCTION_H_
