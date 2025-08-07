/*
 * Copyright (C) 2017 The Android Open Source Project
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
using perfetto::base::ReplaceAll;
using perfetto::base::SplitString;
using perfetto::base::StripChars;
using perfetto::base::StripPrefix;
using perfetto::base::StripSuffix;
using perfetto::base::ToUpper;
using perfetto::base::Uppercase;

// Keep this value in sync with ProtoDecoder::kMaxDecoderFieldId. If they go out
// of sync pbzero.h files will stop compiling, hitting the at() static_assert.
// Not worth an extra dependency.
constexpr int kMaxDecoderFieldId = 999;

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
  return StripSuffix(std::string(proto->name()), ".proto") + ".pbzero";
}

std::string Int32LiteralString(int32_t number) {
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
    for (const auto& key_value : extensions_)
      GenerateExtension(key_value.first, key_value.second);
    GenerateEpilogue();
    return error_.empty();
  }

  void SetOption(const std::string& name, const std::string& value) {
    if (name == "wrapper_namespace") {
      wrapper_namespace_ = value;
    } else if (name == "sdk") {
      sdk_mode_ = (value == "true" || value == "1");
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

  template <class T>
  bool HasSamePackage(const T* descriptor) const {
    return descriptor->file()->package() == package_;
  }

  // Get C++ class name corresponding to proto descriptor.
  // Nested names are splitted by underscores. Underscores in type names aren't
  // prohibited but not recommended in order to avoid name collisions.
  template <class T>
  inline std::string GetCppClassName(const T* descriptor, bool full = false) {
    std::string package(descriptor->file()->package());
    std::string name =
        StripPrefix(std::string(descriptor->full_name()), package + ".");
    name = StripChars(name, ".", '_');

    if (full && !package.empty()) {
      auto get_full_namespace = [&]() {
        std::vector<std::string> namespaces = SplitString(package, ".");
        if (!wrapper_namespace_.empty())
          namespaces.push_back(wrapper_namespace_);

        std::string result = "";
        for (const std::string& ns : namespaces) {
          result += "::";
          result += ns;
        }
        return result;
      };

      std::string namespaces = ReplaceAll(package, ".", "::");
      name = get_full_namespace() + "::" + name;
    }

    return name;
  }

  inline std::string GetFieldNumberConstant(const FieldDescriptor* field) {
    std::string name(field->camelcase_name());
    if (!name.empty()) {
      name.at(0) = Uppercase(name.at(0));
      name = "k" + name + "FieldNumber";
    } else {
      // Protoc allows fields like 'bool _ = 1'.
      Abort("Empty field name in camel case notation.");
    }
    return name;
  }

  // Note: intentionally avoiding depending on protozero sources, as well as
  // protobuf-internal WireFormat/WireFormatLite classes.
  const char* FieldTypeToProtozeroWireType(FieldDescriptor::Type proto_type) {
    switch (proto_type) {
      case FieldDescriptor::TYPE_INT64:
      case FieldDescriptor::TYPE_UINT64:
      case FieldDescriptor::TYPE_INT32:
      case FieldDescriptor::TYPE_BOOL:
      case FieldDescriptor::TYPE_UINT32:
      case FieldDescriptor::TYPE_ENUM:
      case FieldDescriptor::TYPE_SINT32:
      case FieldDescriptor::TYPE_SINT64:
        return "::protozero::proto_utils::ProtoWireType::kVarInt";

      case FieldDescriptor::TYPE_FIXED32:
      case FieldDescriptor::TYPE_SFIXED32:
      case FieldDescriptor::TYPE_FLOAT:
        return "::protozero::proto_utils::ProtoWireType::kFixed32";

      case FieldDescriptor::TYPE_FIXED64:
      case FieldDescriptor::TYPE_SFIXED64:
      case FieldDescriptor::TYPE_DOUBLE:
        return "::protozero::proto_utils::ProtoWireType::kFixed64";

      case FieldDescriptor::TYPE_STRING:
      case FieldDescriptor::TYPE_MESSAGE:
      case FieldDescriptor::TYPE_BYTES:
        return "::protozero::proto_utils::ProtoWireType::kLengthDelimited";

      case FieldDescriptor::TYPE_GROUP:
        Abort("Groups not supported.");
    }
    Abort("Unrecognized FieldDescriptor::Type.");
    return "";
  }

  const char* FieldTypeToPackedBufferType(FieldDescriptor::Type proto_type) {
    switch (proto_type) {
      case FieldDescriptor::TYPE_INT64:
      case FieldDescriptor::TYPE_UINT64:
      case FieldDescriptor::TYPE_INT32:
      case FieldDescriptor::TYPE_BOOL:
      case FieldDescriptor::TYPE_UINT32:
      case FieldDescriptor::TYPE_ENUM:
      case FieldDescriptor::TYPE_SINT32:
      case FieldDescriptor::TYPE_SINT64:
        return "::protozero::PackedVarInt";

      case FieldDescriptor::TYPE_FIXED32:
        return "::protozero::PackedFixedSizeInt<uint32_t>";
      case FieldDescriptor::TYPE_SFIXED32:
        return "::protozero::PackedFixedSizeInt<int32_t>";
      case FieldDescriptor::TYPE_FLOAT:
        return "::protozero::PackedFixedSizeInt<float>";

      case FieldDescriptor::TYPE_FIXED64:
        return "::protozero::PackedFixedSizeInt<uint64_t>";
      case FieldDescriptor::TYPE_SFIXED64:
        return "::protozero::PackedFixedSizeInt<int64_t>";
      case FieldDescriptor::TYPE_DOUBLE:
        return "::protozero::PackedFixedSizeInt<double>";

      case FieldDescriptor::TYPE_STRING:
      case FieldDescriptor::TYPE_MESSAGE:
      case FieldDescriptor::TYPE_BYTES:
      case FieldDescriptor::TYPE_GROUP:
        Abort("Unexpected FieldDescriptor::Type.");
    }
    Abort("Unrecognized FieldDescriptor::Type.");
    return "";
  }

  const char* FieldToProtoSchemaType(const FieldDescriptor* field) {
    switch (field->type()) {
      case FieldDescriptor::TYPE_BOOL:
        return "kBool";
      case FieldDescriptor::TYPE_INT32:
        return "kInt32";
      case FieldDescriptor::TYPE_INT64:
        return "kInt64";
      case FieldDescriptor::TYPE_UINT32:
        return "kUint32";
      case FieldDescriptor::TYPE_UINT64:
        return "kUint64";
      case FieldDescriptor::TYPE_SINT32:
        return "kSint32";
      case FieldDescriptor::TYPE_SINT64:
        return "kSint64";
      case FieldDescriptor::TYPE_FIXED32:
        return "kFixed32";
      case FieldDescriptor::TYPE_FIXED64:
        return "kFixed64";
      case FieldDescriptor::TYPE_SFIXED32:
        return "kSfixed32";
      case FieldDescriptor::TYPE_SFIXED64:
        return "kSfixed64";
      case FieldDescriptor::TYPE_FLOAT:
        return "kFloat";
      case FieldDescriptor::TYPE_DOUBLE:
        return "kDouble";
      case FieldDescriptor::TYPE_ENUM:
        return "kEnum";
      case FieldDescriptor::TYPE_STRING:
        return "kString";
      case FieldDescriptor::TYPE_MESSAGE:
        return "kMessage";
      case FieldDescriptor::TYPE_BYTES:
        return "kBytes";

      case FieldDescriptor::TYPE_GROUP:
        Abort("Groups not supported.");
        return "";
    }
    Abort("Unrecognized FieldDescriptor::Type.");
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
        return GetCppClassName(field->enum_type(),
                               !HasSamePackage(field->enum_type()));
      case FieldDescriptor::TYPE_STRING:
      case FieldDescriptor::TYPE_BYTES:
        return "std::string";
      case FieldDescriptor::TYPE_MESSAGE:
        return GetCppClassName(field->message_type(),
                               !HasSamePackage(field->message_type()));
      case FieldDescriptor::TYPE_GROUP:
        Abort("Groups not supported.");
        return "";
    }
    Abort("Unrecognized FieldDescriptor::Type.");
    return "";
  }

  const char* FieldToRepetitionType(const FieldDescriptor* field) {
    if (!field->is_repeated())
      return "kNotRepeated";
    if (field->is_packed())
      return "kRepeatedPacked";
    return "kRepeatedNotPacked";
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
          std::string extension_name(extension->extension_scope()->name());
          extensions_[extension_name].push_back(extension);

          if (extension->message_type()) {
            // Emit a forward declaration of nested message types, as the outer
            // class will refer to them when creating type aliases.
            referenced_messages_.insert(extension->message_type());
          }
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
      const FileDescriptor* import = source_->dependency(i);
      stack.push_back(import);
      if (public_imports_.count(import) == 0) {
        private_imports_.insert(import);
      }
    }

    while (!stack.empty()) {
      const FileDescriptor* import = stack.back();
      stack.pop_back();
      for (int i = 0; i < import->public_dependency_count(); ++i) {
        stack.push_back(import->public_dependency(i));
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

    full_namespace_prefix_ = "::";
    for (const std::string& ns : namespaces_)
      full_namespace_prefix_ += ns + "::";

    CollectDescriptors();
    CollectDependencies();
  }

  std::string GetNamespaceNameForInnerEnum(const EnumDescriptor* enumeration) {
    return "perfetto_pbzero_enum_" +
           GetCppClassName(enumeration->containing_type());
  }

  // Print top header, namespaces and forward declarations.
  void GeneratePrologue() {
    std::string greeting =
        "// Autogenerated by the ProtoZero compiler plugin. DO NOT EDIT.\n";
    std::string guard = package_ + "_" + std::string(source_->name()) + "_H_";
    guard = ToUpper(guard);
    guard = StripChars(guard, ".-/\\", '_');

    stub_h_->Print(
        "$greeting$\n"
        "#ifndef $guard$\n"
        "#define $guard$\n\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n",
        "greeting", greeting, "guard", guard);

    if (sdk_mode_) {
      stub_h_->Print("#include \"perfetto.h\"\n");
    } else {
      stub_h_->Print(
          "#include \"perfetto/protozero/field_writer.h\"\n"
          "#include \"perfetto/protozero/message.h\"\n"
          "#include \"perfetto/protozero/packed_repeated_fields.h\"\n"
          "#include \"perfetto/protozero/proto_decoder.h\"\n"
          "#include \"perfetto/protozero/proto_utils.h\"\n");
    }

    // Print includes for public imports. In sdk mode, all imports are assumed
    // to be part of the sdk.
    if (!sdk_mode_) {
      for (const FileDescriptor* dependency : public_imports_) {
        // Dependency name could contain slashes but importing from upper-level
        // directories is not possible anyway since build system processes each
        // proto file individually. Hence proto lookup path is always equal to
        // the directory where particular proto file is located and protoc does
        // not allow reference to upper directory (aka ..) in import path.
        //
        // Laconically said:
        // - source_->name() may never have slashes,
        // - dependency->name() may have slashes but always refers to inner
        // path.
        stub_h_->Print("#include \"$name$.h\"\n", "name",
                       ProtoStubName(dependency));
      }
    }
    stub_h_->Print("\n");

    PrintForwardDeclarations();

    // Print namespaces.
    for (const std::string& ns : namespaces_) {
      stub_h_->Print("namespace $ns$ {\n", "ns", ns);
    }
    stub_h_->Print("\n");
  }

  void PrintForwardDeclarations() {
    struct Descriptors {
      std::vector<const Descriptor*> messages_;
      std::vector<const EnumDescriptor*> enums_;
    };
    std::map<std::string, Descriptors> package_to_descriptors;

    for (const Descriptor* message : referenced_messages_) {
      std::string package(message->file()->package());
      package_to_descriptors[package].messages_.push_back(message);
    }

    for (const EnumDescriptor* enumeration : referenced_enums_) {
      std::string package(enumeration->file()->package());
      package_to_descriptors[package].enums_.push_back(enumeration);
    }

    for (const auto& [package, descriptors] : package_to_descriptors) {
      std::vector<std::string> namespaces = SplitString(package, ".");
      namespaces.push_back(wrapper_namespace_);

      // open namespaces
      for (const auto& ns : namespaces) {
        stub_h_->Print("namespace $ns$ {\n", "ns", ns);
      }

      for (const Descriptor* message : descriptors.messages_) {
        stub_h_->Print("class $class$;\n", "class", GetCppClassName(message));
      }

      for (const EnumDescriptor* enumeration : descriptors.enums_) {
        if (enumeration->containing_type()) {
          stub_h_->Print("namespace $namespace_name$ {\n", "namespace_name",
                         GetNamespaceNameForInnerEnum(enumeration));
        }
        stub_h_->Print("enum $class$ : int32_t;\n", "class",
                       enumeration->name());

        if (enumeration->containing_type()) {
          stub_h_->Print("}  // namespace $namespace_name$\n", "namespace_name",
                         GetNamespaceNameForInnerEnum(enumeration));
          stub_h_->Print("using $alias$ = $namespace_name$::$short_name$;\n",
                         "alias", GetCppClassName(enumeration),
                         "namespace_name",
                         GetNamespaceNameForInnerEnum(enumeration),
                         "short_name", enumeration->name());
        }
      }

      // close namespaces
      for (auto it = namespaces.crbegin(); it != namespaces.crend(); ++it) {
        stub_h_->Print("} // Namespace $ns$.\n", "ns", *it);
      }
    }

    stub_h_->Print("\n");
  }

  void GenerateEnumDescriptor(const EnumDescriptor* enumeration) {
    bool is_inner_enum = !!enumeration->containing_type();
    if (is_inner_enum) {
      stub_h_->Print("namespace $namespace_name$ {\n", "namespace_name",
                     GetNamespaceNameForInnerEnum(enumeration));
    }

    stub_h_->Print("enum $class$ : int32_t {\n", "class", enumeration->name());
    stub_h_->Indent();

    std::string min_name, max_name;
    int min_val = std::numeric_limits<int>::max();
    int max_val = -1;
    for (int i = 0; i < enumeration->value_count(); ++i) {
      const EnumValueDescriptor* value = enumeration->value(i);
      const std::string value_name(value->name());
      stub_h_->Print("$name$ = $number$,\n", "name", value_name, "number",
                     Int32LiteralString(value->number()));
      if (value->number() < min_val) {
        min_val = value->number();
        min_name = value_name;
      }
      if (value->number() > max_val) {
        max_val = value->number();
        max_name = value_name;
      }
    }
    stub_h_->Outdent();
    stub_h_->Print("};\n");
    if (is_inner_enum) {
      const std::string namespace_name =
          GetNamespaceNameForInnerEnum(enumeration);
      stub_h_->Print("} // namespace $namespace_name$\n", "namespace_name",
                     namespace_name);
      stub_h_->Print(
          "using $full_enum_name$ = $namespace_name$::$enum_name$;\n\n",
          "full_enum_name", GetCppClassName(enumeration), "enum_name",
          enumeration->name(), "namespace_name", namespace_name);
    }
    stub_h_->Print("\n");
    stub_h_->Print("constexpr $class$ $class$_MIN = $class$::$min$;\n", "class",
                   GetCppClassName(enumeration), "min", min_name);
    stub_h_->Print("constexpr $class$ $class$_MAX = $class$::$max$;\n", "class",
                   GetCppClassName(enumeration), "max", max_name);
    stub_h_->Print("\n");

    GenerateEnumToStringConversion(enumeration);
  }

  void GenerateEnumToStringConversion(const EnumDescriptor* enumeration) {
    std::string fullClassName =
        full_namespace_prefix_ + GetCppClassName(enumeration);
    const char* function_header_stub = R"(
PERFETTO_PROTOZERO_CONSTEXPR14_OR_INLINE
const char* $class_name$_Name($full_class$ value) {
)";
    stub_h_->Print(function_header_stub, "full_class", fullClassName,
                   "class_name", GetCppClassName(enumeration));
    stub_h_->Indent();
    stub_h_->Print("switch (value) {");
    for (int index = 0; index < enumeration->value_count(); ++index) {
      const EnumValueDescriptor* value = enumeration->value(index);
      const char* switch_stub = R"(
case $full_class$::$value_name$:
  return "$value_name$";
)";
      stub_h_->Print(switch_stub, "full_class", fullClassName, "value_name",
                     value->name());
    }
    stub_h_->Print("}\n");
    stub_h_->Print(R"(return "PBZERO_UNKNOWN_ENUM_VALUE";)");
    stub_h_->Print("\n");
    stub_h_->Outdent();
    stub_h_->Print("}\n\n");
  }

  // Packed repeated fields are encoded as a length-delimited field on the wire,
  // where the payload is the concatenation of individually encoded elements.
  void GeneratePackedRepeatedFieldDescriptor(const FieldDescriptor* field) {
    std::map<std::string, std::string> setter;
    setter["name"] = field->lowercase_name();
    setter["field_metadata"] = GetFieldMetadataTypeName(field);
    setter["action"] = "set";
    setter["buffer_type"] = FieldTypeToPackedBufferType(field->type());
    stub_h_->Print(
        setter,
        "void $action$_$name$(const $buffer_type$& packed_buffer) {\n"
        "  AppendBytes($field_metadata$::kFieldId, packed_buffer.data(),\n"
        "              packed_buffer.size());\n"
        "}\n");
  }

  void GenerateSimpleFieldDescriptor(const FieldDescriptor* field) {
    std::map<std::string, std::string> setter;
    setter["id"] = std::to_string(field->number());
    setter["name"] = field->lowercase_name();
    setter["field_metadata"] = GetFieldMetadataTypeName(field);
    setter["action"] = field->is_repeated() ? "add" : "set";
    setter["cpp_type"] = FieldToCppTypeName(field);
    setter["proto_field_type"] = FieldToProtoSchemaType(field);

    const char* code_stub =
        "void $action$_$name$($cpp_type$ value) {\n"
        "  static constexpr uint32_t field_id = $field_metadata$::kFieldId;\n"
        "  // Call the appropriate protozero::Message::Append(field_id, ...)\n"
        "  // method based on the type of the field.\n"
        "  ::protozero::internal::FieldWriter<\n"
        "    ::protozero::proto_utils::ProtoSchemaType::$proto_field_type$>\n"
        "      ::Append(*this, field_id, value);\n"
        "}\n";

    if (field->type() == FieldDescriptor::TYPE_STRING) {
      // Strings and bytes should have an additional accessor which specifies
      // the length explicitly.
      const char* additional_method =
          "void $action$_$name$(const char* data, size_t size) {\n"
          "  AppendBytes($field_metadata$::kFieldId, data, size);\n"
          "}\n"
          "void $action$_$name$(::protozero::ConstChars chars) {\n"
          "  AppendBytes($field_metadata$::kFieldId, chars.data, chars.size);\n"
          "}\n";
      stub_h_->Print(setter, additional_method);
    } else if (field->type() == FieldDescriptor::TYPE_BYTES) {
      const char* additional_method =
          "void $action$_$name$(const uint8_t* data, size_t size) {\n"
          "  AppendBytes($field_metadata$::kFieldId, data, size);\n"
          "}\n"
          "void $action$_$name$(::protozero::ConstBytes bytes) {\n"
          "  AppendBytes($field_metadata$::kFieldId, bytes.data, bytes.size);\n"
          "}\n";
      stub_h_->Print(setter, additional_method);
    } else if (field->type() == FieldDescriptor::TYPE_GROUP ||
               field->type() == FieldDescriptor::TYPE_MESSAGE) {
      Abort("Unsupported field type.");
      return;
    }

    stub_h_->Print(setter, code_stub);
  }

  void GenerateNestedMessageFieldDescriptor(const FieldDescriptor* field) {
    std::string action = field->is_repeated() ? "add" : "set";
    std::string inner_class = GetCppClassName(
        field->message_type(), !HasSamePackage(field->message_type()));
    stub_h_->Print(
        "template <typename T = $inner_class$> T* $action$_$name$() {\n"
        "  return BeginNestedMessage<T>($id$);\n"
        "}\n\n",
        "id", std::to_string(field->number()), "name", field->lowercase_name(),
        "action", action, "inner_class", inner_class);
    if (field->options().lazy()) {
      stub_h_->Print(
          "void $action$_$name$_raw(const std::string& raw) {\n"
          "  return AppendBytes($id$, raw.data(), raw.size());\n"
          "}\n\n",
          "id", std::to_string(field->number()), "name",
          field->lowercase_name(), "action", action, "inner_class",
          inner_class);
    }
  }

  void GenerateDecoder(const Descriptor* message) {
    int max_field_id = 0;
    bool has_nonpacked_repeated_fields = false;
    for (int i = 0; i < message->field_count(); ++i) {
      const FieldDescriptor* field = message->field(i);
      if (field->number() > kMaxDecoderFieldId)
        continue;
      max_field_id = std::max(max_field_id, field->number());
      if (field->is_repeated() && !field->is_packed())
        has_nonpacked_repeated_fields = true;
    }
    // Iterate over all fields in "extend" blocks.
    for (int i = 0; i < message->extension_range_count(); ++i) {
      Descriptor::ExtensionRange::Proto range;
      message->extension_range(i)->CopyTo(&range);
      int candidate = range.end() - 1;
      if (candidate > kMaxDecoderFieldId)
        continue;
      max_field_id = std::max(max_field_id, candidate);
    }

    std::string class_name = GetCppClassName(message) + "_Decoder";
    stub_h_->Print(
        "class $name$ : public "
        "::protozero::TypedProtoDecoder</*MAX_FIELD_ID=*/$max$, "
        "/*HAS_NONPACKED_REPEATED_FIELDS=*/$rep$> {\n",
        "name", class_name, "max", std::to_string(max_field_id), "rep",
        has_nonpacked_repeated_fields ? "true" : "false");
    stub_h_->Print(" public:\n");
    stub_h_->Indent();
    stub_h_->Print(
        "$name$(const uint8_t* data, size_t len) "
        ": TypedProtoDecoder(data, len) {}\n",
        "name", class_name);
    stub_h_->Print(
        "explicit $name$(const std::string& raw) : "
        "TypedProtoDecoder(reinterpret_cast<const uint8_t*>(raw.data()), "
        "raw.size()) {}\n",
        "name", class_name);
    stub_h_->Print(
        "explicit $name$(const ::protozero::ConstBytes& raw) : "
        "TypedProtoDecoder(raw.data, raw.size) {}\n",
        "name", class_name);

    for (int i = 0; i < message->field_count(); ++i) {
      const FieldDescriptor* field = message->field(i);
      if (field->number() > max_field_id) {
        stub_h_->Print("// field $name$ omitted because its id is too high\n",
                       "name", field->name());
        continue;
      }
      std::string getter;
      std::string cpp_type;
      switch (field->type()) {
        case FieldDescriptor::TYPE_BOOL:
          getter = "as_bool";
          cpp_type = "bool";
          break;
        case FieldDescriptor::TYPE_SFIXED32:
        case FieldDescriptor::TYPE_INT32:
          getter = "as_int32";
          cpp_type = "int32_t";
          break;
        case FieldDescriptor::TYPE_SINT32:
          getter = "as_sint32";
          cpp_type = "int32_t";
          break;
        case FieldDescriptor::TYPE_SFIXED64:
        case FieldDescriptor::TYPE_INT64:
          getter = "as_int64";
          cpp_type = "int64_t";
          break;
        case FieldDescriptor::TYPE_SINT64:
          getter = "as_sint64";
          cpp_type = "int64_t";
          break;
        case FieldDescriptor::TYPE_FIXED32:
        case FieldDescriptor::TYPE_UINT32:
          getter = "as_uint32";
          cpp_type = "uint32_t";
          break;
        case FieldDescriptor::TYPE_FIXED64:
        case FieldDescriptor::TYPE_UINT64:
          getter = "as_uint64";
          cpp_type = "uint64_t";
          break;
        case FieldDescriptor::TYPE_FLOAT:
          getter = "as_float";
          cpp_type = "float";
          break;
        case FieldDescriptor::TYPE_DOUBLE:
          getter = "as_double";
          cpp_type = "double";
          break;
        case FieldDescriptor::TYPE_ENUM:
          getter = "as_int32";
          cpp_type = "int32_t";
          break;
        case FieldDescriptor::TYPE_STRING:
          getter = "as_string";
          cpp_type = "::protozero::ConstChars";
          break;
        case FieldDescriptor::TYPE_MESSAGE:
        case FieldDescriptor::TYPE_BYTES:
          getter = "as_bytes";
          cpp_type = "::protozero::ConstBytes";
          break;
        case FieldDescriptor::TYPE_GROUP:
          continue;
      }

      stub_h_->Print("bool has_$name$() const { return at<$id$>().valid(); }\n",
                     "name", field->lowercase_name(), "id",
                     std::to_string(field->number()));

      if (field->is_packed()) {
        const char* protozero_wire_type =
            FieldTypeToProtozeroWireType(field->type());
        stub_h_->Print(
            "::protozero::PackedRepeatedFieldIterator<$wire_type$, $cpp_type$> "
            "$name$(bool* parse_error_ptr) const { return "
            "GetPackedRepeated<$wire_type$, $cpp_type$>($id$, "
            "parse_error_ptr); }\n",
            "wire_type", protozero_wire_type, "cpp_type", cpp_type, "name",
            field->lowercase_name(), "id", std::to_string(field->number()));
      } else if (field->is_repeated()) {
        stub_h_->Print(
            "::protozero::RepeatedFieldIterator<$cpp_type$> $name$() const { "
            "return "
            "GetRepeated<$cpp_type$>($id$); }\n",
            "name", field->lowercase_name(), "cpp_type", cpp_type, "id",
            std::to_string(field->number()));
      } else {
        stub_h_->Print(
            "$cpp_type$ $name$() const { return at<$id$>().$getter$(); }\n",
            "name", field->lowercase_name(), "id",
            std::to_string(field->number()), "cpp_type", cpp_type, "getter",
            getter);
      }
    }
    stub_h_->Outdent();
    stub_h_->Print("};\n\n");
  }

  void GenerateConstantsForMessageFields(const Descriptor* message) {
    const bool has_fields =
        message->field_count() > 0 || message->extension_count() > 0;

    // Field number constants.
    if (has_fields) {
      stub_h_->Print("enum : int32_t {\n");
      stub_h_->Indent();

      for (int i = 0; i < message->field_count(); ++i) {
        const FieldDescriptor* field = message->field(i);
        stub_h_->Print("$name$ = $id$,\n", "name",
                       GetFieldNumberConstant(field), "id",
                       std::to_string(field->number()));
      }

      for (int i = 0; i < message->extension_count(); ++i) {
        const FieldDescriptor* field = message->extension(i);

        stub_h_->Print("$name$ = $id$,\n", "name",
                       GetFieldNumberConstant(field), "id",
                       std::to_string(field->number()));
      }

      stub_h_->Outdent();
      stub_h_->Print("};\n");
    }
  }

  void GenerateMessageDescriptor(const Descriptor* message) {
    GenerateDecoder(message);

    stub_h_->Print(
        "class $name$ : public ::protozero::Message {\n"
        " public:\n",
        "name", GetCppClassName(message));
    stub_h_->Indent();

    stub_h_->Print("using Decoder = $name$_Decoder;\n", "name",
                   GetCppClassName(message));

    GenerateConstantsForMessageFields(message);

    stub_h_->Print(
        "static constexpr const char* GetName() { return \".$name$\"; }\n\n",
        "name", message->full_name());

    // Using statements for nested messages.
    for (int i = 0; i < message->nested_type_count(); ++i) {
      const Descriptor* nested_message = message->nested_type(i);
      stub_h_->Print("using $local_name$ = $global_name$;\n", "local_name",
                     nested_message->name(), "global_name",
                     GetCppClassName(nested_message, true));
    }

    // Using statements for nested enums.
    for (int i = 0; i < message->enum_type_count(); ++i) {
      const EnumDescriptor* nested_enum = message->enum_type(i);
      const char* stub = R"(
using $local_name$ = $global_name$;
static inline const char* $local_name$_Name($local_name$ value) {
  return $global_name$_Name(value);
}
)";
      stub_h_->Print(stub, "local_name", nested_enum->name(), "global_name",
                     GetCppClassName(nested_enum, true));
    }

    // Values of nested enums.
    for (int i = 0; i < message->enum_type_count(); ++i) {
      const EnumDescriptor* nested_enum = message->enum_type(i);

      for (int j = 0; j < nested_enum->value_count(); ++j) {
        const EnumValueDescriptor* value = nested_enum->value(j);
        stub_h_->Print(
            "static inline const $class$ $name$ = $class$::$name$;\n", "class",
            nested_enum->name(), "name", value->name());
      }
    }

    // Field descriptors.
    for (int i = 0; i < message->field_count(); ++i) {
      GenerateFieldDescriptor(GetCppClassName(message), message->field(i));
    }

    stub_h_->Outdent();
    stub_h_->Print("};\n\n");
  }

  std::string GetFieldMetadataTypeName(const FieldDescriptor* field) {
    std::string name(field->camelcase_name());
    if (isalpha(name[0]))
      name[0] = static_cast<char>(toupper(name[0]));
    return "FieldMetadata_" + name;
  }

  std::string GetFieldMetadataVariableName(const FieldDescriptor* field) {
    std::string name(field->camelcase_name());
    if (isalpha(name[0]))
      name[0] = static_cast<char>(toupper(name[0]));
    return "k" + name;
  }

  void GenerateFieldMetadata(const std::string& message_cpp_type,
                             const FieldDescriptor* field) {
    const char* code_stub = R"(
using $field_metadata_type$ =
  ::protozero::proto_utils::FieldMetadata<
    $field_id$,
    ::protozero::proto_utils::RepetitionType::$repetition_type$,
    ::protozero::proto_utils::ProtoSchemaType::$proto_field_type$,
    $cpp_type$,
    $message_cpp_type$>;

static constexpr $field_metadata_type$ $field_metadata_var${};
)";

    stub_h_->Print(code_stub, "field_id", std::to_string(field->number()),
                   "repetition_type", FieldToRepetitionType(field),
                   "proto_field_type", FieldToProtoSchemaType(field),
                   "cpp_type", FieldToCppTypeName(field), "message_cpp_type",
                   message_cpp_type, "field_metadata_type",
                   GetFieldMetadataTypeName(field), "field_metadata_var",
                   GetFieldMetadataVariableName(field));
  }

  void GenerateFieldDescriptor(const std::string& message_cpp_type,
                               const FieldDescriptor* field) {
    GenerateFieldMetadata(message_cpp_type, field);
    if (field->is_packed()) {
      GeneratePackedRepeatedFieldDescriptor(field);
    } else if (field->type() != FieldDescriptor::TYPE_MESSAGE) {
      GenerateSimpleFieldDescriptor(field);
    } else {
      GenerateNestedMessageFieldDescriptor(field);
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

    // TODO(ddrone): ensure that this code works when containing_type located in
    // other file or namespace.
    stub_h_->Print("class $name$ : public $extendee$ {\n", "name",
                   extension_name, "extendee",
                   GetCppClassName(base_message, /*full=*/true));
    stub_h_->Print(" public:\n");
    stub_h_->Indent();
    for (const FieldDescriptor* field : descriptors) {
      if (field->containing_type() != base_message) {
        Abort("one wrapper should extend only one message");
        return;
      }
      GenerateFieldDescriptor(extension_name, field);
    }

    if (!descriptors.empty()) {
      stub_h_->Print("enum : int32_t {\n");
      stub_h_->Indent();

      for (const FieldDescriptor* field : descriptors) {
        stub_h_->Print("$name$ = $id$,\n", "name",
                       GetFieldNumberConstant(field), "id",
                       std::to_string(field->number()));
      }
      stub_h_->Outdent();
      stub_h_->Print("};\n");
    }

    stub_h_->Outdent();
    stub_h_->Print("};\n");
  }

  void GenerateEpilogue() {
    for (unsigned i = 0; i < namespaces_.size(); ++i) {
      stub_h_->Print("} // Namespace.\n");
    }
    stub_h_->Print("#endif  // Include guard.\n");
  }

  const FileDescriptor* const source_;
  Printer* const stub_h_;
  std::string error_;

  std::string package_;
  std::string wrapper_namespace_;
  std::vector<std::string> namespaces_;
  std::string full_namespace_prefix_;
  std::vector<const Descriptor*> messages_;
  std::vector<const EnumDescriptor*> enums_;
  std::map<std::string, std::vector<const FieldDescriptor*>> extensions_;

  // Generate headers that can be used with the Perfetto SDK.
  bool sdk_mode_ = false;

  // The custom *Comp comparators are to ensure determinism of the generator.
  std::set<const FileDescriptor*, FileDescriptorComp> public_imports_;
  std::set<const FileDescriptor*, FileDescriptorComp> private_imports_;
  std::set<const Descriptor*, DescriptorComp> referenced_messages_;
  std::set<const EnumDescriptor*, EnumDescriptorComp> referenced_enums_;
};

class ProtoZeroGenerator : public ::google::protobuf::compiler::CodeGenerator {
 public:
  explicit ProtoZeroGenerator();
  ~ProtoZeroGenerator() override;

  // CodeGenerator implementation
  bool Generate(const google::protobuf::FileDescriptor* file,
                const std::string& options,
                GeneratorContext* context,
                std::string* error) const override;
};

ProtoZeroGenerator::ProtoZeroGenerator() {}

ProtoZeroGenerator::~ProtoZeroGenerator() {}

bool ProtoZeroGenerator::Generate(const FileDescriptor* file,
                                  const std::string& options,
                                  GeneratorContext* context,
                                  std::string* error) const {
  const std::unique_ptr<ZeroCopyOutputStream> stub_h_file_stream(
      context->Open(ProtoStubName(file) + ".h"));
  const std::unique_ptr<ZeroCopyOutputStream> stub_cc_file_stream(
      context->Open(ProtoStubName(file) + ".cc"));

  // Variables are delimited by $.
  Printer stub_h_printer(stub_h_file_stream.get(), '$');
  GeneratorJob job(file, &stub_h_printer);

  Printer stub_cc_printer(stub_cc_file_stream.get(), '$');
  stub_cc_printer.Print("// Intentionally empty (crbug.com/998165)\n");

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
  ::protozero::ProtoZeroGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
