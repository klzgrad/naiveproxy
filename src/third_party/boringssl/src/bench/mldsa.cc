// Copyright 2025 The BoringSSL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>
#include <string_view>
#include <vector>

#include <benchmark/benchmark.h>

#include <openssl/base.h>
#include <openssl/bytestring.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/mldsa.h>
#include <openssl/span.h>

#include "./internal.h"

namespace {
void BM_SpeedMLDSAKeyGen(benchmark::State &state) {
  std::vector<uint8_t> encoded_public_key(MLDSA65_PUBLIC_KEY_BYTES);
  MLDSA65_private_key priv;
  for (auto _ : state) {
    uint8_t seed[MLDSA_SEED_BYTES];
    if (!MLDSA65_generate_key(encoded_public_key.data(), seed, &priv)) {
      state.SkipWithError("Failure in MLDSA65_generate_key.");
      return;
    }
  }
}

void BM_SpeedMLDSASign(benchmark::State &state) {
  std::vector<uint8_t> encoded_public_key(MLDSA65_PUBLIC_KEY_BYTES);
  auto priv = std::make_unique<MLDSA65_private_key>();
  uint8_t seed[MLDSA_SEED_BYTES];
  MLDSA65_generate_key(encoded_public_key.data(), seed, priv.get());
  const auto message = bssl::StringAsBytes("Hello world");
  auto out_encoded_signature =
      std::make_unique<uint8_t[]>(MLDSA65_SIGNATURE_BYTES);
  for (auto _ : state) {
    if (!MLDSA65_sign(out_encoded_signature.get(), priv.get(), message.data(),
                      message.size(), nullptr, 0)) {
      state.SkipWithError("Failure in MLDSA65_sign.");
      return;
    }
  }
}

void BM_SpeedMLDSAParsePubKey(benchmark::State &state) {
  std::vector<uint8_t> encoded_public_key(MLDSA65_PUBLIC_KEY_BYTES);
  auto priv = std::make_unique<MLDSA65_private_key>();
  uint8_t seed[MLDSA_SEED_BYTES];
  MLDSA65_generate_key(encoded_public_key.data(), seed, &*priv);
  MLDSA65_public_key pub;
  for (auto _ : state) {
    CBS cbs;
    CBS_init(&cbs, encoded_public_key.data(), MLDSA65_PUBLIC_KEY_BYTES);
    if (!MLDSA65_parse_public_key(&pub, &cbs)) {
      state.SkipWithError("Failure in MLDSA65_parse_public_key.");
      return;
    }
  }
}

void BM_SpeedMLDSAVerify(benchmark::State &state) {
  std::vector<uint8_t> encoded_public_key(MLDSA65_PUBLIC_KEY_BYTES);
  auto priv = std::make_unique<MLDSA65_private_key>();
  uint8_t seed[MLDSA_SEED_BYTES];
  MLDSA65_generate_key(encoded_public_key.data(), seed, &*priv);
  MLDSA65_public_key pub;
  CBS cbs;
  CBS_init(&cbs, encoded_public_key.data(), MLDSA65_PUBLIC_KEY_BYTES);
  MLDSA65_parse_public_key(&pub, &cbs);
  std::vector<uint8_t> out_encoded_signature(MLDSA65_SIGNATURE_BYTES);
  const auto message = bssl::StringAsBytes("Hello world");
  MLDSA65_sign(out_encoded_signature.data(), &*priv, message.data(),
               message.size(), nullptr, 0);
  for (auto _ : state) {
    if (!MLDSA65_verify(&pub, out_encoded_signature.data(),
                        MLDSA65_SIGNATURE_BYTES, message.data(), message.size(),
                        nullptr, 0)) {
      state.SkipWithError("Failed to verify MLDSA signature.");
      return;
    }
  }
}

void BM_SpeedMLDSAVerifyBadSignature(benchmark::State &state) {
  std::vector<uint8_t> encoded_public_key(MLDSA65_PUBLIC_KEY_BYTES);
  auto priv = std::make_unique<MLDSA65_private_key>();
  uint8_t seed[MLDSA_SEED_BYTES];
  MLDSA65_generate_key(encoded_public_key.data(), seed, &*priv);
  MLDSA65_public_key pub;
  CBS cbs;
  CBS_init(&cbs, encoded_public_key.data(), MLDSA65_PUBLIC_KEY_BYTES);
  MLDSA65_parse_public_key(&pub, &cbs);
  std::vector<uint8_t> out_encoded_signature(MLDSA65_SIGNATURE_BYTES);
  const auto message = bssl::StringAsBytes("Hello world");
  MLDSA65_sign(out_encoded_signature.data(), &*priv, message.data(),
               message.size(), nullptr, 0);
  out_encoded_signature[42] ^= 0x42;
  for (auto _ : state) {
    if (MLDSA65_verify(&pub, out_encoded_signature.data(),
                       MLDSA65_SIGNATURE_BYTES, message.data(), message.size(),
                       nullptr, 0)) {
      state.SkipWithError("MLDSA signature unexpectedly verified.");
      return;
    }
  }
}

BSSL_BENCH_LAZY_REGISTER() {
  BENCHMARK(BM_SpeedMLDSAKeyGen)->Apply(bssl::bench::SetThreads);
  BENCHMARK(BM_SpeedMLDSASign)->Apply(bssl::bench::SetThreads);
  BENCHMARK(BM_SpeedMLDSAParsePubKey)->Apply(bssl::bench::SetThreads);
  BENCHMARK(BM_SpeedMLDSAVerify)->Apply(bssl::bench::SetThreads);
  BENCHMARK(BM_SpeedMLDSAVerifyBadSignature)->Apply(bssl::bench::SetThreads);
}

}  // namespace
