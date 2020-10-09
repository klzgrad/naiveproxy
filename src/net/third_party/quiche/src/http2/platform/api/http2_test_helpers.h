#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_TEST_HELPERS_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_TEST_HELPERS_H_

// Provides VERIFY_* macros, similar to EXPECT_* and ASSERT_*, but they return
// an AssertionResult if the condition is not satisfied.
#include "net/http2/platform/impl/http2_test_helpers_impl.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"  // For AssertionSuccess

#pragma clang diagnostic pop

#define VERIFY_AND_RETURN_SUCCESS(expression) \
  {                                           \
    VERIFY_SUCCESS(expression);               \
    return ::testing::AssertionSuccess();     \
  }

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_TEST_HELPERS_H_
