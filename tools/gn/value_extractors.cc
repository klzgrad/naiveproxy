// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/value_extractors.h"

#include <stddef.h>

#include "tools/gn/build_settings.h"
#include "tools/gn/err.h"
#include "tools/gn/label.h"
#include "tools/gn/source_dir.h"
#include "tools/gn/source_file.h"
#include "tools/gn/target.h"
#include "tools/gn/value.h"

namespace {

// Sets the error and returns false on failure.
template<typename T, class Converter>
bool ListValueExtractor(const Value& value,
                        std::vector<T>* dest,
                        Err* err,
                        const Converter& converter) {
  if (!value.VerifyTypeIs(Value::LIST, err))
    return false;
  const std::vector<Value>& input_list = value.list_value();
  dest->resize(input_list.size());
  for (size_t i = 0; i < input_list.size(); i++) {
    if (!converter(input_list[i], &(*dest)[i], err))
      return false;
  }
  return true;
}

// Like the above version but extracts to a UniqueVector and sets the error if
// there are duplicates.
template<typename T, class Converter>
bool ListValueUniqueExtractor(const Value& value,
                              UniqueVector<T>* dest,
                              Err* err,
                              const Converter& converter) {
  if (!value.VerifyTypeIs(Value::LIST, err))
    return false;
  const std::vector<Value>& input_list = value.list_value();

  for (const auto& item : input_list) {
    T new_one;
    if (!converter(item, &new_one, err))
      return false;
    if (!dest->push_back(new_one)) {
      // Already in the list, throw error.
      *err = Err(item, "Duplicate item in list");
      size_t previous_index = dest->IndexOf(new_one);
      err->AppendSubErr(Err(input_list[previous_index],
                            "This was the previous definition."));
      return false;
    }
  }
  return true;
}

struct RelativeFileConverter {
  RelativeFileConverter(const BuildSettings* build_settings_in,
                        const SourceDir& current_dir_in)
      : build_settings(build_settings_in),
        current_dir(current_dir_in) {
  }
  bool operator()(const Value& v, SourceFile* out, Err* err) const {
    *out = current_dir.ResolveRelativeFile(v, err,
                                           build_settings->root_path_utf8());
    return !err->has_error();
  }
  const BuildSettings* build_settings;
  const SourceDir& current_dir;
};

struct LibFileConverter {
  LibFileConverter(const BuildSettings* build_settings_in,
                   const SourceDir& current_dir_in)
      : build_settings(build_settings_in),
        current_dir(current_dir_in) {
  }
  bool operator()(const Value& v, LibFile* out, Err* err) const {
    if (!v.VerifyTypeIs(Value::STRING, err))
      return false;
    if (v.string_value().find('/') == std::string::npos) {
      *out = LibFile(v.string_value());
    } else {
      *out = LibFile(current_dir.ResolveRelativeFile(
          v, err, build_settings->root_path_utf8()));
    }
    return !err->has_error();
  }
  const BuildSettings* build_settings;
  const SourceDir& current_dir;
};

struct RelativeDirConverter {
  RelativeDirConverter(const BuildSettings* build_settings_in,
                       const SourceDir& current_dir_in)
      : build_settings(build_settings_in),
        current_dir(current_dir_in) {
  }
  bool operator()(const Value& v, SourceDir* out, Err* err) const {
    *out = current_dir.ResolveRelativeDir(v, err,
                                          build_settings->root_path_utf8());
    return true;
  }
  const BuildSettings* build_settings;
  const SourceDir& current_dir;
};

// Fills in a label.
template<typename T> struct LabelResolver {
  LabelResolver(const SourceDir& current_dir_in,
                const Label& current_toolchain_in)
      : current_dir(current_dir_in),
        current_toolchain(current_toolchain_in) {}
  bool operator()(const Value& v, Label* out, Err* err) const {
    if (!v.VerifyTypeIs(Value::STRING, err))
      return false;
    *out = Label::Resolve(current_dir, current_toolchain, v, err);
    return !err->has_error();
  }
  const SourceDir& current_dir;
  const Label& current_toolchain;
};

// Fills the label part of a LabelPtrPair, leaving the pointer null.
template<typename T> struct LabelPtrResolver {
  LabelPtrResolver(const SourceDir& current_dir_in,
                   const Label& current_toolchain_in)
      : current_dir(current_dir_in),
        current_toolchain(current_toolchain_in) {}
  bool operator()(const Value& v, LabelPtrPair<T>* out, Err* err) const {
    if (!v.VerifyTypeIs(Value::STRING, err))
      return false;
    out->label = Label::Resolve(current_dir, current_toolchain, v, err);
    out->origin = v.origin();
    return !err->has_error();
  }
  const SourceDir& current_dir;
  const Label& current_toolchain;
};

struct LabelPatternResolver {
  LabelPatternResolver(const SourceDir& current_dir_in)
      : current_dir(current_dir_in) {
  }
  bool operator()(const Value& v, LabelPattern* out, Err* err) const {
    *out = LabelPattern::GetPattern(current_dir, v, err);
    return !err->has_error();
  }
  const SourceDir& current_dir;
};

}  // namespace

bool ExtractListOfStringValues(const Value& value,
                               std::vector<std::string>* dest,
                               Err* err) {
  if (!value.VerifyTypeIs(Value::LIST, err))
    return false;
  const std::vector<Value>& input_list = value.list_value();
  dest->reserve(input_list.size());
  for (const auto& item : input_list) {
    if (!item.VerifyTypeIs(Value::STRING, err))
      return false;
    dest->push_back(item.string_value());
  }
  return true;
}

bool ExtractListOfRelativeFiles(const BuildSettings* build_settings,
                                const Value& value,
                                const SourceDir& current_dir,
                                std::vector<SourceFile>* files,
                                Err* err) {
  return ListValueExtractor(value, files, err,
                            RelativeFileConverter(build_settings, current_dir));
}

bool ExtractListOfLibs(const BuildSettings* build_settings,
                       const Value& value,
                       const SourceDir& current_dir,
                       std::vector<LibFile>* libs,
                       Err* err) {
  return ListValueExtractor(value, libs, err,
                            LibFileConverter(build_settings, current_dir));
}

bool ExtractListOfRelativeDirs(const BuildSettings* build_settings,
                               const Value& value,
                               const SourceDir& current_dir,
                               std::vector<SourceDir>* dest,
                               Err* err) {
  return ListValueExtractor(value, dest, err,
                            RelativeDirConverter(build_settings, current_dir));
}

bool ExtractListOfLabels(const Value& value,
                         const SourceDir& current_dir,
                         const Label& current_toolchain,
                         LabelTargetVector* dest,
                         Err* err) {
  return ListValueExtractor(value, dest, err,
                            LabelPtrResolver<Target>(current_dir,
                                                     current_toolchain));
}

bool ExtractListOfUniqueLabels(const Value& value,
                               const SourceDir& current_dir,
                               const Label& current_toolchain,
                               UniqueVector<Label>* dest,
                               Err* err) {
  return ListValueUniqueExtractor(value, dest, err,
                                  LabelResolver<Config>(current_dir,
                                                        current_toolchain));
}

bool ExtractListOfUniqueLabels(const Value& value,
                               const SourceDir& current_dir,
                               const Label& current_toolchain,
                               UniqueVector<LabelConfigPair>* dest,
                               Err* err) {
  return ListValueUniqueExtractor(value, dest, err,
                                  LabelPtrResolver<Config>(current_dir,
                                                           current_toolchain));
}

bool ExtractListOfUniqueLabels(const Value& value,
                               const SourceDir& current_dir,
                               const Label& current_toolchain,
                               UniqueVector<LabelTargetPair>* dest,
                               Err* err) {
  return ListValueUniqueExtractor(value, dest, err,
                                  LabelPtrResolver<Target>(current_dir,
                                                           current_toolchain));
}

bool ExtractRelativeFile(const BuildSettings* build_settings,
                         const Value& value,
                         const SourceDir& current_dir,
                         SourceFile* file,
                         Err* err) {
  RelativeFileConverter converter(build_settings, current_dir);
  return converter(value, file, err);
}

bool ExtractListOfLabelPatterns(const Value& value,
                                const SourceDir& current_dir,
                                std::vector<LabelPattern>* patterns,
                                Err* err) {
  return ListValueExtractor(value, patterns, err,
                            LabelPatternResolver(current_dir));
}
