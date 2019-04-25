// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/offline/qpack_offline_decoder.h"

#include <cstddef>
#include <iostream>

#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

int main(int argc, char* argv[]) {
  const char* usage =
      "Usage: qpack_offline_decoder input_filename expected_headers_filename "
      "....";
  std::vector<quic::QuicString> args =
      quic::QuicParseCommandLineFlags(usage, argc, argv);

  if (args.size() < 2 || args.size() % 2 != 0) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    return 1;
  }

  size_t i;
  for (i = 0; 2 * i < args.size(); ++i) {
    const quic::QuicStringPiece input_filename(args[2 * i]);
    const quic::QuicStringPiece expected_headers_filename(args[2 * i + 1]);

    // Every file represents a different connection,
    // therefore every file needs a fresh decoding context.
    quic::QpackOfflineDecoder decoder;
    if (!decoder.DecodeAndVerifyOfflineData(input_filename,
                                            expected_headers_filename)) {
      return 1;
    }
  }

  std::cout << "Successfully verified " << i << " pairs of input files."
            << std::endl;

  return 0;
}
