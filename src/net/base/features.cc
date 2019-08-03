// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/features.h"

namespace net {
namespace features {

// Toggles the `Accept-Language` HTTP request header, which
// https://github.com/WICG/lang-client-hint proposes that we deprecate.
const base::Feature kAcceptLanguageHeader{"AcceptLanguageHeader",
                                          base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kCapRefererHeaderLength = {
    "CapRefererHeaderLength", base::FEATURE_DISABLED_BY_DEFAULT};
const base::FeatureParam<int> kMaxRefererHeaderLength = {
    &kCapRefererHeaderLength, "MaxRefererHeaderLength", 4096};

// Uses a site isolated code cache that is keyed on the resource url and the
// origin lock of the renderer that is requesting the resource. The requests
// to site-isolated code cache are handled by the content/GeneratedCodeCache
// When this flag is enabled, the metadata field of the HttpCache is unused.
const base::Feature kIsolatedCodeCache = {"IsolatedCodeCache",
                                          base::FEATURE_ENABLED_BY_DEFAULT};

// Enables the additional TLS 1.3 server-random-based downgrade protection
// described in https://tools.ietf.org/html/rfc8446#section-4.1.3
//
// This is a MUST-level requirement of TLS 1.3, but has compatibility issues
// with some buggy non-compliant TLS-terminating proxies.
const base::Feature kEnforceTLS13Downgrade{"EnforceTLS13Downgrade",
                                           base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kEnableTLS13EarlyData{"EnableTLS13EarlyData",
                                          base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNetworkQualityEstimator{"NetworkQualityEstimator",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSplitCacheByTopFrameOrigin{
    "SplitCacheByTopFrameOrigin", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPartitionConnectionsByNetworkIsolationKey{
    "PartitionConnectionsByNetworkIsolationKey",
    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kTLS13KeyUpdate{"TLS13KeyUpdate",
                                    base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPostQuantumCECPQ2{"PostQuantumCECPQ2",
                                       base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kNetUnusedIdleSocketTimeout{
    "NetUnusedIdleSocketTimeout", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kSameSiteByDefaultCookies{
    "SameSiteByDefaultCookies", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kCookiesWithoutSameSiteMustBeSecure{
    "CookiesWithoutSameSiteMustBeSecure", base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features
}  // namespace net
