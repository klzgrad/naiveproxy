/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

#include <Windows.h>

#include "perfetto/ext/base/thread_task_runner.h"
#include "perfetto/tracing/internal/tracing_tls.h"
#include "perfetto/tracing/platform.h"

// Thread Termination Callbacks.
// Windows doesn't support a per-thread destructor with its
// TLS primitives. So, we build it manually by inserting a
// function to be called on each thread's exit.
// This magic is from chromium's base/threading/thread_local_storage_win.cc
// which in turn is from http://www.codeproject.com/threads/tls.asp.

#ifdef _WIN64
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:perfetto_thread_callback_base")
#else
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_perfetto_thread_callback_base")
#endif

namespace perfetto {

namespace {

class PlatformWindows : public Platform {
 public:
  static PlatformWindows* instance;
  PlatformWindows();
  ~PlatformWindows() override;

  ThreadLocalObject* GetOrCreateThreadLocalObject() override;
  std::unique_ptr<base::TaskRunner> CreateTaskRunner(
      const CreateTaskRunnerArgs&) override;
  std::string GetCurrentProcessName() override;
  void OnThreadExit();

 private:
  DWORD tls_key_{};
};

using ThreadLocalObject = Platform::ThreadLocalObject;

// static
PlatformWindows* PlatformWindows::instance = nullptr;

PlatformWindows::PlatformWindows() {
  instance = this;
  tls_key_ = ::TlsAlloc();
  PERFETTO_CHECK(tls_key_ != TLS_OUT_OF_INDEXES);
}

PlatformWindows::~PlatformWindows() {
  ::TlsFree(tls_key_);
  instance = nullptr;
}

void PlatformWindows::OnThreadExit() {
  auto tls = static_cast<ThreadLocalObject*>(::TlsGetValue(tls_key_));
  if (tls) {
    // At this point we rely on the TLS object to be still set to the TracingTLS
    // we are deleting. See comments in TracingTLS::~TracingTLS().
    delete tls;
  }
}

ThreadLocalObject* PlatformWindows::GetOrCreateThreadLocalObject() {
  void* tls_ptr = ::TlsGetValue(tls_key_);

  auto* tls = static_cast<ThreadLocalObject*>(tls_ptr);
  if (!tls) {
    tls = ThreadLocalObject::CreateInstance().release();
    ::TlsSetValue(tls_key_, tls);
  }
  return tls;
}

std::unique_ptr<base::TaskRunner> PlatformWindows::CreateTaskRunner(
    const CreateTaskRunnerArgs& args) {
  return std::unique_ptr<base::TaskRunner>(new base::ThreadTaskRunner(
      base::ThreadTaskRunner::CreateAndStart(args.name_for_debugging)));
}

std::string PlatformWindows::GetCurrentProcessName() {
  char buf[MAX_PATH];
  auto len = ::GetModuleFileNameA(nullptr /*current*/, buf, sizeof(buf));
  std::string name(buf, static_cast<size_t>(len));
  size_t sep = name.find_last_of('\\');
  if (sep != std::string::npos)
    name = name.substr(sep + 1);
  return name;
}

}  // namespace

// static
Platform* Platform::GetDefaultPlatform() {
  static PlatformWindows* thread_safe_init_instance = new PlatformWindows();
  return thread_safe_init_instance;
}

}  // namespace perfetto

// -----------------------
// Thread-local destructor
// -----------------------

// .CRT$XLA to .CRT$XLZ is an array of PIMAGE_TLS_CALLBACK pointers that are
// called automatically by the OS loader code (not the CRT) when the module is
// loaded and on thread creation. They are NOT called if the module has been
// loaded by a LoadLibrary() call. It must have implicitly been loaded at
// process startup.
// See VC\crt\src\tlssup.c for reference.

// extern "C" suppresses C++ name mangling so we know the symbol name for the
// linker /INCLUDE:symbol pragma above.
extern "C" {
// The linker must not discard perfetto_thread_callback_base. (We force a
// reference to this variable with a linker /INCLUDE:symbol pragma to ensure
// that.) If this variable is discarded, the OnThreadExit function will never be
// called.

void NTAPI PerfettoOnThreadExit(PVOID, DWORD, PVOID);
void NTAPI PerfettoOnThreadExit(PVOID, DWORD reason, PVOID) {
  if (reason == DLL_THREAD_DETACH || reason == DLL_PROCESS_DETACH) {
    if (perfetto::PlatformWindows::instance)
      perfetto::PlatformWindows::instance->OnThreadExit();
  }
}

#ifdef _WIN64

// .CRT section is merged with .rdata on x64 so it must be constant data.
#pragma const_seg(".CRT$XLP")

// When defining a const variable, it must have external linkage to be sure the
// linker doesn't discard it.
extern const PIMAGE_TLS_CALLBACK perfetto_thread_callback_base;
const PIMAGE_TLS_CALLBACK perfetto_thread_callback_base = PerfettoOnThreadExit;

// Reset the default section.
#pragma const_seg()

#else  // _WIN64

#pragma data_seg(".CRT$XLP")
PIMAGE_TLS_CALLBACK perfetto_thread_callback_base = PerfettoOnThreadExit;
// Reset the default section.
#pragma data_seg()

#endif  // _WIN64

}  // extern "C"

#endif  // OS_WIN
