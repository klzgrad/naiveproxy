// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_LOGGING_H_
#define BASE_LOGGING_H_

#include <stddef.h>

#include <cassert>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/strings/string_piece_forward.h"
#include "base/template_util.h"
#include "util/build_config.h"

//
// Optional message capabilities
// -----------------------------
// Assertion failed messages and fatal errors are displayed in a dialog box
// before the application exits. However, running this UI creates a message
// loop, which causes application messages to be processed and potentially
// dispatched to existing application windows. Since the application is in a
// bad state when this assertion dialog is displayed, these messages may not
// get processed and hang the dialog, or the application might go crazy.
//
// Therefore, it can be beneficial to display the error dialog in a separate
// process from the main application. When the logging system needs to display
// a fatal error dialog box, it will look for a program called
// "DebugMessage.exe" in the same directory as the application executable. It
// will run this application with the message as the command line, and will
// not include the name of the application as is traditional for easier
// parsing.
//
// The code for DebugMessage.exe is only one line. In WinMain, do:
//   MessageBox(NULL, GetCommandLineW(), L"Fatal Error", 0);
//
// If DebugMessage.exe is not found, the logging code will use a normal
// MessageBox, potentially causing the problems discussed above.

// Instructions
// ------------
//
// Make a bunch of macros for logging.  The way to log things is to stream
// things to LOG(<a particular severity level>).  E.g.,
//
//   LOG(INFO) << "Found " << num_cookies << " cookies";
//
// You can also do conditional logging:
//
//   LOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
//
// The CHECK(condition) macro is active in both debug and release builds and
// effectively performs a LOG(FATAL) which terminates the process and
// generates a crashdump unless a debugger is attached.
//
// There are also "debug mode" logging macros like the ones above:
//
//   DLOG(INFO) << "Found cookies";
//
//   DLOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
//
// All "debug mode" logging is compiled away to nothing for non-debug mode
// compiles.  LOG_IF and development flags also work well together
// because the code can be compiled away sometimes.
//
// We also have
//
//   LOG_ASSERT(assertion);
//   DLOG_ASSERT(assertion);
//
// which is syntactic sugar for {,D}LOG_IF(FATAL, assert fails) << assertion;
//
// We also override the standard 'assert' to use 'DLOG_ASSERT'.
//
// Lastly, there is:
//
//   PLOG(ERROR) << "Couldn't do foo";
//   DPLOG(ERROR) << "Couldn't do foo";
//   PLOG_IF(ERROR, cond) << "Couldn't do foo";
//   DPLOG_IF(ERROR, cond) << "Couldn't do foo";
//   PCHECK(condition) << "Couldn't do foo";
//   DPCHECK(condition) << "Couldn't do foo";
//
// which append the last system error to the message in string form (taken from
// GetLastError() on Windows and errno on POSIX).
//
// The supported severity levels for macros that allow you to specify one
// are (in increasing order of severity) INFO, WARNING, ERROR, and FATAL.
//
// Very important: logging a message at the FATAL severity level causes
// the program to terminate (after the message is logged).
//
// There is the special severity of DFATAL, which logs FATAL in debug mode,
// ERROR in normal mode.

namespace logging {

// Sets the log level. Anything at or above this level will be written to the
// log file/displayed to the user (if applicable). Anything below this level
// will be silently ignored. The log level defaults to 0 (everything is logged
// up to level INFO) if this function is not called.
void SetMinLogLevel(int level);

// Gets the current log level.
int GetMinLogLevel();

// Used by LOG_IS_ON to lazy-evaluate stream arguments.
bool ShouldCreateLogMessage(int severity);

// The ANALYZER_ASSUME_TRUE(bool arg) macro adds compiler-specific hints
// to Clang which control what code paths are statically analyzed,
// and is meant to be used in conjunction with assert & assert-like functions.
// The expression is passed straight through if analysis isn't enabled.
//
// ANALYZER_SKIP_THIS_PATH() suppresses static analysis for the current
// codepath and any other branching codepaths that might follow.
#if defined(__clang_analyzer__)

inline constexpr bool AnalyzerNoReturn() __attribute__((analyzer_noreturn)) {
  return false;
}

inline constexpr bool AnalyzerAssumeTrue(bool arg) {
  // AnalyzerNoReturn() is invoked and analysis is terminated if |arg| is
  // false.
  return arg || AnalyzerNoReturn();
}

#define ANALYZER_ASSUME_TRUE(arg) logging::AnalyzerAssumeTrue(!!(arg))
#define ANALYZER_SKIP_THIS_PATH() \
  static_cast<void>(::logging::AnalyzerNoReturn())
#define ANALYZER_ALLOW_UNUSED(var) static_cast<void>(var);

#else  // !defined(__clang_analyzer__)

#define ANALYZER_ASSUME_TRUE(arg) (arg)
#define ANALYZER_SKIP_THIS_PATH()
#define ANALYZER_ALLOW_UNUSED(var) static_cast<void>(var);

#endif  // defined(__clang_analyzer__)

typedef int LogSeverity;
const LogSeverity LOG_VERBOSE = -1;  // This is level 1 verbosity
// Note: the log severities are used to index into the array of names,
// see log_severity_names.
const LogSeverity LOG_INFO = 0;
const LogSeverity LOG_WARNING = 1;
const LogSeverity LOG_ERROR = 2;
const LogSeverity LOG_FATAL = 3;
const LogSeverity LOG_NUM_SEVERITIES = 4;

// LOG_DFATAL is LOG_FATAL in debug mode, ERROR in normal mode
#if defined(NDEBUG)
const LogSeverity LOG_DFATAL = LOG_ERROR;
#else
const LogSeverity LOG_DFATAL = LOG_FATAL;
#endif

// A few definitions of macros that don't generate much code. These are used
// by LOG() and LOG_IF, etc. Since these are used all over our code, it's
// better to have compact code for these operations.
#define COMPACT_GOOGLE_LOG_EX_INFO(ClassName, ...) \
  ::logging::ClassName(__FILE__, __LINE__, ::logging::LOG_INFO, ##__VA_ARGS__)
#define COMPACT_GOOGLE_LOG_EX_WARNING(ClassName, ...)              \
  ::logging::ClassName(__FILE__, __LINE__, ::logging::LOG_WARNING, \
                       ##__VA_ARGS__)
#define COMPACT_GOOGLE_LOG_EX_ERROR(ClassName, ...) \
  ::logging::ClassName(__FILE__, __LINE__, ::logging::LOG_ERROR, ##__VA_ARGS__)
#define COMPACT_GOOGLE_LOG_EX_FATAL(ClassName, ...) \
  ::logging::ClassName(__FILE__, __LINE__, ::logging::LOG_FATAL, ##__VA_ARGS__)
#define COMPACT_GOOGLE_LOG_EX_DFATAL(ClassName, ...) \
  ::logging::ClassName(__FILE__, __LINE__, ::logging::LOG_DFATAL, ##__VA_ARGS__)
#define COMPACT_GOOGLE_LOG_EX_DCHECK(ClassName, ...) \
  ::logging::ClassName(__FILE__, __LINE__, ::logging::LOG_DCHECK, ##__VA_ARGS__)

#define COMPACT_GOOGLE_LOG_INFO COMPACT_GOOGLE_LOG_EX_INFO(LogMessage)
#define COMPACT_GOOGLE_LOG_WARNING COMPACT_GOOGLE_LOG_EX_WARNING(LogMessage)
#define COMPACT_GOOGLE_LOG_ERROR COMPACT_GOOGLE_LOG_EX_ERROR(LogMessage)
#define COMPACT_GOOGLE_LOG_FATAL COMPACT_GOOGLE_LOG_EX_FATAL(LogMessage)
#define COMPACT_GOOGLE_LOG_DFATAL COMPACT_GOOGLE_LOG_EX_DFATAL(LogMessage)
#define COMPACT_GOOGLE_LOG_DCHECK COMPACT_GOOGLE_LOG_EX_DCHECK(LogMessage)

#if defined(OS_WIN)
// wingdi.h defines ERROR to be 0. When we call LOG(ERROR), it gets
// substituted with 0, and it expands to COMPACT_GOOGLE_LOG_0. To allow us
// to keep using this syntax, we define this macro to do the same thing
// as COMPACT_GOOGLE_LOG_ERROR, and also define ERROR the same way that
// the Windows SDK does for consistency.
#define ERROR 0
#define COMPACT_GOOGLE_LOG_EX_0(ClassName, ...) \
  COMPACT_GOOGLE_LOG_EX_ERROR(ClassName, ##__VA_ARGS__)
#define COMPACT_GOOGLE_LOG_0 COMPACT_GOOGLE_LOG_ERROR
// Needed for LOG_IS_ON(ERROR).
const LogSeverity LOG_0 = LOG_ERROR;
#endif

// As special cases, we can assume that LOG_IS_ON(FATAL) always holds. Also,
// LOG_IS_ON(DFATAL) always holds in debug mode. In particular, CHECK()s will
// always fire if they fail.
#define LOG_IS_ON(severity) \
  (::logging::ShouldCreateLogMessage(::logging::LOG_##severity))

// Helper macro which avoids evaluating the arguments to a stream if
// the condition doesn't hold. Condition is evaluated once and only once.
#define LAZY_STREAM(stream, condition) \
  !(condition) ? (void)0 : ::logging::LogMessageVoidify() & (stream)

// We use the preprocessor's merging operator, "##", so that, e.g.,
// LOG(INFO) becomes the token COMPACT_GOOGLE_LOG_INFO.  There's some funny
// subtle difference between ostream member streaming functions (e.g.,
// ostream::operator<<(int) and ostream non-member streaming functions
// (e.g., ::operator<<(ostream&, string&): it turns out that it's
// impossible to stream something like a string directly to an unnamed
// ostream. We employ a neat hack by calling the stream() member
// function of LogMessage which seems to avoid the problem.
#define LOG_STREAM(severity) COMPACT_GOOGLE_LOG_##severity.stream()

#define LOG(severity) LAZY_STREAM(LOG_STREAM(severity), LOG_IS_ON(severity))
#define LOG_IF(severity, condition) \
  LAZY_STREAM(LOG_STREAM(severity), LOG_IS_ON(severity) && (condition))

#define LOG_ASSERT(condition)                       \
  LOG_IF(FATAL, !(ANALYZER_ASSUME_TRUE(condition))) \
      << "Assert failed: " #condition ". "

#if defined(OS_WIN)
#define PLOG_STREAM(severity)                                           \
  COMPACT_GOOGLE_LOG_EX_##severity(Win32ErrorLogMessage,                \
                                   ::logging::GetLastSystemErrorCode()) \
      .stream()
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
#define PLOG_STREAM(severity)                                           \
  COMPACT_GOOGLE_LOG_EX_##severity(ErrnoLogMessage,                     \
                                   ::logging::GetLastSystemErrorCode()) \
      .stream()
#endif

#define PLOG(severity) LAZY_STREAM(PLOG_STREAM(severity), LOG_IS_ON(severity))

#define PLOG_IF(severity, condition) \
  LAZY_STREAM(PLOG_STREAM(severity), LOG_IS_ON(severity) && (condition))

extern std::ostream* g_swallow_stream;

// Note that g_swallow_stream is used instead of an arbitrary LOG() stream to
// avoid the creation of an object with a non-trivial destructor (LogMessage).
// On MSVC x86 (checked on 2015 Update 3), this causes a few additional
// pointless instructions to be emitted even at full optimization level, even
// though the : arm of the ternary operator is clearly never executed. Using a
// simpler object to be &'d with Voidify() avoids these extra instructions.
// Using a simpler POD object with a templated operator<< also works to avoid
// these instructions. However, this causes warnings on statically defined
// implementations of operator<<(std::ostream, ...) in some .cc files, because
// they become defined-but-unreferenced functions. A reinterpret_cast of 0 to an
// ostream* also is not suitable, because some compilers warn of undefined
// behavior.
#define EAT_STREAM_PARAMETERS \
  true ? (void)0              \
       : ::logging::LogMessageVoidify() & (*::logging::g_swallow_stream)

// Captures the result of a CHECK_EQ (for example) and facilitates testing as a
// boolean.
class CheckOpResult {
 public:
  // |message| must be non-null if and only if the check failed.
  CheckOpResult(std::string* message) : message_(message) {}
  // Returns true if the check succeeded.
  operator bool() const { return !message_; }
  // Returns the message.
  std::string* message() { return message_; }

 private:
  std::string* message_;
};

// Crashes in the fastest possible way with no attempt at logging.
// There are different constraints to satisfy here, see http://crbug.com/664209
// for more context:
// - The trap instructions, and hence the PC value at crash time, have to be
//   distinct and not get folded into the same opcode by the compiler.
//   On Linux/Android this is tricky because GCC still folds identical
//   asm volatile blocks. The workaround is generating distinct opcodes for
//   each CHECK using the __COUNTER__ macro.
// - The debug info for the trap instruction has to be attributed to the source
//   line that has the CHECK(), to make crash reports actionable. This rules
//   out the ability of using a inline function, at least as long as clang
//   doesn't support attribute(artificial).
// - Failed CHECKs should produce a signal that is distinguishable from an
//   invalid memory access, to improve the actionability of crash reports.
// - The compiler should treat the CHECK as no-return instructions, so that the
//   trap code can be efficiently packed in the prologue of the function and
//   doesn't interfere with the main execution flow.
// - When debugging, developers shouldn't be able to accidentally step over a
//   CHECK. This is achieved by putting opcodes that will cause a non
//   continuable exception after the actual trap instruction.
// - Don't cause too much binary bloat.
#if defined(COMPILER_GCC)

#if defined(ARCH_CPU_X86_FAMILY) && !defined(OS_NACL)
// int 3 will generate a SIGTRAP.
#define TRAP_SEQUENCE() \
  asm volatile(         \
      "int3; ud2; push %0;" ::"i"(static_cast<unsigned char>(__COUNTER__)))

#elif defined(ARCH_CPU_ARMEL) && !defined(OS_NACL)
// bkpt will generate a SIGBUS when running on armv7 and a SIGTRAP when running
// as a 32 bit userspace app on arm64. There doesn't seem to be any way to
// cause a SIGTRAP from userspace without using a syscall (which would be a
// problem for sandboxing).
#define TRAP_SEQUENCE() \
  asm volatile("bkpt #0; udf %0;" ::"i"(__COUNTER__ % 256))

#elif defined(ARCH_CPU_ARM64) && !defined(OS_NACL)
// This will always generate a SIGTRAP on arm64.
#define TRAP_SEQUENCE() \
  asm volatile("brk #0; hlt %0;" ::"i"(__COUNTER__ % 65536))

#else
// Crash report accuracy will not be guaranteed on other architectures, but at
// least this will crash as expected.
#define TRAP_SEQUENCE() __builtin_trap()
#endif  // ARCH_CPU_*

// CHECK() and the trap sequence can be invoked from a constexpr function.
// This could make compilation fail on GCC, as it forbids directly using inline
// asm inside a constexpr function. However, it allows calling a lambda
// expression including the same asm.
// The side effect is that the top of the stacktrace will not point to the
// calling function, but to this anonymous lambda. This is still useful as the
// full name of the lambda will typically include the name of the function that
// calls CHECK() and the debugger will still break at the right line of code.
#if !defined(__clang__)
#define WRAPPED_TRAP_SEQUENCE() \
  do {                          \
    [] { TRAP_SEQUENCE(); }();  \
  } while (false)
#else
#define WRAPPED_TRAP_SEQUENCE() TRAP_SEQUENCE()
#endif

#define IMMEDIATE_CRASH()    \
  ({                         \
    WRAPPED_TRAP_SEQUENCE(); \
    __builtin_unreachable(); \
  })

#elif defined(COMPILER_MSVC)

// Clang is cleverer about coalescing int3s, so we need to add a unique-ish
// instruction following the __debugbreak() to have it emit distinct locations
// for CHECKs rather than collapsing them all together. It would be nice to use
// a short intrinsic to do this (and perhaps have only one implementation for
// both clang and MSVC), however clang-cl currently does not support intrinsics.
// On the flip side, MSVC x64 doesn't support inline asm. So, we have to have
// two implementations. Normally clang-cl's version will be 5 bytes (1 for
// `int3`, 2 for `ud2`, 2 for `push byte imm`, however, TODO(scottmg):
// https://crbug.com/694670 clang-cl doesn't currently support %'ing
// __COUNTER__, so eventually it will emit the dword form of push.
// TODO(scottmg): Reinvestigate a short sequence that will work on both
// compilers once clang supports more intrinsics. See https://crbug.com/693713.
#if defined(__clang__)
#define IMMEDIATE_CRASH()                           \
  ({                                                \
    {__asm int 3 __asm ud2 __asm push __COUNTER__}; \
    __builtin_unreachable();                        \
  })
#else
#define IMMEDIATE_CRASH() __debugbreak()
#endif  // __clang__

#else
#error Port
#endif

// CHECK dies with a fatal error if condition is not true.  It is *not*
// controlled by NDEBUG, so the check will be executed regardless of
// compilation mode.
//
// We make sure CHECK et al. always evaluates their arguments, as
// doing CHECK(FunctionWithSideEffect()) is a common idiom.

#if defined(OFFICIAL_BUILD) && defined(NDEBUG)

// Make all CHECK functions discard their log strings to reduce code bloat, and
// improve performance, for official release builds.
//
// This is not calling BreakDebugger since this is called frequently, and
// calling an out-of-line function instead of a noreturn inline macro prevents
// compiler optimizations.
#define CHECK(condition) \
  UNLIKELY(!(condition)) ? IMMEDIATE_CRASH() : EAT_STREAM_PARAMETERS

// PCHECK includes the system error code, which is useful for determining
// why the condition failed. In official builds, preserve only the error code
// message so that it is available in crash reports. The stringified
// condition and any additional stream parameters are dropped.
#define PCHECK(condition)                                  \
  LAZY_STREAM(PLOG_STREAM(FATAL), UNLIKELY(!(condition))); \
  EAT_STREAM_PARAMETERS

#define CHECK_OP(name, op, val1, val2) CHECK((val1)op(val2))

#else  // !(OFFICIAL_BUILD && NDEBUG)

#if defined(_PREFAST_) && defined(OS_WIN)
// Use __analysis_assume to tell the VC++ static analysis engine that
// assert conditions are true, to suppress warnings.  The LAZY_STREAM
// parameter doesn't reference 'condition' in /analyze builds because
// this evaluation confuses /analyze. The !! before condition is because
// __analysis_assume gets confused on some conditions:
// http://randomascii.wordpress.com/2011/09/13/analyze-for-visual-studio-the-ugly-part-5/

#define CHECK(condition)                                                  \
  __analysis_assume(!!(condition)), LAZY_STREAM(LOG_STREAM(FATAL), false) \
                                        << "Check failed: " #condition ". "

#define PCHECK(condition)                                                  \
  __analysis_assume(!!(condition)), LAZY_STREAM(PLOG_STREAM(FATAL), false) \
                                        << "Check failed: " #condition ". "

#else  // _PREFAST_

// Do as much work as possible out of line to reduce inline code size.
#define CHECK(condition)                                                      \
  LAZY_STREAM(::logging::LogMessage(__FILE__, __LINE__, #condition).stream(), \
              !ANALYZER_ASSUME_TRUE(condition))

#define PCHECK(condition)                                           \
  LAZY_STREAM(PLOG_STREAM(FATAL), !ANALYZER_ASSUME_TRUE(condition)) \
      << "Check failed: " #condition ". "

#endif  // _PREFAST_

// Helper macro for binary operators.
// Don't use this macro directly in your code, use CHECK_EQ et al below.
// The 'switch' is used to prevent the 'else' from being ambiguous when the
// macro is used in an 'if' clause such as:
// if (a == 1)
//   CHECK_EQ(2, a);
#define CHECK_OP(name, op, val1, val2)                                    \
  switch (0)                                                              \
  case 0:                                                                 \
  default:                                                                \
    if (::logging::CheckOpResult true_if_passed =                         \
            ::logging::Check##name##Impl((val1), (val2),                  \
                                         #val1 " " #op " " #val2))        \
      ;                                                                   \
    else                                                                  \
      ::logging::LogMessage(__FILE__, __LINE__, true_if_passed.message()) \
          .stream()

#endif  // !(OFFICIAL_BUILD && NDEBUG)

// This formats a value for a failing CHECK_XX statement.  Ordinarily,
// it uses the definition for operator<<, with a few special cases below.
template <typename T>
inline typename std::enable_if<
    base::internal::SupportsOstreamOperator<const T&>::value &&
        !std::is_function<typename std::remove_pointer<T>::type>::value,
    void>::type
MakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << v;
}

// Provide an overload for functions and function pointers. Function pointers
// don't implicitly convert to void* but do implicitly convert to bool, so
// without this function pointers are always printed as 1 or 0. (MSVC isn't
// standards-conforming here and converts function pointers to regular
// pointers, so this is a no-op for MSVC.)
template <typename T>
inline typename std::enable_if<
    std::is_function<typename std::remove_pointer<T>::type>::value,
    void>::type
MakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << reinterpret_cast<const void*>(v);
}

// We need overloads for enums that don't support operator<<.
// (i.e. scoped enums where no operator<< overload was declared).
template <typename T>
inline typename std::enable_if<
    !base::internal::SupportsOstreamOperator<const T&>::value &&
        std::is_enum<T>::value,
    void>::type
MakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << static_cast<typename std::underlying_type<T>::type>(v);
}

// We need an explicit overload for std::nullptr_t.
void MakeCheckOpValueString(std::ostream* os, std::nullptr_t p);

// Build the error message string.  This is separate from the "Impl"
// function template because it is not performance critical and so can
// be out of line, while the "Impl" code should be inline.  Caller
// takes ownership of the returned string.
template <class t1, class t2>
std::string* MakeCheckOpString(const t1& v1, const t2& v2, const char* names) {
  std::ostringstream ss;
  ss << names << " (";
  MakeCheckOpValueString(&ss, v1);
  ss << " vs. ";
  MakeCheckOpValueString(&ss, v2);
  ss << ")";
  std::string* msg = new std::string(ss.str());
  return msg;
}

// Commonly used instantiations of MakeCheckOpString<>. Explicitly instantiated
// in logging.cc.
extern template std::string* MakeCheckOpString<int, int>(const int&,
                                                         const int&,
                                                         const char* names);
extern template std::string* MakeCheckOpString<unsigned long, unsigned long>(
    const unsigned long&,
    const unsigned long&,
    const char* names);
extern template std::string* MakeCheckOpString<unsigned long, unsigned int>(
    const unsigned long&,
    const unsigned int&,
    const char* names);
extern template std::string* MakeCheckOpString<unsigned int, unsigned long>(
    const unsigned int&,
    const unsigned long&,
    const char* names);
extern template std::string* MakeCheckOpString<std::string, std::string>(
    const std::string&,
    const std::string&,
    const char* name);

// Helper functions for CHECK_OP macro.
// The (int, int) specialization works around the issue that the compiler
// will not instantiate the template version of the function on values of
// unnamed enum type - see comment below.
//
// The checked condition is wrapped with ANALYZER_ASSUME_TRUE, which under
// static analysis builds, blocks analysis of the current path if the
// condition is false.
#define DEFINE_CHECK_OP_IMPL(name, op)                                       \
  template <class t1, class t2>                                              \
  inline std::string* Check##name##Impl(const t1& v1, const t2& v2,          \
                                        const char* names) {                 \
    if (ANALYZER_ASSUME_TRUE(v1 op v2))                                      \
      return NULL;                                                           \
    else                                                                     \
      return ::logging::MakeCheckOpString(v1, v2, names);                    \
  }                                                                          \
  inline std::string* Check##name##Impl(int v1, int v2, const char* names) { \
    if (ANALYZER_ASSUME_TRUE(v1 op v2))                                      \
      return NULL;                                                           \
    else                                                                     \
      return ::logging::MakeCheckOpString(v1, v2, names);                    \
  }
DEFINE_CHECK_OP_IMPL(EQ, ==)
DEFINE_CHECK_OP_IMPL(NE, !=)
DEFINE_CHECK_OP_IMPL(LE, <=)
DEFINE_CHECK_OP_IMPL(LT, <)
DEFINE_CHECK_OP_IMPL(GE, >=)
DEFINE_CHECK_OP_IMPL(GT, >)
#undef DEFINE_CHECK_OP_IMPL

#define CHECK_EQ(val1, val2) CHECK_OP(EQ, ==, val1, val2)
#define CHECK_NE(val1, val2) CHECK_OP(NE, !=, val1, val2)
#define CHECK_LE(val1, val2) CHECK_OP(LE, <=, val1, val2)
#define CHECK_LT(val1, val2) CHECK_OP(LT, <, val1, val2)
#define CHECK_GE(val1, val2) CHECK_OP(GE, >=, val1, val2)
#define CHECK_GT(val1, val2) CHECK_OP(GT, >, val1, val2)

#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
#define DCHECK_IS_ON() 0
#else
#define DCHECK_IS_ON() 1
#endif

// Definitions for DLOG et al.

#if DCHECK_IS_ON()

#define DLOG_IS_ON(severity) LOG_IS_ON(severity)
#define DLOG_IF(severity, condition) LOG_IF(severity, condition)
#define DLOG_ASSERT(condition) LOG_ASSERT(condition)
#define DPLOG_IF(severity, condition) PLOG_IF(severity, condition)

#else  // DCHECK_IS_ON()

// If !DCHECK_IS_ON(), we want to avoid emitting any references to |condition|
// (which may reference a variable defined only if DCHECK_IS_ON()).
// Contrast this with DCHECK et al., which has different behavior.

#define DLOG_IS_ON(severity) false
#define DLOG_IF(severity, condition) EAT_STREAM_PARAMETERS
#define DLOG_ASSERT(condition) EAT_STREAM_PARAMETERS
#define DPLOG_IF(severity, condition) EAT_STREAM_PARAMETERS

#endif  // DCHECK_IS_ON()

#define DLOG(severity) LAZY_STREAM(LOG_STREAM(severity), DLOG_IS_ON(severity))

#define DPLOG(severity) LAZY_STREAM(PLOG_STREAM(severity), DLOG_IS_ON(severity))

// Definitions for DCHECK et al.

#if DCHECK_IS_ON()

#if DCHECK_IS_CONFIGURABLE
extern LogSeverity LOG_DCHECK;
#else
const LogSeverity LOG_DCHECK = LOG_FATAL;
#endif

#else  // DCHECK_IS_ON()

// There may be users of LOG_DCHECK that are enabled independently
// of DCHECK_IS_ON(), so default to FATAL logging for those.
const LogSeverity LOG_DCHECK = LOG_FATAL;

#endif  // DCHECK_IS_ON()

// DCHECK et al. make sure to reference |condition| regardless of
// whether DCHECKs are enabled; this is so that we don't get unused
// variable warnings if the only use of a variable is in a DCHECK.
// This behavior is different from DLOG_IF et al.
//
// Note that the definition of the DCHECK macros depends on whether or not
// DCHECK_IS_ON() is true. When DCHECK_IS_ON() is false, the macros use
// EAT_STREAM_PARAMETERS to avoid expressions that would create temporaries.

#if defined(_PREFAST_) && defined(OS_WIN)
// See comments on the previous use of __analysis_assume.

#define DCHECK(condition)                                                  \
  __analysis_assume(!!(condition)), LAZY_STREAM(LOG_STREAM(DCHECK), false) \
                                        << "Check failed: " #condition ". "

#define DPCHECK(condition)                                                  \
  __analysis_assume(!!(condition)), LAZY_STREAM(PLOG_STREAM(DCHECK), false) \
                                        << "Check failed: " #condition ". "

#else  // !(defined(_PREFAST_) && defined(OS_WIN))

#if DCHECK_IS_ON()

#define DCHECK(condition)                                           \
  LAZY_STREAM(LOG_STREAM(DCHECK), !ANALYZER_ASSUME_TRUE(condition)) \
      << "Check failed: " #condition ". "
#define DPCHECK(condition)                                           \
  LAZY_STREAM(PLOG_STREAM(DCHECK), !ANALYZER_ASSUME_TRUE(condition)) \
      << "Check failed: " #condition ". "

#else  // DCHECK_IS_ON()

#define DCHECK(condition) EAT_STREAM_PARAMETERS << !(condition)
#define DPCHECK(condition) EAT_STREAM_PARAMETERS << !(condition)

#endif  // DCHECK_IS_ON()

#endif  // defined(_PREFAST_) && defined(OS_WIN)

// Helper macro for binary operators.
// Don't use this macro directly in your code, use DCHECK_EQ et al below.
// The 'switch' is used to prevent the 'else' from being ambiguous when the
// macro is used in an 'if' clause such as:
// if (a == 1)
//   DCHECK_EQ(2, a);
#if DCHECK_IS_ON()

#define DCHECK_OP(name, op, val1, val2)                                   \
  switch (0)                                                              \
  case 0:                                                                 \
  default:                                                                \
    if (::logging::CheckOpResult true_if_passed =                         \
            DCHECK_IS_ON() ? ::logging::Check##name##Impl(                \
                                 (val1), (val2), #val1 " " #op " " #val2) \
                           : nullptr)                                     \
      ;                                                                   \
    else                                                                  \
      ::logging::LogMessage(__FILE__, __LINE__, ::logging::LOG_DCHECK,    \
                            true_if_passed.message())                     \
          .stream()

#else  // DCHECK_IS_ON()

// When DCHECKs aren't enabled, DCHECK_OP still needs to reference operator<<
// overloads for |val1| and |val2| to avoid potential compiler warnings about
// unused functions. For the same reason, it also compares |val1| and |val2|
// using |op|.
//
// Note that the contract of DCHECK_EQ, etc is that arguments are only evaluated
// once. Even though |val1| and |val2| appear twice in this version of the macro
// expansion, this is OK, since the expression is never actually evaluated.
#define DCHECK_OP(name, op, val1, val2)                             \
  EAT_STREAM_PARAMETERS << (::logging::MakeCheckOpValueString(      \
                                ::logging::g_swallow_stream, val1), \
                            ::logging::MakeCheckOpValueString(      \
                                ::logging::g_swallow_stream, val2), \
                            (val1)op(val2))

#endif  // DCHECK_IS_ON()

// Equality/Inequality checks - compare two values, and log a
// LOG_DCHECK message including the two values when the result is not
// as expected.  The values must have operator<<(ostream, ...)
// defined.
//
// You may append to the error message like so:
//   DCHECK_NE(1, 2) << "The world must be ending!";
//
// We are very careful to ensure that each argument is evaluated exactly
// once, and that anything which is legal to pass as a function argument is
// legal here.  In particular, the arguments may be temporary expressions
// which will end up being destroyed at the end of the apparent statement,
// for example:
//   DCHECK_EQ(string("abc")[1], 'b');
//
// WARNING: These don't compile correctly if one of the arguments is a pointer
// and the other is NULL.  In new code, prefer nullptr instead.  To
// work around this for C++98, simply static_cast NULL to the type of the
// desired pointer.

#define DCHECK_EQ(val1, val2) DCHECK_OP(EQ, ==, val1, val2)
#define DCHECK_NE(val1, val2) DCHECK_OP(NE, !=, val1, val2)
#define DCHECK_LE(val1, val2) DCHECK_OP(LE, <=, val1, val2)
#define DCHECK_LT(val1, val2) DCHECK_OP(LT, <, val1, val2)
#define DCHECK_GE(val1, val2) DCHECK_OP(GE, >=, val1, val2)
#define DCHECK_GT(val1, val2) DCHECK_OP(GT, >, val1, val2)

#if !DCHECK_IS_ON() && defined(OS_CHROMEOS)
// Implement logging of NOTREACHED() as a dedicated function to get function
// call overhead down to a minimum.
void LogErrorNotReached(const char* file, int line);
#define NOTREACHED()                                       \
  true ? ::logging::LogErrorNotReached(__FILE__, __LINE__) \
       : EAT_STREAM_PARAMETERS
#else
#define NOTREACHED() DCHECK(false)
#endif

// Redefine the standard assert to use our nice log files
#undef assert
#define assert(x) DLOG_ASSERT(x)

// This class more or less represents a particular log message.  You
// create an instance of LogMessage and then stream stuff to it.
// When you finish streaming to it, ~LogMessage is called and the
// full message gets streamed to the appropriate destination.
//
// You shouldn't actually use LogMessage's constructor to log things,
// though.  You should use the LOG() macro (and variants thereof)
// above.
class LogMessage {
 public:
  // Used for LOG(severity).
  LogMessage(const char* file, int line, LogSeverity severity);

  // Used for CHECK().  Implied severity = LOG_FATAL.
  LogMessage(const char* file, int line, const char* condition);

  // Used for CHECK_EQ(), etc. Takes ownership of the given string.
  // Implied severity = LOG_FATAL.
  LogMessage(const char* file, int line, std::string* result);

  // Used for DCHECK_EQ(), etc. Takes ownership of the given string.
  LogMessage(const char* file,
             int line,
             LogSeverity severity,
             std::string* result);

  ~LogMessage();

  std::ostream& stream() { return stream_; }

  LogSeverity severity() { return severity_; }
  std::string str() { return stream_.str(); }

 private:
  void Init(const char* file, int line);

  LogSeverity severity_;
  std::ostringstream stream_;
  size_t message_start_;  // Offset of the start of the message (past prefix
                          // info).
  // The file and line information passed in to the constructor.
  const char* file_;
  const int line_;

#if defined(OS_WIN)
  // Stores the current value of GetLastError in the constructor and restores
  // it in the destructor by calling SetLastError.
  // This is useful since the LogMessage class uses a lot of Win32 calls
  // that will lose the value of GLE and the code that called the log function
  // will have lost the thread error value when the log call returns.
  class SaveLastError {
   public:
    SaveLastError();
    ~SaveLastError();

    unsigned long get_error() const { return last_error_; }

   protected:
    unsigned long last_error_;
  };

  SaveLastError last_error_;
#endif

  DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

// This class is used to explicitly ignore values in the conditional
// logging macros.  This avoids compiler warnings like "value computed
// is not used" and "statement has no effect".
class LogMessageVoidify {
 public:
  LogMessageVoidify() = default;
  // This has to be an operator with a precedence lower than << but
  // higher than ?:
  void operator&(std::ostream&) {}
};

#if defined(OS_WIN)
typedef unsigned long SystemErrorCode;
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
typedef int SystemErrorCode;
#endif

// Alias for ::GetLastError() on Windows and errno on POSIX. Avoids having to
// pull in windows.h just for GetLastError() and DWORD.
SystemErrorCode GetLastSystemErrorCode();
std::string SystemErrorCodeToString(SystemErrorCode error_code);

#if defined(OS_WIN)
// Appends a formatted system message of the GetLastError() type.
class Win32ErrorLogMessage {
 public:
  Win32ErrorLogMessage(const char* file,
                       int line,
                       LogSeverity severity,
                       SystemErrorCode err);

  // Appends the error message before destructing the encapsulated class.
  ~Win32ErrorLogMessage();

  std::ostream& stream() { return log_message_.stream(); }

 private:
  SystemErrorCode err_;
  LogMessage log_message_;

  DISALLOW_COPY_AND_ASSIGN(Win32ErrorLogMessage);
};
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
// Appends a formatted system message of the errno type
class ErrnoLogMessage {
 public:
  ErrnoLogMessage(const char* file,
                  int line,
                  LogSeverity severity,
                  SystemErrorCode err);

  // Appends the error message before destructing the encapsulated class.
  ~ErrnoLogMessage();

  std::ostream& stream() { return log_message_.stream(); }

 private:
  SystemErrorCode err_;
  LogMessage log_message_;

  DISALLOW_COPY_AND_ASSIGN(ErrnoLogMessage);
};
#endif  // OS_WIN

// Closes the log file explicitly if open.
// NOTE: Since the log file is opened as necessary by the action of logging
//       statements, there's no guarantee that it will stay closed
//       after this call.
void CloseLogFile();

// Async signal safe logging mechanism.
void RawLog(int level, const char* message);

#define RAW_LOG(level, message) \
  ::logging::RawLog(::logging::LOG_##level, message)

#define RAW_CHECK(condition)                               \
  do {                                                     \
    if (!(condition))                                      \
      ::logging::RawLog(::logging::LOG_FATAL,              \
                        "Check failed: " #condition "\n"); \
  } while (0)

#if defined(OS_WIN)
// Returns true if logging to file is enabled.
bool IsLoggingToFileEnabled();

// Returns the default log file path.
std::wstring GetLogFileFullPath();
#endif

}  // namespace logging

// Note that "The behavior of a C++ program is undefined if it adds declarations
// or definitions to namespace std or to a namespace within namespace std unless
// otherwise specified." --C++11[namespace.std]
//
// We've checked that this particular definition has the intended behavior on
// our implementations, but it's prone to breaking in the future, and please
// don't imitate this in your own definitions without checking with some
// standard library experts.
namespace std {
// These functions are provided as a convenience for logging, which is where we
// use streams (it is against Google style to use streams in other places). It
// is designed to allow you to emit non-ASCII Unicode strings to the log file,
// which is normally ASCII. It is relatively slow, so try not to use it for
// common cases. Non-ASCII characters will be converted to UTF-8 by these
// operators.
std::ostream& operator<<(std::ostream& out, const wchar_t* wstr);
inline std::ostream& operator<<(std::ostream& out, const std::wstring& wstr) {
  return out << wstr.c_str();
}
}  // namespace std

// The NOTIMPLEMENTED() macro annotates codepaths which have not been
// implemented yet. If output spam is a serious concern,
// NOTIMPLEMENTED_LOG_ONCE can be used.

#if defined(COMPILER_GCC)
// On Linux, with GCC, we can use __PRETTY_FUNCTION__ to get the demangled name
// of the current function in the NOTIMPLEMENTED message.
#define NOTIMPLEMENTED_MSG "Not implemented reached in " << __PRETTY_FUNCTION__
#else
#define NOTIMPLEMENTED_MSG "NOT IMPLEMENTED"
#endif

#if defined(OS_ANDROID) && defined(OFFICIAL_BUILD)
#define NOTIMPLEMENTED() EAT_STREAM_PARAMETERS
#define NOTIMPLEMENTED_LOG_ONCE() EAT_STREAM_PARAMETERS
#else
#define NOTIMPLEMENTED() LOG(ERROR) << NOTIMPLEMENTED_MSG
#define NOTIMPLEMENTED_LOG_ONCE()                      \
  do {                                                 \
    static bool logged_once = false;                   \
    LOG_IF(ERROR, !logged_once) << NOTIMPLEMENTED_MSG; \
    logged_once = true;                                \
  } while (0);                                         \
  EAT_STREAM_PARAMETERS
#endif

#endif  // BASE_LOGGING_H_
