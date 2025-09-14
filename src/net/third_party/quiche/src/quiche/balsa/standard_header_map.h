// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_STANDARD_HEADER_MAP_H_
#define QUICHE_BALSA_STANDARD_HEADER_MAP_H_

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/common/quiche_text_utils.h"

namespace quiche {

// This specifies an absl::flat_hash_set with case-insensitive lookup and
// hashing
using StandardHttpHeaderNameSet =
    absl::flat_hash_set<absl::string_view, StringPieceCaseHash,
                        StringPieceCaseEqual>;

const StandardHttpHeaderNameSet& GetStandardHeaderSet();

}  // namespace quiche

#endif  // QUICHE_BALSA_STANDARD_HEADER_MAP_H_
