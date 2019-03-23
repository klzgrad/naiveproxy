/* Copyright (c) 2018, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include <openssl/rand.h>

#include "test/abi_test.h"

#if defined(OPENSSL_WINDOWS)
#include <windows.h>
#endif


static bool test_function_was_called = false;
static void TestFunction(int a1, int a2, int a3, int a4, int a5, int a6, int a7,
                         int a8, int a9, int a10) {
  test_function_was_called = true;
  EXPECT_EQ(1, a1);
  EXPECT_EQ(2, a2);
  EXPECT_EQ(3, a3);
  EXPECT_EQ(4, a4);
  EXPECT_EQ(5, a5);
  EXPECT_EQ(6, a6);
  EXPECT_EQ(7, a7);
  EXPECT_EQ(8, a8);
  EXPECT_EQ(9, a9);
  EXPECT_EQ(10, a10);
}

TEST(ABITest, SanityCheck) {
  EXPECT_NE(0, CHECK_ABI(strcmp, "hello", "world"));

  test_function_was_called = false;
  CHECK_ABI(TestFunction, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
  EXPECT_TRUE(test_function_was_called);

#if defined(SUPPORTS_ABI_TEST)
  abi_test::internal::CallerState state;
  RAND_bytes(reinterpret_cast<uint8_t *>(&state), sizeof(state));
  const char *arg1 = "hello", *arg2 = "world";
  crypto_word_t argv[2] = {
      reinterpret_cast<crypto_word_t>(arg1),
      reinterpret_cast<crypto_word_t>(arg2),
  };
  CHECK_ABI(abi_test_trampoline, reinterpret_cast<crypto_word_t>(strcmp),
            &state, argv, 2);
#endif  // SUPPORTS_ABI_TEST
}

#if defined(OPENSSL_X86_64) && defined(SUPPORTS_ABI_TEST)
extern "C" {
void abi_test_clobber_rax(void);
void abi_test_clobber_rbx(void);
void abi_test_clobber_rcx(void);
void abi_test_clobber_rdx(void);
void abi_test_clobber_rsi(void);
void abi_test_clobber_rdi(void);
void abi_test_clobber_rbp(void);
void abi_test_clobber_r8(void);
void abi_test_clobber_r9(void);
void abi_test_clobber_r10(void);
void abi_test_clobber_r11(void);
void abi_test_clobber_r12(void);
void abi_test_clobber_r13(void);
void abi_test_clobber_r14(void);
void abi_test_clobber_r15(void);
void abi_test_clobber_xmm0(void);
void abi_test_clobber_xmm1(void);
void abi_test_clobber_xmm2(void);
void abi_test_clobber_xmm3(void);
void abi_test_clobber_xmm4(void);
void abi_test_clobber_xmm5(void);
void abi_test_clobber_xmm6(void);
void abi_test_clobber_xmm7(void);
void abi_test_clobber_xmm8(void);
void abi_test_clobber_xmm9(void);
void abi_test_clobber_xmm10(void);
void abi_test_clobber_xmm11(void);
void abi_test_clobber_xmm12(void);
void abi_test_clobber_xmm13(void);
void abi_test_clobber_xmm14(void);
void abi_test_clobber_xmm15(void);
}  // extern "C"

TEST(ABITest, X86_64) {
  // abi_test_trampoline hides unsaved registers from the caller, so we can
  // safely call the abi_test_clobber_* functions below.
  abi_test::internal::CallerState state;
  RAND_bytes(reinterpret_cast<uint8_t *>(&state), sizeof(state));
  CHECK_ABI(abi_test_trampoline,
            reinterpret_cast<crypto_word_t>(abi_test_clobber_rbx), &state,
            nullptr, 0);

  CHECK_ABI(abi_test_clobber_rax);
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_rbx), "");
  CHECK_ABI(abi_test_clobber_rcx);
  CHECK_ABI(abi_test_clobber_rdx);
#if defined(OPENSSL_WINDOWS)
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_rdi), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_rsi), "");
#else
  CHECK_ABI(abi_test_clobber_rdi);
  CHECK_ABI(abi_test_clobber_rsi);
#endif
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_rbp), "");
  CHECK_ABI(abi_test_clobber_r8);
  CHECK_ABI(abi_test_clobber_r9);
  CHECK_ABI(abi_test_clobber_r10);
  CHECK_ABI(abi_test_clobber_r11);
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_r12), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_r13), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_r14), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_r15), "");

  CHECK_ABI(abi_test_clobber_xmm0);
  CHECK_ABI(abi_test_clobber_xmm1);
  CHECK_ABI(abi_test_clobber_xmm2);
  CHECK_ABI(abi_test_clobber_xmm3);
  CHECK_ABI(abi_test_clobber_xmm4);
  CHECK_ABI(abi_test_clobber_xmm5);
#if defined(OPENSSL_WINDOWS)
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm6), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm7), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm8), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm9), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm10), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm11), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm12), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm13), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm14), "");
  EXPECT_NONFATAL_FAILURE(CHECK_ABI(abi_test_clobber_xmm15), "");
#else
  CHECK_ABI(abi_test_clobber_xmm6);
  CHECK_ABI(abi_test_clobber_xmm7);
  CHECK_ABI(abi_test_clobber_xmm8);
  CHECK_ABI(abi_test_clobber_xmm9);
  CHECK_ABI(abi_test_clobber_xmm10);
  CHECK_ABI(abi_test_clobber_xmm11);
  CHECK_ABI(abi_test_clobber_xmm12);
  CHECK_ABI(abi_test_clobber_xmm13);
  CHECK_ABI(abi_test_clobber_xmm14);
  CHECK_ABI(abi_test_clobber_xmm15);
#endif
}

#if defined(OPENSSL_WINDOWS)
static void ThrowWindowsException() {
  DebugBreak();
}

static void ExceptionTest() {
  bool handled = false;
  __try {
    CHECK_ABI(ThrowWindowsException);
  } __except (GetExceptionCode() == EXCEPTION_BREAKPOINT
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    handled = true;
  }

  EXPECT_TRUE(handled);
}

// Test that the trampoline's SEH metadata works.
TEST(ABITest, TrampolineSEH) {
  // Wrap the test in |CHECK_ABI|, to confirm the register-restoring annotations
  // were correct.
  CHECK_ABI(ExceptionTest);
}
#endif  // OPENSSL_WINDOWS

#endif   // OPENSSL_X86_64 && SUPPORTS_ABI_TEST
