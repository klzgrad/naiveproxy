// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/blind_sign_auth/cached_blind_sign_auth.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mutex.h"

namespace quiche {

void CachedBlindSignAuth::GetTokens(
    absl::string_view oauth_token, int num_tokens,
    std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
        callback) {
  if (num_tokens > max_tokens_per_request_) {
    callback(absl::InvalidArgumentError(
        absl::StrFormat("Number of tokens requested exceeds maximum: %d",
                        kBlindSignAuthRequestMaxTokens)));
    return;
  }
  if (num_tokens < 0) {
    callback(absl::InvalidArgumentError(absl::StrFormat(
        "Negative number of tokens requested: %d", num_tokens)));
    return;
  }

  std::vector<std::string> output_tokens;
  {
    QuicheWriterMutexLock lock(&mutex_);

    // Try to fill the request from cache.
    if (static_cast<size_t>(num_tokens) <= cached_tokens_.size()) {
      output_tokens = CreateOutputTokens(num_tokens);
    }
  }
  if (!output_tokens.empty() || num_tokens == 0) {
    callback(output_tokens);
    return;
  }

  // Make a GetTokensRequest if the cache can't handle the request size.
  std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
      caching_callback =
          [this, num_tokens,
           callback](absl::StatusOr<absl::Span<const std::string>> tokens) {
            HandleGetTokensResponse(tokens, num_tokens, callback);
          };
  blind_sign_auth_->GetTokens(oauth_token, kBlindSignAuthRequestMaxTokens,
                              caching_callback);
}

void CachedBlindSignAuth::HandleGetTokensResponse(
    absl::StatusOr<absl::Span<const std::string>> tokens, int num_tokens,
    std::function<void(absl::StatusOr<absl::Span<const std::string>>)>
        callback) {
  if (!tokens.ok()) {
    QUICHE_LOG(WARNING) << "BlindSignAuth::GetTokens failed: "
                        << tokens.status();
    callback(tokens);
    return;
  }
  if (tokens->size() < static_cast<size_t>(num_tokens) ||
      tokens->size() > kBlindSignAuthRequestMaxTokens) {
    QUICHE_LOG(WARNING) << "Expected " << num_tokens << " tokens, got "
                        << tokens->size();
  }

  std::vector<std::string> output_tokens;
  size_t cache_size;
  {
    QuicheWriterMutexLock lock(&mutex_);

    // Add returned tokens to cache.
    for (const std::string& token : *tokens) {
      cached_tokens_.push_back(token);
    }

    // Return tokens or a ResourceExhaustedError.
    cache_size = cached_tokens_.size();
    if (cache_size >= static_cast<size_t>(num_tokens)) {
      output_tokens = CreateOutputTokens(num_tokens);
    }
  }

  if (!output_tokens.empty()) {
    callback(output_tokens);
    return;
  }
  callback(absl::ResourceExhaustedError(absl::StrFormat(
      "Requested %d tokens, cache only has %d after GetTokensRequest",
      num_tokens, cache_size)));
}

std::vector<std::string> CachedBlindSignAuth::CreateOutputTokens(
    int num_tokens) {
  std::vector<std::string> output_tokens;
  if (cached_tokens_.size() < static_cast<size_t>(num_tokens)) {
    QUICHE_LOG(FATAL) << "Check failed, not enough tokens in cache: "
                      << cached_tokens_.size() << " < " << num_tokens;
  }
  for (int i = 0; i < num_tokens; i++) {
    output_tokens.push_back(std::move(cached_tokens_.front()));
    cached_tokens_.pop_front();
  }
  return output_tokens;
}

}  // namespace quiche
