// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/merkle_consistency_proof.h"

namespace net {

namespace ct {

MerkleConsistencyProof::MerkleConsistencyProof() {}

MerkleConsistencyProof::MerkleConsistencyProof(
    const std::string& log_id,
    const std::vector<std::string>& proof_nodes,
    uint64_t old_size,
    uint64_t new_size)
    : log_id(log_id),
      nodes(proof_nodes),
      first_tree_size(old_size),
      second_tree_size(new_size) {}

MerkleConsistencyProof::~MerkleConsistencyProof() {}

}  // namespace ct

}  // namespace net
