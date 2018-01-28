// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/variables.h"

namespace variables {

// Built-in variables ----------------------------------------------------------

const char kHostCpu[] = "host_cpu";
const char kHostCpu_HelpShort[] =
    "host_cpu: [string] The processor architecture that GN is running on.";
const char kHostCpu_Help[] =
    R"(host_cpu: The processor architecture that GN is running on.

  This is value is exposed so that cross-compile toolchains can access the host
  architecture when needed.

  The value should generally be considered read-only, but it can be overriden
  in order to handle unusual cases where there might be multiple plausible
  values for the host architecture (e.g., if you can do either 32-bit or 64-bit
  builds). The value is not used internally by GN for any purpose.

Some possible values

  - "x64"
  - "x86"
)";

const char kHostOs[] = "host_os";
const char kHostOs_HelpShort[] =
    "host_os: [string] The operating system that GN is running on.";
const char kHostOs_Help[] =
    R"(host_os: [string] The operating system that GN is running on.

  This value is exposed so that cross-compiles can access the host build
  system's settings.

  This value should generally be treated as read-only. It, however, is not used
  internally by GN for any purpose.

Some possible values

  - "linux"
  - "mac"
  - "win"
)";

const char kInvoker[] = "invoker";
const char kInvoker_HelpShort[] =
    "invoker: [string] The invoking scope inside a template.";
const char kInvoker_Help[] =
    R"(invoker: [string] The invoking scope inside a template.

  Inside a template invocation, this variable refers to the scope of the
  invoker of the template. Outside of template invocations, this variable is
  undefined.

  All of the variables defined inside the template invocation are accessible as
  members of the "invoker" scope. This is the way that templates read values
  set by the callers.

  This is often used with "defined" to see if a value is set on the invoking
  scope.

  See "gn help template" for more examples.

Example

  template("my_template") {
    print(invoker.sources)       # Prints [ "a.cc", "b.cc" ]
    print(defined(invoker.foo))  # Prints false.
    print(defined(invoker.bar))  # Prints true.
  }

  my_template("doom_melon") {
    sources = [ "a.cc", "b.cc" ]
    bar = 123
  }
)";

const char kTargetCpu[] = "target_cpu";
const char kTargetCpu_HelpShort[] =
    "target_cpu: [string] The desired cpu architecture for the build.";
const char kTargetCpu_Help[] =
    R"(target_cpu: The desired cpu architecture for the build.

  This value should be used to indicate the desired architecture for the
  primary objects of the build. It will match the cpu architecture of the
  default toolchain, but not necessarily the current toolchain.

  In many cases, this is the same as "host_cpu", but in the case of
  cross-compiles, this can be set to something different. This value is
  different from "current_cpu" in that it does not change based on the current
  toolchain. When writing rules, "current_cpu" should be used rather than
  "target_cpu" most of the time.

  This value is not used internally by GN for any purpose, so it may be set to
  whatever value is needed for the build. GN defaults this value to the empty
  string ("") and the configuration files should set it to an appropriate value
  (e.g., setting it to the value of "host_cpu") if it is not overridden on the
  command line or in the args.gn file.

Possible values

  - "x86"
  - "x64"
  - "arm"
  - "arm64"
  - "mipsel"
)";

const char kTargetName[] = "target_name";
const char kTargetName_HelpShort[] =
    "target_name: [string] The name of the current target.";
const char kTargetName_Help[] =
    R"(target_name: [string] The name of the current target.

  Inside a target or template invocation, this variable refers to the name
  given to the target or template invocation. Outside of these, this variable
  is undefined.

  This is most often used in template definitions to name targets defined in
  the template based on the name of the invocation. This is necessary both to
  ensure generated targets have unique names and to generate a target with the
  exact name of the invocation that other targets can depend on.

  Be aware that this value will always reflect the innermost scope. So when
  defining a target inside a template, target_name will refer to the target
  rather than the template invocation. To get the name of the template
  invocation in this case, you should save target_name to a temporary variable
  outside of any target definitions.

  See "gn help template" for more examples.

Example

  executable("doom_melon") {
    print(target_name)    # Prints "doom_melon".
  }

  template("my_template") {
    print(target_name)    # Prints "space_ray" when invoked below.

    executable(target_name + "_impl") {
      print(target_name)  # Prints "space_ray_impl".
    }
  }

  my_template("space_ray") {
  }
)";

const char kTargetOs[] = "target_os";
const char kTargetOs_HelpShort[] =
    "target_os: [string] The desired operating system for the build.";
const char kTargetOs_Help[] =
    R"(target_os: The desired operating system for the build.

  This value should be used to indicate the desired operating system for the
  primary object(s) of the build. It will match the OS of the default
  toolchain.

  In many cases, this is the same as "host_os", but in the case of
  cross-compiles, it may be different. This variable differs from "current_os"
  in that it can be referenced from inside any toolchain and will always return
  the initial value.

  This should be set to the most specific value possible. So, "android" or
  "chromeos" should be used instead of "linux" where applicable, even though
  Android and ChromeOS are both Linux variants. This can mean that one needs to
  write

      if (target_os == "android" || target_os == "linux") {
          # ...
      }

  and so forth.

  This value is not used internally by GN for any purpose, so it may be set to
  whatever value is needed for the build. GN defaults this value to the empty
  string ("") and the configuration files should set it to an appropriate value
  (e.g., setting it to the value of "host_os") if it is not set via the command
  line or in the args.gn file.

Possible values

  - "android"
  - "chromeos"
  - "ios"
  - "linux"
  - "nacl"
  - "mac"
  - "win"
)";

const char kCurrentCpu[] = "current_cpu";
const char kCurrentCpu_HelpShort[] =
    "current_cpu: [string] The processor architecture of the current "
        "toolchain.";
const char kCurrentCpu_Help[] =
    R"(current_cpu: The processor architecture of the current toolchain.

  The build configuration usually sets this value based on the value of
  "host_cpu" (see "gn help host_cpu") and then threads this through the
  toolchain definitions to ensure that it always reflects the appropriate
  value.

  This value is not used internally by GN for any purpose. It is set it to the
  empty string ("") by default but is declared so that it can be overridden on
  the command line if so desired.

  See "gn help target_cpu" for a list of common values returned.)";

const char kCurrentOs[] = "current_os";
const char kCurrentOs_HelpShort[] =
    "current_os: [string] The operating system of the current toolchain.";
const char kCurrentOs_Help[] =
    R"(current_os: The operating system of the current toolchain.

  The build configuration usually sets this value based on the value of
  "target_os" (see "gn help target_os"), and then threads this through the
  toolchain definitions to ensure that it always reflects the appropriate
  value.

  This value is not used internally by GN for any purpose. It is set it to the
  empty string ("") by default but is declared so that it can be overridden on
  the command line if so desired.

  See "gn help target_os" for a list of common values returned.
)";

const char kCurrentToolchain[] = "current_toolchain";
const char kCurrentToolchain_HelpShort[] =
    "current_toolchain: [string] Label of the current toolchain.";
const char kCurrentToolchain_Help[] =
    R"(current_toolchain: Label of the current toolchain.

  A fully-qualified label representing the current toolchain. You can use this
  to make toolchain-related decisions in the build. See also
  "default_toolchain".

Example

  if (current_toolchain == "//build:64_bit_toolchain") {
    executable("output_thats_64_bit_only") {
      ...
)";

const char kDefaultToolchain[] = "default_toolchain";
const char kDefaultToolchain_HelpShort[] =
    "default_toolchain: [string] Label of the default toolchain.";
const char kDefaultToolchain_Help[] =
    R"(default_toolchain: [string] Label of the default toolchain.

  A fully-qualified label representing the default toolchain, which may not
  necessarily be the current one (see "current_toolchain").
)";

const char kPythonPath[] = "python_path";
const char kPythonPath_HelpShort[] =
    "python_path: [string] Absolute path of Python.";
const char kPythonPath_Help[] =
    R"(python_path: Absolute path of Python.

  Normally used in toolchain definitions if running some command requires
  Python. You will normally not need this when invoking scripts since GN
  automatically finds it for you.
)";

const char kRootBuildDir[] = "root_build_dir";
const char kRootBuildDir_HelpShort[] =
  "root_build_dir: [string] Directory where build commands are run.";
const char kRootBuildDir_Help[] =
  R"(root_build_dir: [string] Directory where build commands are run.

  This is the root build output directory which will be the current directory
  when executing all compilers and scripts.

  Most often this is used with rebase_path (see "gn help rebase_path") to
  convert arguments to be relative to a script's current directory.
)";

const char kRootGenDir[] = "root_gen_dir";
const char kRootGenDir_HelpShort[] =
    "root_gen_dir: [string] Directory for the toolchain's generated files.";
const char kRootGenDir_Help[] =
    R"(root_gen_dir: Directory for the toolchain's generated files.

  Absolute path to the root of the generated output directory tree for the
  current toolchain. An example would be "//out/Debug/gen" for the default
  toolchain, or "//out/Debug/arm/gen" for the "arm" toolchain.

  This is primarily useful for setting up include paths for generated files. If
  you are passing this to a script, you will want to pass it through
  rebase_path() (see "gn help rebase_path") to convert it to be relative to the
  build directory.

  See also "target_gen_dir" which is usually a better location for generated
  files. It will be inside the root generated dir.
)";

const char kRootOutDir[] = "root_out_dir";
const char kRootOutDir_HelpShort[] =
    "root_out_dir: [string] Root directory for toolchain output files.";
const char kRootOutDir_Help[] =
    R"(root_out_dir: [string] Root directory for toolchain output files.

  Absolute path to the root of the output directory tree for the current
  toolchain. It will not have a trailing slash.

  For the default toolchain this will be the same as the root_build_dir. An
  example would be "//out/Debug" for the default toolchain, or
  "//out/Debug/arm" for the "arm" toolchain.

  This is primarily useful for setting up script calls. If you are passing this
  to a script, you will want to pass it through rebase_path() (see "gn help
  rebase_path") to convert it to be relative to the build directory.

  See also "target_out_dir" which is usually a better location for output
  files. It will be inside the root output dir.

Example

  action("myscript") {
    # Pass the output dir to the script.
    args = [ "-o", rebase_path(root_out_dir, root_build_dir) ]
  }
)";

const char kTargetGenDir[] = "target_gen_dir";
const char kTargetGenDir_HelpShort[] =
    "target_gen_dir: [string] Directory for a target's generated files.";
const char kTargetGenDir_Help[] =
    R"(target_gen_dir: Directory for a target's generated files.

  Absolute path to the target's generated file directory. This will be the
  "root_gen_dir" followed by the relative path to the current build file. If
  your file is in "//tools/doom_melon" then target_gen_dir would be
  "//out/Debug/gen/tools/doom_melon". It will not have a trailing slash.

  This is primarily useful for setting up include paths for generated files. If
  you are passing this to a script, you will want to pass it through
  rebase_path() (see "gn help rebase_path") to convert it to be relative to the
  build directory.

  See also "gn help root_gen_dir".

Example

  action("myscript") {
    # Pass the generated output dir to the script.
    args = [ "-o", rebase_path(target_gen_dir, root_build_dir) ]"
  }
)";

const char kTargetOutDir[] = "target_out_dir";
const char kTargetOutDir_HelpShort[] =
    "target_out_dir: [string] Directory for target output files.";
const char kTargetOutDir_Help[] =
    R"(target_out_dir: [string] Directory for target output files.

  Absolute path to the target's generated file directory. If your current
  target is in "//tools/doom_melon" then this value might be
  "//out/Debug/obj/tools/doom_melon". It will not have a trailing slash.

  This is primarily useful for setting up arguments for calling scripts. If you
  are passing this to a script, you will want to pass it through rebase_path()
  (see "gn help rebase_path") to convert it to be relative to the build
  directory.

  See also "gn help root_out_dir".

Example

  action("myscript") {
    # Pass the output dir to the script.
    args = [ "-o", rebase_path(target_out_dir, root_build_dir) ]"

  }
)";

// Target variables ------------------------------------------------------------

#define COMMON_ORDERING_HELP \
    "\n" \
    "Ordering of flags and values\n" \
    "\n" \
    "  1. Those set on the current target (not in a config).\n" \
    "  2. Those set on the \"configs\" on the target in order that the\n" \
    "     configs appear in the list.\n" \
    "  3. Those set on the \"all_dependent_configs\" on the target in order\n" \
    "     that the configs appear in the list.\n" \
    "  4. Those set on the \"public_configs\" on the target in order that\n" \
    "     those configs appear in the list.\n" \
    "  5. all_dependent_configs pulled from dependencies, in the order of\n" \
    "     the \"deps\" list. This is done recursively. If a config appears\n" \
    "     more than once, only the first occurance will be used.\n" \
    "  6. public_configs pulled from dependencies, in the order of the\n" \
    "     \"deps\" list. If a dependency is public, they will be applied\n" \
    "     recursively.\n"

const char kAllDependentConfigs[] = "all_dependent_configs";
const char kAllDependentConfigs_HelpShort[] =
    "all_dependent_configs: [label list] Configs to be forced on dependents.";
const char kAllDependentConfigs_Help[] =
    R"(all_dependent_configs: Configs to be forced on dependents.

  A list of config labels.

  All targets depending on this one, and recursively, all targets depending on
  those, will have the configs listed in this variable added to them. These
  configs will also apply to the current target.

  This addition happens in a second phase once a target and all of its
  dependencies have been resolved. Therefore, a target will not see these
  force-added configs in their "configs" variable while the script is running,
  and they can not be removed. As a result, this capability should generally
  only be used to add defines and include directories necessary to compile a
  target's headers.

  See also "public_configs".
)"
    COMMON_ORDERING_HELP;

const char kAllowCircularIncludesFrom[] = "allow_circular_includes_from";
const char kAllowCircularIncludesFrom_HelpShort[] =
    "allow_circular_includes_from: [label list] Permit includes from deps.";
const char kAllowCircularIncludesFrom_Help[] =
    R"(allow_circular_includes_from: Permit includes from deps.

  A list of target labels. Must be a subset of the target's "deps". These
  targets will be permitted to include headers from the current target despite
  the dependency going in the opposite direction.

  When you use this, both targets must be included in a final binary for it to
  link. To keep linker errors from happening, it is good practice to have all
  external dependencies depend only on one of the two targets, and to set the
  visibility on the other to enforce this. Thus the targets will always be
  linked together in any output.

Details

  Normally, for a file in target A to include a file from target B, A must list
  B as a dependency. This invariant is enforced by the "gn check" command (and
  the --check flag to "gn gen" -- see "gn help check").

  Sometimes, two targets might be the same unit for linking purposes (two
  source sets or static libraries that would always be linked together in a
  final executable or shared library) and they each include headers from the
  other: you want A to be able to include B's headers, and B to include A's
  headers. This is not an ideal situation but is sometimes unavoidable.

  This list, if specified, lists which of the dependencies of the current
  target can include header files from the current target. That is, if A
  depends on B, B can only include headers from A if it is in A's
  allow_circular_includes_from list. Normally includes must follow the
  direction of dependencies, this flag allows them to go in the opposite
  direction.

Danger

  In the above example, A's headers are likely to include headers from A's
  dependencies. Those dependencies may have public_configs that apply flags,
  defines, and include paths that make those headers work properly.

  With allow_circular_includes_from, B can include A's headers, and
  transitively from A's dependencies, without having the dependencies that
  would bring in the public_configs those headers need. The result may be
  errors or inconsistent builds.

  So when you use allow_circular_includes_from, make sure that any compiler
  settings, flags, and include directories are the same between both targets
  (consider putting such things in a shared config they can both reference).
  Make sure the dependencies are also the same (you might consider a group to
  collect such dependencies they both depend on).

Example

  source_set("a") {
    deps = [ ":b", ":a_b_shared_deps" ]
    allow_circular_includes_from = [ ":b" ]
    ...
  }

  source_set("b") {
    deps = [ ":a_b_shared_deps" ]
    # Sources here can include headers from a despite lack of deps.
    ...
  }

  group("a_b_shared_deps") {
    public_deps = [ ":c" ]
  }
)";

const char kArflags[] = "arflags";
const char kArflags_HelpShort[] =
    "arflags: [string list] Arguments passed to static_library archiver.";
const char kArflags_Help[] =
    R"(arflags: Arguments passed to static_library archiver.

  A list of flags passed to the archive/lib command that creates static
  libraries.

  arflags are NOT pushed to dependents, so applying arflags to source sets or
  any other target type will be a no-op. As with ldflags, you could put the
  arflags in a config and set that as a public or "all dependent" config, but
  that will likely not be what you want. If you have a chain of static
  libraries dependent on each other, this can cause the flags to propagate up
  to other static libraries. Due to the nature of how arflags are typically
  used, you will normally want to apply them directly on static_library targets
  themselves.
)"
    COMMON_ORDERING_HELP;

const char kArgs[] = "args";
const char kArgs_HelpShort[] =
    "args: [string list] Arguments passed to an action.";
const char kArgs_Help[] =
    R"(args: Arguments passed to an action.

  For action and action_foreach targets, args is the list of arguments to pass
  to the script. Typically you would use source expansion (see "gn help
  source_expansion") to insert the source file names.

  See also "gn help action" and "gn help action_foreach".
)";

const char kAssertNoDeps[] = "assert_no_deps";
const char kAssertNoDeps_HelpShort[] =
    "assert_no_deps: [label pattern list] Ensure no deps on these targets.";
const char kAssertNoDeps_Help[] =
    R"(assert_no_deps: Ensure no deps on these targets.

  A list of label patterns.

  This list is a list of patterns that must not match any of the transitive
  dependencies of the target. These include all public, private, and data
  dependencies, and cross shared library boundaries. This allows you to express
  that undesirable code isn't accidentally added to downstream dependencies in
  a way that might otherwise be difficult to notice.

  Checking does not cross executable boundaries. If a target depends on an
  executable, it's assumed that the executable is a tool that is producing part
  of the build rather than something that is linked and distributed. This
  allows assert_no_deps to express what is distributed in the final target
  rather than depend on the internal build steps (which may include
  non-distributable code).

  See "gn help label_pattern" for the format of the entries in the list. These
  patterns allow blacklisting individual targets or whole directory
  hierarchies.

  Sometimes it is desirable to enforce that many targets have no dependencies
  on a target or set of targets. One efficient way to express this is to create
  a group with the assert_no_deps rule on it, and make that group depend on all
  targets you want to apply that assertion to.

Example

  executable("doom_melon") {
    deps = [ "//foo:bar" ]
    ...
    assert_no_deps = [
      "//evil/*",  # Don't link any code from the evil directory.
      "//foo:test_support",  # This target is also disallowed.
    ]
  }
)";

const char kBundleRootDir[] = "bundle_root_dir";
const char kBundleRootDir_HelpShort[] =
    "bundle_root_dir: Expansion of {{bundle_root_dir}} in create_bundle.";
const char kBundleRootDir_Help[] =
    R"(bundle_root_dir: Expansion of {{bundle_root_dir}} in create_bundle.

  A string corresponding to a path in root_build_dir.

  This string is used by the "create_bundle" target to expand the
  {{bundle_root_dir}} of the "bundle_data" target it depends on. This must
  correspond to a path under root_build_dir.

Example

  bundle_data("info_plist") {
    sources = [ "Info.plist" ]
    outputs = [ "{{bundle_contents_dir}}/Info.plist" ]
  }

  create_bundle("doom_melon.app") {
    deps = [ ":info_plist" ]
    bundle_root_dir = "${root_build_dir}/doom_melon.app"
    bundle_contents_dir = "${bundle_root_dir}/Contents"
    bundle_resources_dir = "${bundle_contents_dir}/Resources"
    bundle_executable_dir = "${bundle_contents_dir}/MacOS"
    bundle_plugins_dir = "${bundle_contents_dir}/PlugIns"
  }
)";

const char kBundleContentsDir[] = "bundle_contents_dir";
const char kBundleContentsDir_HelpShort[] =
    "bundle_contents_dir: "
        "Expansion of {{bundle_contents_dir}} in create_bundle.";
const char kBundleContentsDir_Help[] =
    R"(bundle_contents_dir: Expansion of {{bundle_contents_dir}} in
                             create_bundle.

  A string corresponding to a path in $root_build_dir.

  This string is used by the "create_bundle" target to expand the
  {{bundle_contents_dir}} of the "bundle_data" target it depends on. This must
  correspond to a path under "bundle_root_dir".

  See "gn help bundle_root_dir" for examples.
)";

const char kBundleResourcesDir[] = "bundle_resources_dir";
const char kBundleResourcesDir_HelpShort[] =
    "bundle_resources_dir: "
        "Expansion of {{bundle_resources_dir}} in create_bundle.";
const char kBundleResourcesDir_Help[] =
    R"(bundle_resources_dir: Expansion of {{bundle_resources_dir}} in
                             create_bundle.

  A string corresponding to a path in $root_build_dir.

  This string is used by the "create_bundle" target to expand the
  {{bundle_resources_dir}} of the "bundle_data" target it depends on. This must
  correspond to a path under "bundle_root_dir".

  See "gn help bundle_root_dir" for examples.
)";

const char kBundleDepsFilter[] = "bundle_deps_filter";
const char kBundleDepsFilter_HelpShort[] =
    "bundle_deps_filter: [label list] A list of labels that are filtered out.";
const char kBundleDepsFilter_Help[] =
    R"(bundle_deps_filter: [label list] A list of labels that are filtered out.

  A list of target labels.

  This list contains target label patterns that should be filtered out when
  creating the bundle. Any target matching one of those label will be removed
  from the dependencies of the create_bundle target.

  This is mostly useful when creating application extension bundle as the
  application extension has access to runtime resources from the application
  bundle and thus do not require a second copy.

  See "gn help create_bundle" for more information.

Example

  create_bundle("today_extension") {
    deps = [
      "//base"
    ]
    bundle_root_dir = "$root_out_dir/today_extension.appex"
    bundle_deps_filter = [
      # The extension uses //base but does not use any function calling into
      # third_party/icu and thus does not need the icudtl.dat file.
      "//third_party/icu:icudata",
    ]
  }
)";

const char kBundleExecutableDir[] = "bundle_executable_dir";
const char kBundleExecutableDir_HelpShort[] =
    "bundle_executable_dir: "
        "Expansion of {{bundle_executable_dir}} in create_bundle";
const char kBundleExecutableDir_Help[] =
    R"(bundle_executable_dir: Expansion of {{bundle_executable_dir}} in
                              create_bundle.

  A string corresponding to a path in $root_build_dir.

  This string is used by the "create_bundle" target to expand the
  {{bundle_executable_dir}} of the "bundle_data" target it depends on. This
  must correspond to a path under "bundle_root_dir".

  See "gn help bundle_root_dir" for examples.
)";

const char kBundlePlugInsDir[] = "bundle_plugins_dir";
const char kBundlePlugInsDir_HelpShort[] =
    "bundle_plugins_dir: "
        "Expansion of {{bundle_plugins_dir}} in create_bundle.";
const char kBundlePlugInsDir_Help[] =
    R"(bundle_plugins_dir: Expansion of {{bundle_plugins_dir}} in create_bundle.

  A string corresponding to a path in $root_build_dir.

  This string is used by the "create_bundle" target to expand the
  {{bundle_plugins_dir}} of the "bundle_data" target it depends on. This must
  correspond to a path under "bundle_root_dir".

  See "gn help bundle_root_dir" for examples.
)";

const char kCflags[] = "cflags";
const char kCflags_HelpShort[] =
    "cflags: [string list] Flags passed to all C compiler variants.";
const char kCommonCflagsHelp[] =
    R"(cflags*: Flags passed to the C compiler.

  A list of strings.

  "cflags" are passed to all invocations of the C, C++, Objective C, and
  Objective C++ compilers.

  To target one of these variants individually, use "cflags_c", "cflags_cc",
  "cflags_objc", and "cflags_objcc", respectively. These variant-specific
  versions of cflags* will be appended on the compiler command line after
  "cflags".

  See also "asmflags" for flags for assembly-language files.
)"
    COMMON_ORDERING_HELP;
const char* kCflags_Help = kCommonCflagsHelp;

const char kAsmflags[] = "asmflags";
const char kAsmflags_HelpShort[] =
    "asmflags: [string list] Flags passed to the assembler.";
const char* kAsmflags_Help =
    R"(asmflags: Flags passed to the assembler.

  A list of strings.

  "asmflags" are passed to any invocation of a tool that takes an .asm or .S
  file as input.
)"
    COMMON_ORDERING_HELP;

const char kCflagsC[] = "cflags_c";
const char kCflagsC_HelpShort[] =
    "cflags_c: [string list] Flags passed to the C compiler.";
const char* kCflagsC_Help = kCommonCflagsHelp;

const char kCflagsCC[] = "cflags_cc";
const char kCflagsCC_HelpShort[] =
    "cflags_cc: [string list] Flags passed to the C++ compiler.";
const char* kCflagsCC_Help = kCommonCflagsHelp;

const char kCflagsObjC[] = "cflags_objc";
const char kCflagsObjC_HelpShort[] =
    "cflags_objc: [string list] Flags passed to the Objective C compiler.";
const char* kCflagsObjC_Help = kCommonCflagsHelp;

const char kCflagsObjCC[] = "cflags_objcc";
const char kCflagsObjCC_HelpShort[] =
    "cflags_objcc: [string list] Flags passed to the Objective C++ compiler.";
const char* kCflagsObjCC_Help = kCommonCflagsHelp;

const char kCheckIncludes[] = "check_includes";
const char kCheckIncludes_HelpShort[] =
    "check_includes: [boolean] Controls whether a target's files are checked.";
const char kCheckIncludes_Help[] =
    R"(check_includes: [boolean] Controls whether a target's files are checked.

  When true (the default), the "gn check" command (as well as "gn gen" with the
  --check flag) will check this target's sources and headers for proper
  dependencies.

  When false, the files in this target will be skipped by default. This does
  not affect other targets that depend on the current target, it just skips
  checking the includes of the current target's files.

  If there are a few conditionally included headers that trip up checking, you
  can exclude headers individually by annotating them with "nogncheck" (see "gn
  help nogncheck").

  The topic "gn help check" has general information on how checking works and
  advice on how to pass a check in problematic cases.

Example

  source_set("busted_includes") {
    # This target's includes are messed up, exclude it from checking.
    check_includes = false
    ...
  }
)";

const char kCodeSigningArgs[] = "code_signing_args";
const char kCodeSigningArgs_HelpShort[] =
    "code_signing_args: [string list] Arguments passed to code signing script.";
const char kCodeSigningArgs_Help[] =
    R"(code_signing_args: [string list] Arguments passed to code signing script.

  For create_bundle targets, code_signing_args is the list of arguments to pass
  to the code signing script. Typically you would use source expansion (see "gn
  help source_expansion") to insert the source file names.

  See also "gn help create_bundle".
)";

const char kCodeSigningScript[] = "code_signing_script";
const char kCodeSigningScript_HelpShort[] =
    "code_signing_script: [file name] Script for code signing.";
const char kCodeSigningScript_Help[] =
    R"(code_signing_script: [file name] Script for code signing."

  An absolute or buildfile-relative file name of a Python script to run for a
  create_bundle target to perform code signing step.

  See also "gn help create_bundle".
)";

const char kCodeSigningSources[] = "code_signing_sources";
const char kCodeSigningSources_HelpShort[] =
    "code_signing_sources: [file list] Sources for code signing step.";
const char kCodeSigningSources_Help[] =
    R"(code_signing_sources: [file list] Sources for code signing step.

  A list of files used as input for code signing script step of a create_bundle
  target. Non-absolute paths will be resolved relative to the current build
  file.

  See also "gn help create_bundle".
)";

const char kCodeSigningOutputs[] = "code_signing_outputs";
const char kCodeSigningOutputs_HelpShort[] =
    "code_signing_outputs: [file list] Output files for code signing step.";
const char kCodeSigningOutputs_Help[] =
    R"(code_signing_outputs: [file list] Output files for code signing step.

  Outputs from the code signing step of a create_bundle target. Must refer to
  files in the build directory.

  See also "gn help create_bundle".
)";

const char kCompleteStaticLib[] = "complete_static_lib";
const char kCompleteStaticLib_HelpShort[] =
    "complete_static_lib: [boolean] Links all deps into a static library.";
const char kCompleteStaticLib_Help[] =
    R"(complete_static_lib: [boolean] Links all deps into a static library.

  A static library normally doesn't include code from dependencies, but instead
  forwards the static libraries and source sets in its deps up the dependency
  chain until a linkable target (an executable or shared library) is reached.
  The final linkable target only links each static library once, even if it
  appears more than once in its dependency graph.

  In some cases the static library might be the final desired output. For
  example, you may be producing a static library for distribution to third
  parties. In this case, the static library should include code for all
  dependencies in one complete package. However, complete static libraries
  themselves are never linked into other complete static libraries. All
  complete static libraries are for distribution and linking them in would
  cause code duplication in this case. If the static library is not for
  distribution, it should not be complete.

  GN treats non-complete static libraries as source sets when they are linked
  into complete static libraries. This is done because some tools like AR do
  not handle dependent static libraries properly. This makes it easier to write
  "alink" rules.

  In rare cases it makes sense to list a header in more than one target if it
  could be considered conceptually a member of both. libraries.

Example

  static_library("foo") {
    complete_static_lib = true
    deps = [ "bar" ]
  }
)";

const char kConfigs[] = "configs";
const char kConfigs_HelpShort[] =
    "configs: [label list] Configs applying to this target or config.";
const char kConfigs_Help[] =
    R"(configs: Configs applying to this target or config.

  A list of config labels.

Configs on a target

  When used on a target, the include_dirs, defines, etc. in each config are
  appended in the order they appear to the compile command for each file in the
  target. They will appear after the include_dirs, defines, etc. that the
  target sets directly.

  Since configs apply after the values set on a target, directly setting a
  compiler flag will prepend it to the command line. If you want to append a
  flag instead, you can put that flag in a one-off config and append that
  config to the target's configs list.

  The build configuration script will generally set up the default configs
  applying to a given target type (see "set_defaults"). When a target is being
  defined, it can add to or remove from this list.

Configs on a config

  It is possible to create composite configs by specifying configs on a config.
  One might do this to forward values, or to factor out blocks of settings from
  very large configs into more manageable named chunks.

  In this case, the composite config is expanded to be the concatenation of its
  own values, and in order, the values from its sub-configs *before* anything
  else happens. This has some ramifications:

   - A target has no visibility into a config's sub-configs. Target code only
     sees the name of the composite config. It can't remove sub-configs or opt
     in to only parts of it. The composite config may not even be defined
     before the target is.

   - You can get duplication of values if a config is listed twice, say, on a
     target and in a sub-config that also applies. In other cases, the configs
     applying to a target are de-duped. It's expected that if a config is
     listed as a sub-config that it is only used in that context. (Note that
     it's possible to fix this and de-dupe, but it's not normally relevant and
     complicates the implementation.)
)"
    COMMON_ORDERING_HELP
R"(
Example

  # Configs on a target.
  source_set("foo") {
    # Don't use the default RTTI config that BUILDCONFIG applied to us.
    configs -= [ "//build:no_rtti" ]

    # Add some of our own settings.
    configs += [ ":mysettings" ]
  }

  # Create a default_optimization config that forwards to one of a set of more
  # specialized configs depending on build flags. This pattern is useful
  # because it allows a target to opt in to either a default set, or a more
  # specific set, while avoid duplicating the settings in two places.
  config("super_optimization") {
    cflags = [ ... ]
  }
  config("default_optimization") {
    if (optimize_everything) {
      configs = [ ":super_optimization" ]
    } else {
      configs = [ ":no_optimization" ]
    }
  }
)";

const char kData[] = "data";
const char kData_HelpShort[] =
    "data: [file list] Runtime data file dependencies.";
const char kData_Help[] =
    R"(data: Runtime data file dependencies.

  Lists files or directories required to run the given target. These are
  typically data files or directories of data files. The paths are interpreted
  as being relative to the current build file. Since these are runtime
  dependencies, they do not affect which targets are built or when. To declare
  input files to a script, use "inputs".

  Appearing in the "data" section does not imply any special handling such as
  copying them to the output directory. This is just used for declaring runtime
  dependencies. Runtime dependencies can be queried using the "runtime_deps"
  category of "gn desc" or written during build generation via
  "--runtime-deps-list-file".

  GN doesn't require data files to exist at build-time. So actions that produce
  files that are in turn runtime dependencies can list those generated files
  both in the "outputs" list as well as the "data" list.

  By convention, directories are listed with a trailing slash:
    data = [ "test/data/" ]
  However, no verification is done on these so GN doesn't enforce this. The
  paths are just rebased and passed along when requested.

  Note: On iOS and macOS, create_bundle targets will not be recursed into when
  gathering data. See "gn help create_bundle" for details.

  See "gn help runtime_deps" for how these are used.
)";

const char kDataDeps[] = "data_deps";
const char kDataDeps_HelpShort[] =
    "data_deps: [label list] Non-linked dependencies.";
const char kDataDeps_Help[] =
    R"(data_deps: Non-linked dependencies.

  A list of target labels.

  Specifies dependencies of a target that are not actually linked into the
  current target. Such dependencies will be built and will be available at
  runtime.

  This is normally used for things like plugins or helper programs that a
  target needs at runtime.

  Note: On iOS and macOS, create_bundle targets will not be recursed into when
  gathering data_deps. See "gn help create_bundle" for details.

  See also "gn help deps" and "gn help data".

Example

  executable("foo") {
    deps = [ "//base" ]
    data_deps = [ "//plugins:my_runtime_plugin" ]
  }
)";

const char kDefines[] = "defines";
const char kDefines_HelpShort[] =
    "defines: [string list] C preprocessor defines.";
const char kDefines_Help[] =
    R"(defines: C preprocessor defines.

  A list of strings

  These strings will be passed to the C/C++ compiler as #defines. The strings
  may or may not include an "=" to assign a value.
)"
    COMMON_ORDERING_HELP
R"(
Example

  defines = [ "AWESOME_FEATURE", "LOG_LEVEL=3" ]
)";

const char kDepfile[] = "depfile";
const char kDepfile_HelpShort[] =
    "depfile: [string] File name for input dependencies for actions.";
const char kDepfile_Help[] =
    R"(depfile: [string] File name for input dependencies for actions.

  If nonempty, this string specifies that the current action or action_foreach
  target will generate the given ".d" file containing the dependencies of the
  input. Empty or unset means that the script doesn't generate the files.

  A depfile should be used only when a target depends on files that are not
  already specified by a target's inputs and sources. Likewise, depfiles should
  specify only those dependencies not already included in sources or inputs.

  The .d file should go in the target output directory. If you have more than
  one source file that the script is being run over, you can use the output
  file expansions described in "gn help action_foreach" to name the .d file
  according to the input."

  The format is that of a Makefile and all paths must be relative to the root
  build directory. Only one output may be listed and it must match the first
  output of the action.

  Although depfiles are created by an action, they should not be listed in the
  action's "outputs" unless another target will use the file as an input.

Example

  action_foreach("myscript_target") {
    script = "myscript.py"
    sources = [ ... ]

    # Locate the depfile in the output directory named like the
    # inputs but with a ".d" appended.
    depfile = "$relative_target_output_dir/{{source_name}}.d"

    # Say our script uses "-o <d file>" to indicate the depfile.
    args = [ "{{source}}", "-o", depfile ]
  }
)";

const char kDeps[] = "deps";
const char kDeps_HelpShort[] =
    "deps: [label list] Private linked dependencies.";
const char kDeps_Help[] =
    R"(deps: Private linked dependencies.

  A list of target labels.

  Specifies private dependencies of a target. Private dependencies are
  propagated up the dependency tree and linked to dependant targets, but do not
  grant the ability to include headers from the dependency. Public configs are
  not forwarded.

Details of dependency propagation

  Source sets, shared libraries, and non-complete static libraries will be
  propagated up the dependency tree across groups, non-complete static
  libraries and source sets.

  Executables, shared libraries, and complete static libraries will link all
  propagated targets and stop propagation. Actions and copy steps also stop
  propagation, allowing them to take a library as an input but not force
  dependants to link to it.

  Propagation of all_dependent_configs and public_configs happens independently
  of target type. all_dependent_configs are always propagated across all types
  of targets, and public_configs are always propagated across public deps of
  all types of targets.

  Data dependencies are propagated differently. See "gn help data_deps" and
  "gn help runtime_deps".

  See also "public_deps".
)";

const char kXcodeExtraAttributes[] = "xcode_extra_attributes";
const char kXcodeExtraAttributes_HelpShort[] =
    "xcode_extra_attributes: [scope] Extra attributes for Xcode projects.";
const char kXcodeExtraAttributes_Help[] =
    R"(xcode_extra_attributes: [scope] Extra attributes for Xcode projects.

  The value defined in this scope will be copied to the EXTRA_ATTRIBUTES
  property of the generated Xcode project. They are only meaningful when
  generating with --ide=xcode.

  See "gn help create_bundle" for more information.
)";

const char kIncludeDirs[] = "include_dirs";
const char kIncludeDirs_HelpShort[] =
    "include_dirs: [directory list] Additional include directories.";
const char kIncludeDirs_Help[] =
    R"(include_dirs: Additional include directories.

  A list of source directories.

  The directories in this list will be added to the include path for the files
  in the affected target.
)"
    COMMON_ORDERING_HELP
R"(
Example

  include_dirs = [ "src/include", "//third_party/foo" ]
)";

const char kInputs[] = "inputs";
const char kInputs_HelpShort[] =
    "inputs: [file list] Additional compile-time dependencies.";
const char kInputs_Help[] =
    R"(inputs: Additional compile-time dependencies.

  Inputs are compile-time dependencies of the current target. This means that
  all inputs must be available before compiling any of the sources or executing
  any actions.

  Inputs are typically only used for action and action_foreach targets.

Inputs for actions

  For action and action_foreach targets, inputs should be the inputs to script
  that don't vary. These should be all .py files that the script uses via
  imports (the main script itself will be an implicit dependency of the action
  so need not be listed).

  For action targets, inputs and sources are treated the same, but from a style
  perspective, it's recommended to follow the same rule as action_foreach and
  put helper files in the inputs, and the data used by the script (if any) in
  sources.

  Note that another way to declare input dependencies from an action is to have
  the action write a depfile (see "gn help depfile"). This allows the script to
  dynamically write input dependencies, that might not be known until actually
  executing the script. This is more efficient than doing processing while
  running GN to determine the inputs, and is easier to keep in-sync than
  hardcoding the list.

Script input gotchas

  It may be tempting to write a script that enumerates all files in a directory
  as inputs. Don't do this! Even if you specify all the files in the inputs or
  sources in the GN target (or worse, enumerate the files in an exec_script
  call when running GN, which will be slow), the dependencies will be broken.

  The problem happens if a file is ever removed because the inputs are not
  listed on the command line to the script. Because the script hasn't changed
  and all inputs are up to date, the script will not re-run and you will get a
  stale build. Instead, either list all inputs on the command line to the
  script, or if there are many, create a separate list file that the script
  reads. As long as this file is listed in the inputs, the build will detect
  when it has changed in any way and the action will re-run.

Inputs for binary targets

  Any input dependencies will be resolved before compiling any sources.
  Normally, all actions that a target depends on will be run before any files
  in a target are compiled. So if you depend on generated headers, you do not
  typically need to list them in the inputs section.

  Inputs for binary targets will be treated as implicit dependencies, meaning
  that changes in any of the inputs will force all sources in the target to be
  recompiled. If an input only applies to a subset of source files, you may
  want to split those into a separate target to avoid unnecessary recompiles.

Example

  action("myscript") {
    script = "domything.py"
    inputs = [ "input.data" ]
  }
)";

const char kLdflags[] = "ldflags";
const char kLdflags_HelpShort[] =
    "ldflags: [string list] Flags passed to the linker.";
const char kLdflags_Help[] =
    R"(ldflags: Flags passed to the linker.

  A list of strings.

  These flags are passed on the command-line to the linker and generally
  specify various linking options. Most targets will not need these and will
  use "libs" and "lib_dirs" instead.

  ldflags are NOT pushed to dependents, so applying ldflags to source sets or
  static libraries will be a no-op. If you want to apply ldflags to dependent
  targets, put them in a config and set it in the all_dependent_configs or
  public_configs.
)"
    COMMON_ORDERING_HELP;

#define COMMON_LIB_INHERITANCE_HELP \
    "\n" \
    "  libs and lib_dirs work differently than other flags in two respects.\n" \
    "  First, then are inherited across static library boundaries until a\n" \
    "  shared library or executable target is reached. Second, they are\n" \
    "  uniquified so each one is only passed once (the first instance of it\n" \
    "  will be the one used).\n"

#define LIBS_AND_LIB_DIRS_ORDERING_HELP \
    "\n" \
    "  For \"libs\" and \"lib_dirs\" only, the values propagated from\n" \
    "  dependencies (as described above) are applied last assuming they\n" \
    "  are not already in the list.\n"

const char kLibDirs[] = "lib_dirs";
const char kLibDirs_HelpShort[] =
    "lib_dirs: [directory list] Additional library directories.";
const char kLibDirs_Help[] =
    R"(lib_dirs: Additional library directories.

  A list of directories.

  Specifies additional directories passed to the linker for searching for the
  required libraries. If an item is not an absolute path, it will be treated as
  being relative to the current build file.
)"
    COMMON_LIB_INHERITANCE_HELP
    COMMON_ORDERING_HELP
    LIBS_AND_LIB_DIRS_ORDERING_HELP
R"(
Example

  lib_dirs = [ "/usr/lib/foo", "lib/doom_melon" ]
)";

const char kLibs[] = "libs";
const char kLibs_HelpShort[] =
    "libs: [string list] Additional libraries to link.";
const char kLibs_Help[] =
    R"(libs: Additional libraries to link.

  A list of library names or library paths.

  These libraries will be linked into the final binary (executable or shared
  library) containing the current target.
)"
    COMMON_LIB_INHERITANCE_HELP
R"(
Types of libs

  There are several different things that can be expressed in libs:

  File paths
      Values containing '/' will be treated as references to files in the
      checkout. They will be rebased to be relative to the build directory and
      specified in the "libs" for linker tools. This facility should be used
      for libraries that are checked in to the version control. For libraries
      that are generated by the build, use normal GN deps to link them.

  System libraries
      Values not containing '/' will be treated as system library names. These
      will be passed unmodified to the linker and prefixed with the
      "lib_prefix" attribute of the linker tool. Generally you would set the
      "lib_dirs" so the given library is found. Your BUILD.gn file should not
      specify the switch (like "-l"): this will be encoded in the "lib_prefix"
      of the tool.

  Apple frameworks
      System libraries ending in ".framework" will be special-cased: the switch
      "-framework" will be prepended instead of the lib_prefix, and the
      ".framework" suffix will be trimmed. This is to support the way Mac links
      framework dependencies.
)"
    COMMON_ORDERING_HELP
    LIBS_AND_LIB_DIRS_ORDERING_HELP
R"(
Examples

  On Windows:
    libs = [ "ctl3d.lib" ]

  On Linux:
    libs = [ "ld" ]
)";

const char kOutputExtension[] = "output_extension";
const char kOutputExtension_HelpShort[] =
    "output_extension: [string] Value to use for the output's file extension.";
const char kOutputExtension_Help[] =
    R"(output_extension: Value to use for the output's file extension.

  Normally the file extension for a target is based on the target type and the
  operating system, but in rare cases you will need to override the name (for
  example to use "libfreetype.so.6" instead of libfreetype.so on Linux).

  This value should not include a leading dot. If undefined, the default
  specified on the tool will be used. If set to the empty string, no output
  extension will be used.

  The output_extension will be used to set the "{{output_extension}}" expansion
  which the linker tool will generally use to specify the output file name. See
  "gn help tool".

Example

  shared_library("freetype") {
    if (is_linux) {
      # Call the output "libfreetype.so.6"
      output_extension = "so.6"
    }
    ...
  }

  # On Windows, generate a "mysettings.cpl" control panel applet. Control panel
  # applets are actually special shared libraries.
  if (is_win) {
    shared_library("mysettings") {
      output_extension = "cpl"
      ...
    }
  }
)";

const char kOutputDir[] = "output_dir";
const char kOutputDir_HelpShort[] =
    "output_dir: [directory] Directory to put output file in.";
const char kOutputDir_Help[] =
    R"(output_dir: [directory] Directory to put output file in.

  For library and executable targets, overrides the directory for the final
  output. This must be in the root_build_dir or a child thereof.

  This should generally be in the root_out_dir or a subdirectory thereof (the
  root_out_dir will be the same as the root_build_dir for the default
  toolchain, and will be a subdirectory for other toolchains). Not putting the
  output in a subdirectory of root_out_dir can result in collisions between
  different toolchains, so you will need to take steps to ensure that your
  target is only present in one toolchain.

  Normally the toolchain specifies the output directory for libraries and
  executables (see "gn help tool"). You will have to consult that for the
  default location. The default location will be used if output_dir is
  undefined or empty.

Example

  shared_library("doom_melon") {
    output_dir = "$root_out_dir/plugin_libs"
    ...
  }
)";

const char kOutputName[] = "output_name";
const char kOutputName_HelpShort[] =
    "output_name: [string] Name for the output file other than the default.";
const char kOutputName_Help[] =
    R"(output_name: Define a name for the output file other than the default.

  Normally the output name of a target will be based on the target name, so the
  target "//foo/bar:bar_unittests" will generate an output file such as
  "bar_unittests.exe" (using Windows as an example).

  Sometimes you will want an alternate name to avoid collisions or if the
  internal name isn't appropriate for public distribution.

  The output name should have no extension or prefixes, these will be added
  using the default system rules. For example, on Linux an output name of "foo"
  will produce a shared library "libfoo.so". There is no way to override the
  output prefix of a linker tool on a per- target basis. If you need more
  flexibility, create a copy target to produce the file you want.

  This variable is valid for all binary output target types.

Example

  static_library("doom_melon") {
    output_name = "fluffy_bunny"
  }
)";

const char kOutputPrefixOverride[] = "output_prefix_override";
const char kOutputPrefixOverride_HelpShort[] =
    "output_prefix_override: [boolean] Don't use prefix for output name.";
const char kOutputPrefixOverride_Help[] =
    R"(output_prefix_override: Don't use prefix for output name.

  A boolean that overrides the output prefix for a target. Defaults to false.

  Some systems use prefixes for the names of the final target output file. The
  normal example is "libfoo.so" on Linux for a target named "foo".

  The output prefix for a given target type is specified on the linker tool
  (see "gn help tool"). Sometimes this prefix is undesired.

  See also "gn help output_extension".

Example

  shared_library("doom_melon") {
    # Normally this will produce "libdoom_melon.so" on Linux. Setting this flag
    # will produce "doom_melon.so".
    output_prefix_override = true
    ...
  }
)";

const char kPartialInfoPlist[] = "partial_info_plist";
const char kPartialInfoPlist_HelpShort[] =
    "partial_info_plist: [filename] Path plist from asset catalog compiler.";
const char kPartialInfoPlist_Help[] =
    R"(partial_info_plist: [filename] Path plist from asset catalog compiler.

  Valid for create_bundle target, corresponds to the path for the partial
  Info.plist created by the asset catalog compiler that needs to be merged
  with the application Info.plist (usually done by the code signing script).

  The file will be generated regardless of whether the asset compiler has
  been invoked or not. See "gn help create_bundle".
)";

const char kOutputs[] = "outputs";
const char kOutputs_HelpShort[] =
    "outputs: [file list] Output files for actions and copy targets.";
const char kOutputs_Help[] =
    R"(outputs: Output files for actions and copy targets.

  Outputs is valid for "copy", "action", and "action_foreach" target types and
  indicates the resulting files. Outputs must always refer to files in the
  build directory.

  copy
    Copy targets should have exactly one entry in the outputs list. If there is
    exactly one source, this can be a literal file name or a source expansion.
    If there is more than one source, this must contain a source expansion to
    map a single input name to a single output name. See "gn help copy".

  action_foreach
    Action_foreach targets must always use source expansions to map input files
    to output files. There can be more than one output, which means that each
    invocation of the script will produce a set of files (presumably based on
    the name of the input file). See "gn help action_foreach".

  action
    Action targets (excluding action_foreach) must list literal output file(s)
    with no source expansions. See "gn help action".
)";

const char kPool[] = "pool";
const char kPool_HelpShort[] =
    "pool: [string] Label of the pool used by the action.";
const char kPool_Help[] =
    R"(pool: Label of the pool used by the action.

  A fully-qualified label representing the pool that will be used for the
  action. Pools are defined using the pool() {...} declaration.

Example

  action("action") {
    pool = "//build:custom_pool"
    ...
  }
)";

const char kPrecompiledHeader[] = "precompiled_header";
const char kPrecompiledHeader_HelpShort[] =
    "precompiled_header: [string] Header file to precompile.";
const char kPrecompiledHeader_Help[] =
    R"(precompiled_header: [string] Header file to precompile.

  Precompiled headers will be used when a target specifies this value, or a
  config applying to this target specifies this value. In addition, the tool
  corresponding to the source files must also specify precompiled headers (see
  "gn help tool"). The tool will also specify what type of precompiled headers
  to use, by setting precompiled_header_type to either "gcc" or "msvc".

  The precompiled header/source variables can be specified on a target or a
  config, but must be the same for all configs applying to a given target since
  a target can only have one precompiled header.

  If you use both C and C++ sources, the precompiled header and source file
  will be compiled once per language. You will want to make sure to wrap C++
  includes in __cplusplus #ifdefs so the file will compile in C mode.

GCC precompiled headers

  When using GCC-style precompiled headers, "precompiled_source" contains the
  path of a .h file that is precompiled and then included by all source files
  in targets that set "precompiled_source".

  The value of "precompiled_header" is not used with GCC-style precompiled
  headers.

MSVC precompiled headers

  When using MSVC-style precompiled headers, the "precompiled_header" value is
  a string corresponding to the header. This is NOT a path to a file that GN
  recognises, but rather the exact string that appears in quotes after
  an #include line in source code. The compiler will match this string against
  includes or forced includes (/FI).

  MSVC also requires a source file to compile the header with. This must be
  specified by the "precompiled_source" value. In contrast to the header value,
  this IS a GN-style file name, and tells GN which source file to compile to
  make the .pch file used for subsequent compiles.

  For example, if the toolchain specifies MSVC headers:

    toolchain("vc_x64") {
      ...
      tool("cxx") {
        precompiled_header_type = "msvc"
        ...

  You might make a config like this:

    config("use_precompiled_headers") {
      precompiled_header = "build/precompile.h"
      precompiled_source = "//build/precompile.cc"

      # Either your source files should #include "build/precompile.h"
      # first, or you can do this to force-include the header.
      cflags = [ "/FI$precompiled_header" ]
    }

  And then define a target that uses the config:

    executable("doom_melon") {
      configs += [ ":use_precompiled_headers" ]
      ...
)";

const char kPrecompiledHeaderType[] = "precompiled_header_type";
const char kPrecompiledHeaderType_HelpShort[] =
    "precompiled_header_type: [string] \"gcc\" or \"msvc\".";
const char kPrecompiledHeaderType_Help[] =
    R"(precompiled_header_type: [string] "gcc" or "msvc".

  See "gn help precompiled_header".
)";

const char kPrecompiledSource[] = "precompiled_source";
const char kPrecompiledSource_HelpShort[] =
    "precompiled_source: [file name] Source file to precompile.";
const char kPrecompiledSource_Help[] =
    R"(precompiled_source: [file name] Source file to precompile.

  The source file that goes along with the precompiled_header when using
  "msvc"-style precompiled headers. It will be implicitly added to the sources
  of the target. See "gn help precompiled_header".
)";

const char kProductType[] = "product_type";
const char kProductType_HelpShort[] =
    "product_type: [string] Product type for Xcode projects.";
const char kProductType_Help[] =
    R"(product_type: Product type for Xcode projects.

  Correspond to the type of the product of a create_bundle target. Only
  meaningful to Xcode (used as part of the Xcode project generation).

  When generating Xcode project files, only create_bundle target with a
  non-empty product_type will have a corresponding target in Xcode project.
)";

const char kPublic[] = "public";
const char kPublic_HelpShort[] =
    "public: [file list] Declare public header files for a target.";
const char kPublic_Help[] =
    R"(public: Declare public header files for a target.

  A list of files that other targets can include. These permissions are checked
  via the "check" command (see "gn help check").

  If no public files are declared, other targets (assuming they have visibility
  to depend on this target) can include any file in the sources list. If this
  variable is defined on a target, dependent targets may only include files on
  this whitelist.

  Header file permissions are also subject to visibility. A target must be
  visible to another target to include any files from it at all and the public
  headers indicate which subset of those files are permitted. See "gn help
  visibility" for more.

  Public files are inherited through the dependency tree. So if there is a
  dependency A -> B -> C, then A can include C's public headers. However, the
  same is NOT true of visibility, so unless A is in C's visibility list, the
  include will be rejected.

  GN only knows about files declared in the "sources" and "public" sections of
  targets. If a file is included that is not known to the build, it will be
  allowed.

Examples

  These exact files are public:
    public = [ "foo.h", "bar.h" ]

  No files are public (no targets may include headers from this one):
    public = []
)";

const char kPublicConfigs[] = "public_configs";
const char kPublicConfigs_HelpShort[] =
    "public_configs: [label list] Configs applied to dependents.";
const char kPublicConfigs_Help[] =
    R"(public_configs: Configs to be applied on dependents.

  A list of config labels.

  Targets directly depending on this one will have the configs listed in this
  variable added to them. These configs will also apply to the current target.

  This addition happens in a second phase once a target and all of its
  dependencies have been resolved. Therefore, a target will not see these
  force-added configs in their "configs" variable while the script is running,
  and they can not be removed. As a result, this capability should generally
  only be used to add defines and include directories necessary to compile a
  target's headers.

  See also "all_dependent_configs".
)"
    COMMON_ORDERING_HELP;

const char kPublicDeps[] = "public_deps";
const char kPublicDeps_HelpShort[] =
    "public_deps: [label list] Declare public dependencies.";
const char kPublicDeps_Help[] =
    R"(public_deps: Declare public dependencies.

  Public dependencies are like private dependencies (see "gn help deps") but
  additionally express that the current target exposes the listed deps as part
  of its public API.

  This has several ramifications:

    - public_configs that are part of the dependency are forwarded to direct
      dependents.

    - Public headers in the dependency are usable by dependents (includes do
      not require a direct dependency or visibility).

    - If the current target is a shared library, other shared libraries that it
      publicly depends on (directly or indirectly) are propagated up the
      dependency tree to dependents for linking.

Discussion

  Say you have three targets: A -> B -> C. C's visibility may allow B to depend
  on it but not A. Normally, this would prevent A from including any headers
  from C, and C's public_configs would apply only to B.

  If B lists C in its public_deps instead of regular deps, A will now inherit
  C's public_configs and the ability to include C's public headers.

  Generally if you are writing a target B and you include C's headers as part
  of B's public headers, or targets depending on B should consider B and C to
  be part of a unit, you should use public_deps instead of deps.

Example

  # This target can include files from "c" but not from
  # "super_secret_implementation_details".
  executable("a") {
    deps = [ ":b" ]
  }

  shared_library("b") {
    deps = [ ":super_secret_implementation_details" ]
    public_deps = [ ":c" ]
  }
)";

const char kResponseFileContents[] = "response_file_contents";
const char kResponseFileContents_HelpShort[] =
    "response_file_contents: [string list] Contents of .rsp file for actions.";
const char kResponseFileContents_Help[] =
   R"*(response_file_contents: Contents of a response file for actions.

  Sometimes the arguments passed to a script can be too long for the system's
  command-line capabilities. This is especially the case on Windows where the
  maximum command-line length is less than 8K. A response file allows you to
  pass an unlimited amount of data to a script in a temporary file for an
  action or action_foreach target.

  If the response_file_contents variable is defined and non-empty, the list
  will be treated as script args (including possibly substitution patterns)
  that will be written to a temporary file at build time. The name of the
  temporary file will be substituted for "{{response_file_name}}" in the script
  args.

  The response file contents will always be quoted and escaped according to
  Unix shell rules. To parse the response file, the Python script should use
  "shlex.split(file_contents)".

Example

  action("process_lots_of_files") {
    script = "process.py",
    inputs = [ ... huge list of files ... ]

    # Write all the inputs to a response file for the script. Also,
    # make the paths relative to the script working directory.
    response_file_contents = rebase_path(inputs, root_build_dir)

    # The script expects the name of the response file in --file-list.
    args = [
      "--enable-foo",
      "--file-list={{response_file_name}}",
    ]
  }
)*";

const char kScript[] = "script";
const char kScript_HelpShort[] =
    "script: [file name] Script file for actions.";
const char kScript_Help[] =
    R"(script: Script file for actions.

  An absolute or buildfile-relative file name of a Python script to run for a
  action and action_foreach targets (see "gn help action" and "gn help
  action_foreach").
)";

const char kSources[] = "sources";
const char kSources_HelpShort[] =
    "sources: [file list] Source files for a target.";
const char kSources_Help[] =
    R"(sources: Source files for a target

  A list of files. Non-absolute paths will be resolved relative to the current
  build file.

Sources for binary targets

  For binary targets (source sets, executables, and libraries), the known file
  types will be compiled with the associated tools. Unknown file types and
  headers will be skipped. However, you should still list all C/C+ header files
  so GN knows about the existence of those files for the purposes of include
  checking.

  As a special case, a file ending in ".def" will be treated as a Windows
  module definition file. It will be appended to the link line with a
  preceeding "/DEF:" string. There must be at most one .def file in a target
  and they do not cross dependency boundaries (so specifying a .def file in a
  static library or source set will have no effect on the executable or shared
  library they're linked into).

Sources for non-binary targets

  action_foreach
    The sources are the set of files that the script will be executed over. The
    script will run once per file.

  action
    The sources will be treated the same as inputs. See "gn help inputs" for
    more information and usage advice.

  copy
    The source are the source files to copy.
)";

const char kXcodeTestApplicationName[] = "xcode_test_application_name";
const char kXcodeTestApplicationName_HelpShort[] =
    "test_application_name: [string] Test application name for unit or ui test "
    "target.";
const char kXcodeTestApplicationName_Help[] =
    R"(test_application_name: Test application name for unit or ui test target.

  Each unit and ui test target must have a test application target, and this
  value is used to specify the relationship. Only meaningful to Xcode (used as
  part of the Xcode project generation).

  See "gn help create_bundle" for more information.

Exmaple

  create_bundle("chrome_xctest") {
    test_application_name = "chrome"
    ...
  }
)";

const char kTestonly[] = "testonly";
const char kTestonly_HelpShort[] =
    "testonly: [boolean] Declares a target must only be used for testing.";
const char kTestonly_Help[] =
    R"(testonly: Declares a target must only be used for testing.

  Boolean. Defaults to false.

  When a target is marked "testonly = true", it must only be depended on by
  other test-only targets. Otherwise, GN will issue an error that the
  depenedency is not allowed.

  This feature is intended to prevent accidentally shipping test code in a
  final product.

Example

  source_set("test_support") {
    testonly = true
    ...
  }
)";

const char kVisibility[] = "visibility";
const char kVisibility_HelpShort[] =
    "visibility: [label list] A list of labels that can depend on a target.";
const char kVisibility_Help[] =
    R"(visibility: A list of labels that can depend on a target.

  A list of labels and label patterns that define which targets can depend on
  the current one. These permissions are checked via the "check" command (see
  "gn help check").

  If visibility is not defined, it defaults to public ("*").

  If visibility is defined, only the targets with labels that match it can
  depend on the current target. The empty list means no targets can depend on
  the current target.

  Tip: Often you will want the same visibility for all targets in a BUILD file.
  In this case you can just put the definition at the top, outside of any
  target, and the targets will inherit that scope and see the definition.

Patterns

  See "gn help label_pattern" for more details on what types of patterns are
  supported. If a toolchain is specified, only targets in that toolchain will
  be matched. If a toolchain is not specified on a pattern, targets in all
  toolchains will be matched.

Examples

  Only targets in the current buildfile ("private"):
    visibility = [ ":*" ]

  No targets (used for targets that should be leaf nodes):
    visibility = []

  Any target ("public", the default):
    visibility = [ "*" ]

  All targets in the current directory and any subdirectory:
    visibility = [ "./*" ]

  Any target in "//bar/BUILD.gn":
    visibility = [ "//bar:*" ]

  Any target in "//bar/" or any subdirectory thereof:
    visibility = [ "//bar/*" ]

  Just these specific targets:
    visibility = [ ":mything", "//foo:something_else" ]

  Any target in the current directory and any subdirectory thereof, plus
  any targets in "//bar/" and any subdirectory thereof.
    visibility = [ "./*", "//bar/*" ]
)";

const char kWriteRuntimeDeps[] = "write_runtime_deps";
const char kWriteRuntimeDeps_HelpShort[] =
    "write_runtime_deps: Writes the target's runtime_deps to the given path.";
const char kWriteRuntimeDeps_Help[] =
    R"(write_runtime_deps: Writes the target's runtime_deps to the given path.

  Does not synchronously write the file, but rather schedules it to be written
  at the end of generation.

  If the file exists and the contents are identical to that being written, the
  file will not be updated. This will prevent unnecessary rebuilds of targets
  that depend on this file.

  Path must be within the output directory.

  See "gn help runtime_deps" for how the runtime dependencies are computed.

  The format of this file will list one file per line with no escaping. The
  files will be relative to the root_build_dir. The first line of the file will
  be the main output file of the target itself. The file contents will be the
  same as requesting the runtime deps be written on the command line (see "gn
  help --runtime-deps-list-file").
)";

// -----------------------------------------------------------------------------

VariableInfo::VariableInfo()
    : help_short(""),
      help("") {
}

VariableInfo::VariableInfo(const char* in_help_short, const char* in_help)
    : help_short(in_help_short),
      help(in_help) {
}

#define INSERT_VARIABLE(var) \
    info_map[k##var] = VariableInfo(k##var##_HelpShort, k##var##_Help);

const VariableInfoMap& GetBuiltinVariables() {
  static VariableInfoMap info_map;
  if (info_map.empty()) {
    INSERT_VARIABLE(CurrentCpu)
    INSERT_VARIABLE(CurrentOs)
    INSERT_VARIABLE(CurrentToolchain)
    INSERT_VARIABLE(DefaultToolchain)
    INSERT_VARIABLE(HostCpu)
    INSERT_VARIABLE(HostOs)
    INSERT_VARIABLE(Invoker)
    INSERT_VARIABLE(PythonPath)
    INSERT_VARIABLE(RootBuildDir)
    INSERT_VARIABLE(RootGenDir)
    INSERT_VARIABLE(RootOutDir)
    INSERT_VARIABLE(TargetCpu)
    INSERT_VARIABLE(TargetOs)
    INSERT_VARIABLE(TargetGenDir)
    INSERT_VARIABLE(TargetName)
    INSERT_VARIABLE(TargetOutDir)
  }
  return info_map;
}

const VariableInfoMap& GetTargetVariables() {
  static VariableInfoMap info_map;
  if (info_map.empty()) {
    INSERT_VARIABLE(AllDependentConfigs)
    INSERT_VARIABLE(AllowCircularIncludesFrom)
    INSERT_VARIABLE(Arflags)
    INSERT_VARIABLE(Args)
    INSERT_VARIABLE(Asmflags)
    INSERT_VARIABLE(AssertNoDeps)
    INSERT_VARIABLE(BundleRootDir)
    INSERT_VARIABLE(BundleContentsDir)
    INSERT_VARIABLE(BundleResourcesDir)
    INSERT_VARIABLE(BundleDepsFilter)
    INSERT_VARIABLE(BundleExecutableDir)
    INSERT_VARIABLE(BundlePlugInsDir)
    INSERT_VARIABLE(Cflags)
    INSERT_VARIABLE(CflagsC)
    INSERT_VARIABLE(CflagsCC)
    INSERT_VARIABLE(CflagsObjC)
    INSERT_VARIABLE(CflagsObjCC)
    INSERT_VARIABLE(CheckIncludes)
    INSERT_VARIABLE(CodeSigningArgs)
    INSERT_VARIABLE(CodeSigningScript)
    INSERT_VARIABLE(CodeSigningSources)
    INSERT_VARIABLE(CodeSigningOutputs)
    INSERT_VARIABLE(CompleteStaticLib)
    INSERT_VARIABLE(Configs)
    INSERT_VARIABLE(Data)
    INSERT_VARIABLE(DataDeps)
    INSERT_VARIABLE(Defines)
    INSERT_VARIABLE(Depfile)
    INSERT_VARIABLE(Deps)
    INSERT_VARIABLE(XcodeExtraAttributes)
    INSERT_VARIABLE(IncludeDirs)
    INSERT_VARIABLE(Inputs)
    INSERT_VARIABLE(Ldflags)
    INSERT_VARIABLE(Libs)
    INSERT_VARIABLE(LibDirs)
    INSERT_VARIABLE(OutputDir)
    INSERT_VARIABLE(OutputExtension)
    INSERT_VARIABLE(OutputName)
    INSERT_VARIABLE(OutputPrefixOverride)
    INSERT_VARIABLE(Outputs)
    INSERT_VARIABLE(PartialInfoPlist)
    INSERT_VARIABLE(Pool)
    INSERT_VARIABLE(PrecompiledHeader)
    INSERT_VARIABLE(PrecompiledHeaderType)
    INSERT_VARIABLE(PrecompiledSource)
    INSERT_VARIABLE(ProductType)
    INSERT_VARIABLE(Public)
    INSERT_VARIABLE(PublicConfigs)
    INSERT_VARIABLE(PublicDeps)
    INSERT_VARIABLE(ResponseFileContents)
    INSERT_VARIABLE(Script)
    INSERT_VARIABLE(Sources)
    INSERT_VARIABLE(XcodeTestApplicationName)
    INSERT_VARIABLE(Testonly)
    INSERT_VARIABLE(Visibility)
    INSERT_VARIABLE(WriteRuntimeDeps)
  }
  return info_map;
}

#undef INSERT_VARIABLE

}  // namespace variables
