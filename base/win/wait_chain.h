// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_WAIT_CHAIN_H_
#define BASE_WIN_WAIT_CHAIN_H_

#include <windows.h>
#include <wct.h>

#include <vector>

#include "base/strings/string16.h"

namespace base {
namespace win {

using WaitChainNodeVector = std::vector<WAITCHAIN_NODE_INFO>;

// Gets the wait chain for |thread_id|. Also specifies if the |wait_chain|
// contains a deadlock. Returns true on success.
//
// From MSDN: A wait chain is an alternating sequence of threads and
// synchronization objects; each thread waits for the object that follows it,
// which is owned by the subsequent thread in the chain.
//
// On error, |failure_reason| and/or |last_error| will contain the details of
// the failure, given that they are not null.
// TODO(pmonette): Remove |failure_reason| and |last_error| when UMA is
// supported in the watcher process and pre-rendez-vous.
BASE_EXPORT bool GetThreadWaitChain(DWORD thread_id,
                                    WaitChainNodeVector* wait_chain,
                                    bool* is_deadlock,
                                    base::string16* failure_reason,
                                    DWORD* last_error);

// Returns a string that represents the node for a wait chain.
BASE_EXPORT base::string16 WaitChainNodeToString(
    const WAITCHAIN_NODE_INFO& node);

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_WAIT_CHAIN_H_
