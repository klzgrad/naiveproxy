// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_connection_context.h"

#include "quiche/common/platform/api/quiche_thread_local.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {
namespace {
DEFINE_QUICHE_THREAD_LOCAL_POINTER(CurrentContext, QuicConnectionContext);
}  // namespace

std::string QuicConnectionProcessPacketContext::DebugString() const {
  if (decrypted_payload.empty()) {
    return "Not processing packet";
  }

  return absl::StrCat("current_frame_offset: ", current_frame_offset,
                      ", payload size: ", decrypted_payload.size(),
                      ", payload hexdump: ",
                      quiche::QuicheTextUtils::HexDump(decrypted_payload));
}

// static
QuicConnectionContext* QuicConnectionContext::Current() {
  return GET_QUICHE_THREAD_LOCAL_POINTER(CurrentContext);
}

QuicConnectionContextSwitcher::QuicConnectionContextSwitcher(
    QuicConnectionContext* new_context)
    : old_context_(QuicConnectionContext::Current()) {
  SET_QUICHE_THREAD_LOCAL_POINTER(CurrentContext, new_context);
  if (new_context && new_context->tracer) {
    new_context->tracer->Activate();
  }
}

QuicConnectionContextSwitcher::~QuicConnectionContextSwitcher() {
  QuicConnectionContext* current = QuicConnectionContext::Current();
  if (current && current->tracer) {
    current->tracer->Deactivate();
  }
  SET_QUICHE_THREAD_LOCAL_POINTER(CurrentContext, old_context_);
}

}  // namespace quic
