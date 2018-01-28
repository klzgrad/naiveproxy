// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This utility can dump the contents of CRL set, optionally augmented with a
// delta CRL set.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "base/at_exit.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_number_conversions.h"
#include "net/cert/crl_set.h"
#include "net/cert/crl_set_storage.h"

static int Usage(const char* argv0) {
  fprintf(stderr, "Usage: %s <crl-set file> [<delta file>]"
                  " [<resulting output file>]\n", argv0);
  return 1;
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit_manager;

  base::FilePath crl_set_filename, delta_filename, output_filename;

  if (argc < 2 || argc > 4)
    return Usage(argv[0]);

  crl_set_filename = base::FilePath::FromUTF8Unsafe(argv[1]);
  if (argc >= 3)
    delta_filename = base::FilePath::FromUTF8Unsafe(argv[2]);
  if (argc >= 4)
    output_filename = base::FilePath::FromUTF8Unsafe(argv[3]);

  std::string crl_set_bytes, delta_bytes;
  if (!base::ReadFileToString(crl_set_filename, &crl_set_bytes))
    return 1;
  if (!delta_filename.empty() &&
      !base::ReadFileToString(delta_filename, &delta_bytes)) {
    return 1;
  }

  scoped_refptr<net::CRLSet> crl_set, final_crl_set;
  if (!net::CRLSetStorage::Parse(crl_set_bytes, &crl_set)) {
    fprintf(stderr, "Failed to parse CRLSet\n");
    return 1;
  }

  if (!delta_bytes.empty()) {
    if (!net::CRLSetStorage::ApplyDelta(
            crl_set.get(), delta_bytes, &final_crl_set)) {
      fprintf(stderr, "Failed to apply delta to CRLSet\n");
      return 1;
    }
  } else {
    final_crl_set = crl_set;
  }

  if (!output_filename.empty()) {
    const std::string out = net::CRLSetStorage::Serialize(final_crl_set.get());
    if (base::WriteFile(output_filename, out.data(), out.size()) == -1) {
      fprintf(stderr, "Failed to write resulting CRL set\n");
      return 1;
    }
  }

  const net::CRLSet::CRLList& crls = final_crl_set->crls();
  for (net::CRLSet::CRLList::const_iterator i = crls.begin(); i != crls.end();
       i++) {
    printf("%s\n", base::HexEncode(i->first.data(), i->first.size()).c_str());
    for (std::vector<std::string>::const_iterator j = i->second.begin();
         j != i->second.end(); j++) {
      printf("  %s\n", base::HexEncode(j->data(), j->size()).c_str());
    }
  }

  return 0;
}
