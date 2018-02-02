// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_SCOPE_H_
#define TOOLS_GN_SCOPE_H_

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "tools/gn/err.h"
#include "tools/gn/pattern.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/value.h"

class Item;
class ParseNode;
class Settings;
class Template;

// Scope for the script execution.
//
// Scopes are nested. Writing goes into the toplevel scope, reading checks
// values resursively down the stack until a match is found or there are no
// more containing scopes.
//
// A containing scope can be const or non-const. The const containing scope is
// used primarily to refer to the master build config which is shared across
// many invocations. A const containing scope, however, prevents us from
// marking variables "used" which prevents us from issuing errors on unused
// variables. So you should use a non-const containing scope whenever possible.
class Scope {
 public:
  typedef base::hash_map<base::StringPiece, Value, base::StringPieceHash>
      KeyValueMap;
  // Holds an owning list of Items.
  typedef std::vector<std::unique_ptr<Item>> ItemVector;

  // A flag to indicate whether a function should recurse into nested scopes,
  // or only operate on the current scope.
  enum SearchNested {
    SEARCH_NESTED,
    SEARCH_CURRENT
  };

  // Allows code to provide values for built-in variables. This class will
  // automatically register itself on construction and deregister itself on
  // destruction.
  class ProgrammaticProvider {
   public:
    explicit ProgrammaticProvider(Scope* scope) : scope_(scope) {
      scope_->AddProvider(this);
    }
    virtual ~ProgrammaticProvider();

    // Returns a non-null value if the given value can be programmatically
    // generated, or NULL if there is none.
    virtual const Value* GetProgrammaticValue(
        const base::StringPiece& ident) = 0;

   protected:
    Scope* scope_;
  };

  // Options for configuring scope merges.
  struct MergeOptions {
    MergeOptions();
    ~MergeOptions();

    // When set, all existing avlues in the destination scope will be
    // overwritten.
    //
    // When false, it will be an error to merge a variable into another scope
    // where a variable with the same name is already set. The exception is
    // if both of the variables have the same value (which happens if you
    // somehow multiply import the same file, for example). This case will be
    // ignored since there is nothing getting lost.
    bool clobber_existing;

    // When true, private variables (names beginning with an underscore) will
    // be copied to the destination scope. When false, private values will be
    // skipped.
    bool skip_private_vars;

    // When set, values copied to the destination scope will be marked as used
    // so won't trigger an unused variable warning. You want this when doing an
    // import, for example, or files that don't need a variable from the .gni
    // file will throw an error.
    bool mark_dest_used;

    // When set, those variables are not merged.
    std::set<std::string> excluded_values;
  };

  // Creates an empty toplevel scope.
  explicit Scope(const Settings* settings);

  // Creates a dependent scope.
  explicit Scope(Scope* parent);
  explicit Scope(const Scope* parent);

  ~Scope();

  const Settings* settings() const { return settings_; }

  // See the const_/mutable_containing_ var declarations below. Yes, it's a
  // bit weird that we can have a const pointer to the "mutable" one.
  Scope* mutable_containing() { return mutable_containing_; }
  const Scope* mutable_containing() const { return mutable_containing_; }
  const Scope* const_containing() const { return const_containing_; }
  const Scope* containing() const {
    return mutable_containing_ ? mutable_containing_ : const_containing_;
  }

  // Clears any references to containing scopes. This scope will now be
  // self-sufficient.
  void DetachFromContaining();

  // Returns true if the scope has any values set. This does not check other
  // things that may be set like templates or defaults.
  //
  // Currently this does not search nested scopes and this will assert if you
  // want to search nested scopes. The enum is passed so the callers are
  // unambiguous about nested scope handling. This can be added if needed.
  bool HasValues(SearchNested search_nested) const;

  // Returns NULL if there's no such value.
  //
  // counts_as_used should be set if the variable is being read in a way that
  // should count for unused variable checking.
  //
  // found_in_scope is set to the scope that contains the definition of the
  // ident. If the value was provided programmatically (like host_cpu),
  // found_in_scope will be set to null.
  const Value* GetValue(const base::StringPiece& ident,
                        bool counts_as_used);
  const Value* GetValue(const base::StringPiece& ident) const;
  const Value* GetValueWithScope(const base::StringPiece& ident,
                                 const Scope** found_in_scope) const;
  const Value* GetValueWithScope(const base::StringPiece& ident,
                                 bool counts_as_used,
                                 const Scope** found_in_scope);

  // Returns the requested value as a mutable one if possible. If the value
  // is not found in a mutable scope, then returns null. Note that the value
  // could still exist in a const scope, so GetValue() could still return
  // non-null in this case.
  //
  // Say you have a local scope that then refers to the const root scope from
  // the master build config. You can't change the values from the master
  // build config (it's read-only so it can be read from multiple threads
  // without locking). Read-only operations would work on values from the root
  // scope, but write operations would only work on values in the derived
  // scope(s).
  //
  // Be careful when calling this. It's not normally correct to modify values,
  // but you should instead do a new Set each time.
  //
  // Consider this code:
  //   a = 5
  //    {
  //       a = 6
  //    }
  // The 6 should get set on the nested scope rather than modify the value
  // in the outer one.
  Value* GetMutableValue(const base::StringPiece& ident,
                         SearchNested search_mode,
                         bool counts_as_used);

  // Returns the StringPiece used to identify the value. This string piece
  // will have the same contents as "ident" passed in, but may point to a
  // different underlying buffer. This is useful because this StringPiece is
  // static and won't be deleted for the life of the program, so it can be used
  // as keys in places that may outlive a temporary. It will return an empty
  // string for programmatic and nonexistant values.
  base::StringPiece GetStorageKey(const base::StringPiece& ident) const;

  // The set_node indicates the statement that caused the set, for displaying
  // errors later. Returns a pointer to the value in the current scope (a copy
  // is made for storage).
  Value* SetValue(const base::StringPiece& ident,
                  Value v,
                  const ParseNode* set_node);

  // Removes the value with the given identifier if it exists on the current
  // scope. This does not search recursive scopes. Does nothing if not found.
  void RemoveIdentifier(const base::StringPiece& ident);

  // Removes from this scope all identifiers and templates that are considered
  // private.
  void RemovePrivateIdentifiers();

  // Templates associated with this scope. A template can only be set once, so
  // AddTemplate will fail and return false if a rule with that name already
  // exists. GetTemplate returns NULL if the rule doesn't exist, and it will
  // check all containing scoped rescursively.
  bool AddTemplate(const std::string& name, const Template* templ);
  const Template* GetTemplate(const std::string& name) const;

  // Marks the given identifier as (un)used in the current scope.
  void MarkUsed(const base::StringPiece& ident);
  void MarkAllUsed();
  void MarkAllUsed(const std::set<std::string>& excluded_values);
  void MarkUnused(const base::StringPiece& ident);

  // Checks to see if the scope has a var set that hasn't been used. This is
  // called before replacing the var with a different one. It does not check
  // containing scopes.
  //
  // If the identifier is present but hasnn't been used, return true.
  bool IsSetButUnused(const base::StringPiece& ident) const;

  // Checks the scope to see if any values were set but not used, and fills in
  // the error and returns false if they were.
  bool CheckForUnusedVars(Err* err) const;

  // Returns all values set in the current scope, without going to the parent
  // scopes.
  void GetCurrentScopeValues(KeyValueMap* output) const;

  // Copies this scope's values into the destination. Values from the
  // containing scope(s) (normally shadowed into the current one) will not be
  // copied, neither will the reference to the containing scope (this is why
  // it's "non-recursive").
  //
  // This is used in different contexts. When generating the error, the given
  // parse node will be blamed, and the given desc will be used to describe
  // the operation that doesn't support doing this. For example, desc_for_err
  // would be "import" when doing an import, and the error string would say
  // something like "The import contains...".
  bool NonRecursiveMergeTo(Scope* dest,
                           const MergeOptions& options,
                           const ParseNode* node_for_err,
                           const char* desc_for_err,
                           Err* err) const;

  // Constructs a scope that is a copy of the current one. Nested scopes will
  // be collapsed until we reach a const containing scope. Private values will
  // be included. The resulting closure will reference the const containing
  // scope as its containing scope (since we assume the const scope won't
  // change, we don't have to copy its values).
  std::unique_ptr<Scope> MakeClosure() const;

  // Makes an empty scope with the given name. Overwrites any existing one.
  Scope* MakeTargetDefaults(const std::string& target_type);

  // Gets the scope associated with the given target name, or null if it hasn't
  // been set.
  const Scope* GetTargetDefaults(const std::string& target_type) const;

  // Filter to apply when the sources variable is assigned. May return NULL.
  const PatternList* GetSourcesAssignmentFilter() const;
  void set_sources_assignment_filter(std::unique_ptr<PatternList> f) {
    sources_assignment_filter_ = std::move(f);
  }

  // Indicates if we're currently processing the build configuration file.
  // This is true when processing the config file for any toolchain.
  //
  // To set or clear the flag, it must currently be in the opposite state in
  // the current scope. Note that querying the state of the flag recursively
  // checks all containing scopes until it reaches the top or finds the flag
  // set.
  void SetProcessingBuildConfig();
  void ClearProcessingBuildConfig();
  bool IsProcessingBuildConfig() const;

  // Indicates if we're currently processing an import file.
  //
  // See SetProcessingBaseConfig for how flags work.
  void SetProcessingImport();
  void ClearProcessingImport();
  bool IsProcessingImport() const;

  // The source directory associated with this scope. This will check embedded
  // scopes until it finds a nonempty source directory. This will default to
  // an empty dir if no containing scope has a source dir set.
  const SourceDir& GetSourceDir() const;
  void set_source_dir(const SourceDir& d) { source_dir_ = d; }

  // The item collector is where Items (Targets, Configs, etc.) go that have
  // been defined. If a scope can generate items, this non-owning pointer will
  // point to the storage for such items. The creator of this scope will be
  // responsible for setting up the collector and then dealing with the
  // collected items once execution of the context is complete.
  //
  // The items in a scope are collected as we go and then dispatched at the end
  // of execution of a scope so that we can query the previously-generated
  // targets (like getting the outputs).
  //
  // This can be null if the current scope can not generate items (like for
  // imports and such).
  //
  // When retrieving the collector, the non-const scopes are recursively
  // queried. The collector is not copied for closures, etc.
  void set_item_collector(ItemVector* collector) {
    item_collector_ = collector;
  }
  ItemVector* GetItemCollector();

  // Properties are opaque pointers that code can use to set state on a Scope
  // that it can retrieve later.
  //
  // The key should be a pointer to some use-case-specific object (to avoid
  // collisions, otherwise it doesn't matter). Memory management is up to the
  // setter. Setting the value to NULL will delete the property.
  //
  // Getting a property recursively searches all scopes, and the optional
  // |found_on_scope| variable will be filled with the actual scope containing
  // the key (if the pointer is non-NULL).
  void SetProperty(const void* key, void* value);
  void* GetProperty(const void* key, const Scope** found_on_scope) const;

 private:
  friend class ProgrammaticProvider;

  struct Record {
    Record() : used(false) {}
    explicit Record(const Value& v) : used(false), value(v) {}

    bool used;  // Set to true when the variable is used.
    Value value;
  };

  typedef base::hash_map<base::StringPiece, Record, base::StringPieceHash>
      RecordMap;

  void AddProvider(ProgrammaticProvider* p);
  void RemoveProvider(ProgrammaticProvider* p);

  // Returns true if the two RecordMaps contain the same values (the origins
  // of the values may be different).
  static bool RecordMapValuesEqual(const RecordMap& a, const RecordMap& b);

  // Scopes can have no containing scope (both null), a mutable containing
  // scope, or a const containing scope. The reason is that when we're doing
  // a new target, we want to refer to the base_config scope which will be read
  // by multiple threads at the same time, so we REALLY want it to be const.
  // When you jsut do a nested {}, however, we sometimes want to be able to
  // change things (especially marking unused vars).
  const Scope* const_containing_;
  Scope* mutable_containing_;

  const Settings* settings_;

  // Bits set for different modes. See the flag definitions in the .cc file
  // for more.
  unsigned mode_flags_;

  RecordMap values_;

  // Note that this can't use string pieces since the names are constructed from
  // Values which might be deallocated before this goes out of scope.
  typedef base::hash_map<std::string, std::unique_ptr<Scope>> NamedScopeMap;
  NamedScopeMap target_defaults_;

  // Null indicates not set and that we should fallback to the containing
  // scope's filter.
  std::unique_ptr<PatternList> sources_assignment_filter_;

  // Owning pointers, must be deleted.
  typedef std::map<std::string, scoped_refptr<const Template> > TemplateMap;
  TemplateMap templates_;

  ItemVector* item_collector_;

  // Opaque pointers. See SetProperty() above.
  typedef std::map<const void*, void*> PropertyMap;
  PropertyMap properties_;

  typedef std::set<ProgrammaticProvider*> ProviderSet;
  ProviderSet programmatic_providers_;

  SourceDir source_dir_;

  DISALLOW_COPY_AND_ASSIGN(Scope);
};

#endif  // TOOLS_GN_SCOPE_H_
