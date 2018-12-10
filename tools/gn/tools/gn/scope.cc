// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/scope.h"

#include <memory>

#include "base/logging.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/source_file.h"
#include "tools/gn/template.h"

namespace {

// FLags set in the mode_flags_ of a scope. If a bit is set, it applies
// recursively to all dependent scopes.
const unsigned kProcessingBuildConfigFlag = 1;
const unsigned kProcessingImportFlag = 2;

// Returns true if this variable name should be considered private. Private
// values start with an underscore, and are not imported from "gni" files
// when processing an import.
bool IsPrivateVar(const base::StringPiece& name) {
  return name.empty() || name[0] == '_';
}

}  // namespace

// Defaults to all false, which are the things least likely to cause errors.
Scope::MergeOptions::MergeOptions()
    : clobber_existing(false),
      skip_private_vars(false),
      mark_dest_used(false) {}

Scope::MergeOptions::~MergeOptions() = default;

Scope::ProgrammaticProvider::~ProgrammaticProvider() {
  scope_->RemoveProvider(this);
}

Scope::Scope(const Settings* settings)
    : const_containing_(nullptr),
      mutable_containing_(nullptr),
      settings_(settings),
      mode_flags_(0),
      item_collector_(nullptr) {}

Scope::Scope(Scope* parent)
    : const_containing_(nullptr),
      mutable_containing_(parent),
      settings_(parent->settings()),
      mode_flags_(0),
      item_collector_(nullptr),
      build_dependency_files_(parent->build_dependency_files_) {}

Scope::Scope(const Scope* parent)
    : const_containing_(parent),
      mutable_containing_(nullptr),
      settings_(parent->settings()),
      mode_flags_(0),
      item_collector_(nullptr),
      build_dependency_files_(parent->build_dependency_files_) {}

Scope::~Scope() = default;

void Scope::DetachFromContaining() {
  const_containing_ = nullptr;
  mutable_containing_ = nullptr;
}

bool Scope::HasValues(SearchNested search_nested) const {
  DCHECK(search_nested == SEARCH_CURRENT);
  return !values_.empty();
}

const Value* Scope::GetValue(const base::StringPiece& ident,
                             bool counts_as_used) {
  const Scope* found_in_scope = nullptr;
  return GetValueWithScope(ident, counts_as_used, &found_in_scope);
}

const Value* Scope::GetValueWithScope(const base::StringPiece& ident,
                                      bool counts_as_used,
                                      const Scope** found_in_scope) {
  // First check for programmatically-provided values.
  for (auto* provider : programmatic_providers_) {
    const Value* v = provider->GetProgrammaticValue(ident);
    if (v) {
      *found_in_scope = nullptr;
      return v;
    }
  }

  RecordMap::iterator found = values_.find(ident);
  if (found != values_.end()) {
    if (counts_as_used)
      found->second.used = true;
    *found_in_scope = this;
    return &found->second.value;
  }

  // Search in the parent scope.
  if (const_containing_)
    return const_containing_->GetValueWithScope(ident, found_in_scope);
  if (mutable_containing_) {
    return mutable_containing_->GetValueWithScope(ident, counts_as_used,
                                                  found_in_scope);
  }
  return nullptr;
}

Value* Scope::GetMutableValue(const base::StringPiece& ident,
                              SearchNested search_mode,
                              bool counts_as_used) {
  // Don't do programmatic values, which are not mutable.
  RecordMap::iterator found = values_.find(ident);
  if (found != values_.end()) {
    if (counts_as_used)
      found->second.used = true;
    return &found->second.value;
  }

  // Search in the parent mutable scope if requested, but not const one.
  if (search_mode == SEARCH_NESTED && mutable_containing_) {
    return mutable_containing_->GetMutableValue(ident, Scope::SEARCH_NESTED,
                                                counts_as_used);
  }
  return nullptr;
}

base::StringPiece Scope::GetStorageKey(const base::StringPiece& ident) const {
  RecordMap::const_iterator found = values_.find(ident);
  if (found != values_.end())
    return found->first;

  // Search in parent scope.
  if (containing())
    return containing()->GetStorageKey(ident);
  return base::StringPiece();
}

const Value* Scope::GetValue(const base::StringPiece& ident) const {
  const Scope* found_in_scope = nullptr;
  return GetValueWithScope(ident, &found_in_scope);
}

const Value* Scope::GetValueWithScope(const base::StringPiece& ident,
                                      const Scope** found_in_scope) const {
  RecordMap::const_iterator found = values_.find(ident);
  if (found != values_.end()) {
    *found_in_scope = this;
    return &found->second.value;
  }
  if (containing())
    return containing()->GetValueWithScope(ident, found_in_scope);
  return nullptr;
}

Value* Scope::SetValue(const base::StringPiece& ident,
                       Value v,
                       const ParseNode* set_node) {
  Record& r = values_[ident];  // Clears any existing value.
  r.value = std::move(v);
  r.value.set_origin(set_node);
  return &r.value;
}

void Scope::RemoveIdentifier(const base::StringPiece& ident) {
  RecordMap::iterator found = values_.find(ident);
  if (found != values_.end())
    values_.erase(found);
}

void Scope::RemovePrivateIdentifiers() {
  // Do it in two phases to avoid mutating while iterating. Our hash map is
  // currently backed by several different vendor-specific implementations and
  // I'm not sure if all of them support mutating while iterating. Since this
  // is not perf-critical, do the safe thing.
  std::vector<base::StringPiece> to_remove;
  for (const auto& cur : values_) {
    if (IsPrivateVar(cur.first))
      to_remove.push_back(cur.first);
  }

  for (const auto& cur : to_remove)
    values_.erase(cur);
}

bool Scope::AddTemplate(const std::string& name, const Template* templ) {
  if (GetTemplate(name))
    return false;
  templates_[name] = templ;
  return true;
}

const Template* Scope::GetTemplate(const std::string& name) const {
  TemplateMap::const_iterator found = templates_.find(name);
  if (found != templates_.end())
    return found->second.get();
  if (containing())
    return containing()->GetTemplate(name);
  return nullptr;
}

void Scope::MarkUsed(const base::StringPiece& ident) {
  RecordMap::iterator found = values_.find(ident);
  if (found == values_.end()) {
    NOTREACHED();
    return;
  }
  found->second.used = true;
}

void Scope::MarkAllUsed() {
  for (auto& cur : values_)
    cur.second.used = true;
}

void Scope::MarkAllUsed(const std::set<std::string>& excluded_values) {
  for (auto& cur : values_) {
    if (!excluded_values.empty() &&
        excluded_values.find(cur.first.as_string()) != excluded_values.end()) {
      continue;  // Skip this excluded value.
    }
    cur.second.used = true;
  }
}

void Scope::MarkUnused(const base::StringPiece& ident) {
  RecordMap::iterator found = values_.find(ident);
  if (found == values_.end()) {
    NOTREACHED();
    return;
  }
  found->second.used = false;
}

bool Scope::IsSetButUnused(const base::StringPiece& ident) const {
  RecordMap::const_iterator found = values_.find(ident);
  if (found != values_.end()) {
    if (!found->second.used) {
      return true;
    }
  }
  return false;
}

bool Scope::CheckForUnusedVars(Err* err) const {
  for (const auto& pair : values_) {
    if (!pair.second.used) {
      std::string help =
          "You set the variable \"" + pair.first.as_string() +
          "\" here and it was unused before it went\nout of scope.";

      const BinaryOpNode* binary = pair.second.value.origin()->AsBinaryOp();
      if (binary && binary->op().type() == Token::EQUAL) {
        // Make a nicer error message for normal var sets.
        *err =
            Err(binary->left()->GetRange(), "Assignment had no effect.", help);
      } else {
        // This will happen for internally-generated variables.
        *err =
            Err(pair.second.value.origin(), "Assignment had no effect.", help);
      }
      return false;
    }
  }
  return true;
}

void Scope::GetCurrentScopeValues(KeyValueMap* output) const {
  for (const auto& pair : values_)
    (*output)[pair.first] = pair.second.value;
}

bool Scope::CheckCurrentScopeValuesEqual(const Scope* other) const {
  // If there are containing scopes, equality shouldn't work.
  if (containing()) {
    return false;
  }
  if (values_.size() != other->values_.size()) {
    return false;
  }
  for (const auto& pair : values_) {
    const Value* v = other->GetValue(pair.first);
    if (!v || *v != pair.second.value) {
      return false;
    }
  }
  return true;
}

bool Scope::NonRecursiveMergeTo(Scope* dest,
                                const MergeOptions& options,
                                const ParseNode* node_for_err,
                                const char* desc_for_err,
                                Err* err) const {
  // Values.
  for (const auto& pair : values_) {
    const base::StringPiece& current_name = pair.first;
    if (options.skip_private_vars && IsPrivateVar(current_name))
      continue;  // Skip this private var.
    if (!options.excluded_values.empty() &&
        options.excluded_values.find(current_name.as_string()) !=
            options.excluded_values.end()) {
      continue;  // Skip this excluded value.
    }

    const Value& new_value = pair.second.value;
    if (!options.clobber_existing) {
      const Value* existing_value = dest->GetValue(current_name);
      if (existing_value && new_value != *existing_value) {
        // Value present in both the source and the dest.
        std::string desc_string(desc_for_err);
        *err = Err(node_for_err, "Value collision.",
                   "This " + desc_string + " contains \"" +
                       current_name.as_string() + "\"");
        err->AppendSubErr(
            Err(pair.second.value, "defined here.",
                "Which would clobber the one in your current scope"));
        err->AppendSubErr(
            Err(*existing_value, "defined here.",
                "Executing " + desc_string +
                    " should not conflict with anything "
                    "in the current\nscope unless the values are identical."));
        return false;
      }
    }
    dest->values_[current_name] = pair.second;

    if (options.mark_dest_used)
      dest->MarkUsed(current_name);
  }

  // Target defaults are owning pointers.
  for (const auto& pair : target_defaults_) {
    const std::string& current_name = pair.first;
    if (!options.excluded_values.empty() &&
        options.excluded_values.find(current_name) !=
            options.excluded_values.end()) {
      continue;  // Skip the excluded value.
    }

    if (!options.clobber_existing) {
      const Scope* dest_defaults = dest->GetTargetDefaults(current_name);
      if (dest_defaults) {
        if (RecordMapValuesEqual(pair.second->values_,
                                 dest_defaults->values_)) {
          // Values of the two defaults are equivalent, just ignore the
          // collision.
          continue;
        } else {
          // TODO(brettw) it would be nice to know the origin of a
          // set_target_defaults so we can give locations for the colliding
          // target defaults.
          std::string desc_string(desc_for_err);
          *err = Err(node_for_err, "Target defaults collision.",
                     "This " + desc_string +
                         " contains target defaults for\n"
                         "\"" +
                         current_name +
                         "\" which would clobber one for the\n"
                         "same target type in your current scope. It's "
                         "unfortunate that "
                         "I'm too stupid\nto tell you the location of where "
                         "the target "
                         "defaults were set. Usually\nthis happens in the "
                         "BUILDCONFIG.gn "
                         "file or in a related .gni file.\n");
          return false;
        }
      }
    }

    std::unique_ptr<Scope>& dest_scope = dest->target_defaults_[current_name];
    dest_scope = std::make_unique<Scope>(settings_);
    pair.second->NonRecursiveMergeTo(dest_scope.get(), options, node_for_err,
                                     "<SHOULDN'T HAPPEN>", err);
  }

  // Sources assignment filter.
  if (sources_assignment_filter_) {
    if (!options.clobber_existing) {
      if (dest->GetSourcesAssignmentFilter()) {
        // Sources assignment filter present in both the source and the dest.
        std::string desc_string(desc_for_err);
        *err = Err(node_for_err, "Assignment filter collision.",
                   "The " + desc_string +
                       " contains a sources_assignment_filter "
                       "which\nwould clobber the one in your current scope.");
        return false;
      }
    }
    dest->sources_assignment_filter_ =
        std::make_unique<PatternList>(*sources_assignment_filter_);
  }

  // Templates.
  for (const auto& pair : templates_) {
    const std::string& current_name = pair.first;
    if (options.skip_private_vars && IsPrivateVar(current_name))
      continue;  // Skip this private template.
    if (!options.excluded_values.empty() &&
        options.excluded_values.find(current_name) !=
            options.excluded_values.end()) {
      continue;  // Skip the excluded value.
    }

    if (!options.clobber_existing) {
      const Template* existing_template = dest->GetTemplate(current_name);
      // Since templates are refcounted, we can check if it's the same one by
      // comparing pointers.
      if (existing_template && pair.second.get() != existing_template) {
        // Rule present in both the source and the dest, and they're not the
        // same one.
        std::string desc_string(desc_for_err);
        *err = Err(node_for_err, "Template collision.",
                   "This " + desc_string + " contains a template \"" +
                       current_name + "\"");
        err->AppendSubErr(
            Err(pair.second->GetDefinitionRange(), "defined here.",
                "Which would clobber the one in your current scope"));
        err->AppendSubErr(Err(existing_template->GetDefinitionRange(),
                              "defined here.",
                              "Executing " + desc_string +
                                  " should not conflict with anything "
                                  "in the current\nscope."));
        return false;
      }
    }

    // Be careful to delete any pointer we're about to clobber.
    dest->templates_[current_name] = pair.second;
  }

  // Propogate build dependency files,
  dest->build_dependency_files_.insert(build_dependency_files_.begin(),
                                       build_dependency_files_.end());

  return true;
}

std::unique_ptr<Scope> Scope::MakeClosure() const {
  std::unique_ptr<Scope> result;
  if (const_containing_) {
    // We reached the top of the mutable scope stack. The result scope just
    // references the const scope (which will never change).
    result = std::make_unique<Scope>(const_containing_);
  } else if (mutable_containing_) {
    // There are more nested mutable scopes. Recursively go up the stack to
    // get the closure.
    result = mutable_containing_->MakeClosure();
  } else {
    // This is a standalone scope, just copy it.
    result = std::make_unique<Scope>(settings_);
  }

  // Want to clobber since we've flattened some nested scopes, and our parent
  // scope may have a duplicate value set.
  MergeOptions options;
  options.clobber_existing = true;

  // Add in our variables and we're done.
  Err err;
  NonRecursiveMergeTo(result.get(), options, nullptr, "<SHOULDN'T HAPPEN>",
                      &err);
  DCHECK(!err.has_error());
  return result;
}

Scope* Scope::MakeTargetDefaults(const std::string& target_type) {
  std::unique_ptr<Scope>& dest = target_defaults_[target_type];
  dest = std::make_unique<Scope>(settings_);
  return dest.get();
}

const Scope* Scope::GetTargetDefaults(const std::string& target_type) const {
  NamedScopeMap::const_iterator found = target_defaults_.find(target_type);
  if (found != target_defaults_.end())
    return found->second.get();
  if (containing())
    return containing()->GetTargetDefaults(target_type);
  return nullptr;
}

const PatternList* Scope::GetSourcesAssignmentFilter() const {
  if (sources_assignment_filter_)
    return sources_assignment_filter_.get();
  if (containing())
    return containing()->GetSourcesAssignmentFilter();
  return nullptr;
}

void Scope::SetProcessingBuildConfig() {
  DCHECK((mode_flags_ & kProcessingBuildConfigFlag) == 0);
  mode_flags_ |= kProcessingBuildConfigFlag;
}

void Scope::ClearProcessingBuildConfig() {
  DCHECK(mode_flags_ & kProcessingBuildConfigFlag);
  mode_flags_ &= ~(kProcessingBuildConfigFlag);
}

bool Scope::IsProcessingBuildConfig() const {
  if (mode_flags_ & kProcessingBuildConfigFlag)
    return true;
  if (containing())
    return containing()->IsProcessingBuildConfig();
  return false;
}

void Scope::SetProcessingImport() {
  DCHECK((mode_flags_ & kProcessingImportFlag) == 0);
  mode_flags_ |= kProcessingImportFlag;
}

void Scope::ClearProcessingImport() {
  DCHECK(mode_flags_ & kProcessingImportFlag);
  mode_flags_ &= ~(kProcessingImportFlag);
}

bool Scope::IsProcessingImport() const {
  if (mode_flags_ & kProcessingImportFlag)
    return true;
  if (containing())
    return containing()->IsProcessingImport();
  return false;
}

const SourceDir& Scope::GetSourceDir() const {
  if (!source_dir_.is_null())
    return source_dir_;
  if (containing())
    return containing()->GetSourceDir();
  return source_dir_;
}

void Scope::AddBuildDependencyFile(const SourceFile& build_dependency_file) {
  build_dependency_files_.insert(build_dependency_file);
}

Scope::ItemVector* Scope::GetItemCollector() {
  if (item_collector_)
    return item_collector_;
  if (mutable_containing())
    return mutable_containing()->GetItemCollector();
  return nullptr;
}

void Scope::SetProperty(const void* key, void* value) {
  if (!value) {
    DCHECK(properties_.find(key) != properties_.end());
    properties_.erase(key);
  } else {
    properties_[key] = value;
  }
}

void* Scope::GetProperty(const void* key, const Scope** found_on_scope) const {
  PropertyMap::const_iterator found = properties_.find(key);
  if (found != properties_.end()) {
    if (found_on_scope)
      *found_on_scope = this;
    return found->second;
  }
  if (containing())
    return containing()->GetProperty(key, found_on_scope);
  return nullptr;
}

void Scope::AddProvider(ProgrammaticProvider* p) {
  programmatic_providers_.insert(p);
}

void Scope::RemoveProvider(ProgrammaticProvider* p) {
  DCHECK(programmatic_providers_.find(p) != programmatic_providers_.end());
  programmatic_providers_.erase(p);
}

// static
bool Scope::RecordMapValuesEqual(const RecordMap& a, const RecordMap& b) {
  if (a.size() != b.size())
    return false;
  for (const auto& pair : a) {
    const auto& found_b = b.find(pair.first);
    if (found_b == b.end())
      return false;  // Item in 'a' but not 'b'.
    if (pair.second.value != found_b->second.value)
      return false;  // Values for variable in 'a' and 'b' are different.
  }
  return true;
}
