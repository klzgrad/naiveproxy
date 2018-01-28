// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_HOSTNAME_UTILS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_HOSTNAME_UTILS_IMPL_H_

#include "base/macros.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicHostnameUtilsImpl {
 public:
  // Returns true if the sni is valid, false otherwise.
  //  (1) disallow IP addresses;
  //  (2) check that the hostname contains valid characters only; and
  //  (3) contains at least one dot.
  static bool IsValidSNI(QuicStringPiece sni);

  // Convert hostname to lowercase and remove the trailing '.'.
  // WARNING: mutates |hostname| in place and returns |hostname|.
  static char* NormalizeHostname(char* hostname);

  // Creates a QuicServerId from a string formatted in same manner as
  // QuicServerId::ToString().
  static void StringToQuicServerId(const std::string& str, QuicServerId* out);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicHostnameUtilsImpl);
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_HOSTNAME_UTILS_IMPL_H_
