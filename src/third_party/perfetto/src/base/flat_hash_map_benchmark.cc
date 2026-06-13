// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/flat_hash_map_v1.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/scoped_file.h"

// This benchmark allows to compare our FlatHashMap implementation against
// reference implementations from Absl (Google), Folly F14 (FB), and Tssil's
// reference RobinHood hashmap.
// Those libraries are not checked in into the repo. If you want to reproduce
// the benchmark you need to:
// - Manually install the three libraries using following the instructions in
//   their readme (they all use cmake).
// - When running cmake, remember to pass
//   -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS='-DNDEBUG -O3 -msse4.2 -mavx'.
//   That sets cflags for a more fair comparison.
// - Set is_debug=false in the GN args.
// - Set the GN var perfetto_benchmark_3p_libs_prefix="/usr/local" (or whatever
//   other directory you set as DESTDIR when running make install).
// The presence of the perfetto_benchmark_3p_libs_prefix GN variable will
// automatically define PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS.

#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
// Use the checked-in abseil-cpp
#include <absl/container/flat_hash_map.h>
#endif

#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
// Last tested: https://github.com/facebook/folly @ 028a9abae3.
#include <folly/container/F14Map.h>

// Last tested: https://github.com/Tessil/robin-map @ a603419b9.
#include <tsl/robin_map.h>
#endif

namespace {

using namespace perfetto;
using benchmark::Counter;
using perfetto::base::AlreadyHashed;
using perfetto::base::LinearProbe;
using perfetto::base::QuadraticHalfProbe;
using perfetto::base::QuadraticProbe;

// Our FlatHashMap doesn't have a STL-like interface, mainly because we use
// columnar-oriented storage, not array-of-tuples, so we can't easily map into
// that interface. This wrapper makes our FlatHashMap compatible with STL (just
// for what it takes to build this translation unit), at the cost of some small
// performance penalty (around 1-2%).
template <typename Key, typename Value, typename Hasher, typename Probe>
class Ours : public base::FlatHashMap<Key, Value, Hasher, Probe> {
 public:
  struct Iterator {
    using value_type = std::pair<const Key&, Value&>;
    Iterator(const Key& k, Value& v) : pair_{k, v} {}
    value_type* operator->() { return &pair_; }
    value_type& operator*() { return pair_; }
    bool operator==(const Iterator& other) const {
      return &pair_.first == &other.pair_.first;
    }
    bool operator!=(const Iterator& other) const { return !operator==(other); }
    value_type pair_;
  };

  void insert(std::pair<Key, Value>&& pair) {
    this->Insert(std::move(pair.first), std::move(pair.second));
  }

  Iterator find(const Key& key) {
    const size_t idx = this->FindInternal(key);
    return Iterator(this->keys_[idx], this->values_[idx]);
  }

  // Heterogeneous find
  template <typename K,
            typename H = Hasher,
            typename = typename H::is_transparent>
  Iterator find(const K& key) {
    const size_t idx = this->FindInternal(key);
    return Iterator(this->keys_[idx], this->values_[idx]);
  }

  Iterator end() {
    return Iterator(this->keys_[this->kNotFound],
                    this->values_[this->kNotFound]);
  }

  void clear() { this->Clear(); }

  bool erase(const Key& key) { return this->Erase(key); }
};

// Wrapper for FlatHashMapV2 to make it STL-compatible for benchmarking.
// FlatHashMapV2 uses Swiss Table probing (fixed), so Probe param is ignored.
template <typename Key, typename Value, typename Hasher, typename /* Probe */>
class OursV2 : public base::FlatHashMapV2<Key, Value, Hasher> {
 public:
  struct Iterator {
    using value_type = std::pair<bool, Value&>;
    Iterator(bool is_real, Value& v) : pair_{is_real, v} {}
    value_type* operator->() { return &pair_; }
    value_type& operator*() { return pair_; }
    bool operator==(const Iterator& other) const {
      return &pair_.first == &other.pair_.first;
    }
    bool operator!=(const Iterator& other) const { return !operator==(other); }
    value_type pair_;
  };

  void insert(std::pair<Key, Value>&& pair) {
    this->Insert(std::move(pair.first), std::move(pair.second));
  }

  Iterator find(const Key& key) { return Iterator(true, *this->Find(key)); }

  // Heterogeneous find
  template <typename K,
            typename H = Hasher,
            typename = typename H::is_transparent>
  Iterator find(const K& key) {
    return Iterator(true, *this->Find(key));
  }

  Iterator end() { return Iterator(false, not_real_); }

  void clear() { this->Clear(); }

  bool erase(const Key& key) { return this->Erase(key); }

  Value not_real_;
};

std::vector<uint64_t> LoadTraceStrings(benchmark::State& state) {
  std::vector<uint64_t> str_hashes;
  // This requires that the user has downloaded the file
  // go/perfetto-benchmark-trace-strings into /tmp/trace_strings. The file is
  // too big (2.3 GB after uncompression) and it's not worth adding it to the
  // //test/data. Also it contains data from a team member's phone and cannot
  // be public.
  base::ScopedFstream f(fopen("/tmp/trace_strings", "re"));
  if (!f) {
    state.SkipWithError(
        "Test strings missing. Googlers: download "
        "go/perfetto-benchmark-trace-strings and save into /tmp/trace_strings");
    return str_hashes;
  }
  char line[4096];
  while (fgets(line, sizeof(line), *f)) {
    size_t len = strlen(line);
    str_hashes.emplace_back(
        base::MurmurHash<std::string_view>{}(std::string_view(line, len)));
  }
  return str_hashes;
}

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

size_t num_samples() {
  return IsBenchmarkFunctionalOnly() ? size_t(100) : size_t(10 * 1000 * 1000);
}

void VaryingSizeArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Arg(100);
    return;
  }
  for (int64_t size = 100; size <= 10000000; size *= 100) {
    b->Arg(size);
  }
}

void MissRateArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Arg(50);
    return;
  }
  b->Arg(0)->Arg(50)->Arg(100);
}

// Uses directly the base::FlatHashMap with no STL wrapper. Configures the map
// in append-only mode.
void BM_HashMap_InsertTraceStrings_AppendOnly(benchmark::State& state) {
  std::vector<uint64_t> hashes = LoadTraceStrings(state);
  for (auto _ : state) {
    base::FlatHashMap<uint64_t, uint64_t, AlreadyHashed<uint64_t>, LinearProbe,
                      /*AppendOnly=*/true>
        mapz;
    for (uint64_t hash : hashes)
      mapz.Insert(hash, 42);

    benchmark::ClobberMemory();
    state.counters["uniq"] = Counter(static_cast<double>(mapz.size()));
  }
  state.counters["tot"] = Counter(static_cast<double>(hashes.size()),
                                  Counter::kIsIterationInvariant);
  state.counters["rate"] = Counter(static_cast<double>(hashes.size()),
                                   Counter::kIsIterationInvariantRate);
}

template <typename MapType>
void BM_HashMap_InsertTraceStrings(benchmark::State& state) {
  std::vector<uint64_t> hashes = LoadTraceStrings(state);
  for (auto _ : state) {
    MapType mapz;
    for (uint64_t hash : hashes)
      mapz.insert({hash, 42});

    benchmark::ClobberMemory();
    state.counters["uniq"] = Counter(static_cast<double>(mapz.size()));
  }
  state.counters["tot"] = Counter(static_cast<double>(hashes.size()),
                                  Counter::kIsIterationInvariant);
  state.counters["rate"] = Counter(static_cast<double>(hashes.size()),
                                   Counter::kIsIterationInvariantRate);
}

template <typename MapType>
void BM_HashMap_TraceTids(benchmark::State& state) {
  std::vector<std::pair<char, int>> ops_and_tids;
  {
    base::ScopedFstream f(fopen("/tmp/tids", "re"));
    if (!f) {
      // This test requires a large (800MB) test file. It's not checked into the
      // repository's //test/data because it would slow down all developers for
      // a marginal benefit.
      state.SkipWithError(
          "Please run `curl -Lo /tmp/tids "
          "https://storage.googleapis.com/perfetto/test_data/"
          "long_trace_tids.txt"
          "` and try again.");
      return;
    }
    char op;
    int tid;
    while (fscanf(*f, "%c %d\n", &op, &tid) == 2)
      ops_and_tids.emplace_back(op, tid);
  }

  for (auto _ : state) {
    MapType mapz;
    for (const auto& ops_and_tid : ops_and_tids) {
      if (ops_and_tid.first == '[') {
        mapz[ops_and_tid.second]++;
      } else {
        mapz.insert({ops_and_tid.second, 0});
      }
    }

    benchmark::ClobberMemory();
    state.counters["uniq"] = Counter(static_cast<double>(mapz.size()));
  }
  state.counters["rate"] = Counter(static_cast<double>(ops_and_tids.size()),
                                   Counter::kIsIterationInvariantRate);
}

template <typename MapType>
void BM_HashMap_InsertRandInts(benchmark::State& state) {
  std::minstd_rand0 rng(0);
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(rng());
  for (auto _ : state) {
    MapType mapz;
    for (const auto key : keys)
      mapz.insert({key, key});
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["insertions"] = Counter(static_cast<double>(keys.size()),
                                         Counter::kIsIterationInvariantRate);
}

// This test is performs insertions on integers that are designed to create
// lot of clustering on the same small set of buckets.
// This covers the unlucky case of using a map with a poor hashing function.
template <typename MapType>
void BM_HashMap_InsertCollidingInts(benchmark::State& state) {
  std::vector<size_t> keys;
  const size_t kNumSamples = num_samples();

  // Generates numbers that are all distinct from each other, but that are
  // designed to collide on the same buckets.
  constexpr size_t kShift = 8;  // Collide on the same 2^8 = 256 buckets.
  for (size_t i = 0; i < kNumSamples; i++) {
    size_t bucket = i & ((1 << kShift) - 1);  // [0, 256].
    size_t multiplier = i >> kShift;          // 0,0,0... 1,1,1..., 2,2,2...
    size_t key = 8192 * multiplier + bucket;
    keys.push_back(key);
  }
  for (auto _ : state) {
    MapType mapz;
    for (const size_t key : keys)
      mapz.insert({key, key});
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["insertions"] = Counter(static_cast<double>(keys.size()),
                                         Counter::kIsIterationInvariantRate);
}

// Unlike the previous benchmark, here integers don't just collide on the same
// buckets, they have a large number of duplicates with the same values.
// Most of those insertions are no-ops. This tests the ability of the hashmap
// to deal with cases where the hash function is good but the insertions contain
// lot of dupes (e.g. dealing with pids).
template <typename MapType>
void BM_HashMap_InsertDupeInts(benchmark::State& state) {
  std::vector<size_t> keys;
  const size_t kNumSamples = num_samples();

  for (size_t i = 0; i < kNumSamples; i++)
    keys.push_back(i % 16384);

  for (auto _ : state) {
    MapType mapz;
    for (const size_t key : keys)
      mapz.insert({key, key});
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["insertions"] = Counter(static_cast<double>(keys.size()),
                                         Counter::kIsIterationInvariantRate);
}

template <typename MapType>
void BM_HashMap_LookupRandInts(benchmark::State& state) {
  std::minstd_rand0 rng(0);
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(rng());

  MapType mapz;
  for (const size_t key : keys)
    mapz.insert({key, key});

  for (auto _ : state) {
    int64_t total = 0;
    for (const size_t key : keys) {
      auto it = mapz.find(static_cast<uint64_t>(key));
      PERFETTO_CHECK(it != mapz.end());
      total += it->second;
    }
    benchmark::DoNotOptimize(total);
    benchmark::ClobberMemory();
    state.counters["sum"] = Counter(static_cast<double>(total));
  }
  state.counters["lookups"] = Counter(static_cast<double>(keys.size()),
                                      Counter::kIsIterationInvariantRate);
}

template <typename MapType>
void BM_HashMap_RandomIntsClear(benchmark::State& state) {
  std::minstd_rand0 rng(0);
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(rng());

  MapType mapz;
  for (const size_t key : keys)
    mapz.insert({key, key});

  for (auto _ : state) {
    mapz.clear();
    benchmark::ClobberMemory();
  }
  state.counters["operations"] = Counter(static_cast<double>(keys.size()),
                                         Counter::kIsIterationInvariantRate);
}

// Benchmark with varying map sizes to test cache behavior
template <typename MapType>
void BM_HashMap_InsertVaryingSize(benchmark::State& state) {
  const size_t size = static_cast<size_t>(state.range(0));
  std::minstd_rand0 rng(0);
  std::vector<size_t> keys;
  keys.reserve(size);
  for (size_t i = 0; i < size; i++)
    keys.push_back(rng());

  for (auto _ : state) {
    MapType mapz;
    for (const auto key : keys)
      mapz.insert({key, key});
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["insertions"] = Counter(static_cast<double>(keys.size()),
                                         Counter::kIsIterationInvariantRate);
}

// Benchmark lookups with varying miss rates
template <typename MapType>
void BM_HashMap_LookupWithMisses(benchmark::State& state) {
  const int miss_percent = static_cast<int>(state.range(0));
  std::minstd_rand0 rng(0);
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(rng());

  MapType mapz;
  for (const size_t key : keys)
    mapz.insert({key, key});

  // Generate lookup keys: some hits, some misses
  std::vector<size_t> lookup_keys;
  lookup_keys.reserve(num_samples());
  std::minstd_rand0 rng2(42);
  for (size_t i = 0; i < num_samples(); i++) {
    if (static_cast<int>(rng2() % 100) < miss_percent) {
      // Generate a key that doesn't exist (use high bit to avoid collision)
      lookup_keys.push_back(rng2() | (1ULL << 63));
    } else {
      lookup_keys.push_back(keys[rng2() % keys.size()]);
    }
  }

  for (auto _ : state) {
    int64_t found = 0;
    for (const size_t key : lookup_keys) {
      auto it = mapz.find(key);
      if (it != mapz.end())
        found++;
    }
    benchmark::DoNotOptimize(found);
    benchmark::ClobberMemory();
  }
  state.counters["lookups"] = Counter(static_cast<double>(lookup_keys.size()),
                                      Counter::kIsIterationInvariantRate);
}

// Benchmark with sequential keys (common pattern like row IDs)
template <typename MapType>
void BM_HashMap_InsertSequentialInts(benchmark::State& state) {
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(i);

  for (auto _ : state) {
    MapType mapz;
    for (const auto key : keys)
      mapz.insert({key, key});
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["insertions"] = Counter(static_cast<double>(keys.size()),
                                         Counter::kIsIterationInvariantRate);
}

// Benchmark lookup of sequential keys
template <typename MapType>
void BM_HashMap_LookupSequentialInts(benchmark::State& state) {
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(i);

  MapType mapz;
  for (const size_t key : keys)
    mapz.insert({key, key});

  for (auto _ : state) {
    int64_t total = 0;
    for (const size_t key : keys) {
      auto it = mapz.find(key);
      PERFETTO_CHECK(it != mapz.end());
      total += it->second;
    }
    benchmark::DoNotOptimize(total);
    benchmark::ClobberMemory();
  }
  state.counters["lookups"] = Counter(static_cast<double>(keys.size()),
                                      Counter::kIsIterationInvariantRate);
}

}  // namespace

// =============================================================================
// Type aliases for benchmarks
// =============================================================================

// Category 1: Default hash functions (realistic 1:1 comparison)
// Each map uses its native/default hash function
using Ours_Default =
    Ours<uint64_t, uint64_t, base::MurmurHash<uint64_t>, LinearProbe>;
using OursV2_Default =
    OursV2<uint64_t, uint64_t, base::MurmurHash<uint64_t>, LinearProbe>;
using StdUnorderedMap_Default =
    std::unordered_map<uint64_t, uint64_t>;  // std::hash
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
using AbslFlatHashMap_Default =
    absl::flat_hash_map<uint64_t, uint64_t>;  // absl hash
#endif

// Category 2: MurmurHash for all (compare pure map performance, same hash)
using StdUnorderedMap_Murmur =
    std::unordered_map<uint64_t, uint64_t, base::MurmurHash<uint64_t>>;
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
using AbslFlatHashMap_Murmur =
    absl::flat_hash_map<uint64_t, uint64_t, base::MurmurHash<uint64_t>>;
#endif

// Category 3: AlreadyHashed (pure map performance, no hash cost)
using Ours_PreHashed =
    Ours<uint64_t, uint64_t, AlreadyHashed<uint64_t>, LinearProbe>;
using OursV2_PreHashed =
    OursV2<uint64_t, uint64_t, AlreadyHashed<uint64_t>, LinearProbe>;
using StdUnorderedMap_PreHashed =
    std::unordered_map<uint64_t, uint64_t, AlreadyHashed<uint64_t>>;
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
using AbslFlatHashMap_PreHashed =
    absl::flat_hash_map<uint64_t, uint64_t, AlreadyHashed<uint64_t>>;
#endif

// Third party libs (default hash)
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
using RobinMap_Default = tsl::robin_map<uint64_t, uint64_t>;
using FollyF14_Default = folly::F14FastMap<uint64_t, uint64_t>;
using RobinMap_Murmur =
    tsl::robin_map<uint64_t, uint64_t, base::MurmurHash<uint64_t>>;
using FollyF14_Murmur =
    folly::F14FastMap<uint64_t, uint64_t, base::MurmurHash<uint64_t>>;
using RobinMap_PreHashed =
    tsl::robin_map<uint64_t, uint64_t, AlreadyHashed<uint64_t>>;
using FollyF14_PreHashed =
    folly::F14FastMap<uint64_t, uint64_t, AlreadyHashed<uint64_t>>;
#endif

// =============================================================================
// TraceStrings benchmark (uses pre-hashed keys - simulates string interning)
// =============================================================================
BENCHMARK(BM_HashMap_InsertTraceStrings_AppendOnly);
BENCHMARK_TEMPLATE(BM_HashMap_InsertTraceStrings, Ours_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_InsertTraceStrings, OursV2_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_InsertTraceStrings, StdUnorderedMap_PreHashed);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertTraceStrings, AbslFlatHashMap_PreHashed);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertTraceStrings, RobinMap_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_InsertTraceStrings, FollyF14_PreHashed);
#endif

// =============================================================================
// TraceTids benchmark (uses MurmurHash - realistic workload)
// =============================================================================
using Ours_Tid = Ours<int, uint64_t, base::MurmurHash<int>, LinearProbe>;
using OursV2_Tid = OursV2<int, uint64_t, base::MurmurHash<int>, LinearProbe>;
using StdUnorderedMap_Tid =
    std::unordered_map<int, uint64_t, base::MurmurHash<int>>;
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
using AbslFlatHashMap_Tid =
    absl::flat_hash_map<int, uint64_t, base::MurmurHash<int>>;
#endif
BENCHMARK_TEMPLATE(BM_HashMap_TraceTids, Ours_Tid);
BENCHMARK_TEMPLATE(BM_HashMap_TraceTids, OursV2_Tid);
BENCHMARK_TEMPLATE(BM_HashMap_TraceTids, StdUnorderedMap_Tid);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_TraceTids, AbslFlatHashMap_Tid);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
using RobinMap_Tid = tsl::robin_map<int, uint64_t, base::MurmurHash<int>>;
using FollyF14_Tid = folly::F14FastMap<int, uint64_t, base::MurmurHash<int>>;
BENCHMARK_TEMPLATE(BM_HashMap_TraceTids, RobinMap_Tid);
BENCHMARK_TEMPLATE(BM_HashMap_TraceTids, FollyF14_Tid);
#endif

// =============================================================================
// InsertRandInts benchmarks (Default, Murmur, PreHashed)
// =============================================================================
// Default hash
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, OursV2_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, AbslFlatHashMap_Default);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, FollyF14_Default);
#endif
// MurmurHash
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, StdUnorderedMap_Murmur);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, AbslFlatHashMap_Murmur);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, RobinMap_Murmur);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, FollyF14_Murmur);
#endif
// PreHashed
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, Ours_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, OursV2_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, StdUnorderedMap_PreHashed);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, AbslFlatHashMap_PreHashed);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, RobinMap_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_InsertRandInts, FollyF14_PreHashed);
#endif

// =============================================================================
// LookupRandInts benchmarks (Default, Murmur, PreHashed)
// =============================================================================
// Default hash
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, OursV2_Default);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, AbslFlatHashMap_Default);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, FollyF14_Default);
#endif
// MurmurHash
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, StdUnorderedMap_Murmur);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, AbslFlatHashMap_Murmur);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, RobinMap_Murmur);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, FollyF14_Murmur);
#endif
// PreHashed
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, Ours_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, OursV2_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, StdUnorderedMap_PreHashed);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, AbslFlatHashMap_PreHashed);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, RobinMap_PreHashed);
BENCHMARK_TEMPLATE(BM_HashMap_LookupRandInts, FollyF14_PreHashed);
#endif

// =============================================================================
// InsertCollidingInts benchmarks (Default only - pathological case)
// =============================================================================
BENCHMARK_TEMPLATE(BM_HashMap_InsertCollidingInts, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertCollidingInts, OursV2_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertCollidingInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertCollidingInts, AbslFlatHashMap_Default);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertCollidingInts, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertCollidingInts, FollyF14_Default);
#endif

// =============================================================================
// InsertDupeInts benchmarks (Default only - realistic workload)
// =============================================================================
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, OursV2_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, AbslFlatHashMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, AbslFlatHashMap_Murmur);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertDupeInts, FollyF14_Default);
#endif

BENCHMARK_TEMPLATE(BM_HashMap_RandomIntsClear, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_RandomIntsClear, OursV2_Default);

// Heterogeneous lookup benchmarks
template <typename MapType>
void BM_HashMap_HeterogeneousLookup_String(benchmark::State& state) {
  std::vector<std::string> keys;
  const size_t kNumSamples = num_samples();

  // Create a set of unique string keys
  for (size_t i = 0; i < kNumSamples; i++) {
    keys.push_back("key_" + std::to_string(i));
  }

  // Build the map
  MapType mapz;
  for (const auto& key : keys) {
    mapz.insert({key, 42});
  }

  // Benchmark looking up using string_view (heterogeneous lookup)
  for (auto _ : state) {
    int64_t total = 0;
    for (const auto& key : keys) {
      std::string_view key_view = key;
      auto it = mapz.find(key_view);
      if (it != mapz.end()) {
        total += it->second;
      }
    }
    benchmark::DoNotOptimize(total);
    benchmark::ClobberMemory();
  }
  state.counters["lookups"] = Counter(static_cast<double>(keys.size()),
                                      Counter::kIsIterationInvariantRate);
}

template <typename MapType>
void BM_HashMap_RegularLookup_String(benchmark::State& state) {
  std::vector<std::string> keys;
  const size_t kNumSamples = num_samples();

  // Create a set of unique string keys
  for (size_t i = 0; i < kNumSamples; i++) {
    keys.push_back("key_" + std::to_string(i));
  }

  // Build the map
  MapType mapz;
  for (const auto& key : keys) {
    mapz.insert({key, 42});
  }

  // Benchmark looking up using std::string (regular lookup)
  for (auto _ : state) {
    int64_t total = 0;
    for (const auto& key : keys) {
      auto it = mapz.find(key);
      if (it != mapz.end()) {
        total += it->second;
      }
    }
    benchmark::DoNotOptimize(total);
    benchmark::ClobberMemory();
  }
  state.counters["lookups"] = Counter(static_cast<double>(keys.size()),
                                      Counter::kIsIterationInvariantRate);
}

// String benchmarks - each map uses its default hash function
using Ours_String =
    Ours<std::string, uint64_t, base::MurmurHash<std::string>, LinearProbe>;
using OursV2_String =
    OursV2<std::string, uint64_t, base::MurmurHash<std::string>, LinearProbe>;
using StdUnorderedMap_String =
    std::unordered_map<std::string, uint64_t>;  // std::hash
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
using AbslFlatHashMap_String =
    absl::flat_hash_map<std::string, uint64_t>;  // absl hash
// Same hash (MurmurHash) for fair map-only comparison
using AbslFlatHashMap_String_Murmur =
    absl::flat_hash_map<std::string, uint64_t, base::MurmurHash<std::string>>;
#endif

BENCHMARK_TEMPLATE(BM_HashMap_HeterogeneousLookup_String, Ours_String);
BENCHMARK_TEMPLATE(BM_HashMap_HeterogeneousLookup_String, OursV2_String);
BENCHMARK_TEMPLATE(BM_HashMap_RegularLookup_String, Ours_String);
BENCHMARK_TEMPLATE(BM_HashMap_RegularLookup_String, OursV2_String);
BENCHMARK_TEMPLATE(BM_HashMap_RegularLookup_String, StdUnorderedMap_String);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_RegularLookup_String, AbslFlatHashMap_String);
BENCHMARK_TEMPLATE(BM_HashMap_RegularLookup_String,
                   AbslFlatHashMap_String_Murmur);
#endif

// =============================================================================
// Varying size benchmarks (test cache behavior at different sizes)
// =============================================================================
BENCHMARK_TEMPLATE(BM_HashMap_InsertVaryingSize, Ours_Default)
    ->Apply(VaryingSizeArgs);
BENCHMARK_TEMPLATE(BM_HashMap_InsertVaryingSize, OursV2_Default)
    ->Apply(VaryingSizeArgs);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertVaryingSize, AbslFlatHashMap_Default)
    ->Apply(VaryingSizeArgs);
#endif

// =============================================================================
// Lookup with misses benchmarks (0%, 50%, 100% miss rate)
// =============================================================================
BENCHMARK_TEMPLATE(BM_HashMap_LookupWithMisses, Ours_Default)
    ->Apply(MissRateArgs);
BENCHMARK_TEMPLATE(BM_HashMap_LookupWithMisses, OursV2_Default)
    ->Apply(MissRateArgs);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_LookupWithMisses, AbslFlatHashMap_Default)
    ->Apply(MissRateArgs);
#endif

// =============================================================================
// Sequential key benchmarks (common pattern like row IDs)
// =============================================================================
BENCHMARK_TEMPLATE(BM_HashMap_InsertSequentialInts, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertSequentialInts, OursV2_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertSequentialInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertSequentialInts, AbslFlatHashMap_Default);
#endif

BENCHMARK_TEMPLATE(BM_HashMap_LookupSequentialInts, Ours_Default);
BENCHMARK_TEMPLATE(BM_HashMap_LookupSequentialInts, OursV2_Default);
BENCHMARK_TEMPLATE(BM_HashMap_LookupSequentialInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_LookupSequentialInts, AbslFlatHashMap_Default);
#endif

// =============================================================================
// Erase benchmarks (test deletion performance)
// =============================================================================

// Benchmark erasing all keys from a fully populated map (random order).
// This tests the raw erase throughput.
template <typename MapType>
void BM_HashMap_EraseRandInts(benchmark::State& state) {
  std::minstd_rand0 rng(0);
  std::vector<size_t> keys;
  keys.reserve(num_samples());
  for (size_t i = 0; i < num_samples(); i++)
    keys.push_back(rng());

  // Shuffle keys for random erase order
  std::vector<size_t> erase_order = keys;
  std::shuffle(erase_order.begin(), erase_order.end(), std::minstd_rand0(42));

  for (auto _ : state) {
    state.PauseTiming();
    MapType mapz;
    for (const auto key : keys)
      mapz.insert({key, key});
    state.ResumeTiming();

    for (const auto key : erase_order)
      mapz.erase(key);

    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["erases"] = Counter(static_cast<double>(keys.size()),
                                     Counter::kIsIterationInvariantRate);
}

// Benchmark interleaved insert and erase operations.
// This simulates a map that grows and shrinks over time.
template <typename MapType>
void BM_HashMap_InsertEraseInterleaved(benchmark::State& state) {
  std::minstd_rand0 rng(0);
  const size_t kNumOps = num_samples();

  // Generate operations: insert first half, then alternate insert/erase
  std::vector<std::pair<bool, size_t>> ops;  // true = insert, false = erase
  ops.reserve(kNumOps);

  std::vector<size_t> inserted_keys;
  for (size_t i = 0; i < kNumOps; i++) {
    size_t key = rng();
    if (i < kNumOps / 2 || inserted_keys.empty()) {
      // Insert phase or need to insert because map is empty
      ops.emplace_back(true, key);
      inserted_keys.push_back(key);
    } else if (rng() % 2 == 0) {
      // Insert
      ops.emplace_back(true, key);
      inserted_keys.push_back(key);
    } else {
      // Erase a random existing key
      size_t idx = rng() % inserted_keys.size();
      ops.emplace_back(false, inserted_keys[idx]);
      // Don't remove from inserted_keys to keep it simple
      // (erasing non-existent key is a no-op)
    }
  }

  for (auto _ : state) {
    MapType mapz;
    for (const auto& [is_insert, key] : ops) {
      if (is_insert) {
        mapz.insert({key, key});
      } else {
        mapz.erase(key);
      }
    }
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["operations"] = Counter(static_cast<double>(ops.size()),
                                         Counter::kIsIterationInvariantRate);
}

// Benchmark erasing with tombstone buildup - repeatedly fill and partially
// erase to stress tombstone handling.
template <typename MapType>
void BM_HashMap_EraseTombstoneStress(benchmark::State& state) {
  std::minstd_rand0 rng(0);
  const size_t kMapSize = IsBenchmarkFunctionalOnly() ? 100 : 100000;
  const size_t kErasePercent = 50;  // Erase half the keys each round

  std::vector<size_t> keys;
  keys.reserve(kMapSize);
  for (size_t i = 0; i < kMapSize; i++)
    keys.push_back(rng());

  for (auto _ : state) {
    MapType mapz;
    // Initial fill
    for (const auto key : keys)
      mapz.insert({key, key});

    // Multiple rounds of partial erase + refill (creates tombstones)
    for (size_t round = 0; round < 5; round++) {
      // Erase half
      std::minstd_rand0 erase_rng(round);
      for (const auto key : keys) {
        if (erase_rng() % 100 < kErasePercent)
          mapz.erase(key);
      }
      // Refill
      for (const auto key : keys)
        mapz.insert({key, key});
    }
    benchmark::DoNotOptimize(mapz);
    benchmark::ClobberMemory();
  }
  state.counters["map_size"] =
      Counter(static_cast<double>(kMapSize), Counter::kIsIterationInvariant);
}

// Wrapper types that support erase (exclude AppendOnly Ours which doesn't)
// Note: Ours (FlatHashMap V1 non-append-only) supports erase
using Ours_Erasable =
    Ours<uint64_t, uint64_t, base::MurmurHash<uint64_t>, LinearProbe>;
using OursV2_Erasable =
    OursV2<uint64_t, uint64_t, base::MurmurHash<uint64_t>, LinearProbe>;

BENCHMARK_TEMPLATE(BM_HashMap_EraseRandInts, Ours_Erasable);
BENCHMARK_TEMPLATE(BM_HashMap_EraseRandInts, OursV2_Erasable);
BENCHMARK_TEMPLATE(BM_HashMap_EraseRandInts, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_EraseRandInts, AbslFlatHashMap_Default);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_EraseRandInts, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_EraseRandInts, FollyF14_Default);
#endif

BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, Ours_Erasable);
BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, OursV2_Erasable);
BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, AbslFlatHashMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, AbslFlatHashMap_Murmur);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_InsertEraseInterleaved, FollyF14_Default);
#endif

BENCHMARK_TEMPLATE(BM_HashMap_EraseTombstoneStress, Ours_Erasable);
BENCHMARK_TEMPLATE(BM_HashMap_EraseTombstoneStress, OursV2_Erasable);
BENCHMARK_TEMPLATE(BM_HashMap_EraseTombstoneStress, StdUnorderedMap_Default);
#if defined(PERFETTO_HASH_MAP_COMPARE_ABSL)
BENCHMARK_TEMPLATE(BM_HashMap_EraseTombstoneStress, AbslFlatHashMap_Default);
#endif
#if defined(PERFETTO_HASH_MAP_COMPARE_THIRD_PARTY_LIBS)
BENCHMARK_TEMPLATE(BM_HashMap_EraseTombstoneStress, RobinMap_Default);
BENCHMARK_TEMPLATE(BM_HashMap_EraseTombstoneStress, FollyF14_Default);
#endif
