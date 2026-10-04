// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/padding_utils.h"

#include <array>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "net/http/http_response_headers.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/third_party/quiche/src/quiche/http2/hpack/hpack_constants.h"

namespace net {
namespace {
bool g_nonindex_codes_initialized;
std::array<uint8_t, 17> g_nonindex_codes;
}  // namespace

void InitializeNonindexCodes() {
  if (g_nonindex_codes_initialized) {
    return;
  }
  g_nonindex_codes_initialized = true;
  unsigned i = 0;
  for (const auto& symbol : spdy::HpackHuffmanCodeVector()) {
    if (symbol.id >= 0x20 && symbol.id <= 0x7f && symbol.length >= 8) {
      if (i >= sizeof(g_nonindex_codes)) {
        break;
      }
      g_nonindex_codes[i] = symbol.id;
      ++i;
    }
  }
  CHECK(i == sizeof(g_nonindex_codes));
}

void FillNonindexHeaderValue(uint64_t unique_bits, base::span<uint8_t> span) {
  DCHECK(g_nonindex_codes_initialized);
  int first = span.size() < 16 ? span.size() : 16;
  for (int i = 0; i < first; i++) {
    span[i] = g_nonindex_codes[unique_bits & 0b1111];
    unique_bits >>= 4;
  }
  for (size_t i = first; i < span.size(); ++i) {
    span[i] = g_nonindex_codes[16];
  }
}

std::optional<PaddingType> ParsePaddingHeaders(
    const HttpResponseHeaders& headers) {
  const bool has_padding = headers.HasHeader(kPaddingHeader);
  const std::optional<std::string> padding_type_reply =
      headers.GetNormalizedHeader(kPaddingTypeReplyHeader);

  if (!padding_type_reply.has_value()) {
    // Backward compatibility with before kVariant1 when the padding-version
    // header does not exist.
    return has_padding ? PaddingType::kVariant1 : PaddingType::kNone;
  }
  std::optional<PaddingType> padding_type =
      ParsePaddingType(*padding_type_reply);
  if (!padding_type.has_value()) {
    LOG(ERROR) << "Received invalid padding type: " << *padding_type_reply;
  }
  return padding_type;
}
}  // namespace net
