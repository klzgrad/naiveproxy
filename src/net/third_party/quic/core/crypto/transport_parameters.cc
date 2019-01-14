// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/transport_parameters.h"

#include "net/third_party/quic/core/crypto/crypto_framer.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"

namespace quic {

namespace {

// Values of the TransportParameterId enum as defined in
// draft-ietf-quic-transport-08 section 7.4. When parameters are encoded, one of
// these enum values is used to indicate which parameter is encoded.
enum TransportParameterId : uint16_t {
  kInitialMaxStreamDataId = 0,
  kInitialMaxDataId = 1,
  kInitialMaxBidiStreamsId = 2,
  kIdleTimeoutId = 3,
  kMaxPacketSizeId = 5,
  kStatelessResetTokenId = 6,
  kAckDelayExponentId = 7,
  kInitialMaxUniStreamsId = 8,

  kMaxKnownParameterId = 9,
};

// Value for the TransportParameterId to use for non-standard Google QUIC params
// in Transport Parameters.
const uint16_t kGoogleQuicParamId = 18257;

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
static constexpr uint16_t kRequiredParamsMask = (1 << kInitialMaxStreamDataId) |
                                                (1 << kInitialMaxDataId) |
                                                (1 << kIdleTimeoutId);

}  // namespace

TransportParameters::TransportParameters() = default;

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
      !CBB_add_u16(&params, kInitialMaxStreamDataId) ||
      !CBB_add_u16_length_prefixed(&params, &initial_max_stream_data_param) ||
      !CBB_add_u32(&initial_max_stream_data_param,
                   in.initial_max_stream_data) ||
      // initial_max_data
      !CBB_add_u16(&params, kInitialMaxDataId) ||
      !CBB_add_u16_length_prefixed(&params, &initial_max_data_param) ||
      !CBB_add_u32(&initial_max_data_param, in.initial_max_data) ||
      // idle_timeout
      !CBB_add_u16(&params, kIdleTimeoutId) ||
      !CBB_add_u16_length_prefixed(&params, &idle_timeout_param) ||
      !CBB_add_u16(&idle_timeout_param, in.idle_timeout)) {
    return false;
  }

  CBB stateless_reset_token_param;
  if (!in.stateless_reset_token.empty()) {
    if (!CBB_add_u16(&params, kStatelessResetTokenId) ||
        !CBB_add_u16_length_prefixed(&params, &stateless_reset_token_param) ||
        !CBB_add_bytes(&stateless_reset_token_param,
                       in.stateless_reset_token.data(),
                       in.stateless_reset_token.size())) {
      return false;
    }
  }

  CBB initial_max_bidi_streams_param;
  if (in.initial_max_bidi_streams.present) {
    if (!CBB_add_u16(&params, kInitialMaxBidiStreamsId) ||
        !CBB_add_u16_length_prefixed(&params,
                                     &initial_max_bidi_streams_param) ||
        !CBB_add_u16(&initial_max_bidi_streams_param,
                     in.initial_max_bidi_streams.value)) {
      return false;
    }
  }
  CBB initial_max_uni_streams_param;
  if (in.initial_max_uni_streams.present) {
    if (!CBB_add_u16(&params, kInitialMaxUniStreamsId) ||
        !CBB_add_u16_length_prefixed(&params, &initial_max_uni_streams_param) ||
        !CBB_add_u16(&initial_max_uni_streams_param,
                     in.initial_max_uni_streams.value)) {
      return false;
    }
  }
  CBB max_packet_size_param;
  if (in.max_packet_size.present) {
    if (!CBB_add_u16(&params, kMaxPacketSizeId) ||
        !CBB_add_u16_length_prefixed(&params, &max_packet_size_param) ||
        !CBB_add_u16(&max_packet_size_param, in.max_packet_size.value)) {
      return false;
    }
  }
  CBB ack_delay_exponent_param;
  if (in.ack_delay_exponent.present) {
    if (!CBB_add_u16(&params, kAckDelayExponentId) ||
        !CBB_add_u16_length_prefixed(&params, &ack_delay_exponent_param) ||
        !CBB_add_u8(&ack_delay_exponent_param, in.ack_delay_exponent.value)) {
      return false;
    }
  }
  CBB google_quic_params;
  if (in.google_quic_params) {
    const QuicData& serialized_google_quic_params =
        in.google_quic_params->GetSerialized();
    if (!CBB_add_u16(&params, kGoogleQuicParamId) ||
        !CBB_add_u16_length_prefixed(&params, &google_quic_params) ||
        !CBB_add_bytes(&google_quic_params,
                       reinterpret_cast<const uint8_t*>(
                           serialized_google_quic_params.data()),
                       serialized_google_quic_params.length())) {
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
  bool has_google_quic_params = false;
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
      case kInitialMaxStreamDataId:
        if (!CBS_get_u32(&value, &out->initial_max_stream_data) ||
            CBS_len(&value) != 0) {
          return false;
        }
        break;
      case kInitialMaxDataId:
        if (!CBS_get_u32(&value, &out->initial_max_data) ||
            CBS_len(&value) != 0) {
          return false;
        }
        break;
      case kInitialMaxBidiStreamsId:
        if (!CBS_get_u16(&value, &out->initial_max_bidi_streams.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->initial_max_bidi_streams.present = true;
        break;
      case kIdleTimeoutId:
        if (!CBS_get_u16(&value, &out->idle_timeout) || CBS_len(&value) != 0) {
          return false;
        }
        break;
      case kMaxPacketSizeId:
        if (!CBS_get_u16(&value, &out->max_packet_size.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->max_packet_size.present = true;
        break;
      case kStatelessResetTokenId:
        if (CBS_len(&value) == 0) {
          return false;
        }
        out->stateless_reset_token.assign(CBS_data(&value),
                                          CBS_data(&value) + CBS_len(&value));
        break;
      case kAckDelayExponentId:
        if (!CBS_get_u8(&value, &out->ack_delay_exponent.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->ack_delay_exponent.present = true;
        break;
      case kInitialMaxUniStreamsId:
        if (!CBS_get_u16(&value, &out->initial_max_uni_streams.value) ||
            CBS_len(&value) != 0) {
          return false;
        }
        out->initial_max_uni_streams.present = true;
        break;
      case kGoogleQuicParamId:
        if (has_google_quic_params) {
          return false;
        }
        has_google_quic_params = true;
        QuicStringPiece serialized_params(
            reinterpret_cast<const char*>(CBS_data(&value)), CBS_len(&value));
        out->google_quic_params = CryptoFramer::ParseMessage(serialized_params);
    }
  }
  if ((present_params & kRequiredParamsMask) != kRequiredParamsMask) {
    return false;
  }
  return out->is_valid();
}

}  // namespace quic
