#ifndef QUICHE_COMMON_BUG_UTILS_TEST_HELPER_H_
#define QUICHE_COMMON_BUG_UTILS_TEST_HELPER_H_

#include "quiche/common/bug_utils.h"

// Sticking various logging functions used by the test in a separate file,
// so their line numbers are unlikely to change as we modify the test file
// itself, as the expectations we set take the file + line numbers into account.

#define QUICHE_TEST_BUG(bug_id)                      \
  GENERIC_BUG_UNBRACED_ELSE_BLOCKER                  \
  GENERIC_BUG_IMPL("QUICHE_TEST_BUG", bug_id, false, \
                   ::quiche::internal::DefaultBugOptions(__FILE__, __LINE__))

#define QUICHE_TEST_BUG_IF(bug_id, condition)                                  \
  GENERIC_BUG_UNBRACED_ELSE_BLOCKER                                            \
  if (!(condition)) { /* Do nothing */                                         \
  } else              /* NOLINT */                                             \
    GENERIC_BUG_IMPL("QUICHE_TEST_BUG", bug_id, false,                         \
                     ::quiche::internal::DefaultBugOptions(__FILE__, __LINE__) \
                         .SetCondition(#condition))

inline void LogBugLine23() { QUICHE_TEST_BUG(Bug 23) << "Here on line 23"; }

inline void LogBugLine26() {
  QUICHE_TEST_BUG(Bug 26) << "Here on line 26";
  QUICHE_TEST_BUG(Bug 27) << "And 27!";
}

inline void LogIfBugLine31(bool condition) {
  QUICHE_TEST_BUG_IF(Bug 31, condition) << "Here on line 31";
}

inline void LogIfBugNullCheckLine35(int *ptr) {
  QUICHE_TEST_BUG_IF(Bug 35, ptr == nullptr) << "Here on line 35";
}

#define QUICHE_TEST_BUG_OPTIONS() \
  ::quiche::internal::DefaultBugOptions(__FILE__, __LINE__)

#endif  // QUICHE_COMMON_BUG_UTILS_TEST_HELPER_H_
