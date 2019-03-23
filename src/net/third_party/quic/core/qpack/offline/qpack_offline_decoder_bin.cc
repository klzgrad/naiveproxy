// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/offline/qpack_offline_decoder.h"

#include <cstddef>
#include <iostream>

#include "base/command_line.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

  if (argc < 3 || argc % 2 != 1) {
    QUIC_LOG(ERROR) << "Usage: " << argv[0]
                    << " input_filename expected_headers_filename ...";
    return 1;
  }

  int i;
  for (i = 0; 2 * i + 1 < argc; ++i) {
    const quic::QuicStringPiece input_filename(argv[2 * i + 1]);
    const quic::QuicStringPiece expected_headers_filename(argv[2 * i + 2]);

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
