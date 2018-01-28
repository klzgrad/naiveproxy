// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_QUIC_HTTP_UTILS_H_
#define NET_QUIC_CHROMIUM_QUIC_HTTP_UTILS_H_

#include "base/values.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/quic/core/quic_packets.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_protocol.h"

namespace net {

NET_EXPORT_PRIVATE SpdyPriority
ConvertRequestPriorityToQuicPriority(RequestPriority priority);

NET_EXPORT_PRIVATE RequestPriority
ConvertQuicPriorityToRequestPriority(SpdyPriority priority);

// Converts a SpdyHeaderBlock and priority into NetLog event parameters.
NET_EXPORT std::unique_ptr<base::Value> QuicRequestNetLogCallback(
    QuicStreamId stream_id,
    const SpdyHeaderBlock* headers,
    SpdyPriority priority,
    NetLogCaptureMode capture_mode);

// Parses |alt_svc_versions| into a QuicTransportVersionVector and removes
// all entries that aren't found in |supported_versions|.
NET_EXPORT QuicTransportVersionVector FilterSupportedAltSvcVersions(
    const SpdyAltSvcWireFormat::AlternativeService& quic_alt_svc,
    const QuicTransportVersionVector& supported_versions,
    bool support_ietf_format_quic_altsvc);

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_HTTP_UTILS_H_
