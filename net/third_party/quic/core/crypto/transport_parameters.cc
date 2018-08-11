// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/transport_parameters.h"

#include "third_party/boringssl/src/include/openssl/bytestring.h"

namespace net {

namespace {

// Values of the TransportParameterId enum as defined in
// draft-ietf-quic-transport-08 section 7.4. When parameters are encoded, one of
// these enum values is used to indicate which parameter is encoded.
enum TransportParameterId : uint16_t {
  kInitialMaxStreamData = 0,
  kInitialMaxData = 1,
  kInitialMaxStreamIdBidi = 2,
  kIdleTimeout = 3,
  kOmitConnectionId = 4,
  kMaxPacketSize = 5,
  kStatelessResetToken = 6,
  kAckDelayExponent = 7,
  kInitialMaxStreamIdUni = 8,

  kMaxKnownParameterId = 9,
};

// The following constants define minimum and maximum allowed values for some of
// the parameters. These come from draft-ietf-quic-transport-08 section 7.4.1.
const uint16_t kMaxAllowedIdleTimeout = 600;
const uint16_t kMinAllowedMaxPacketSize = 1200;
const uint16_t kMaxAllowedMaxPacketSize = 65527;
const uint8_t kMaxAllowedAckDelayExponent = 20;

static_assert(kMaxKnownParameterId <= 32, "too many parameters to bit pack");

// The initial_max_stream_data, initial_max_data, and idle_timeout parameters
// are always required to be present. When parsing the extension, a bitmask is
// used to keep track of which parameter have been seen so far, and that bitmask
// will be compared to this mask to check that all of the required parameters
// were present.
static constexpr uint16_t kRequiredParamsMask =
    (1 << kInitialMaxStreamData) | (1 << kInitialMaxData) | (1 << kIdleTimeout);

}  // namespace

TransportParameters::TransportParameters() = default;

TransportParameters::TransportParameters(
    const TransportParameters& transport_params) = default;

TransportParameters::~TransportParameters() = default;

bool TransportParameters::is_valid() const {
  if (perspective == Perspective::IS_CLIENT && !stateless_reset_token.empty()) {
    return false;
  }
  if (perspective == Perspective::IS_SERVER &&
      stateless_reset_token.size() != 16) {
    return false;
  }
  if (idle_timeout > kMaxAllowedIdleTimeout ||
      (max_packet_size.present &&
       (max_packet_size.value > kMaxAllowedMaxPacketSize ||
        max_packet_size.value < kMinAllowedMaxPacketSize)) ||
      (ack_delay_exponent.present &&
       ack_delay_exponent.value > kMaxAllowedAckDelayExponent)) {
    return false;
  }
  return true;
}

bool SerializeTransportParameters(const TransportParameters& in,
                                  std::vector<uint8_t>* out) {
  if (!in.is_valid()) {
    return false;
  }
  bssl::ScopedCBB cbb;
  // 28 is the minimum size that the serialized TransportParameters can be,
  // which is when it is for a client and only the required parameters are
  // present. The CBB will grow to fit larger serializations.
  if (!CBB_init(cbb.get(), 28) || !CBB_add_u32(cbb.get(), in.version)) {
    return false;
  }
  CBB versions;
  if (in.perspective == Perspective::IS_SERVER) {
    if (!CBB_add_u8_length_prefixed(cbb.get(), &versions)) {
      return false;
    }
    for (QuicVersionLabel version : in.supported_versions) {
      if (!CBB_add_u32(&versions, version)) {
        return false;
      }
    }
  }

  CBB params, initial_max_stream_data_param, initial_max_data_param,
      idle_timeout_param;
  // required parameters
  if (!CBB_add_u16_length_prefixed(cbb.get(), &params) ||
      // initial_max_stream_data
      !CBB_add_u16(&params, kInitialMaxStreamData) ||
      !CBB_add_u16_length_prefixed(&params, &initial_max_stream_data_param) ||
      !CBB_add_u32(&initial_max_stream_data_param,
                   in.initial_max_stream_data) ||
      // initial_max_data
      !CBB_add_u16(&params, kInitialMaxData) ||
      !CBB_add_u16_length_prefixed(&params, &initial_max_data_param) ||
      !CBB_add_u32(&initial_max_data_param, in.initial_max_data) ||
      // idle_timeout
      !CBB_add_u16(&params, kIdleTimeout) ||
      !CBB_add_u16_length_prefixed(&params, &idle_timeout_param) ||
      !CBB_add_u16(&idle_timeout_param, in.idle_timeout)) {
    return false;
  }

  CBB stateless_reset_token_param;
  if (!in.stateless_reset_token.empty()) {
    if (!CBB_add_u16(&params, kStatelessResetToken) ||
        !CBB_add_u16_length_prefixed(&params, &stateless_reset_token_param) ||
        !CBB_add_bytes(&stateless_reset_token_param,
                       in.stateless_reset_token.data(),
                       in.stateless_reset_token.size())) {
      return false;
    }
  }

  CBB initial_max_stream_id_bidi_param;
  if (in.initial_max_stream_id_bidi.present) {
    if (!CBB_add_u16(&params, kInitialMaxStreamIdBidi) ||
        !CBB_add_u16_length_prefixed(&params,
                                     &initial_max_stream_id_bidi_param) ||
        !CBB_add_u32(&initial_max_stream_id_bidi_param,
                     in.initial_max_stream_id_bidi.value)) {
      return false;
    }
  }
  CBB initial_max_stream_id_uni_param;
  if (in.initial_max_stream_id_uni.present) {
    if (!CBB_add_u16(&params, kInitialMaxStreamIdUni) ||
        !CBB_add_u16_length_prefixed(&params,
                                     &initial_max_stream_id_uni_param) ||
        !CBB_add_u32(&initial_max_stream_id_uni_param,
                     in.initial_max_stream_id_uni.value)) {
      return false;
    }
  }
  if (in.omit_connection_id) {
    if (!CBB_add_u16(&params, kOmitConnectionId) || !CBB_add_u16(&params, 0)) {
      return false;
    }
  }
  CBB max_packet_size_param;
  if (in.max_packet_size.present) {
    if (!CBB_add_u16(&params, kMaxPacketSize) ||
        !CBB_add_u16_length_prefixed(&params, &max_packet_size_param) ||
        !CBB_add_u16(&max_packet_size_param, in.max_packet_size.value)) {
      return false;
    }
  }
  CBB ack_delay_exponent_param;
  if (in.ack_delay_exponent.present) {
    if (!CBB_add_u16(&params, kAckDelayExponent) ||
        !CBB_add_u16_length_prefixed(&params, &ack_delay_exponent_param) ||
        !CBB_add_u8(&ack_delay_exponent_param, in.ack_delay_exponent.value)) {
      return false;
    }
  }
  if (!CBB_flush(cbb.get())) {
    return false;
  }
  out->resize(CBB_len(cbb.get()));
  memcpy(out->data(), CBB_data(cbb.get()), CBB_len(cbb.get()));
  return true;
}

bool ParseTransportParameters(const uint8_t* in,
                              size_t in_len,
                              Perspective perspective,
                              TransportParameters* out) {
  CBS cbs;
  CBS_init(&cbs, in, in_len);
  if (!CBS_get_u32(&cbs, &out->version)) {
    return false;
  }
  if (perspective == Perspective::IS_SERVER) {
    CBS versions;
    if (!CBS_get_u8_length_prefixed(&cbs, &versions) ||
        CBS_len(&versions) % 4 != 0) {
      return false;
    }
    while (CBS_len(&versions) > 0) {
      QuicVersionLabel version;
      if (!CBS_get_u32(&versions, &version)) {
        return false;
      }
      out->supported_versions.push_back(version);
    }
  }
  out->perspective = perspective;

  uint32_t present_params = 0;
  CBS params;
  if (!CBS_get_u16_length_prefixed(&cbs, &params)) {
    return false;
  }
  while (CBS_len(&params) > 0) {
    uint16_t param_id;
    CBS value;
    if (!CBS_get_u16(&params, &param_id) ||
        !CBS_get_u16_length_prefixed(&params, &value)) {
      return false;
    }
    if (param_id < kMaxKnownParameterId) {
      uint16_t mask = 1 << param_id;
      if (present_params & mask) {
        return false;
      }
      present_params |= mask;
    }
    switch (param_id) {
      case kInitialMaxStreamData:
        if (!CBS_get_u32(&value, &out->initial_max_stream_data) ||
            CBS_len(&value) != 0) {
          return false;
        }
        break;
      case kInitialMaxData:
        if (!CBS_get_u32(&value, &out->initial_max_data) ||
            CBS_len(&value) != 0) {
          return false;
        }
        break;
      case kInitialMaxStreamIdBidi:
        if (!CBS_get_u32(&value, &out->initial_max_stream_id_bidi.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->initial_max_stream_id_bidi.present = true;
        break;
      case kIdleTimeout:
        if (!CBS_get_u16(&value, &out->idle_timeout) || CBS_len(&value) != 0) {
          return false;
        }
        break;
      case kOmitConnectionId:
        if (CBS_len(&value) != 0) {
          return false;
        }
        out->omit_connection_id = true;
        break;
      case kMaxPacketSize:
        if (!CBS_get_u16(&value, &out->max_packet_size.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->max_packet_size.present = true;
        break;
      case kStatelessResetToken:
        if (CBS_len(&value) == 0) {
          return false;
        }
        out->stateless_reset_token.assign(CBS_data(&value),
                                          CBS_data(&value) + CBS_len(&value));
        break;
      case kAckDelayExponent:
        if (!CBS_get_u8(&value, &out->ack_delay_exponent.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->ack_delay_exponent.present = true;
        break;
      case kInitialMaxStreamIdUni:
        if (!CBS_get_u32(&value, &out->initial_max_stream_id_uni.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->initial_max_stream_id_uni.present = true;
    }
  }
  if ((present_params & kRequiredParamsMask) != kRequiredParamsMask) {
    return false;
  }
  return out->is_valid();
}

}  // namespace net
