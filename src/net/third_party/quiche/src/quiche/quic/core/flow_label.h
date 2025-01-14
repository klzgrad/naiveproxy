// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_FLOW_LABEL_H_
#define QUICHE_QUIC_CORE_QUIC_FLOW_LABEL_H_

#include <cstdint>

#if defined(__linux__)
#include <linux/in6.h>
#include <sys/socket.h>

#ifndef IPV6_FLOWLABEL
#define IPV6_FLOWINFO 11
#define IPV6_FLOWINFO_FLOWLABEL 0x000fffff
#endif

static constexpr int kCmsgSpaceForFlowLabel = CMSG_SPACE(sizeof(uint32_t));

#endif

#endif  // QUICHE_QUIC_CORE_QUIC_FLOW_LABEL_H_
