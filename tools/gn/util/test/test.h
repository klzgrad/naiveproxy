// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_TEST_TEST_H_
#define UTIL_TEST_TEST_H_

#include <string.h>

#include <sstream>
#include <string>

// This is a minimal googletest-like testing framework. It's originally derived
// from Ninja's src/test.h. You might prefer that one if you have different
// tradeoffs (in particular, if you don't need to stream message to assertion
// failures, Ninja's is a bit simpler.)
namespace testing {

class Test {
 public:
  Test() : failed_(false) {}
  virtual ~Test() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
  virtual void Run() = 0;

  bool Failed() const { return failed_; }

 private:
  friend class TestResult;

  bool failed_;
  int assertion_failures_;
};

extern testing::Test* g_current_test;

class TestResult {
 public:
  TestResult(bool condition, const char* error)
      : condition_(condition), error_(error) {
    if (!condition)
      g_current_test->failed_ = true;
  }

  operator bool() const { return condition_; }
  const char* error() const { return error_; }

 private:
  bool condition_;
  const char* error_;
};

class Message {
 public:
  Message() {}
  ~Message() { printf("%s\n\n", ss_.str().c_str()); }

  template <typename T>
  inline Message& operator<<(const T& val) {
    ss_ << val;
    return *this;
  }

 private:
  std::stringstream ss_;
};

class AssertHelper {
 public:
  AssertHelper(const char* file, int line, const TestResult& test_result)
      : file_(file), line_(line), error_(test_result.error()) {}

  void operator=(const Message& message) const {
    printf("\n*** FAILURE %s:%d: %s\n", file_, line_, error_);
  }

 private:
  const char* file_;
  int line_;
  const char* error_;
};

}  // namespace testing

void RegisterTest(testing::Test* (*)(), const char*);

#define TEST_F_(x, y, name)                                                    \
  struct y : public x {                                                        \
    static testing::Test* Create() { return testing::g_current_test = new y; } \
    virtual void Run();                                                        \
  };                                                                           \
  struct Register##y {                                                         \
    Register##y() { RegisterTest(y::Create, name); }                           \
  };                                                                           \
  Register##y g_register_##y;                                                  \
  void y::Run()

#define TEST_F(x, y) TEST_F_(x, x##y, #x "." #y)
#define TEST(x, y) TEST_F_(testing::Test, x##y, #x "." #y)

#define FRIEND_TEST(x, y) friend class x##y

// Some compilers emit a warning if nested "if" statements are followed by an
// "else" statement and braces are not used to explicitly disambiguate the
// "else" binding.  This leads to problems with code like:
//
//   if (something)
//     ASSERT_TRUE(condition) << "Some message";
#define TEST_AMBIGUOUS_ELSE_BLOCKER_ \
  switch (0)                         \
  case 0:                            \
  default:

#define TEST_ASSERT_(expression, on_failure)                  \
  TEST_AMBIGUOUS_ELSE_BLOCKER_                                \
  if (const ::testing::TestResult test_result = (expression)) \
    ;                                                         \
  else                                                        \
    on_failure(test_result)

#define TEST_NONFATAL_FAILURE_(message) \
  ::testing::AssertHelper(__FILE__, __LINE__, message) = ::testing::Message()

#define TEST_FATAL_FAILURE_(message)                            \
  return ::testing::AssertHelper(__FILE__, __LINE__, message) = \
             ::testing::Message()

#define EXPECT_EQ(a, b)                                     \
  TEST_ASSERT_(::testing::TestResult(a == b, #a " == " #b), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_NE(a, b)                                     \
  TEST_ASSERT_(::testing::TestResult(a != b, #a " != " #b), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_LT(a, b)                                   \
  TEST_ASSERT_(::testing::TestResult(a < b, #a " < " #b), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_GT(a, b)                                   \
  TEST_ASSERT_(::testing::TestResult(a > b, #a " > " #b), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_LE(a, b)                                     \
  TEST_ASSERT_(::testing::TestResult(a <= b, #a " <= " #b), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_GE(a, b)                                     \
  TEST_ASSERT_(::testing::TestResult(a >= b, #a " >= " #b), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_TRUE(a)                                          \
  TEST_ASSERT_(::testing::TestResult(static_cast<bool>(a), #a), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_FALSE(a)                                          \
  TEST_ASSERT_(::testing::TestResult(!static_cast<bool>(a), #a), \
               TEST_NONFATAL_FAILURE_)

#define EXPECT_STREQ(a, b)                                                \
  TEST_ASSERT_(::testing::TestResult(strcmp(a, b) == 0, #a " str== " #b), \
               TEST_NONFATAL_FAILURE_)

#define ASSERT_EQ(a, b) \
  TEST_ASSERT_(::testing::TestResult(a == b, #a " == " #b), TEST_FATAL_FAILURE_)

#define ASSERT_NE(a, b) \
  TEST_ASSERT_(::testing::TestResult(a != b, #a " != " #b), TEST_FATAL_FAILURE_)

#define ASSERT_LT(a, b) \
  TEST_ASSERT_(::testing::TestResult(a < b, #a " < " #b), TEST_FATAL_FAILURE_)

#define ASSERT_GT(a, b) \
  TEST_ASSERT_(::testing::TestResult(a > b, #a " > " #b), TEST_FATAL_FAILURE_)

#define ASSERT_LE(a, b) \
  TEST_ASSERT_(::testing::TestResult(a <= b, #a " <= " #b), TEST_FATAL_FAILURE_)

#define ASSERT_GE(a, b) \
  TEST_ASSERT_(::testing::TestResult(a >= b, #a " >= " #b), TEST_FATAL_FAILURE_)

#define ASSERT_TRUE(a)                                          \
  TEST_ASSERT_(::testing::TestResult(static_cast<bool>(a), #a), \
               TEST_FATAL_FAILURE_)

#define ASSERT_FALSE(a)                                          \
  TEST_ASSERT_(::testing::TestResult(!static_cast<bool>(a), #a), \
               TEST_FATAL_FAILURE_)

#define ASSERT_STREQ(a, b)                                                \
  TEST_ASSERT_(::testing::TestResult(strcmp(a, b) == 0, #a " str== " #b), \
               TEST_FATAL_FAILURE_)

#endif  // UTIL_TEST_TEST_H_
