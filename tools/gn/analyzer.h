// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_ANALYZER_H_
#define TOOLS_GN_ANALYZER_H_

#include <set>
#include <string>
#include <vector>

#include "tools/gn/builder.h"
#include "tools/gn/item.h"
#include "tools/gn/label.h"
#include "tools/gn/source_file.h"

// An Analyzer can answer questions about a build graph. It is used
// to answer queries for the `refs` and `analyze` commands, where we
// need to look at the graph in ways that can't easily be determined
// from just a single Target.
class Analyzer {
 public:
  Analyzer(const Builder& builder,
           const SourceFile& build_config_file,
           const SourceFile& dot_file,
           const std::set<SourceFile>& build_args_dependency_files);
  ~Analyzer();

  // Figures out from a Buider and a JSON-formatted string containing lists
  // of files and targets, which targets would be affected by modifications
  // to the files . See the help text for the analyze command (kAnalyze_Help)
  // for the specification of the input and output string formats and the
  // expected behavior of the method.
  std::string Analyze(const std::string& input, Err* err) const;

 private:
  // Returns the set of all items that might be affected, directly or
  // indirectly, by modifications to the given source files.
  std::set<const Item*> GetAllAffectedItems(
      const std::set<const SourceFile*>& source_files) const;

  // Returns the set of labels that do not refer to objects in the graph.
  std::set<Label> InvalidLabels(const std::set<Label>& labels) const;

  // Returns the set of all targets that have a label in the given set.
  // Invalid (or missing) labels will be ignored.
  std::set<const Target*> TargetsFor(const std::set<Label>& labels) const;

  // Returns a filtered set of the given targets, meaning that for each of the
  // given targets,
  // - if the target is not a group, add it to the set
  // - if the target is a group, recursively filter each dependency and add
  //   its filtered results to the set.
  //
  // For example, if we had:
  //
  //   group("foobar") { deps = [ ":foo", ":bar" ] }
  //   group("bar") { deps = [ ":baz", ":quux" ] }
  //   executable("foo") { ... }
  //   executable("baz") { ... }
  //   executable("quux") { ... }
  //
  // Then the filtered version of {"foobar"} would be {":foo", ":baz",
  // ":quux"}. This is used by the analyze command in order to only build
  // the affected dependencies of a group (and not also build the unaffected
  // ones).
  //
  // This filtering behavior is also known as "pruning" the list of targets.
  std::set<const Target*> Filter(const std::set<const Target*>& targets) const;

  // Filter an individual target and adds the results to filtered
  // (see Filter(), above).
  void FilterTarget(const Target*,
                    std::set<const Target*>* seen,
                    std::set<const Target*>* filtered) const;

  bool ItemRefersToFile(const Item* item, const SourceFile* file) const;

  void AddItemsDirectlyReferringToFile(
      const SourceFile* file,
      std::set<const Item*>* affected_items) const;

  void AddAllItemsReferringToItem(const Item* item,
                                  std::set<const Item*>* affected_items) const;

  // Main GN files stand for files whose context are used globally to execute
  // every other build files, this list includes dot file, build config file,
  // build args files etc.
  bool WereMainGNFilesModified(
      const std::set<const SourceFile*>& modified_files) const;

  std::vector<const Item*> all_items_;
  std::map<Label, const Item*> labels_to_items_;
  Label default_toolchain_;

  // Maps items to the list of items that depend on them.
  std::multimap<const Item*, const Item*> dep_map_;

  const SourceFile build_config_file_;
  const SourceFile dot_file_;
  const std::set<SourceFile> build_args_dependency_files_;
};

#endif  // TOOLS_GN_ANALYZER_H_
