// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file intentionally does not have header guards, it's included
// inside a macro to generate values. The following line silences a
// presubmit warning that would otherwise be triggered by this:
// no-include-guard-because-multiply-included

// Specifies type of filters that can be created.  Do not change the values
// of this enum; it is preserved in a histogram.
SOURCE_STREAM_TYPE(BROTLI)
SOURCE_STREAM_TYPE(DEFLATE)
SOURCE_STREAM_TYPE(GZIP)
SOURCE_STREAM_TYPE(GZIP_FALLBACK_DEPRECATED)
SOURCE_STREAM_TYPE(SDCH_DEPRECATED)
SOURCE_STREAM_TYPE(SDCH_POSSIBLE_DEPRECATED)
SOURCE_STREAM_TYPE(INVALID)
SOURCE_STREAM_TYPE(NONE)
SOURCE_STREAM_TYPE(REJECTED)
SOURCE_STREAM_TYPE(UNKNOWN)
