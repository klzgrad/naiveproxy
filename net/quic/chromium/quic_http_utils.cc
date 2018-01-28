// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_http_utils.h"

#include <utility>

#include "base/metrics/histogram_macros.h"
#include "net/quic/platform/api/quic_endian.h"

namespace net {

namespace {

enum AltSvcFormat { GOOGLE_FORMAT = 0, IETF_FORMAT = 1, ALTSVC_FORMAT_MAX };

void RecordAltSvcFormat(AltSvcFormat format) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicAltSvcFormat", format, ALTSVC_FORMAT_MAX);
}

};  // namespace

SpdyPriority ConvertRequestPriorityToQuicPriority(
    const RequestPriority priority) {
  DCHECK_GE(priority, MINIMUM_PRIORITY);
  DCHECK_LE(priority, MAXIMUM_PRIORITY);
  return static_cast<SpdyPriority>(HIGHEST - priority);
}

RequestPriority ConvertQuicPriorityToRequestPriority(SpdyPriority priority) {
  // Handle invalid values gracefully.
  return (priority >= 5) ? IDLE
                         : static_cast<RequestPriority>(HIGHEST - priority);
}

std::unique_ptr<base::Value> QuicRequestNetLogCallback(
    QuicStreamId stream_id,
    const SpdyHeaderBlock* headers,
    SpdyPriority priority,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(
      static_cast<base::DictionaryValue*>(
          SpdyHeaderBlockNetLogCallback(headers, capture_mode).release()));
  dict->SetInteger("quic_priority", static_cast<int>(priority));
  dict->SetInteger("quic_stream_id", static_cast<int>(stream_id));
  return std::move(dict);
}

QuicTransportVersionVector FilterSupportedAltSvcVersions(
    const SpdyAltSvcWireFormat::AlternativeService& quic_alt_svc,
    const QuicTransportVersionVector& supported_versions,
    bool support_ietf_format_quic_altsvc) {
  QuicTransportVersionVector supported_alt_svc_versions;
  if (support_ietf_format_quic_altsvc && quic_alt_svc.protocol_id == "hq") {
    // Using IETF format for advertising QUIC. In this case,
    // |alternative_service_entry.version| will store QUIC version labels.
    for (uint32_t quic_version_label : quic_alt_svc.version) {
      for (QuicTransportVersion supported : supported_versions) {
        QuicVersionLabel supported_version_label_network_order =
            FLAGS_quic_reloadable_flag_quic_use_net_byte_order_version_label
                ? QuicVersionToQuicVersionLabel(supported)
                : QuicEndian::HostToNet32(
                      QuicVersionToQuicVersionLabel(supported));
        if (supported_version_label_network_order == quic_version_label) {
          supported_alt_svc_versions.push_back(supported);
          RecordAltSvcFormat(IETF_FORMAT);
        }
      }
    }
  } else if (quic_alt_svc.protocol_id == "quic") {
    for (uint32_t quic_version : quic_alt_svc.version) {
      for (QuicTransportVersion supported : supported_versions) {
        if (static_cast<uint32_t>(supported) == quic_version) {
          supported_alt_svc_versions.push_back(supported);
          RecordAltSvcFormat(GOOGLE_FORMAT);
        }
      }
    }
  }
  return supported_alt_svc_versions;
}

}  // namespace net
