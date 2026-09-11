// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/test_tools/qpack/qpack_offline_decoder.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"

int main(int argc, char* argv[]) {
  const char* usage =
      "Usage: qpack_offline_decoder input_filename expected_headers_filename "
      "....";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  if (args.size() < 2 || args.size() % 2 != 0) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  size_t i;
  size_t success_count = 0;
  for (i = 0; 2 * i < args.size(); ++i) {
    const absl::string_view input_filename(args[2 * i]);
    const absl::string_view expected_headers_filename(args[2 * i + 1]);

    // Every file represents a different connection,
    // therefore every file needs a fresh decoding context.
    quic::QpackOfflineDecoder decoder;
    if (decoder.DecodeAndVerifyOfflineData(input_filename,
                                           expected_headers_filename)) {
      ++success_count;
    }
  }

  std::cout << "Processed " << i << " pairs of input files, " << success_count
            << " passed, " << (i - success_count) << " failed." << std::endl;

  // Return success if all input files pass.
  return (success_count == i) ? 0 : 1;
}
