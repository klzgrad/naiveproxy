// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_ANALYZER_H_
#define TOOLS_GN_ANALYZER_H_

#include <set>
#include <string>
#include <vector>

#include "tools/gn/builder.h"
#include "tools/gn/label.h"
#include "tools/gn/source_file.h"
#include "tools/gn/target.h"

// An Analyzer can answer questions about a build graph. It is used
// to answer queries for the `refs` and `analyze` commands, where we
// need to look at the graph in ways that can't easily be determined
// from just a single Target.
class Analyzer {
 public:
  using LabelSet = std::set<Label>;
  using SourceFileSet = std::set<const SourceFile*>;
  using TargetSet = std::set<const Target*>;

  explicit Analyzer(const Builder& builder);
  ~Analyzer();

  // Figures out from a Buider and a JSON-formatted string containing lists
  // of files and targets, which targets would be affected by modifications
  // to the files . See the help text for the analyze command (kAnalyze_Help)
  // for the specification of the input and output string formats and the
  // expected behavior of the method.
  std::string Analyze(const std::string& input, Err* err) const;

 private:
  // Returns the roots of the build graph: the set of targets that
  // no other target depends on.
  TargetSet& roots() { return roots_; };

  // Returns the set of all targets that might be affected, directly or
  // indirectly, by modifications to the given source files.
  TargetSet AllAffectedTargets(const SourceFileSet& source_files) const;

  // Returns the set of labels that do not refer to objects in the graph.
  LabelSet InvalidLabels(const LabelSet& labels) const;

  // Returns the set of all targets that have a label in the given set.
  // Invalid (or missing) labels will be ignored.
  TargetSet TargetsFor(const LabelSet& labels) const;

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
  TargetSet Filter(const TargetSet& targets) const;

  // Filter an individual target and adds the results to filtered
  // (see Filter(), above).
  void FilterTarget(const Target*, TargetSet* seen, TargetSet* filtered) const;

  bool TargetRefersToFile(const Target* target, const SourceFile* file) const;

  void AddTargetsDirectlyReferringToFileTo(const SourceFile* file,
                                           TargetSet* matches) const;

  void AddAllRefsTo(const Target* target, TargetSet* matches) const;

  std::vector<const Target*> all_targets_;
  std::map<const Label, const Target*> labels_to_targets_;
  Label default_toolchain_;
  std::set<const Target*> roots_;

  // Maps targets to the list of targets that depend on them.
  std::multimap<const Target*, const Target*> dep_map_;
};

#endif  // TOOLS_GN_ANALYZER_H_
