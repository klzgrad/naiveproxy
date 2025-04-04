// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "base/mac/authorization_util.h"

#import <Foundation/Foundation.h>
#include <stddef.h>
#include <sys/wait.h>

#include <string>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/apple/osstatus_logging.h"
#include "base/logging.h"
#include "base/mac/scoped_authorizationref.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/hang_watcher.h"

namespace base::mac {

ScopedAuthorizationRef CreateAuthorization() {
  ScopedAuthorizationRef authorization;
  OSStatus status = AuthorizationCreate(
      /*rights=*/nullptr, kAuthorizationEmptyEnvironment,
      kAuthorizationFlagDefaults, authorization.InitializeInto());
  if (status != errAuthorizationSuccess) {
    OSSTATUS_LOG(ERROR, status) << "AuthorizationCreate";
    return ScopedAuthorizationRef();
  }

  return authorization;
}

ScopedAuthorizationRef GetAuthorizationRightsWithPrompt(
    AuthorizationRights* rights,
    CFStringRef prompt,
    AuthorizationFlags extra_flags) {
  ScopedAuthorizationRef authorization = CreateAuthorization();
  if (!authorization) {
    return authorization;
  }

  // Never consider the current WatchHangsInScope as hung. There was most likely
  // one created in ThreadControllerWithMessagePumpImpl::DoWork(). The current
  // hang watching deadline is not valid since the user can take unbounded time
  // to answer the password prompt. HangWatching will resume when the next task
  // or event is pumped in MessagePumpCFRunLoop so there is not need to
  // reactivate it. You can see the function comments for more details.
  base::HangWatcher::InvalidateActiveExpectations();

  AuthorizationFlags flags = kAuthorizationFlagDefaults |
                             kAuthorizationFlagInteractionAllowed |
                             kAuthorizationFlagExtendRights |
                             kAuthorizationFlagPreAuthorize | extra_flags;

  // product_logo_32.png is used instead of app.icns because Authorization
  // Services can't deal with .icns files.
  NSString* icon_path =
      [base::apple::FrameworkBundle() pathForResource:@"product_logo_32"
                                               ofType:@"png"];
  const char* icon_path_c = [icon_path fileSystemRepresentation];
  size_t icon_path_length = icon_path_c ? strlen(icon_path_c) : 0;

  // The OS will display |prompt| along with a sentence asking the user to type
  // the "password to allow this."
  std::string prompt_string;
  const char* prompt_c = nullptr;
  size_t prompt_length = 0;
  if (prompt) {
    prompt_string = SysCFStringRefToUTF8(prompt);
    prompt_c = prompt_string.c_str();
    prompt_length = prompt_string.length();
  }

  AuthorizationItem environment_items[] = {
      {kAuthorizationEnvironmentIcon, icon_path_length, (void*)icon_path_c, 0},
      {kAuthorizationEnvironmentPrompt, prompt_length, (void*)prompt_c, 0}};

  AuthorizationEnvironment environment = {std::size(environment_items),
                                          environment_items};

  OSStatus status = AuthorizationCopyRights(authorization, rights, &environment,
                                            flags, nullptr);

  if (status != errAuthorizationSuccess) {
    if (status != errAuthorizationCanceled) {
      OSSTATUS_LOG(ERROR, status) << "AuthorizationCopyRights";
    }
    return ScopedAuthorizationRef();
  }

  return authorization;
}

ScopedAuthorizationRef AuthorizationCreateToRunAsRoot(CFStringRef prompt) {
  // Specify the "system.privilege.admin" right, which allows
  // AuthorizationExecuteWithPrivileges to run commands as root.
  AuthorizationItem right_items[] = {
      {kAuthorizationRightExecute, 0, nullptr, 0}};
  AuthorizationRights rights = {std::size(right_items), right_items};

  return GetAuthorizationRightsWithPrompt(&rights, prompt, /*extra_flags=*/0);
}

OSStatus ExecuteWithPrivilegesAndGetPID(AuthorizationRef authorization,
                                        const char* tool_path,
                                        AuthorizationFlags options,
                                        const char** arguments,
                                        FILE** pipe,
                                        pid_t* pid) {
  // pipe may be NULL, but this function needs one.  In that case, use a local
  // pipe.
  FILE* local_pipe;
  FILE** pipe_pointer;
  if (pipe) {
    pipe_pointer = pipe;
  } else {
    pipe_pointer = &local_pipe;
  }

// AuthorizationExecuteWithPrivileges is deprecated in macOS 10.7, but no good
// replacement exists. https://crbug.com/593133.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // AuthorizationExecuteWithPrivileges wants |char* const*| for |arguments|,
  // but it doesn't actually modify the arguments, and that type is kind of
  // silly and callers probably aren't dealing with that.  Put the cast here
  // to make things a little easier on callers.
  OSStatus status = AuthorizationExecuteWithPrivileges(
      authorization, tool_path, options, (char* const*)arguments, pipe_pointer);
#pragma clang diagnostic pop
  if (status != errAuthorizationSuccess) {
    return status;
  }

  int line_pid = -1;
  size_t line_length = 0;
  char* line_c = fgetln(*pipe_pointer, &line_length);
  if (line_c) {
    if (line_length > 0 && line_c[line_length - 1] == '\n') {
      // line_c + line_length is the start of the next line if there is one.
      // Back up one character.
      --line_length;
    }
    std::string line(line_c, line_length);
    if (!base::StringToInt(line, &line_pid)) {
      // StringToInt may have set line_pid to something, but if the conversion
      // was imperfect, use -1.
      LOG(ERROR) << "ExecuteWithPrivilegesAndGetPid: funny line: " << line;
      line_pid = -1;
    }
  } else {
    LOG(ERROR) << "ExecuteWithPrivilegesAndGetPid: no line";
  }

  if (!pipe) {
    fclose(*pipe_pointer);
  }

  if (pid) {
    *pid = line_pid;
  }

  return status;
}

OSStatus ExecuteWithPrivilegesAndWait(AuthorizationRef authorization,
                                      const char* tool_path,
                                      AuthorizationFlags options,
                                      const char** arguments,
                                      FILE** pipe,
                                      int* exit_status) {
  pid_t pid;
  OSStatus status = ExecuteWithPrivilegesAndGetPID(
      authorization, tool_path, options, arguments, pipe, &pid);
  if (status != errAuthorizationSuccess) {
    return status;
  }

  // exit_status may be NULL, but this function needs it.  In that case, use a
  // local version.
  int local_exit_status;
  int* exit_status_pointer;
  if (exit_status) {
    exit_status_pointer = exit_status;
  } else {
    exit_status_pointer = &local_exit_status;
  }

  if (pid != -1) {
    pid_t wait_result = HANDLE_EINTR(waitpid(pid, exit_status_pointer, 0));
    if (wait_result != pid) {
      PLOG(ERROR) << "waitpid";
      *exit_status_pointer = -1;
    }
  } else {
    *exit_status_pointer = -1;
  }

  return status;
}

}  // namespace base::mac
