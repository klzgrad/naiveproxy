// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_ip_address.h"

namespace quic {

// List of QUIC versions that support MASQUE. Currently restricted to IETF QUIC.
QUIC_NO_EXPORT ParsedQuicVersionVector MasqueSupportedVersions();

enum : QuicByteCount {
  kMasqueIpPacketBufferSize = 1501,
  // Enough for a VLAN tag, but not Stacked VLANs.
  kMasqueEthernetFrameBufferSize = 1523,
};

// Mode that MASQUE is operating in.
enum class MasqueMode : uint8_t {
  kInvalid = 0,  // Should never be used.
  kOpen = 2,  // Open mode uses the MASQUE HTTP CONNECT-UDP method as documented
  // in <https://www.rfc-editor.org/rfc/rfc9298.html>. This mode allows
  // unauthenticated clients (a more restricted mode will be added to this enum
  // at a later date).
  kConnectIp =
      1,  // ConnectIp mode uses MASQUE HTTP CONNECT-IP as documented in
  // <https://datatracker.ietf.org/doc/html/draft-ietf-masque-connect-ip>. This
  // mode also allows unauthenticated clients.
  kConnectEthernet =
      3,  // ConnectEthernet mode uses MASQUE HTTP CONNECT-ETHERNET.
  // <https://datatracker.ietf.org/doc/draft-asedeno-masque-connect-ethernet/>
  // This mode also allows unauthenticated clients.
};

QUIC_NO_EXPORT std::string MasqueModeToString(MasqueMode masque_mode);
QUIC_NO_EXPORT std::ostream& operator<<(std::ostream& os,
                                        const MasqueMode& masque_mode);

// Create a TUN interface, with the specified `client_address`. Requires root.
int CreateTunInterface(const QuicIpAddress& client_address, bool server = true);

// Create a TAP interface. Requires root.
int CreateTapInterface();

inline constexpr size_t kSignatureAuthSignatureInputSize = 32;
inline constexpr size_t kSignatureAuthVerificationSize = 16;
inline constexpr size_t kSignatureAuthExporterSize =
    kSignatureAuthSignatureInputSize + kSignatureAuthVerificationSize;
inline constexpr uint16_t kEd25519SignatureScheme = 0x0807;
inline constexpr absl::string_view kSignatureAuthLabel =
    "EXPORTER-HTTP-Signature-Authentication";

// Returns the signature auth TLS key exporter context.
QUIC_NO_EXPORT std::string ComputeSignatureAuthContext(
    uint16_t signature_scheme, absl::string_view key_id,
    absl::string_view public_key, absl::string_view scheme,
    absl::string_view host, uint16_t port, absl::string_view realm);

// Returns the data covered by signature auth signatures, computed by
// concatenating a fixed prefix from the specification and the signature input.
QUIC_NO_EXPORT std::string SignatureAuthDataCoveredBySignature(
    absl::string_view signature_input);

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_UTILS_H_
