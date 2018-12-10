// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/eclipse_writer.h"

#include <fstream>
#include <memory>

#include "base/files/file_path.h"
#include "tools/gn/builder.h"
#include "tools/gn/config_values_extractors.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/loader.h"
#include "tools/gn/xml_element_writer.h"

namespace {

// Escapes |unescaped| for use in XML element content.
std::string EscapeForXML(const std::string& unescaped) {
  std::string result;
  result.reserve(unescaped.length());
  for (const char c : unescaped) {
    if (c == '<')
      result += "&lt;";
    else if (c == '>')
      result += "&gt;";
    else if (c == '&')
      result += "&amp;";
    else
      result.push_back(c);
  }
  return result;
}

}  // namespace

EclipseWriter::EclipseWriter(const BuildSettings* build_settings,
                             const Builder& builder,
                             std::ostream& out)
    : build_settings_(build_settings), builder_(builder), out_(out) {
  languages_.push_back("C++ Source File");
  languages_.push_back("C Source File");
  languages_.push_back("Assembly Source File");
  languages_.push_back("GNU C++");
  languages_.push_back("GNU C");
  languages_.push_back("Assembly");
}

EclipseWriter::~EclipseWriter() = default;

// static
bool EclipseWriter::RunAndWriteFile(const BuildSettings* build_settings,
                                    const Builder& builder,
                                    Err* err) {
  base::FilePath file = build_settings->GetFullPath(build_settings->build_dir())
                            .AppendASCII("eclipse-cdt-settings.xml");
  std::ofstream file_out;
  file_out.open(FilePathToUTF8(file).c_str(),
                std::ios_base::out | std::ios_base::binary);
  if (file_out.fail()) {
    *err =
        Err(Location(), "Couldn't open eclipse-cdt-settings.xml for writing");
    return false;
  }

  EclipseWriter gen(build_settings, builder, file_out);
  gen.Run();
  return true;
}

void EclipseWriter::Run() {
  GetAllIncludeDirs();
  GetAllDefines();
  WriteCDTSettings();
}

void EclipseWriter::GetAllIncludeDirs() {
  std::vector<const Target*> targets = builder_.GetAllResolvedTargets();
  for (const Target* target : targets) {
    if (!UsesDefaultToolchain(target))
      continue;

    for (ConfigValuesIterator it(target); !it.done(); it.Next()) {
      for (const SourceDir& include_dir : it.cur().include_dirs()) {
        include_dirs_.insert(
            FilePathToUTF8(build_settings_->GetFullPath(include_dir)));
      }
    }
  }
}

void EclipseWriter::GetAllDefines() {
  std::vector<const Target*> targets = builder_.GetAllResolvedTargets();
  for (const Target* target : targets) {
    if (!UsesDefaultToolchain(target))
      continue;

    for (ConfigValuesIterator it(target); !it.done(); it.Next()) {
      for (const std::string& define : it.cur().defines()) {
        size_t equal_pos = define.find('=');
        std::string define_key;
        std::string define_value;
        if (equal_pos == std::string::npos) {
          define_key = define;
        } else {
          define_key = define.substr(0, equal_pos);
          define_value = define.substr(equal_pos + 1);
        }
        defines_[define_key] = define_value;
      }
    }
  }
}

bool EclipseWriter::UsesDefaultToolchain(const Target* target) const {
  return target->toolchain()->label() ==
         builder_.loader()->GetDefaultToolchain();
}

void EclipseWriter::WriteCDTSettings() {
  out_ << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
  XmlElementWriter cdt_properties_element(out_, "cdtprojectproperties",
                                          XmlAttributes());

  {
    const char* kIncludesSectionName =
        "org.eclipse.cdt.internal.ui.wizards.settingswizards.IncludePaths";
    std::unique_ptr<XmlElementWriter> section_element =
        cdt_properties_element.SubElement(
            "section", XmlAttributes("name", kIncludesSectionName));

    section_element->SubElement(
        "language", XmlAttributes("name", "holder for library settings"));

    for (const std::string& language : languages_) {
      std::unique_ptr<XmlElementWriter> language_element =
          section_element->SubElement("language",
                                      XmlAttributes("name", language));
      for (const std::string& include_dir : include_dirs_) {
        language_element
            ->SubElement("includepath",
                         XmlAttributes("workspace_path", "false"))
            ->Text(EscapeForXML(include_dir));
      }
    }
  }

  {
    const char* kMacrosSectionName =
        "org.eclipse.cdt.internal.ui.wizards.settingswizards.Macros";
    std::unique_ptr<XmlElementWriter> section_element =
        cdt_properties_element.SubElement(
            "section", XmlAttributes("name", kMacrosSectionName));

    section_element->SubElement(
        "language", XmlAttributes("name", "holder for library settings"));

    for (const std::string& language : languages_) {
      std::unique_ptr<XmlElementWriter> language_element =
          section_element->SubElement("language",
                                      XmlAttributes("name", language));
      for (const auto& key_val : defines_) {
        std::unique_ptr<XmlElementWriter> macro_element =
            language_element->SubElement("macro");
        macro_element->SubElement("name")->Text(EscapeForXML(key_val.first));
        macro_element->SubElement("value")->Text(EscapeForXML(key_val.second));
      }
    }
  }
}
