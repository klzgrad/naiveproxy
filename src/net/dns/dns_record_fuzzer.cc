// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "net/dns/dns_response.h"

void InitLogging() {
  // For debugging, it may be helpful to enable verbose logging by setting the
  // minimum log level to (-LOG_FATAL).
  logging::SetMinLogLevel(logging::LOG_FATAL);

  logging::LoggingSettings settings;
  settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  settings.log_file = nullptr;
  logging::InitLogging(settings);
}

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  InitLogging();

  net::DnsRecordParser parser(data, size, 0);
  if (!parser.IsValid()) {
    return 0;
  }
  net::DnsResourceRecord record;
  while (parser.ReadRecord(&record)) {
  }
  return 0;
}
