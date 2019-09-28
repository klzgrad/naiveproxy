// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <sddl.h>

#include <memory>

#include "base/command_line.h"
#include "base/memory/free_deleter.h"
#include "base/memory/shared_memory.h"
#include "base/process/process.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "base/win/scoped_handle.h"
#include "base/win/win_util.h"
#include "testing/multiprocess_func_list.h"

namespace base {
namespace {
const char* kHandleSwitchName = "shared_memory_win_test_switch";

// Creates a process token with a low integrity SID.
win::ScopedHandle CreateLowIntegritySID() {
  HANDLE process_token_raw = nullptr;
  BOOL success = ::OpenProcessToken(GetCurrentProcess(),
                                    TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT |
                                        TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY,
                                    &process_token_raw);
  if (!success)
    return base::win::ScopedHandle();
  win::ScopedHandle process_token(process_token_raw);

  HANDLE lowered_process_token_raw = nullptr;
  success =
      ::DuplicateTokenEx(process_token.Get(), 0, NULL, SecurityImpersonation,
                         TokenPrimary, &lowered_process_token_raw);
  if (!success)
    return base::win::ScopedHandle();
  win::ScopedHandle lowered_process_token(lowered_process_token_raw);

  // Low integrity SID
  WCHAR integrity_sid_string[20] = L"S-1-16-4096";
  PSID integrity_sid = nullptr;
  success = ::ConvertStringSidToSid(integrity_sid_string, &integrity_sid);
  if (!success)
    return base::win::ScopedHandle();

  TOKEN_MANDATORY_LABEL TIL = {};
  TIL.Label.Attributes = SE_GROUP_INTEGRITY;
  TIL.Label.Sid = integrity_sid;
  success = ::SetTokenInformation(
      lowered_process_token.Get(), TokenIntegrityLevel, &TIL,
      sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(integrity_sid));
  if (!success)
    return base::win::ScopedHandle();
  return lowered_process_token;
}

// Reads a HANDLE from the pipe as a raw int, least significant digit first.
win::ScopedHandle ReadHandleFromPipe(HANDLE pipe) {
  // Read from parent pipe.
  const size_t buf_size = 1000;
  char buffer[buf_size];
  memset(buffer, 0, buf_size);
  DWORD bytes_read;
  BOOL success = ReadFile(pipe, buffer, buf_size, &bytes_read, NULL);

  if (!success || bytes_read == 0) {
    LOG(ERROR) << "Failed to read handle from pipe.";
    return win::ScopedHandle();
  }

  int handle_as_int = 0;
  int power_of_ten = 1;
  for (unsigned int i = 0; i < bytes_read; ++i) {
    handle_as_int += buffer[i] * power_of_ten;
    power_of_ten *= 10;
  }

  return win::ScopedHandle(reinterpret_cast<HANDLE>(handle_as_int));
}

// Writes a HANDLE to a pipe as a raw int, least significant digit first.
void WriteHandleToPipe(HANDLE pipe, HANDLE handle) {
  uint32_t handle_as_int = base::win::HandleToUint32(handle);

  std::unique_ptr<char, base::FreeDeleter> buffer(
      static_cast<char*>(malloc(1000)));
  size_t index = 0;
  while (handle_as_int > 0) {
    buffer.get()[index] = handle_as_int % 10;
    handle_as_int /= 10;
    ++index;
  }

  ::ConnectNamedPipe(pipe, nullptr);
  DWORD written;
  ASSERT_TRUE(::WriteFile(pipe, buffer.get(), index, &written, NULL));
}

// Creates a communication pipe with the given name.
win::ScopedHandle CreateCommunicationPipe(const std::wstring& name) {
  return win::ScopedHandle(CreateNamedPipe(name.c_str(),  // pipe name
                                           PIPE_ACCESS_DUPLEX, PIPE_WAIT, 255,
                                           1000, 1000, 0, NULL));
}

// Generates a random name for a communication pipe.
std::wstring CreateCommunicationPipeName() {
  uint64_t rand_values[4];
  RandBytes(&rand_values, sizeof(rand_values));
  std::wstring child_pipe_name = StringPrintf(
      L"\\\\.\\pipe\\SharedMemoryWinTest_%016llx%016llx%016llx%016llx",
      rand_values[0], rand_values[1], rand_values[2], rand_values[3]);
  return child_pipe_name;
}

class SharedMemoryWinTest : public base::MultiProcessTest {
 protected:
  CommandLine MakeCmdLine(const std::string& procname) override {
    CommandLine line = base::MultiProcessTest::MakeCmdLine(procname);
    line.AppendSwitchASCII(kHandleSwitchName, communication_pipe_name_);
    return line;
  }

  std::string communication_pipe_name_;
};

MULTIPROCESS_TEST_MAIN(LowerPermissions) {
  std::string handle_name =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kHandleSwitchName);
  std::wstring handle_name16 = SysUTF8ToWide(handle_name);
  win::ScopedHandle parent_pipe(
      ::CreateFile(handle_name16.c_str(),  // pipe name
                   GENERIC_READ,
                   0,              // no sharing
                   NULL,           // default security attributes
                   OPEN_EXISTING,  // opens existing pipe
                   0,              // default attributes
                   NULL));         // no template file
  if (parent_pipe.Get() == INVALID_HANDLE_VALUE) {
    LOG(ERROR) << "Failed to open communication pipe.";
    return 1;
  }

  win::ScopedHandle received_handle = ReadHandleFromPipe(parent_pipe.Get());
  if (!received_handle.Get()) {
    LOG(ERROR) << "Failed to read handle from pipe.";
    return 1;
  }

  // Attempting to add the WRITE_DAC permission should fail.
  HANDLE duped_handle;
  BOOL success = ::DuplicateHandle(GetCurrentProcess(), received_handle.Get(),
                                   GetCurrentProcess(), &duped_handle,
                                   FILE_MAP_READ | WRITE_DAC, FALSE, 0);
  if (success) {
    LOG(ERROR) << "Should not have been able to add WRITE_DAC permission.";
    return 1;
  }

  // Attempting to add the FILE_MAP_WRITE permission should fail.
  success = ::DuplicateHandle(GetCurrentProcess(), received_handle.Get(),
                              GetCurrentProcess(), &duped_handle,
                              FILE_MAP_READ | FILE_MAP_WRITE, FALSE, 0);
  if (success) {
    LOG(ERROR) << "Should not have been able to add FILE_MAP_WRITE permission.";
    return 1;
  }

  // Attempting to duplicate the HANDLE with the same permissions should
  // succeed.
  success = ::DuplicateHandle(GetCurrentProcess(), received_handle.Get(),
                              GetCurrentProcess(), &duped_handle, FILE_MAP_READ,
                              FALSE, 0);
  if (!success) {
    LOG(ERROR) << "Failed to duplicate handle.";
    return 4;
  }
  ::CloseHandle(duped_handle);
  return 0;
}

TEST_F(SharedMemoryWinTest, LowerPermissions) {
  std::wstring communication_pipe_name = CreateCommunicationPipeName();
  communication_pipe_name_ = SysWideToUTF8(communication_pipe_name);

  win::ScopedHandle communication_pipe =
      CreateCommunicationPipe(communication_pipe_name);
  ASSERT_TRUE(communication_pipe.Get());

  win::ScopedHandle lowered_process_token = CreateLowIntegritySID();
  ASSERT_TRUE(lowered_process_token.Get());

  base::LaunchOptions options;
  options.as_user = lowered_process_token.Get();
  base::Process process = SpawnChildWithOptions("LowerPermissions", options);
  ASSERT_TRUE(process.IsValid());

  SharedMemory memory;
  memory.CreateAndMapAnonymous(1001);

  // Duplicate into child process, giving only FILE_MAP_READ permissions.
  HANDLE raw_handle = nullptr;
  ::DuplicateHandle(::GetCurrentProcess(), memory.handle().GetHandle(),
                    process.Handle(), &raw_handle,
                    FILE_MAP_READ | SECTION_QUERY, FALSE, 0);
  ASSERT_TRUE(raw_handle);

  WriteHandleToPipe(communication_pipe.Get(), raw_handle);

  int exit_code;
  EXPECT_TRUE(process.WaitForExitWithTimeout(TestTimeouts::action_max_timeout(),
                                             &exit_code));
  EXPECT_EQ(0, exit_code);
}

}  // namespace
}  // namespace base
