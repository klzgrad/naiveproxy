// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_PROTOCOL_H_
#define NET_TOOLS_NAIVE_NAIVE_PROTOCOL_H_

namespace net {
enum class ClientProtocol {
  kSocks5,
  kHttp,
  kRedir,
};

// Adds padding for traffic from this direction.
// Removes padding for traffic from the opposite direction.
enum Direction {
  kClient = 0,
  kServer = 1,
  kNumDirections = 2,
  kNone = 2,
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROTOCOL_H_
