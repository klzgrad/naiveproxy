// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/wait_chain.h"

#include <memory>

#include "base/logging.h"
#include "base/strings/stringprintf.h"

namespace base {
namespace win {

namespace {

// Helper deleter to hold a HWCT into a unique_ptr.
struct WaitChainSessionDeleter {
  using pointer = HWCT;
  void operator()(HWCT session_handle) const {
    ::CloseThreadWaitChainSession(session_handle);
  }
};

using ScopedWaitChainSessionHandle =
    std::unique_ptr<HWCT, WaitChainSessionDeleter>;

const wchar_t* WctObjectTypeToString(WCT_OBJECT_TYPE type) {
  switch (type) {
    case WctCriticalSectionType:
      return L"CriticalSection";
    case WctSendMessageType:
      return L"SendMessage";
    case WctMutexType:
      return L"Mutex";
    case WctAlpcType:
      return L"Alpc";
    case WctComType:
      return L"Com";
    case WctThreadWaitType:
      return L"ThreadWait";
    case WctProcessWaitType:
      return L"ProcessWait";
    case WctThreadType:
      return L"Thread";
    case WctComActivationType:
      return L"ComActivation";
    case WctUnknownType:
      return L"Unknown";
    case WctSocketIoType:
      return L"SocketIo";
    case WctSmbIoType:
      return L"SmbIo";
    case WctMaxType:
      break;
  }
  NOTREACHED();
  return L"";
}

const wchar_t* WctObjectStatusToString(WCT_OBJECT_STATUS status) {
  switch (status) {
    case WctStatusNoAccess:
      return L"NoAccess";
    case WctStatusRunning:
      return L"Running";
    case WctStatusBlocked:
      return L"Blocked";
    case WctStatusPidOnly:
      return L"PidOnly";
    case WctStatusPidOnlyRpcss:
      return L"PidOnlyRpcss";
    case WctStatusOwned:
      return L"Owned";
    case WctStatusNotOwned:
      return L"NotOwned";
    case WctStatusAbandoned:
      return L"Abandoned";
    case WctStatusUnknown:
      return L"Unknown";
    case WctStatusError:
      return L"Error";
    case WctStatusMax:
      break;
  }
  NOTREACHED();
  return L"";
}

}  // namespace

bool GetThreadWaitChain(DWORD thread_id,
                        WaitChainNodeVector* wait_chain,
                        bool* is_deadlock,
                        base::string16* failure_reason,
                        DWORD* last_error) {
  DCHECK(wait_chain);
  DCHECK(is_deadlock);

  constexpr wchar_t kWaitChainSessionFailureReason[] =
      L"OpenThreadWaitChainSession() failed.";
  constexpr wchar_t kGetWaitChainFailureReason[] =
      L"GetThreadWaitChain() failed.";

  // Open a synchronous session.
  ScopedWaitChainSessionHandle session_handle(
      ::OpenThreadWaitChainSession(0, nullptr));
  if (!session_handle) {
    if (last_error)
      *last_error = ::GetLastError();
    if (failure_reason)
      *failure_reason = kWaitChainSessionFailureReason;
    DPLOG(ERROR) << kWaitChainSessionFailureReason;
    return false;
  }

  DWORD nb_nodes = WCT_MAX_NODE_COUNT;
  wait_chain->resize(nb_nodes);
  BOOL is_cycle;
  if (!::GetThreadWaitChain(session_handle.get(), NULL, 0, thread_id, &nb_nodes,
                            wait_chain->data(), &is_cycle)) {
    if (last_error)
      *last_error = ::GetLastError();
    if (failure_reason)
      *failure_reason = kGetWaitChainFailureReason;
    DPLOG(ERROR) << kGetWaitChainFailureReason;
    return false;
  }

  *is_deadlock = is_cycle ? true : false;
  wait_chain->resize(nb_nodes);

  return true;
}

base::string16 WaitChainNodeToString(const WAITCHAIN_NODE_INFO& node) {
  if (node.ObjectType == WctThreadType) {
    return base::StringPrintf(L"Thread %d in process %d with status %ls",
                              node.ThreadObject.ThreadId,
                              node.ThreadObject.ProcessId,
                              WctObjectStatusToString(node.ObjectStatus));
  } else {
    return base::StringPrintf(L"Lock of type %ls with status %ls",
                              WctObjectTypeToString(node.ObjectType),
                              WctObjectStatusToString(node.ObjectStatus));
  }
}

}  // namespace win
}  // namespace base
