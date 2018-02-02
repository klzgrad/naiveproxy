// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/category_registry.h"

#include <string.h>

#include <type_traits>

#include "base/atomicops.h"
#include "base/debug/leak_annotations.h"
#include "base/logging.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h"
#include "base/trace_event/trace_category.h"

namespace base {
namespace trace_event {

namespace {

constexpr size_t kMaxCategories = 200;
const int kNumBuiltinCategories = 4;

// |g_categories| might end up causing creating dynamic initializers if not POD.
static_assert(std::is_pod<TraceCategory>::value, "TraceCategory must be POD");

// These entries must be kept consistent with the kCategory* consts below.
TraceCategory g_categories[kMaxCategories] = {
    {0, 0, "tracing categories exhausted; must increase kMaxCategories"},
    {0, 0, "tracing already shutdown"},  // See kCategoryAlreadyShutdown below.
    {0, 0, "__metadata"},                // See kCategoryMetadata below.
    {0, 0, "toplevel"},                  // Warmup the toplevel category.
};

base::subtle::AtomicWord g_category_index = kNumBuiltinCategories;

bool IsValidCategoryPtr(const TraceCategory* category) {
  // If any of these are hit, something has cached a corrupt category pointer.
  uintptr_t ptr = reinterpret_cast<uintptr_t>(category);
  return ptr % sizeof(void*) == 0 &&
         ptr >= reinterpret_cast<uintptr_t>(&g_categories[0]) &&
         ptr <= reinterpret_cast<uintptr_t>(&g_categories[kMaxCategories - 1]);
}

}  // namespace

// static
TraceCategory* const CategoryRegistry::kCategoryExhausted = &g_categories[0];
TraceCategory* const CategoryRegistry::kCategoryAlreadyShutdown =
    &g_categories[1];
TraceCategory* const CategoryRegistry::kCategoryMetadata = &g_categories[2];

// static
void CategoryRegistry::Initialize() {
  // Trace is enabled or disabled on one thread while other threads are
  // accessing the enabled flag. We don't care whether edge-case events are
  // traced or not, so we allow races on the enabled flag to keep the trace
  // macros fast.
  for (size_t i = 0; i < kMaxCategories; ++i) {
    ANNOTATE_BENIGN_RACE(g_categories[i].state_ptr(),
                         "trace_event category enabled");
    // If this DCHECK is hit in a test it means that ResetForTesting() is not
    // called and the categories state leaks between test fixtures.
    DCHECK(!g_categories[i].is_enabled());
  }
}

// static
void CategoryRegistry::ResetForTesting() {
  // reset_for_testing clears up only the enabled state and filters. The
  // categories themselves cannot be cleared up because the static pointers
  // injected by the macros still point to them and cannot be reset.
  for (size_t i = 0; i < kMaxCategories; ++i)
    g_categories[i].reset_for_testing();
}

// static
TraceCategory* CategoryRegistry::GetCategoryByName(const char* category_name) {
  DCHECK(!strchr(category_name, '"'))
      << "Category names may not contain double quote";

  // The g_categories is append only, avoid using a lock for the fast path.
  size_t category_index = base::subtle::Acquire_Load(&g_category_index);

  // Search for pre-existing category group.
  for (size_t i = 0; i < category_index; ++i) {
    if (strcmp(g_categories[i].name(), category_name) == 0) {
      return &g_categories[i];
    }
  }
  return nullptr;
}

bool CategoryRegistry::GetOrCreateCategoryLocked(
    const char* category_name,
    CategoryInitializerFn category_initializer_fn,
    TraceCategory** category) {
  // This is the slow path: the lock is not held in the fastpath
  // (GetCategoryByName), so more than one thread could have reached here trying
  // to add the same category.
  *category = GetCategoryByName(category_name);
  if (*category)
    return false;

  // Create a new category.
  size_t category_index = base::subtle::Acquire_Load(&g_category_index);
  if (category_index >= kMaxCategories) {
    NOTREACHED() << "must increase kMaxCategories";
    *category = kCategoryExhausted;
    return false;
  }

  // TODO(primiano): this strdup should be removed. The only documented reason
  // for it was TraceWatchEvent, which is gone. However, something might have
  // ended up relying on this. Needs some auditing before removal.
  const char* category_name_copy = strdup(category_name);
  ANNOTATE_LEAKING_OBJECT_PTR(category_name_copy);

  *category = &g_categories[category_index];
  DCHECK(!(*category)->is_valid());
  DCHECK(!(*category)->is_enabled());
  (*category)->set_name(category_name_copy);
  category_initializer_fn(*category);

  // Update the max index now.
  base::subtle::Release_Store(&g_category_index, category_index + 1);
  return true;
}

// static
const TraceCategory* CategoryRegistry::GetCategoryByStatePtr(
    const uint8_t* category_state) {
  const TraceCategory* category = TraceCategory::FromStatePtr(category_state);
  DCHECK(IsValidCategoryPtr(category));
  return category;
}

// static
bool CategoryRegistry::IsBuiltinCategory(const TraceCategory* category) {
  DCHECK(IsValidCategoryPtr(category));
  return category < &g_categories[kNumBuiltinCategories];
}

// static
CategoryRegistry::Range CategoryRegistry::GetAllCategories() {
  // The |g_categories| array is append only. We have to only guarantee to
  // not return an index to a category which is being initialized by
  // GetOrCreateCategoryByName().
  size_t category_index = base::subtle::Acquire_Load(&g_category_index);
  return CategoryRegistry::Range(&g_categories[0],
                                 &g_categories[category_index]);
}

}  // namespace trace_event
}  // namespace base
