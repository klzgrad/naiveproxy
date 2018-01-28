// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_NETWORK_QUALITY_OBSERVATION_SOURCE_H_
#define NET_NQE_NETWORK_QUALITY_OBSERVATION_SOURCE_H_

namespace net {

// On Android, a Java counterpart will be generated for this enum.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.net
// GENERATED_JAVA_CLASS_NAME_OVERRIDE: NetworkQualityObservationSource
// GENERATED_JAVA_PREFIX_TO_STRIP: NETWORK_QUALITY_OBSERVATION_SOURCE_
enum NetworkQualityObservationSource {
  // The observation was taken at the request layer, e.g., a round trip time
  // is recorded as the time between the request being sent and the first byte
  // being received.
  NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP = 0,

  // The observation is taken from TCP statistics maintained by the kernel.
  NETWORK_QUALITY_OBSERVATION_SOURCE_TCP = 1,

  // The observation is taken at the QUIC layer.
  NETWORK_QUALITY_OBSERVATION_SOURCE_QUIC = 2,

  // The observation is a previously cached estimate of the metric.  The metric
  // was computed at the HTTP layer.
  NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP_CACHED_ESTIMATE = 3,

  // The observation is derived from network connection information provided
  // by the platform. For example, typical RTT and throughput values are used
  // for a given type of network connection.  The metric was provided for use
  // at the HTTP layer.
  NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_HTTP_FROM_PLATFORM = 4,

  // The observation came from a Chromium-external source. The metric was
  // computed by the external source at the HTTP layer.
  NETWORK_QUALITY_OBSERVATION_SOURCE_HTTP_EXTERNAL_ESTIMATE = 5,

  // The observation is a previously cached estimate of the metric. The metric
  // was computed at the transport layer.
  NETWORK_QUALITY_OBSERVATION_SOURCE_TRANSPORT_CACHED_ESTIMATE = 6,

  // The observation is derived from the network connection information provided
  // by the platform. For example, typical RTT and throughput values are used
  // for a given type of network connection.  The metric was provided for use
  // at the transport layer.
  NETWORK_QUALITY_OBSERVATION_SOURCE_DEFAULT_TRANSPORT_FROM_PLATFORM = 7,

  NETWORK_QUALITY_OBSERVATION_SOURCE_MAX,
};

namespace nqe {

namespace internal {
// Returns the string equivalent of |source|.
const char* GetNameForObservationSource(NetworkQualityObservationSource source);

// Different categories to which an observation source can belong to. Each
// oberation source belongs to exactly one category.
enum class ObservationCategory { kHttp = 0, kTransport = 1 };

}  // namespace internal

}  // namespace nqe

}  // namespace net

#endif  // NET_NQE_NETWORK_QUALITY_OBSERVATION_SOURCE_H_
