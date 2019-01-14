#!/bin/bash

# Runs a benchmark inside a gbash unit test, where the benchmark binary must
# end with _perftest and the gbash unit test must end with _perftest_test,
# and the two must share the same prefix before the above suffixes.

readonly THIS_DIR=$(cd "$(dirname "$0")" && pwd)
readonly THIS_FILE=$(basename "$0")

# Pass unknown flags to main via GBASH_ARGV, along with non-flag args.
GBASH_PASSTHROUGH_UNKNOWN_FLAGS=1

source gbash.sh || exit
source module gbash_unit.sh

# Override the default benchmark_max_iters so that this test normally just does
# a modest number of iterations in order to test that the benchmark works, and
# allow it to be overridden on the command-line (or in the blaze file).
DEFINE_int benchmark_max_iters 100 \
    "Maximum number of iterations per benchmark"

DEFINE_int benchmark_min_iters 10 "Minimum number of iterations per benchmark"

DEFINE_int benchmark_repetitions 1 "Repetitions of each benchmark"

DEFINE_int items_per_iteration 1 \
    "Items to be decoded during each benchmark iteration"

function test::perftest::runs() {
  local -r WITHOUT_PERFTEST_TEST=${THIS_FILE%_perftest_test}
  CHECK_STRNE "$WITHOUT_PERFTEST_TEST" "$THIS_FILE"

  local -r PERFTEST="$THIS_DIR/${WITHOUT_PERFTEST_TEST}_perftest"
  CHECK_FILE_EXISTS "$PERFTEST"
  CHECK_FILE_EXECUTABLE "$PERFTEST"

  local -a CMD=(${PERFTEST})
  CMD+=(--benchmark_max_iters=$FLAGS_benchmark_max_iters)
  CMD+=(--benchmark_min_iters=$FLAGS_benchmark_min_iters)
  CMD+=(--benchmark_repetitions=$FLAGS_benchmark_repetitions)
  CMD+=(--items_per_iteration=$FLAGS_items_per_iteration)
  CMD+=(--undefok=$(gbash::join ',' "${FLAGS_undefok[@]}"))
  CMD+=("${GBASH_ARGV[@]}")

  echo "CMD=${CMD[*]}"

  "${CMD[@]}"
  RESULT=$?
  EXPECT_EQ 0 $RESULT
}

gbash::unit::main "$@"
