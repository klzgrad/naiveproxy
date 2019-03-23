// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ftp/ftp_server_type_histograms.h"

#include "base/metrics/histogram_macros.h"

namespace net {

// We're using a histogram as a group of counters, with one bucket for each
// enumeration value.  We're only interested in the values of the counters.
// Ignore the shape, average, and standard deviation of the histograms because
// they are meaningless.
//
// We use two histograms.  In the first histogram we tally whether the user has
// seen an FTP server of a given type during that session.  In the second
// histogram we tally the number of transactions with FTP server of a given type
// the user has made during that session.
void UpdateFtpServerTypeHistograms(FtpServerType type) {
  static bool had_server_type[NUM_OF_SERVER_TYPES];
  if (type >= 0 && type < NUM_OF_SERVER_TYPES) {
    if (!had_server_type[type]) {
      had_server_type[type] = true;
      UMA_HISTOGRAM_ENUMERATION("Net.HadFtpServerType2",
                                type, NUM_OF_SERVER_TYPES);
    }
  }
  UMA_HISTOGRAM_ENUMERATION("Net.FtpServerTypeCount2",
                            type, NUM_OF_SERVER_TYPES);
}

}  // namespace net
