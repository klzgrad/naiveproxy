// Copyright (C) 2025 Rivos Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdlib.h>

#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include "perfetto/ext/base/string_utils.h"

namespace protozero {
namespace {

using google::protobuf::Descriptor;
using google::protobuf::EnumDescriptor;
using google::protobuf::EnumValueDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::FileDescriptor;
using google::protobuf::compiler::GeneratorContext;
using google::protobuf::io::Printer;
using google::protobuf::io::ZeroCopyOutputStream;
using perfetto::base::ReplaceAll;
using perfetto::base::SplitString;
using perfetto::base::StripChars;
using perfetto::base::StripPrefix;
using perfetto::base::StripSuffix;
using perfetto::base::ToUpper;
using perfetto::base::Uppercase;

void Assert(bool condition) {
  if (!condition)
    abort();
}

// Maximum line length for single-line pb_enum! macro invocations.
// Enums that would produce output longer than this threshold will be
// formatted across multiple lines for readability.
constexpr size_t kMaxSingleLinePbEnumLength = 60;

struct FileDescriptorComp {
  bool operator()(const FileDescriptor* lhs, const FileDescriptor* rhs) const {
    int comp = lhs->name().compare(rhs->name());
    Assert(comp != 0 || lhs == rhs);
    return comp < 0;
  }
};

struct DescriptorComp {
  bool operator()(const Descriptor* lhs, const Descriptor* rhs) const {
    int comp = lhs->full_name().compare(rhs->full_name());
    Assert(comp != 0 || lhs == rhs);
    return comp < 0;
  }
};

struct EnumDescriptorComp {
  bool operator()(const EnumDescriptor* lhs, const EnumDescriptor* rhs) const {
    int comp = lhs->full_name().compare(rhs->full_name());
    Assert(comp != 0 || lhs == rhs);
    return comp < 0;
  }
};

inline std::string ProtoStubName(const FileDescriptor* proto) {
  return StripSuffix(std::string(proto->name()), ".proto");
}

std::string IntLiteralString(int number) {
  // Special case for -2147483648. If int is 32-bit, the compiler will
  // misinterpret it.
  if (number == std::numeric_limits<int32_t>::min()) {
    return "-2147483647 - 1";
  }
  return std::to_string(number);
}

class GeneratorJob {
 public:
  GeneratorJob(const FileDescriptor* file, Printer* stub_rs_printer)
      : source_(file), stub_rs_(stub_rs_printer) {}

  bool GenerateStubs() {
    Preprocess();
    GeneratePrologue();
    for (const EnumDescriptor* enumeration : enums_)
      GenerateEnumDescriptor(enumeration);
    for (const Descriptor* message : messages_)
      GenerateMessageDescriptor(message);
    return error_.empty();
  }

  void SetOption(const std::string& name, const std::string& value) {
    if (name == "wrapper_namespace") {
      wrapper_namespace_ = value;
    } else if (name == "path_strip_prefix") {
      path_strip_prefix_ = value;
    } else if (name == "path_add_prefix") {
      path_add_prefix_ = value;
    } else if (name == "invoker") {
      invoker_ = value;
    } else {
      Abort(std::string() + "Unknown plugin option '" + name + "'.");
    }
  }

  // If generator fails to produce stubs for a particular proto definitions
  // it finishes with undefined output and writes the first error occurred.
  const std::string& GetFirstError() const { return error_; }

 private:
  // Only the first error will be recorded.
  void Abort(const std::string& reason) {
    if (error_.empty())
      error_ = reason;
  }

  // Get Rust struct name corresponding to proto descriptor (simple name only).
  template <class T>
  inline std::string GetRustStructName(const T* descriptor) {
    return StripChars(std::string(descriptor->name()), ".", '_');
  }

  // Get full Rust struct name including parent type names for nested messages.
  std::string GetFullRustMessageName(const Descriptor* descriptor) {
    std::string name;
    if (descriptor->containing_type()) {
      name.append(GetFullRustMessageName(descriptor->containing_type()));
    }
    name.append(GetRustStructName(descriptor));
    return name;
  }

  std::string FieldToRustTypeName(const FieldDescriptor* field) {
    switch (field->type()) {
      case FieldDescriptor::TYPE_BOOL:
        return "bool";
      case FieldDescriptor::TYPE_INT32:
        return "i32";
      case FieldDescriptor::TYPE_INT64:
        return "i64";
      case FieldDescriptor::TYPE_UINT32:
        return "u32";
      case FieldDescriptor::TYPE_UINT64:
        return "u64";
      case FieldDescriptor::TYPE_SINT32:
        return "i32";
      case FieldDescriptor::TYPE_SINT64:
        return "i64";
      case FieldDescriptor::TYPE_FIXED32:
        return "u32";
      case FieldDescriptor::TYPE_FIXED64:
        return "u64";
      case FieldDescriptor::TYPE_SFIXED32:
        return "i32";
      case FieldDescriptor::TYPE_SFIXED64:
        return "i64";
      case FieldDescriptor::TYPE_FLOAT:
        return "f32";
      case FieldDescriptor::TYPE_DOUBLE:
        return "f64";
      case FieldDescriptor::TYPE_ENUM: {
        std::string name;
        if (field->enum_type()->containing_type()) {
          name.append(GetRustStructName(field->enum_type()->containing_type()));
        }
        name.append(GetRustStructName(field->enum_type()));
        return name;
      }
      case FieldDescriptor::TYPE_STRING:
        return "String";
      case FieldDescriptor::TYPE_BYTES:
        return "String";
      case FieldDescriptor::TYPE_MESSAGE:
        return GetFullRustMessageName(field->message_type());
      case FieldDescriptor::TYPE_GROUP:
        Abort("Groups not supported.");
        return "";
    }
    Abort("Unrecognized FieldDescriptor::Type.");
    return "";
  }

  void CollectDescriptors() {
    // Collect message descriptors in DFS order.
    std::vector<const Descriptor*> stack;
    stack.reserve(static_cast<size_t>(source_->message_type_count()));
    for (int i = 0; i < source_->message_type_count(); ++i)
      stack.push_back(source_->message_type(i));

    while (!stack.empty()) {
      const Descriptor* message = stack.back();
      stack.pop_back();

      if (message->extension_count() > 0) {
        if (message->field_count() > 0 || message->nested_type_count() > 0 ||
            message->enum_type_count() > 0) {
          Abort("message with extend blocks shouldn't contain anything else");
        }

        // Iterate over all fields in "extend" blocks.
        for (int i = 0; i < message->extension_count(); ++i) {
          const FieldDescriptor* extension = message->extension(i);

          // Protoc plugin API does not group fields in "extend" blocks.
          // As the support for extensions in protozero is limited, the code
          // assumes that extend blocks are located inside a wrapper message and
          // name of this message is used to group them.
          std::string extension_name =
              GetRustStructName(extension->extension_scope());
          extensions_[extension_name].push_back(extension);
        }
      } else {
        messages_.push_back(message);
        for (int i = 0; i < message->nested_type_count(); ++i) {
          stack.push_back(message->nested_type(i));
          // Emit a use statemewnt for nested message types, as the outer
          // struct will refer to them.
          referenced_messages_.insert(message->nested_type(i));
        }
      }
    }

    // Collect enums.
    for (int i = 0; i < source_->enum_type_count(); ++i)
      enums_.push_back(source_->enum_type(i));

    for (const Descriptor* message : messages_) {
      for (int i = 0; i < message->enum_type_count(); ++i) {
        enums_.push_back(message->enum_type(i));
      }
    }
  }

  void CollectDependencies() {
    // Public import basically means that callers only need to import this
    // proto in order to use the stuff publicly imported by this proto.
    for (int i = 0; i < source_->public_dependency_count(); ++i)
      public_imports_.insert(source_->public_dependency(i));

    if (source_->weak_dependency_count() > 0)
      Abort("Weak imports are not supported.");

    // Validations. Collect public imports (of collected imports) in DFS order.
    // Visibilty for current proto:
    // - all imports listed in current proto,
    // - public imports of everything imported (recursive).
    std::vector<const FileDescriptor*> stack;
    for (int i = 0; i < source_->dependency_count(); ++i) {
      const FileDescriptor* imp = source_->dependency(i);
      stack.push_back(imp);
      if (public_imports_.count(imp) == 0) {
        private_imports_.insert(imp);
      }
    }

    while (!stack.empty()) {
      const FileDescriptor* imp = stack.back();
      stack.pop_back();
      for (int i = 0; i < imp->public_dependency_count(); ++i) {
        stack.push_back(imp->public_dependency(i));
      }
    }

    // Collect descriptors of messages and enums used in current proto.
    // It will be used to generate the necessary "use" statements.
    for (const Descriptor* message : messages_) {
      for (int i = 0; i < message->field_count(); ++i) {
        const FieldDescriptor* field = message->field(i);

        if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
          if (public_imports_.count(field->message_type()->file()) == 0) {
            referenced_messages_.insert(field->message_type());
          }
        } else if (field->type() == FieldDescriptor::TYPE_ENUM) {
          if (public_imports_.count(field->enum_type()->file()) == 0) {
            referenced_enums_.insert(field->enum_type());
          }
        }
      }
    }
  }

  void Preprocess() {
    // Package name maps to a series of namespaces.
    package_ = source_->package();
    namespaces_ = SplitString(package_, ".");
    if (!wrapper_namespace_.empty())
      namespaces_.push_back(wrapper_namespace_);

    full_namespace_prefix_ = "";
    for (size_t i = 0; i < namespaces_.size(); i++) {
      full_namespace_prefix_ += namespaces_[i];
      if (i + 1 != namespaces_.size()) {
        full_namespace_prefix_ += "_";
      }
    }

    CollectDescriptors();
    CollectDependencies();
  }

  // Print top header, namespaces and forward declarations.
  void GeneratePrologue() {
    stub_rs_->Print(
        R"(// Copyright (C) 2025 Rivos Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

)");
    stub_rs_->Print(
        "// Autogenerated by the ProtoZero Rust compiler plugin.\n");
    if (!invoker_.empty()) {
      stub_rs_->Print("// Invoked by $invoker$\n", "invoker", invoker_);
    }
    stub_rs_->Print("// DO NOT EDIT.\n\n");
    if (!enums_.empty()) {
      stub_rs_->Print("use crate::pb_enum;\n");
    }
    if (!messages_.empty()) {
      stub_rs_->Print("use crate::pb_msg;\n");
    }

    // Print use statements for public imports, enums and messages.
    std::vector<std::string> imports;
    for (const FileDescriptor* dependency : public_imports_) {
      imports.push_back(ProtoStubName(dependency));
    }
    for (const EnumDescriptor* e : referenced_enums_) {
      if (e->file() != source_) {
        imports.push_back(ProtoStubName(e->file()));
      }
    }
    for (const Descriptor* m : referenced_messages_) {
      if (m->file() != source_) {
        imports.push_back(ProtoStubName(m->file()));
      }
    }

    std::sort(imports.begin(), imports.end());

    // Remove consecutive duplicates.
    auto last = std::unique(imports.begin(), imports.end());
    imports.erase(last, imports.end());

    for (const std::string& imp : imports) {
      std::string mod_path = imp;
      if (!path_strip_prefix_.empty()) {
        mod_path = StripPrefix(imp, path_strip_prefix_);
      }
      stub_rs_->Print("use crate::protos$mod$::*;\n", "mod",
                      ReplaceAll(mod_path, "/", "::"));
    }
  }

  void GenerateEnumDescriptor(const EnumDescriptor* enumeration) {
    std::string name;
    if (enumeration->containing_type()) {
      name.append(GetRustStructName(enumeration->containing_type()));
    }
    name.append(GetRustStructName(enumeration));

    // Build enum values content and calculate single-line length
    std::string values_content;
    for (int i = 0; i < enumeration->value_count(); ++i) {
      const EnumValueDescriptor* value = enumeration->value(i);
      if (i > 0) {
        values_content += ", ";
      }
      values_content += value->name();
      values_content += ": ";
      values_content += IntLiteralString(value->number());
    }

    // Single-line format: "pb_enum!(" + name + " { " + values + " });"
    // For empty: "pb_enum!(" + name + " {});" = 14 + name.length()
    // For non-empty: "pb_enum!(" + name + " { " + values + " });" = 16 +
    // name.length() + values.length()
    size_t single_line_length =
        enumeration->value_count() == 0
            ? 14 + name.length()
            : 16 + name.length() + values_content.length();

    if (single_line_length <= kMaxSingleLinePbEnumLength) {
      if (enumeration->value_count() == 0) {
        stub_rs_->Print("\npb_enum!($name$ {});\n", "name", name);
      } else {
        stub_rs_->Print("\npb_enum!($name$ { $values$ });\n", "name", name,
                        "values", values_content);
      }
    } else {
      stub_rs_->Print("\npb_enum!($name$ {\n", "name", name);
      for (int i = 0; i < enumeration->value_count(); ++i) {
        const EnumValueDescriptor* value = enumeration->value(i);
        const std::string value_name = std::string(value->name());
        stub_rs_->Print("    ");
        stub_rs_->Print("$val$: $number$,\n", "val", value_name, "number",
                        IntLiteralString(value->number()));
      }
      stub_rs_->Print("});\n");
    }
  }

  // Generate field descriptor content as a string (without
  // indentation/newline).
  std::string GetFieldDescriptorContent(const FieldDescriptor* field) {
    std::string name = std::string(field->lowercase_name());
    std::string id = std::to_string(field->number());

    if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
      std::string inner_struct = GetFullRustMessageName(field->message_type());
      return name + ": " + inner_struct + ", msg, " + id + ",";
    }

    std::string type = FieldToRustTypeName(field);
    std::string kind;
    switch (field->type()) {
      case FieldDescriptor::TYPE_ENUM:
        kind = "enum";
        break;
      case FieldDescriptor::TYPE_GROUP:
        Abort("Groups not supported.");
        return "";
      default:
        kind = "primitive";
        break;
    }
    return name + ": " + type + ", " + kind + ", " + id + ",";
  }

  void GenerateMessageDescriptor(const Descriptor* message) {
    std::string name = GetFullRustMessageName(message);

    if (message->field_count() == 0) {
      stub_rs_->Print("\npb_msg!($name$ {});\n", "name", name);
    } else {
      stub_rs_->Print("\npb_msg!($name$ {\n", "name", name);
      for (int i = 0; i < message->field_count(); ++i) {
        stub_rs_->Print("    $field$\n", "field",
                        GetFieldDescriptorContent(message->field(i)));
      }
      stub_rs_->Print("});\n");
    }
  }

  const FileDescriptor* const source_;
  Printer* const stub_rs_;
  std::string error_;

  std::string package_;
  std::string wrapper_namespace_;
  std::string path_strip_prefix_;
  std::string path_add_prefix_;
  std::string invoker_;
  std::vector<std::string> namespaces_;
  std::string full_namespace_prefix_;
  std::vector<const Descriptor*> messages_;
  std::vector<const EnumDescriptor*> enums_;
  std::map<std::string, std::vector<const FieldDescriptor*>> extensions_;

  // The custom *Comp comparators are to ensure determinism of the generator.
  std::set<const FileDescriptor*, FileDescriptorComp> public_imports_;
  std::set<const FileDescriptor*, FileDescriptorComp> private_imports_;
  std::set<const Descriptor*, DescriptorComp> referenced_messages_;
  std::set<const EnumDescriptor*, EnumDescriptorComp> referenced_enums_;
};

class ProtoZeroRustGenerator
    : public google::protobuf::compiler::CodeGenerator {
 public:
  explicit ProtoZeroRustGenerator();
  ~ProtoZeroRustGenerator() override;

  // CodeGenerator implementation
  bool Generate(const google::protobuf::FileDescriptor* file,
                const std::string& options,
                GeneratorContext* context,
                std::string* error) const override;
};

ProtoZeroRustGenerator::ProtoZeroRustGenerator() {}

ProtoZeroRustGenerator::~ProtoZeroRustGenerator() {}

bool ProtoZeroRustGenerator::Generate(const FileDescriptor* file,
                                      const std::string& options,
                                      GeneratorContext* context,
                                      std::string* error) const {
  const std::unique_ptr<ZeroCopyOutputStream> stub_rs_file_stream(
      context->Open(ProtoStubName(file) + ".pz.rs"));

  // Variables are delimited by $.
  Printer stub_rs_printer(stub_rs_file_stream.get(), '$');
  GeneratorJob job(file, &stub_rs_printer);

  // Parse additional options.
  for (const std::string& option : SplitString(options, ",")) {
    std::vector<std::string> option_pair = SplitString(option, "=");
    job.SetOption(option_pair[0], option_pair[1]);
  }

  if (!job.GenerateStubs()) {
    *error = job.GetFirstError();
    return false;
  }
  return true;
}

}  // namespace
}  // namespace protozero

int main(int argc, char* argv[]) {
  protozero::ProtoZeroRustGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
