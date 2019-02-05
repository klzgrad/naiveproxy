#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_TEST_HELPERS_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_TEST_HELPERS_H_

// Provides VERIFY_* macros, similar to EXPECT_* and ASSERT_*, but they return
// an AssertionResult if the condition is not satisfied.
#include "net/third_party/http2/platform/impl/http2_test_helpers_impl.h"

#include "testing/gtest/include/gtest/gtest.h"  // For AssertionSuccess

#define VERIFY_AND_RETURN_SUCCESS(expression) \
  {                                           \
    VERIFY_SUCCESS(expression);               \
    return ::testing::AssertionSuccess();     \
  }

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_TEST_HELPERS_H_
