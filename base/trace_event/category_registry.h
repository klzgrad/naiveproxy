// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_CATEGORY_REGISTRY_H_
#define BASE_TRACE_EVENT_CATEGORY_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

#include "base/base_export.h"
#include "base/logging.h"

namespace base {
namespace trace_event {

struct TraceCategory;
class TraceCategoryTest;
class TraceLog;

// Allows fast and thread-safe acces to the state of all tracing categories.
// All the methods in this class can be concurrently called on multiple threads,
// unless otherwise noted (e.g., GetOrCreateCategoryLocked).
// The reason why this is a fully static class with global state is to allow to
// statically define known categories as global linker-initialized structs,
// without requiring static initializers.
class BASE_EXPORT CategoryRegistry {
 public:
  // Allows for-each iterations over a slice of the categories array.
  class Range {
   public:
    Range(TraceCategory* begin, TraceCategory* end) : begin_(begin), end_(end) {
      DCHECK_LE(begin, end);
    }
    TraceCategory* begin() const { return begin_; }
    TraceCategory* end() const { return end_; }

   private:
    TraceCategory* const begin_;
    TraceCategory* const end_;
  };

  // Known categories.
  static TraceCategory* const kCategoryExhausted;
  static TraceCategory* const kCategoryMetadata;
  static TraceCategory* const kCategoryAlreadyShutdown;

  // Returns a category entry from the Category.state_ptr() pointer.
  // TODO(primiano): trace macros should just keep a pointer to the entire
  // TraceCategory, not just the enabled state pointer. That would remove the
  // need for this function and make everything cleaner at no extra cost (as
  // long as the |state_| is the first field of the struct, which can be
  // guaranteed via static_assert, see TraceCategory ctor).
  static const TraceCategory* GetCategoryByStatePtr(
      const uint8_t* category_state);

  // Returns a category from its name or nullptr if not found.
  // The output |category| argument is an undefinitely lived pointer to the
  // TraceCategory owned by the registry. TRACE_EVENTx macros will cache this
  // pointer and use it for checks in their fast-paths.
  static TraceCategory* GetCategoryByName(const char* category_name);

  static bool IsBuiltinCategory(const TraceCategory*);

 private:
  friend class TraceCategoryTest;
  friend class TraceLog;
  using CategoryInitializerFn = void (*)(TraceCategory*);

  // Only for debugging/testing purposes, is a no-op on release builds.
  static void Initialize();

  // Resets the state of all categories, to clear up the state between tests.
  static void ResetForTesting();

  // Used to get/create a category in the slow-path. If the category exists
  // already, this has the same effect of GetCategoryByName and returns false.
  // If not, a new category is created and the CategoryInitializerFn is invoked
  // before retuning true. The caller must guarantee serialization: either call
  // this method from a single thread or hold a lock when calling this.
  static bool GetOrCreateCategoryLocked(const char* category_name,
                                        CategoryInitializerFn,
                                        TraceCategory**);

  // Allows to iterate over the valid categories in a for-each loop.
  // This includes builtin categories such as __metadata.
  static Range GetAllCategories();
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_CATEGORY_REGISTRY_H_
