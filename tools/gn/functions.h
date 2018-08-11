// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_FUNCTIONS_H_
#define TOOLS_GN_FUNCTIONS_H_

#include <map>
#include <string>
#include <vector>

#include "base/strings/string_piece.h"

class Err;
class BlockNode;
class FunctionCallNode;
class Label;
class ListNode;
class ParseNode;
class Scope;
class Value;

// -----------------------------------------------------------------------------

namespace functions {

// This type of function invocation has no block and evaluates its arguments
// itself rather than taking a pre-executed list. This allows us to implement
// certain built-in functions.
typedef Value (*SelfEvaluatingArgsFunction)(Scope* scope,
                                            const FunctionCallNode* function,
                                            const ListNode* args_list,
                                            Err* err);

// This type of function invocation takes a block node that it will execute.
typedef Value (*GenericBlockFunction)(Scope* scope,
                                      const FunctionCallNode* function,
                                      const std::vector<Value>& args,
                                      BlockNode* block,
                                      Err* err);

// This type of function takes a block, but does not need to control execution
// of it. The dispatch function will pre-execute the block and pass the
// resulting block_scope to the function.
typedef Value(*ExecutedBlockFunction)(const FunctionCallNode* function,
                                      const std::vector<Value>& args,
                                      Scope* block_scope,
                                      Err* err);

// This type of function does not take a block. It just has arguments.
typedef Value (*NoBlockFunction)(Scope* scope,
                                 const FunctionCallNode* function,
                                 const std::vector<Value>& args,
                                 Err* err);

extern const char kAction[];
extern const char kAction_HelpShort[];
extern const char kAction_Help[];
Value RunAction(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                BlockNode* block,
                Err* err);

extern const char kActionForEach[];
extern const char kActionForEach_HelpShort[];
extern const char kActionForEach_Help[];
Value RunActionForEach(Scope* scope,
                       const FunctionCallNode* function,
                       const std::vector<Value>& args,
                       BlockNode* block,
                       Err* err);

extern const char kAssert[];
extern const char kAssert_HelpShort[];
extern const char kAssert_Help[];
Value RunAssert(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                Err* err);

extern const char kBundleData[];
extern const char kBundleData_HelpShort[];
extern const char kBundleData_Help[];
Value RunBundleData(Scope* scope,
                    const FunctionCallNode* function,
                    const std::vector<Value>& args,
                    BlockNode* block,
                    Err* err);

extern const char kCreateBundle[];
extern const char kCreateBundle_HelpShort[];
extern const char kCreateBundle_Help[];
Value RunCreateBundle(Scope* scope,
                      const FunctionCallNode* function,
                      const std::vector<Value>& args,
                      BlockNode* block,
                      Err* err);

extern const char kConfig[];
extern const char kConfig_HelpShort[];
extern const char kConfig_Help[];
Value RunConfig(const FunctionCallNode* function,
                const std::vector<Value>& args,
                Scope* block_scope,
                Err* err);

extern const char kCopy[];
extern const char kCopy_HelpShort[];
extern const char kCopy_Help[];
Value RunCopy(const FunctionCallNode* function,
              const std::vector<Value>& args,
              Scope* block_scope,
              Err* err);

extern const char kDeclareArgs[];
extern const char kDeclareArgs_HelpShort[];
extern const char kDeclareArgs_Help[];
Value RunDeclareArgs(Scope* scope,
                     const FunctionCallNode* function,
                     const std::vector<Value>& args,
                     BlockNode* block,
                     Err* err);

extern const char kDefined[];
extern const char kDefined_HelpShort[];
extern const char kDefined_Help[];
Value RunDefined(Scope* scope,
                 const FunctionCallNode* function,
                 const ListNode* args_list,
                 Err* err);

extern const char kExecScript[];
extern const char kExecScript_HelpShort[];
extern const char kExecScript_Help[];
Value RunExecScript(Scope* scope,
                    const FunctionCallNode* function,
                    const std::vector<Value>& args,
                    Err* err);

extern const char kExecutable[];
extern const char kExecutable_HelpShort[];
extern const char kExecutable_Help[];
Value RunExecutable(Scope* scope,
                    const FunctionCallNode* function,
                    const std::vector<Value>& args,
                    BlockNode* block,
                    Err* err);

extern const char kForEach[];
extern const char kForEach_HelpShort[];
extern const char kForEach_Help[];
Value RunForEach(Scope* scope,
                 const FunctionCallNode* function,
                 const ListNode* args_list,
                 Err* err);

extern const char kForwardVariablesFrom[];
extern const char kForwardVariablesFrom_HelpShort[];
extern const char kForwardVariablesFrom_Help[];
Value RunForwardVariablesFrom(Scope* scope,
                              const FunctionCallNode* function,
                              const ListNode* args_list,
                              Err* err);

extern const char kGetEnv[];
extern const char kGetEnv_HelpShort[];
extern const char kGetEnv_Help[];
Value RunGetEnv(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                Err* err);

extern const char kGetLabelInfo[];
extern const char kGetLabelInfo_HelpShort[];
extern const char kGetLabelInfo_Help[];
Value RunGetLabelInfo(Scope* scope,
                      const FunctionCallNode* function,
                      const std::vector<Value>& args,
                      Err* err);

extern const char kGetPathInfo[];
extern const char kGetPathInfo_HelpShort[];
extern const char kGetPathInfo_Help[];
Value RunGetPathInfo(Scope* scope,
                     const FunctionCallNode* function,
                     const std::vector<Value>& args,
                     Err* err);

extern const char kGetTargetOutputs[];
extern const char kGetTargetOutputs_HelpShort[];
extern const char kGetTargetOutputs_Help[];
Value RunGetTargetOutputs(Scope* scope,
                          const FunctionCallNode* function,
                          const std::vector<Value>& args,
                          Err* err);

extern const char kGroup[];
extern const char kGroup_HelpShort[];
extern const char kGroup_Help[];
Value RunGroup(Scope* scope,
               const FunctionCallNode* function,
               const std::vector<Value>& args,
               BlockNode* block,
               Err* err);

extern const char kImport[];
extern const char kImport_HelpShort[];
extern const char kImport_Help[];
Value RunImport(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                Err* err);

extern const char kLoadableModule[];
extern const char kLoadableModule_HelpShort[];
extern const char kLoadableModule_Help[];
Value RunLoadableModule(Scope* scope,
                        const FunctionCallNode* function,
                        const std::vector<Value>& args,
                        BlockNode* block,
                        Err* err);

extern const char kNotNeeded[];
extern const char kNotNeeded_HelpShort[];
extern const char kNotNeeded_Help[];
Value RunNotNeeded(Scope* scope,
                   const FunctionCallNode* function,
                   const ListNode* args_list,
                   Err* err);

extern const char kPool[];
extern const char kPool_HelpShort[];
extern const char kPool_Help[];
Value RunPool(const FunctionCallNode* function,
              const std::vector<Value>& args,
              Scope* block_scope,
              Err* err);

extern const char kPrint[];
extern const char kPrint_HelpShort[];
extern const char kPrint_Help[];
Value RunPrint(Scope* scope,
               const FunctionCallNode* function,
               const std::vector<Value>& args,
               Err* err);

extern const char kProcessFileTemplate[];
extern const char kProcessFileTemplate_HelpShort[];
extern const char kProcessFileTemplate_Help[];
Value RunProcessFileTemplate(Scope* scope,
                             const FunctionCallNode* function,
                             const std::vector<Value>& args,
                             Err* err);

extern const char kReadFile[];
extern const char kReadFile_HelpShort[];
extern const char kReadFile_Help[];
Value RunReadFile(Scope* scope,
                  const FunctionCallNode* function,
                  const std::vector<Value>& args,
                  Err* err);

extern const char kRebasePath[];
extern const char kRebasePath_HelpShort[];
extern const char kRebasePath_Help[];
Value RunRebasePath(Scope* scope,
                    const FunctionCallNode* function,
                    const std::vector<Value>& args,
                    Err* err);

extern const char kSetDefaults[];
extern const char kSetDefaults_HelpShort[];
extern const char kSetDefaults_Help[];
Value RunSetDefaults(Scope* scope,
                     const FunctionCallNode* function,
                     const std::vector<Value>& args,
                     BlockNode* block,
                     Err* err);

extern const char kSetDefaultToolchain[];
extern const char kSetDefaultToolchain_HelpShort[];
extern const char kSetDefaultToolchain_Help[];
Value RunSetDefaultToolchain(Scope* scope,
                             const FunctionCallNode* function,
                             const std::vector<Value>& args,
                             Err* err);

extern const char kSetSourcesAssignmentFilter[];
extern const char kSetSourcesAssignmentFilter_HelpShort[];
extern const char kSetSourcesAssignmentFilter_Help[];
Value RunSetSourcesAssignmentFilter(Scope* scope,
                                    const FunctionCallNode* function,
                                    const std::vector<Value>& args,
                                    Err* err);

extern const char kSharedLibrary[];
extern const char kSharedLibrary_HelpShort[];
extern const char kSharedLibrary_Help[];
Value RunSharedLibrary(Scope* scope,
                       const FunctionCallNode* function,
                       const std::vector<Value>& args,
                       BlockNode* block,
                       Err* err);

extern const char kSourceSet[];
extern const char kSourceSet_HelpShort[];
extern const char kSourceSet_Help[];
Value RunSourceSet(Scope* scope,
                   const FunctionCallNode* function,
                   const std::vector<Value>& args,
                   BlockNode* block,
                   Err* err);

extern const char kSplitList[];
extern const char kSplitList_HelpShort[];
extern const char kSplitList_Help[];
Value RunSplitList(Scope* scope,
                   const FunctionCallNode* function,
                   const ListNode* args_list,
                   Err* err);

extern const char kStaticLibrary[];
extern const char kStaticLibrary_HelpShort[];
extern const char kStaticLibrary_Help[];
Value RunStaticLibrary(Scope* scope,
                       const FunctionCallNode* function,
                       const std::vector<Value>& args,
                       BlockNode* block,
                       Err* err);

extern const char kTarget[];
extern const char kTarget_HelpShort[];
extern const char kTarget_Help[];
Value RunTarget(Scope* scope,
                const FunctionCallNode* function,
                const std::vector<Value>& args,
                BlockNode* block,
                Err* err);

extern const char kTemplate[];
extern const char kTemplate_HelpShort[];
extern const char kTemplate_Help[];
Value RunTemplate(Scope* scope,
                  const FunctionCallNode* function,
                  const std::vector<Value>& args,
                  BlockNode* block,
                  Err* err);

extern const char kTool[];
extern const char kTool_HelpShort[];
extern const char kTool_Help[];
Value RunTool(Scope* scope,
              const FunctionCallNode* function,
              const std::vector<Value>& args,
              BlockNode* block,
              Err* err);

extern const char kToolchain[];
extern const char kToolchain_HelpShort[];
extern const char kToolchain_Help[];
Value RunToolchain(Scope* scope,
                   const FunctionCallNode* function,
                   const std::vector<Value>& args,
                   BlockNode* block,
                   Err* err);

extern const char kWriteFile[];
extern const char kWriteFile_HelpShort[];
extern const char kWriteFile_Help[];
Value RunWriteFile(Scope* scope,
                   const FunctionCallNode* function,
                   const std::vector<Value>& args,
                   Err* err);

// -----------------------------------------------------------------------------

// One function record. Only one of the given runner types will be non-null
// which indicates the type of function it is.
struct FunctionInfo {
  FunctionInfo();
  FunctionInfo(SelfEvaluatingArgsFunction seaf,
               const char* in_help_short,
               const char* in_help,
               bool in_is_target);
  FunctionInfo(GenericBlockFunction gbf,
               const char* in_help_short,
               const char* in_help,
               bool in_is_target);
  FunctionInfo(ExecutedBlockFunction ebf,
               const char* in_help_short,
               const char* in_help,
               bool in_is_target);
  FunctionInfo(NoBlockFunction nbf,
               const char* in_help_short,
               const char* in_help,
               bool in_is_target);

  SelfEvaluatingArgsFunction self_evaluating_args_runner;
  GenericBlockFunction generic_block_runner;
  ExecutedBlockFunction executed_block_runner;
  NoBlockFunction no_block_runner;

  const char* help_short;
  const char* help;

  bool is_target;
};

typedef std::map<base::StringPiece, FunctionInfo> FunctionInfoMap;

// Returns the mapping of all built-in functions.
const FunctionInfoMap& GetFunctions();

// Runs the given function.
Value RunFunction(Scope* scope,
                  const FunctionCallNode* function,
                  const ListNode* args_list,
                  BlockNode* block,  // Optional.
                  Err* err);

}  // namespace functions

// Helper functions -----------------------------------------------------------

// Validates that the scope that a value is defined in is not the scope
// of the current declare_args() call, if that's what we're in. It is
// illegal to read a value from inside the same declare_args() call, since
// the overrides will not have been applied yet (see `gn help declare_args`
// for more).
bool EnsureNotReadingFromSameDeclareArgs(const ParseNode* node,
                                         const Scope* cur_scope,
                                         const Scope* val_scope,
                                         Err* err);

// Verifies that the current scope is not processing an import. If it is, it
// will set the error, blame the given parse node for it, and return false.
bool EnsureNotProcessingImport(const ParseNode* node,
                               const Scope* scope,
                               Err* err);

// Like EnsureNotProcessingImport but checks for running the build config.
bool EnsureNotProcessingBuildConfig(const ParseNode* node,
                                    const Scope* scope,
                                    Err* err);

// Sets up the |block_scope| for executing a target (or something like it).
// The |scope| is the containing scope. It should have been already set as the
// parent for the |block_scope| when the |block_scope| was created.
//
// This will set up the target defaults and set the |target_name| variable in
// the block scope to the current target name, which is assumed to be the first
// argument to the function.
//
// On success, returns true. On failure, sets the error and returns false.
bool FillTargetBlockScope(const Scope* scope,
                          const FunctionCallNode* function,
                          const std::string& target_type,
                          const BlockNode* block,
                          const std::vector<Value>& args,
                          Scope* block_scope,
                          Err* err);

// Sets the given error to a message explaining that the function call requires
// a block.
void FillNeedsBlockError(const FunctionCallNode* function, Err* err);

// Validates that the given function call has one string argument. This is
// the most common function signature, so it saves space to have this helper.
// Returns false and sets the error on failure.
bool EnsureSingleStringArg(const FunctionCallNode* function,
                           const std::vector<Value>& args,
                           Err* err);

// Returns the name of the toolchain for the given scope.
const Label& ToolchainLabelForScope(const Scope* scope);

// Generates a label for the given scope, using the current directory and
// toolchain, and the given name.
Label MakeLabelForScope(const Scope* scope,
                        const FunctionCallNode* function,
                        const std::string& name);

// Some types of blocks can't be nested inside other ones. For such cases,
// instantiate this object upon entering the block and Enter() will fail if
// there is already another non-nestable block on the stack.
class NonNestableBlock {
 public:
  // type_description is a string that will be used in error messages
  // describing the type of the block, for example, "template" or "config".
  NonNestableBlock(Scope* scope,
                   const FunctionCallNode* function,
                   const char* type_description);
  ~NonNestableBlock();

  bool Enter(Err* err);

 private:
  // Used as a void* key for the Scope to track our property. The actual value
  // is never used.
  static const int kKey;

  Scope* scope_;
  const FunctionCallNode* function_;
  const char* type_description_;

  // Set to true when the key is added to the scope so we don't try to
  // delete nonexistant keys which will cause assertions.
  bool key_added_;
};

#endif  // TOOLS_GN_FUNCTIONS_H_
