// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_IO_QUIC_DEFAULT_EVENT_LOOP_H_
#define QUICHE_QUIC_CORE_IO_QUIC_DEFAULT_EVENT_LOOP_H_

#include <memory>

#include "quiche/quic/core/io/quic_event_loop.h"

namespace quic {

// Returns the default implementation of QuicheEventLoop.  The embedders can
// override this using the platform API.  The factory pointer returned is an
// unowned static variable.
QuicEventLoopFactory* GetDefaultEventLoop();

// Returns the factory objects for all event loops.  This is particularly useful
// for the unit tests.  The factory pointers returned are unowned static
// variables.
std::vector<QuicEventLoopFactory*> GetAllSupportedEventLoops();

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_IO_QUIC_DEFAULT_EVENT_LOOP_H_
