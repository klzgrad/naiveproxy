/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
  return StripSuffix(std::string(proto->name()), ".proto") + ".pzc";
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
  GeneratorJob(const FileDescriptor* file, Printer* stub_h_printer)
      : source_(file), stub_h_(stub_h_printer) {}

  bool GenerateStubs() {
    Preprocess();
    GeneratePrologue();
    for (const EnumDescriptor* enumeration : enums_)
      GenerateEnumDescriptor(enumeration);
    for (const Descriptor* message : messages_)
      GenerateMessageDescriptor(message);
    for (const auto& [name, descriptors] : extensions_)
      GenerateExtension(name, descriptors);
    GenerateEpilogue();
    return error_.empty();
  }

  void SetOption(const std::string& name, const std::string& value) {
    if (name == "wrapper_namespace") {
      wrapper_namespace_ = value;
    } else if (name == "guard_strip_prefix") {
      guard_strip_prefix_ = value;
    } else if (name == "guard_add_prefix") {
      guard_add_prefix_ = value;
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

  // Get C++ class name corresponding to proto descriptor.
  // Nested names are splitted by underscores. Underscores in type names aren't
  // prohibited but not recommended in order to avoid name collisions.
  template <class T>
  inline std::string GetCppClassName(const T* descriptor) {
    return StripChars(std::string(descriptor->full_name()), ".", '_');
  }

  const char* FieldTypeToPackedBufferType(FieldDescriptor::Type type) {
    switch (type) {
      case FieldDescriptor::TYPE_ENUM:
      case FieldDescriptor::TYPE_INT32:
        return "Int32";
      case FieldDescriptor::TYPE_INT64:
        return "Int64";
      case FieldDescriptor::TYPE_UINT32:
        return "Uint32";
      case FieldDescriptor::TYPE_UINT64:
        return "Uint64";
      case FieldDescriptor::TYPE_SINT32:
        return "Sint32";
      case FieldDescriptor::TYPE_SINT64:
        return "Sint64";
      case FieldDescriptor::TYPE_FIXED32:
        return "Fixed32";
      case FieldDescriptor::TYPE_FIXED64:
        return "Fixed64";
      case FieldDescriptor::TYPE_SFIXED32:
        return "Sfixed32";
      case FieldDescriptor::TYPE_SFIXED64:
        return "Sfixed64";
      case FieldDescriptor::TYPE_FLOAT:
        return "Float";
      case FieldDescriptor::TYPE_DOUBLE:
        return "Double";
      case FieldDescriptor::TYPE_BOOL:
      case FieldDescriptor::TYPE_STRING:
      case FieldDescriptor::TYPE_BYTES:
      case FieldDescriptor::TYPE_MESSAGE:
      case FieldDescriptor::TYPE_GROUP:
        break;
    }
    Abort("Unsupported packed type");
    return "";
  }
  std::string FieldToCppTypeName(const FieldDescriptor* field) {
    switch (field->type()) {
      case FieldDescriptor::TYPE_BOOL:
        return "bool";
      case FieldDescriptor::TYPE_INT32:
        return "int32_t";
      case FieldDescriptor::TYPE_INT64:
        return "int64_t";
      case FieldDescriptor::TYPE_UINT32:
        return "uint32_t";
      case FieldDescriptor::TYPE_UINT64:
        return "uint64_t";
      case FieldDescriptor::TYPE_SINT32:
        return "int32_t";
      case FieldDescriptor::TYPE_SINT64:
        return "int64_t";
      case FieldDescriptor::TYPE_FIXED32:
        return "uint32_t";
      case FieldDescriptor::TYPE_FIXED64:
        return "uint64_t";
      case FieldDescriptor::TYPE_SFIXED32:
        return "int32_t";
      case FieldDescriptor::TYPE_SFIXED64:
        return "int64_t";
      case FieldDescriptor::TYPE_FLOAT:
        return "float";
      case FieldDescriptor::TYPE_DOUBLE:
        return "double";
      case FieldDescriptor::TYPE_ENUM:
        return "enum " + GetCppClassName(field->enum_type());
      case FieldDescriptor::TYPE_STRING:
      case FieldDescriptor::TYPE_BYTES:
        return "const char*";
      case FieldDescriptor::TYPE_MESSAGE:
        return GetCppClassName(field->message_type());
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
              GetCppClassName(extension->extension_scope());
          extensions_[extension_name].push_back(extension);
        }
      } else {
        messages_.push_back(message);
        for (int i = 0; i < message->nested_type_count(); ++i) {
          stack.push_back(message->nested_type(i));
          // Emit a forward declaration of nested message types, as the outer
          // class will refer to them when creating type aliases.
          referenced_messages_.insert(message->nested_type(i));
        }
      }
    }

    // Collect enums.
    for (int i = 0; i < source_->enum_type_count(); ++i)
      enums_.push_back(source_->enum_type(i));

    if (source_->extension_count() > 0) {
      // TODO(b/336524288): emit field numbers
    }

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
    // It will be used to generate necessary forward declarations and
    // check that everything lays in the same namespace.
    for (const Descriptor* message : messages_) {
      for (int i = 0; i < message->field_count(); ++i) {
        const FieldDescriptor* field = message->field(i);

        if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
          if (public_imports_.count(field->message_type()->file()) == 0) {
            // Avoid multiple forward declarations since
            // public imports have been already included.
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

  std::string GenerateGuard() {
    std::string guard = StripSuffix(std::string(source_->name()), ".proto");
    guard = ToUpper(guard);
    guard = StripChars(guard, ".-/\\", '_');
    guard = StripPrefix(guard, guard_strip_prefix_);
    guard = guard_add_prefix_ + guard + "_PZC_H_";
    return guard;
  }

  // Print top header, namespaces and forward declarations.
  void GeneratePrologue() {
    stub_h_->Print(
        R"(/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

)");
    stub_h_->Print("// Autogenerated by the ProtoZero C compiler plugin.\n");
    if (!invoker_.empty()) {
      stub_h_->Print("// Invoked by $invoker$\n", "invoker", invoker_);
    }
    stub_h_->Print("// DO NOT EDIT.\n");

    stub_h_->Print(
        "#ifndef $guard$\n"
        "#define $guard$\n\n"
        "#include <stdbool.h>\n"
        "#include <stdint.h>\n\n"
        "#include \"perfetto/public/pb_macros.h\"\n",
        "guard", GenerateGuard());

    // Print includes for public imports and enums which cannot be forward
    // declared.
    std::vector<std::string> imports;
    for (const FileDescriptor* dependency : public_imports_) {
      imports.push_back(ProtoStubName(dependency));
    }
    for (const EnumDescriptor* e : referenced_enums_) {
      if (e->file() != source_) {
        imports.push_back(ProtoStubName(e->file()));
      }
    }

    std::sort(imports.begin(), imports.end());

    for (const std::string& imp : imports) {
      std::string include_path = imp;
      if (!path_strip_prefix_.empty()) {
        include_path = StripPrefix(imp, path_strip_prefix_);
      }
      include_path = path_add_prefix_ + include_path;

      stub_h_->Print("#include \"$name$.h\"\n", "name", include_path);
    }
    stub_h_->Print("\n");

    // Print forward declarations.
    for (const Descriptor* message : referenced_messages_) {
      stub_h_->Print("PERFETTO_PB_MSG_DECL($class$);\n", "class",
                     GetCppClassName(message));
    }

    stub_h_->Print("\n");
  }

  void GenerateEnumDescriptor(const EnumDescriptor* enumeration) {
    if (enumeration->containing_type()) {
      stub_h_->Print("PERFETTO_PB_ENUM_IN_MSG($msg$, $class$){\n", "msg",
                     GetCppClassName(enumeration->containing_type()), "class",
                     enumeration->name());
    } else {
      stub_h_->Print("PERFETTO_PB_ENUM($class$){\n", "class",
                     GetCppClassName(enumeration));
    }
    stub_h_->Indent();

    for (int i = 0; i < enumeration->value_count(); ++i) {
      const EnumValueDescriptor* value = enumeration->value(i);
      const std::string value_name = std::string(value->name());

      if (enumeration->containing_type()) {
        stub_h_->Print(
            "PERFETTO_PB_ENUM_IN_MSG_ENTRY($msg$, $val$) = $number$,\n", "msg",
            GetCppClassName(enumeration->containing_type()), "val", value_name,
            "number", IntLiteralString(value->number()));
      } else {
        stub_h_->Print("PERFETTO_PB_ENUM_ENTRY($val$) = $number$, \n", "val",
                       full_namespace_prefix_ + "_" + value_name, "number",
                       IntLiteralString(value->number()));
      }
    }
    stub_h_->Outdent();
    stub_h_->Print("};\n\n");
  }

  // Packed repeated fields are encoded as a length-delimited field on the wire,
  // where the payload is the concatenation of individually encoded elements.
  void GeneratePackedRepeatedFieldDescriptorArgs(
      const std::string& message_cpp_type,
      const FieldDescriptor* field) {
    std::map<std::string, std::string> setter;
    setter["id"] = std::to_string(field->number());
    setter["name"] = field->lowercase_name();
    setter["class"] = message_cpp_type;
    setter["buffer_type"] = FieldTypeToPackedBufferType(field->type());
    stub_h_->Print(setter, "$class$, PACKED, $buffer_type$, $name$, $id$\n");
  }

  void GeneratePackedRepeatedFieldDescriptor(
      const std::string& message_cpp_type,
      const FieldDescriptor* field) {
    stub_h_->Print("PERFETTO_PB_FIELD(");
    GeneratePackedRepeatedFieldDescriptorArgs(message_cpp_type, field);
    stub_h_->Print(");\n");
  }

  void GeneratePackedRepeatedFieldDescriptorForExtension(
      const std::string& field_cpp_prefix,
      const std::string& message_cpp_type,
      const FieldDescriptor* field) {
    stub_h_->Print("PERFETTO_PB_EXTENSION_FIELD($prefix$, ", "prefix",
                   field_cpp_prefix);
    GeneratePackedRepeatedFieldDescriptorArgs(message_cpp_type, field);
    stub_h_->Print(");\n");
  }

  void GenerateSimpleFieldDescriptorArgs(const std::string& message_cpp_type,
                                         const FieldDescriptor* field) {
    std::map<std::string, std::string> setter;
    setter["id"] = std::to_string(field->number());
    setter["name"] = field->lowercase_name();
    setter["ctype"] = FieldToCppTypeName(field);
    setter["class"] = message_cpp_type;

    switch (field->type()) {
      case FieldDescriptor::TYPE_BYTES:
      case FieldDescriptor::TYPE_STRING:
        stub_h_->Print(setter, "$class$, STRING, const char*, $name$, $id$");
        break;
      case FieldDescriptor::TYPE_UINT64:
      case FieldDescriptor::TYPE_UINT32:
      case FieldDescriptor::TYPE_INT64:
      case FieldDescriptor::TYPE_INT32:
      case FieldDescriptor::TYPE_BOOL:
      case FieldDescriptor::TYPE_ENUM:
        stub_h_->Print(setter, "$class$, VARINT, $ctype$, $name$, $id$");
        break;
      case FieldDescriptor::TYPE_SINT64:
      case FieldDescriptor::TYPE_SINT32:
        stub_h_->Print(setter, "$class$, ZIGZAG, $ctype$, $name$, $id$");
        break;
      case FieldDescriptor::TYPE_SFIXED32:
      case FieldDescriptor::TYPE_FIXED32:
      case FieldDescriptor::TYPE_FLOAT:
        stub_h_->Print(setter, "$class$, FIXED32, $ctype$, $name$, $id$");
        break;
      case FieldDescriptor::TYPE_SFIXED64:
      case FieldDescriptor::TYPE_FIXED64:
      case FieldDescriptor::TYPE_DOUBLE:
        stub_h_->Print(setter, "$class$, FIXED64, $ctype$, $name$, $id$");
        break;
      case FieldDescriptor::TYPE_MESSAGE:
      case FieldDescriptor::TYPE_GROUP:
        Abort("Groups not supported.");
        break;
    }
  }

  void GenerateSimpleFieldDescriptor(const std::string& message_cpp_type,
                                     const FieldDescriptor* field) {
    stub_h_->Print("PERFETTO_PB_FIELD(");
    GenerateSimpleFieldDescriptorArgs(message_cpp_type, field);
    stub_h_->Print(");\n");
  }

  void GenerateSimpleFieldDescriptorForExtension(
      const std::string& field_cpp_prefix,
      const std::string& message_cpp_type,
      const FieldDescriptor* field) {
    stub_h_->Print("PERFETTO_PB_EXTENSION_FIELD($prefix$, ", "prefix",
                   field_cpp_prefix);
    GenerateSimpleFieldDescriptorArgs(message_cpp_type, field);
    stub_h_->Print(");\n");
  }

  void GenerateNestedMessageFieldDescriptor(const std::string& message_cpp_type,
                                            const FieldDescriptor* field) {
    std::string inner_class = GetCppClassName(field->message_type());
    stub_h_->Print(
        "PERFETTO_PB_FIELD($class$, MSG, $inner_class$, $name$, $id$);\n",
        "class", message_cpp_type, "id", std::to_string(field->number()),
        "name", field->lowercase_name(), "inner_class", inner_class);
  }

  void GenerateNestedMessageFieldDescriptorForExtension(
      const std::string& field_cpp_prefix,
      const std::string& message_cpp_type,
      const FieldDescriptor* field) {
    std::string inner_class = GetCppClassName(field->message_type());
    stub_h_->Print(
        "PERFETTO_PB_EXTENSION_FIELD($prefix$, $class$, MSG, $inner_class$, "
        "$name$, $id$);\n",
        "prefix", field_cpp_prefix, "class", message_cpp_type, "id",
        std::to_string(field->number()), "name", field->lowercase_name(),
        "inner_class", inner_class);
  }

  void GenerateMessageDescriptor(const Descriptor* message) {
    stub_h_->Print("PERFETTO_PB_MSG($name$);\n", "name",
                   GetCppClassName(message));

    // Field descriptors.
    for (int i = 0; i < message->field_count(); ++i) {
      GenerateFieldDescriptor(GetCppClassName(message), message->field(i));
    }
    stub_h_->Print("\n");
  }

  void GenerateFieldDescriptor(const std::string& message_cpp_type,
                               const FieldDescriptor* field) {
    // GenerateFieldMetadata(message_cpp_type, field);
    if (field->is_packed()) {
      GeneratePackedRepeatedFieldDescriptor(message_cpp_type, field);
    } else if (field->type() != FieldDescriptor::TYPE_MESSAGE) {
      GenerateSimpleFieldDescriptor(message_cpp_type, field);
    } else {
      GenerateNestedMessageFieldDescriptor(message_cpp_type, field);
    }
  }

  void GenerateExtensionFieldDescriptor(const std::string& field_cpp_prefix,
                                        const std::string& message_cpp_type,
                                        const FieldDescriptor* field) {
    // GenerateFieldMetadata(message_cpp_type, field);
    if (field->is_packed()) {
      GeneratePackedRepeatedFieldDescriptorForExtension(
          field_cpp_prefix, message_cpp_type, field);
    } else if (field->type() != FieldDescriptor::TYPE_MESSAGE) {
      GenerateSimpleFieldDescriptorForExtension(field_cpp_prefix,
                                                message_cpp_type, field);
    } else {
      GenerateNestedMessageFieldDescriptorForExtension(field_cpp_prefix,
                                                       message_cpp_type, field);
    }
  }

  // Generate extension class for a group of FieldDescriptor instances
  // representing one "extend" block in proto definition. For example:
  //
  //   message SpecificExtension {
  //     extend GeneralThing {
  //       optional Fizz fizz = 101;
  //       optional Buzz buzz = 102;
  //     }
  //   }
  //
  // This is going to be passed as a vector of two elements, "fizz" and
  // "buzz". Wrapping message is used to provide a name for generated
  // extension class.
  //
  // In the example above, generated code is going to look like:
  //
  //   class SpecificExtension : public GeneralThing {
  //     Fizz* set_fizz();
  //     Buzz* set_buzz();
  //   }
  void GenerateExtension(
      const std::string& extension_name,
      const std::vector<const FieldDescriptor*>& descriptors) {
    // Use an arbitrary descriptor in order to get generic information not
    // specific to any of them.
    const FieldDescriptor* descriptor = descriptors[0];
    const Descriptor* base_message = descriptor->containing_type();

    for (const FieldDescriptor* field : descriptors) {
      if (field->containing_type() != base_message) {
        Abort("one wrapper should extend only one message");
        return;
      }
      GenerateExtensionFieldDescriptor(extension_name,
                                       GetCppClassName(base_message), field);
    }
  }

  void GenerateEpilogue() {
    stub_h_->Print("#endif  // $guard$\n", "guard", GenerateGuard());
  }

  const FileDescriptor* const source_;
  Printer* const stub_h_;
  std::string error_;

  std::string package_;
  std::string wrapper_namespace_;
  std::string guard_strip_prefix_;
  std::string guard_add_prefix_;
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

class ProtoZeroCGenerator : public google::protobuf::compiler::CodeGenerator {
 public:
  explicit ProtoZeroCGenerator();
  ~ProtoZeroCGenerator() override;

  // CodeGenerator implementation
  bool Generate(const google::protobuf::FileDescriptor* file,
                const std::string& options,
                GeneratorContext* context,
                std::string* error) const override;
};

ProtoZeroCGenerator::ProtoZeroCGenerator() {}

ProtoZeroCGenerator::~ProtoZeroCGenerator() {}

bool ProtoZeroCGenerator::Generate(const FileDescriptor* file,
                                   const std::string& options,
                                   GeneratorContext* context,
                                   std::string* error) const {
  const std::unique_ptr<ZeroCopyOutputStream> stub_h_file_stream(
      context->Open(ProtoStubName(file) + ".h"));

  // Variables are delimited by $.
  Printer stub_h_printer(stub_h_file_stream.get(), '$');
  GeneratorJob job(file, &stub_h_printer);

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
  protozero::ProtoZeroCGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
