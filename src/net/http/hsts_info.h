// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HSTS_INFO_H_
#define NET_HTTP_HSTS_INFO_H_

// Details on whether HSTS was applied to a particular host. This enum is used
// to histogram the impact of fixing a spec compliance issue in our HSTS
// implementation. See https://crbug.com/821811.
//
// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "HstsInfo" in src/tools/metrics/histograms/enums.xml.
enum class HstsInfo {
  // HSTS was disabled and the results matched the spec.
  kDisabled = 0,
  // HSTS was enabled and the results matched the spec.
  kEnabled = 1,
  // HSTS should have been enabled via the header but was not.
  kDynamicIncorrectlyMasked = 2,
  // HSTS should have been enabled via the header, was not, but it matched the
  // static list.
  kDynamicIncorrectlyMaskedButMatchedStatic = 3,
  kMaxValue = kDynamicIncorrectlyMaskedButMatchedStatic,
};

#endif  // NET_HTTP_HSTS_INFO_H_
