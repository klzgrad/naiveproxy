#!/usr/bin/env python3
#
# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Writes a build_config file.

The build_config file for a target is a json file containing information about
how to build that target based on the target's dependencies. This includes
things like: the javac classpath, the list of android resources dependencies,
etc. It also includes the information needed to create the build_config for
other targets that depend on that one.

Android build scripts should not refer to the build_config directly, and the
build specification should instead pass information in using the special
file-arg syntax (see build_utils.py:ExpandFileArgs). That syntax allows passing
of values in a json dict in a file and looks like this:
  --python-arg=@FileArg(build_config_path:javac:classpath)

Note: If paths to input files are passed in this way, it is important that:
  1. inputs/deps of the action ensure that the files are available the first
  time the action runs.
  2. Either (a) or (b)
    a. inputs/deps ensure that the action runs whenever one of the files changes
    b. the files are added to the action's depfile

NOTE: All paths within .build_config files are relative to $OUTPUT_CHROMIUM_DIR.

This is a technical note describing the format of .build_config files.
Please keep it updated when changing this script. For extraction and
visualization instructions, see build/android/docs/build_config.md

------------- BEGIN_MARKDOWN ---------------------------------------------------
The .build_config file format
===

# Introduction

This document tries to explain the format of `.build_config` generated during
the Android build of Chromium. For a higher-level explanation of these files,
please read
[build/android/docs/build_config.md](build/android/docs/build_config.md).

# The `deps_info` top-level dictionary:

All `.build_config` files have a required `'deps_info'` key, whose value is a
dictionary describing the target and its dependencies. The latter has the
following required keys:

## Required keys in `deps_info`:

* `deps_info['type']`: The target type as a string.

    The following types are known by the internal GN build rules and the
    build scripts altogether:

    * [java_binary](#target_java_binary)
    * [java_annotation_processor](#target_java_annotation_processor)
    * [robolectric_binary](#target_robolectric_binary)
    * [java_library](#target_java_library)
    * [android_assets](#target_android_assets)
    * [android_resources](#target_android_resources)
    * [android_apk](#target_android_apk)
    * [android_app_bundle_module](#target_android_app_bundle_module)
    * [android_app_bundle](#target_android_app_bundle)
    * [dist_jar](#target_dist_jar)
    * [dist_aar](#target_dist_aar)
    * [group](#target_group)

    See later sections for more details of some of these.

* `deps_info['path']`: Path to the target's `.build_config` file.

* `deps_info['name']`: Nothing more than the basename of `deps_info['path']`
at the moment.

* `deps_info['deps_configs']`: List of paths to the `.build_config` files of
all *direct* dependencies of the current target.

    NOTE: Because the `.build_config` of a given target is always generated
    after the `.build_config` of its dependencies, the `write_build_config.py`
    script can use chains of `deps_configs` to compute transitive dependencies
    for each target when needed.

## Optional keys in `deps_info`:

The following keys will only appear in the `.build_config` files of certain
target types:

* `deps_info['requires_android']`: True to indicate that the corresponding
code uses Android-specific APIs, and thus cannot run on the host within a
regular JVM. May only appear in Java-related targets.

* `deps_info['supports_android']`:
May appear in Java-related targets, and indicates that
the corresponding code doesn't use Java APIs that are not available on
Android. As such it may run either on the host or on an Android device.

* `deps_info['assets']`:
Only seen for the [`android_assets`](#target_android_assets) type. See below.

* `deps_info['package_name']`: Java package name associated with this target.

    NOTE: For `android_resources` targets,
    this is the package name for the corresponding R class. For `android_apk`
    targets, this is the corresponding package name. This does *not* appear for
    other target types.

* `deps_info['android_manifest']`:
Path to an AndroidManifest.xml file related to the current target.

* `deps_info['base_module_config']`:
Only seen for the [`android_app_bundle`](#target_android_app_bundle) type.
Path to the base module for the bundle.

* `deps_info['module_name']`:
Only seen for the
[`android_app_bundle_module`](#target_android_app_bundle_module)
type. The name of the feature module.

* `deps_info['dependency_zips']`:
List of `deps_info['resources_zip']` entries for all `android_resources`
dependencies for the current target.

* `deps_info['extra_package_names']`:
Always empty for `android_resources` types. Otherwise,
the list of `deps_info['package_name']` entries for all `android_resources`
dependencies for the current target. Computed automatically by
`write_build_config.py`.

* `deps_info['dependency_r_txt_files']`:
Exists only on dist_aar. It is the list of deps_info['r_text_path'] from
transitive dependencies. Computed automatically.


# `.build_config` target types description:

## <a name="target_group">Target type `group`</a>:

This type corresponds to a simple target that is only used to group
dependencies. It matches the `java_group()` GN template. Its only top-level
`deps_info` keys are `supports_android` (always True), and `deps_configs`.


## <a name="target_android_resources">Target type `android_resources`</a>:

This type corresponds to targets that are used to group Android resource files.
For example, all `android_resources` dependencies of an `android_apk` will
end up packaged into the final APK by the build system.

It uses the following keys:


* `deps_info['res_sources_path']`:
Path to file containing a list of resource source files used by the
android_resources target.

* `deps_info['resources_zip']`:
*Required*. Path to the `.resources.zip` file that contains all raw/uncompiled
resource files for this target (and also no `R.txt`, `R.java` or `R.class`).

    If `deps_info['res_sources_path']` is missing, this must point to a prebuilt
    `.aar` archive containing resources. Otherwise, this will point to a zip
    archive generated at build time, wrapping the sources listed in
    `deps_info['res_sources_path']` into a single zip file.

* `deps_info['package_name']`:
Java package name that the R class for this target belongs to.

* `deps_info['android_manifest']`:
Optional. Path to the top-level Android manifest file associated with these
resources (if not provided, an empty manifest will be used to generate R.txt).

* `deps_info['resource_overlay']`:
Optional. Whether the resources in resources_zip should override resources with
the same name. Does not affect the behaviour of any android_resources()
dependencies of this target.  If a target with resource_overlay=true depends
on another target with resource_overlay=true the target with the dependency
overrides the other.

* `deps_info['r_text_path']`:
Provide the path to the `R.txt` file that describes the resources wrapped by
this target. Normally this file is generated from the content of the resource
directories or zip file, but some targets can provide their own `R.txt` file
if they want.

* `deps_info['srcjar_path']`:
Path to the `.srcjar` file that contains the auto-generated `R.java` source
file corresponding to the content of `deps_info['r_text_path']`. This is
*always* generated from the content of `deps_info['r_text_path']` by the
`build/android/gyp/process_resources.py` script.

## <a name="target_android_assets">Target type `android_assets`</a>:

This type corresponds to targets used to group Android assets, i.e. liberal
files that will be placed under `//assets/` within the final APK.

These use an `deps_info['assets']` key to hold a dictionary of values related
to assets covered by this target.

* `assets['sources']`:
The list of all asset source paths for this target. Each source path can
use an optional `:<zipPath>` suffix, where `<zipPath>` is the final location
of the assets (relative to `//assets/`) within the APK.

* `assets['outputs']`:
Optional. Some of the sources might be renamed before being stored in the
final //assets/ sub-directory. When this happens, this contains a list of
all renamed output file paths

    NOTE: When not empty, the first items of `assets['sources']` must match
    every item in this list. Extra sources correspond to non-renamed sources.

    NOTE: This comes from the `asset_renaming_destinations` parameter for the
    `android_assets()` GN template.

* `assets['disable_compression']`:
Optional. Will be True to indicate that these assets should be stored
uncompressed in the final APK. For example, this is necessary for locale
.pak files used by the System WebView feature.

* `assets['treat_as_locale_paks']`:
Optional. Will be True to indicate that these assets are locale `.pak` files
(containing localized strings for C++). These are later processed to generate
a special ``.build_config`.java` source file, listing all supported Locales in
the current build.


## <a name="target_java_library">Target type `java_library`</a>:

This type is used to describe target that wrap Java bytecode, either created
by compiling sources, or providing them with a prebuilt jar.

* `deps_info['public_deps_configs']`: List of paths to the `.build_config` files
of *direct* dependencies of the current target which are exposed as part of the
current target's public API.

* `deps_info['unprocessed_jar_path']`:
Path to the original .jar file for this target, before any kind of processing
through Proguard or other tools. For most targets this is generated
from sources, with a name like `$target_name.javac.jar`. However, when using
a prebuilt jar, this will point to the source archive directly.

* `deps_info['device_jar_path']`:
Path to a file that is the result of processing
`deps_info['unprocessed_jar_path']` with various tools (ready to be dexed).

* `deps_info['host_jar_path']`:
Path to a file that is the result of processing
`deps_info['unprocessed_jar_path']` with various tools (use by java_binary).

* `deps_info['interface_jar_path']:
Path to the interface jar generated for this library. This corresponds to
a jar file that only contains declarations. Generated by running the `ijar` on
`deps_info['unprocessed_jar_path']` or the `turbine` tool on source files.

* `deps_info['dex_path']`:
Path to the `.dex` file generated for this target, from
`deps_info['device_jar_path']` unless this comes from a prebuilt `.aar` archive.

* `deps_info['is_prebuilt']`:
True to indicate that this target corresponds to a prebuilt `.jar` file.
In this case, `deps_info['unprocessed_jar_path']` will point to the source
`.jar` file. Otherwise, it will be point to a build-generated file.

* `deps_info['target_sources_file']`:
Path to a single `.sources` file listing all the Java and Kotlin sources that
were used to generate the library (simple text format, one `.jar` path per
line).

* `deps_info['lint_android_manifest']`:
Path to an AndroidManifest.xml file to use for this lint target.

* `deps_info['lint_sources']`:
The list of all `deps_info['target_sources_file']` entries for all library
dependencies that are chromium code. Note: this is a list of files, where each
file contains a list of Java and Kotlin source files. This is used for lint.

* `deps_info['lint_aars']`:
List of all aars from transitive java dependencies. This allows lint to collect
their custom annotations.zip and run checks like @IntDef on their annotations.

* `deps_info['lint_srcjars']`:
List of all bundled srcjars of all transitive java library targets. Excludes
non-chromium java libraries.

* `deps_info['lint_resource_sources']`:
List of all resource sources files belonging to all transitive resource
dependencies of this target. Excludes resources owned by non-chromium code.

* `deps_info['lint_resource_zips']`:
List of all resource zip files belonging to all transitive resource dependencies
of this target. Excludes resources owned by non-chromium code.

* `deps_info['javac']`:
A dictionary containing information about the way the sources in this library
are compiled. Appears also on other Java-related targets. See the [dedicated
section about this](#dict_javac) below for details.

* `deps_info['javac_full_classpath']`:
The classpath used when performing bytecode processing. Essentially the
collection of all `deps_info['unprocessed_jar_path']` entries for the target
and all its dependencies.

* `deps_info['javac_full_interface_classpath']`:
The classpath used when using the errorprone compiler.

* `deps_info['proguard_enabled"]`:
True to indicate that ProGuard processing is enabled for this target.

* `deps_info['proguard_configs"]`:
A list of paths to ProGuard configuration files related to this library.

* `deps_info['extra_classpath_jars']:
For some Java related types, a list of extra `.jar` files to use at build time
but not at runtime.

## <a name="target_java_binary">Target type `java_binary`</a>:

This type corresponds to a Java binary, which is nothing more than a
`java_library` target that also provides a main class name. It thus inherits
all entries from the `java_library` type, and adds:

* `deps_info['main_class']`:
Name of the main Java class that serves as an entry point for the binary.

* `deps_info['device_classpath']`:
The classpath used when running a Java or Android binary. Essentially the
collection of all `deps_info['device_jar_path']` entries for the target and all
its dependencies.

* `deps_info['all_dex_files']`:
The list of paths to all `deps_info['dex_path']` entries for all libraries
that comprise this APK. Valid only for debug builds.

* `deps_info['preferred_dep']`:
Whether the target should be the preferred dep. This is usually the case when we
have a java_group that depends on either the public or internal dep accordingly,
and it is better to depend on the group rather than the underlying dep. Another
case is for android_library_factory targets, the factory target should be
preferred instead of the actual implementation.

## <a name="target_robolectric_binary">Target type `robolectric_binary`</a>:

A target type for JUnit-specific binaries. Identical to
[`java_binary`](#target_java_binary) in the context of `.build_config` files,
except the name.


## <a name="target_java_annotation_processor">Target type \
`java_annotation_processor`</a>:

A target type for Java annotation processors. Identical to
[`java_binary`](#target_java_binary) in the context of `.build_config` files,
except the name, except that it requires a `deps_info['main_class']` entry.


## <a name="target_android_apk">Target type `android_apk`</a>:

Corresponds to an Android APK. Inherits from the
[`java_binary`](#target_java_binary) type and adds:

* `deps_info['apk_path']`:
Path to the raw, unsigned, APK generated by this target.

* `deps_info['incremental_apk_path']`:
Path to the raw, unsigned, incremental APK generated by this target.

* `deps_info['incremental_install_json_path']`:
Path to the JSON file with per-apk details for incremental install.
See `build/android/gyp/incremental/write_installer_json.py` for more
details about its content.

* `deps_info['dist_jar']['all_interface_jars']`:
For `android_apk` and `dist_jar` targets, a list of all interface jar files
that will be merged into the final `.jar` file for distribution.

* `deps_info['final_dex']['path']`:
Path to the final classes.dex file (or classes.zip in case of multi-dex)
for this APK - only used for proguarded builds.

* `native['libraries']`
List of native libraries for the primary ABI to be embedded in this APK.
E.g. [ "libchrome.so" ] (i.e. this doesn't include any ABI sub-directory
prefix).

* `native['java_libraries_list']`
The same list as `native['libraries']` as a string holding a Java source
fragment, e.g. `"{\"chrome\"}"`, without any `lib` prefix, and `.so`
suffix (as expected by `System.loadLibrary()`).

* `native['second_abi_libraries']`
List of native libraries for the secondary ABI to be embedded in this APK.
Empty if only a single ABI is supported.

* `native['loadable_modules']`
A list of native libraries to store within the APK, in addition to those from
`native['libraries']`. These correspond to things like the Chromium linker
or instrumentation libraries.

* `native['secondary_abi_loadable_modules']`
Secondary ABI version of loadable_modules

* `native['library_always_compress']`
A list of library files that we always compress.

* `assets`
A list of assets stored compressed in the APK. Each entry has the format
`<source-path>:<destination-path>`, where `<source-path>` is relative to
`$CHROMIUM_OUTPUT_DIR`, and `<destination-path>` is relative to `//assets/`
within the APK.

NOTE: Not to be confused with the `deps_info['assets']` dictionary that
belongs to `android_assets` targets only.

* `uncompressed_assets`
A list of uncompressed assets stored in the APK. Each entry has the format
`<source-path>:<destination-path>` too.

* `locales_java_list`
A string holding a Java source fragment that gives the list of locales stored
uncompressed as android assets.

* `extra_android_manifests`
A list of `deps_configs['android_manifest]` entries, for all resource
dependencies for this target. I.e. a list of paths to manifest files for
all the resources in this APK. These will be merged with the root manifest
file to generate the final one used to build the APK.

* `java_resources_jars`
This is a list of `.jar` files whose *Java* resources should be included in
the final APK. For example, this is used to copy the `.res` files from the
EMMA Coverage tool. The copy will omit any `.class` file and the top-level
`//meta-inf/` directory from the input jars. Everything else will be copied
into the final APK as-is.

NOTE: This has nothing to do with *Android* resources.

* `deps_info['proguard_all_configs']`:
The collection of all 'deps_info['proguard_configs']` values from this target
and all its dependencies.

* `deps_info['proguard_classpath_jars']`:
The collection of all 'deps_info['extra_classpath_jars']` values from all
dependencies.

* `deps_info['proguard_under_test_mapping']`:
Applicable to apks with proguard enabled that have an apk_under_test. This is
the path to the apk_under_test's output proguard .mapping file.

## <a name="target_android_app_bundle_module">Target type \
`android_app_bundle_module`</a>:

Corresponds to an Android app bundle module. Very similar to an APK and
inherits the same fields, except that this does not generate an installable
file (see `android_app_bundle`), and for the following omitted fields:

* `deps_info['apk_path']`, `deps_info['incremental_apk_path']` and
  `deps_info['incremental_install_json_path']` are omitted.

* top-level `dist_jar` is omitted as well.

In addition to `android_apk` targets though come these new fields:

* `deps_info['proto_resources_path']`:
The path of an zip archive containing the APK's resources compiled to the
protocol buffer format (instead of regular binary xml + resources.arsc).

* `deps_info['module_rtxt_path']`:
The path of the R.txt file generated when compiling the resources for the bundle
module.

* `deps_info['module_pathmap_path']`:
The path of the pathmap file generated when compiling the resources for the
bundle module, if resource path shortening is enabled.

* `deps_info['base_allowlist_rtxt_path']`:
Optional path to an R.txt file used as a allowlist for base string resources.
This means that any string resource listed in this file *and* in
`deps_info['module_rtxt_path']` will end up in the base split APK of any
`android_app_bundle` target that uses this target as its base module.

This ensures that such localized strings are available to all bundle installs,
even when language based splits are enabled (e.g. required for WebView strings
inside the Monochrome bundle).


## <a name="target_android_app_bundle">Target type `android_app_bundle`</a>

This target type corresponds to an Android app bundle, and is built from one
or more `android_app_bundle_module` targets listed as dependencies.


## <a name="target_dist_aar">Target type `dist_aar`</a>:

This type corresponds to a target used to generate an `.aar` archive for
distribution. The archive's content is determined by the target's dependencies.

This always has the following entries:

  * `deps_info['supports_android']` (always True).
  * `deps_info['requires_android']` (always True).
  * `deps_info['proguard_configs']` (optional).


## <a name="target_dist_jar">Target type `dist_jar`</a>:

This type is similar to [`dist_aar`](#target_dist_aar) but is not
Android-specific, and used to create a `.jar` file that can be later
redistributed.

This always has the following entries:

  * `deps_info['proguard_enabled']` (False by default).
  * `deps_info['proguard_configs']` (optional).
  * `deps_info['supports_android']` (True by default).
  * `deps_info['requires_android']` (False by default).



## <a name="dict_javac">The `deps_info['javac']` dictionary</a>:

This dictionary appears in Java-related targets (e.g. `java_library`,
`android_apk` and others), and contains information related to the compilation
of Java sources, class files, and jars.

* `javac['classpath']`
The classpath used to compile this target when annotation processors are
present.

* `javac['interface_classpath']`
The classpath used to compile this target when annotation processors are
not present. These are also always used to known when a target needs to be
rebuilt.

* `javac['processor_classpath']`
The classpath listing the jars used for annotation processors. I.e. sent as
`-processorpath` when invoking `javac`.

* `javac['processor_classes']`
The list of annotation processor main classes. I.e. sent as `-processor' when
invoking `javac`.

## <a name="android_app_bundle">Target type `android_app_bundle`</a>:

This type corresponds to an Android app bundle (`.aab` file).

--------------- END_MARKDOWN ---------------------------------------------------
"""

import collections
import itertools
import json
import optparse
import os
import shutil
import sys
import xml.dom.minidom

from util import build_utils
from util import resource_utils
import action_helpers  # build_utils adds //build to sys.path.


# Types that should never be used as a dependency of another build config.
_ROOT_TYPES = ('android_apk', 'java_binary', 'java_annotation_processor',
               'robolectric_binary', 'android_app_bundle')
# Types that should not allow code deps to pass through.
_RESOURCE_TYPES = ('android_assets', 'android_resources', 'system_java_library')

# Cache of path -> JSON dict.
_dep_config_cache = {}


class OrderedSet(collections.OrderedDict):
  @staticmethod
  def fromkeys(iterable):
    out = OrderedSet()
    out.update(iterable)
    return out

  def add(self, key):
    self[key] = True

  def remove(self, key):
    if key in self:
      del self[key]

  def update(self, iterable):
    for v in iterable:
      self.add(v)

  def difference_update(self, iterable):
    for v in iterable:
      self.remove(v)


def _ExtractMarkdownDocumentation(input_text):
  """Extract Markdown documentation from a list of input strings lines.

     This generates a list of strings extracted from |input_text|, by looking
     for '-- BEGIN_MARKDOWN --' and '-- END_MARKDOWN --' line markers."""
  in_markdown = False
  result = []
  for line in input_text.splitlines():
    if in_markdown:
      if '-- END_MARKDOWN --' in line:
        in_markdown = False
      else:
        result.append(line)
    else:
      if '-- BEGIN_MARKDOWN --' in line:
        in_markdown = True

  return result


class AndroidManifest:
  def __init__(self, path):
    self.path = path
    dom = xml.dom.minidom.parse(path)
    manifests = dom.getElementsByTagName('manifest')
    assert len(manifests) == 1
    self.manifest = manifests[0]

  def GetInstrumentationElements(self):
    instrumentation_els = self.manifest.getElementsByTagName('instrumentation')
    if len(instrumentation_els) == 0:
      return None
    return instrumentation_els

  def CheckInstrumentationElements(self, expected_package):
    instrs = self.GetInstrumentationElements()
    if not instrs:
      raise Exception('No <instrumentation> elements found in %s' % self.path)
    for instr in instrs:
      instrumented_package = instr.getAttributeNS(
          'http://schemas.android.com/apk/res/android', 'targetPackage')
      if instrumented_package != expected_package:
        raise Exception(
            'Wrong instrumented package. Expected %s, got %s'
            % (expected_package, instrumented_package))

  def GetPackageName(self):
    return self.manifest.getAttribute('package')


def GetDepConfigRoot(path):
  if not path in _dep_config_cache:
    with open(path) as jsonfile:
      _dep_config_cache[path] = json.load(jsonfile)
  return _dep_config_cache[path]


def GetDepConfig(path):
  return GetDepConfigRoot(path)['deps_info']


def DepsOfType(wanted_type, configs):
  return [c for c in configs if c['type'] == wanted_type]


def DepPathsOfType(wanted_type, config_paths):
  return [p for p in config_paths if GetDepConfig(p)['type'] == wanted_type]


def GetAllDepsConfigsInOrder(deps_config_paths, filter_func=None):
  def apply_filter(paths):
    if filter_func:
      return [p for p in paths if filter_func(GetDepConfig(p))]
    return paths

  def discover(path):
    config = GetDepConfig(path)
    all_deps = config['deps_configs'] + config.get('public_deps_configs', [])
    return apply_filter(all_deps)

  deps_config_paths = apply_filter(deps_config_paths)
  deps_config_paths = build_utils.GetSortedTransitiveDependencies(
      deps_config_paths, discover)
  return deps_config_paths


def GetObjectByPath(obj, key_path):
  """Given an object, return its nth child based on a key path.
  """
  return GetObjectByPath(obj[key_path[0]], key_path[1:]) if key_path else obj


def RemoveObjDups(obj, base, *key_path):
  """Remove array items from an object[*kep_path] that are also
     contained in the base[*kep_path] (duplicates).
  """
  base_target = set(GetObjectByPath(base, key_path))
  target = GetObjectByPath(obj, key_path)
  target[:] = [x for x in target if x not in base_target]


class Deps:
  def __init__(self, direct_deps_config_paths):
    self._all_deps_config_paths = GetAllDepsConfigsInOrder(
        direct_deps_config_paths)
    self._direct_deps_configs = [
        GetDepConfig(p) for p in direct_deps_config_paths
    ]
    self._all_deps_configs = [
        GetDepConfig(p) for p in self._all_deps_config_paths
    ]
    self._direct_deps_config_paths = direct_deps_config_paths

  def All(self, wanted_type=None):
    if wanted_type is None:
      return self._all_deps_configs
    return DepsOfType(wanted_type, self._all_deps_configs)

  def Direct(self, wanted_type=None):
    if wanted_type is None:
      return self._direct_deps_configs
    return DepsOfType(wanted_type, self._direct_deps_configs)

  def AllConfigPaths(self):
    return self._all_deps_config_paths

  def GradlePrebuiltJarPaths(self):
    ret = []

    def helper(cur):
      for config in cur.Direct('java_library'):
        if config['is_prebuilt'] or config['gradle_treat_as_prebuilt']:
          if config['unprocessed_jar_path'] not in ret:
            ret.append(config['unprocessed_jar_path'])

    helper(self)
    return ret

  def GradleLibraryProjectDeps(self):
    ret = []

    def helper(cur):
      for config in cur.Direct('java_library'):
        if config['is_prebuilt']:
          pass
        elif config['gradle_treat_as_prebuilt']:
          all_deps = config['deps_configs'] + config.get(
              'public_deps_configs', [])
          helper(Deps(all_deps))
        elif config not in ret:
          ret.append(config)

    helper(self)
    return ret


def _MergeAssets(all_assets):
  """Merges all assets from the given deps.

  Returns:
    A tuple of: (compressed, uncompressed, locale_paks)
    |compressed| and |uncompressed| are lists of "srcPath:zipPath". srcPath is
    the path of the asset to add, and zipPath is the location within the zip
    (excluding assets/ prefix).
    |locale_paks| is a set of all zipPaths that have been marked as
    treat_as_locale_paks=true.
  """
  compressed = {}
  uncompressed = {}
  locale_paks = set()
  for asset_dep in all_assets:
    entry = asset_dep['assets']
    disable_compression = entry.get('disable_compression')
    treat_as_locale_paks = entry.get('treat_as_locale_paks')
    dest_map = uncompressed if disable_compression else compressed
    other_map = compressed if disable_compression else uncompressed
    outputs = entry.get('outputs', [])
    for src, dest in itertools.zip_longest(entry['sources'], outputs):
      if not dest:
        dest = os.path.basename(src)
      # Merge so that each path shows up in only one of the lists, and that
      # deps of the same target override previous ones.
      other_map.pop(dest, 0)
      dest_map[dest] = src
      if treat_as_locale_paks:
        locale_paks.add(dest)

  def create_list(asset_map):
    # Sort to ensure deterministic ordering.
    items = sorted(asset_map.items())
    return [f'{src}:{dest}' for dest, src in items]

  return create_list(compressed), create_list(uncompressed), locale_paks


def _SuffixAssets(suffix_names, suffix, assets):
  new_assets = []
  for x in assets:
    src_path, apk_subpath = x.split(':', 1)
    if apk_subpath in suffix_names:
      apk_subpath += suffix
    new_assets.append(f'{src_path}:{apk_subpath}')
  return new_assets


def _ResolveGroupsAndPublicDeps(config_paths):
  """Returns a list of configs with all groups inlined."""

  def helper(config_path):
    config = GetDepConfig(config_path)
    if config['type'] == 'group':
      # Groups combine public_deps with deps_configs, so no need to check
      # public_config_paths separately.
      return config['deps_configs']
    if config['type'] == 'android_resources':
      # android_resources targets do not support public_deps, but instead treat
      # all resource deps as public deps.
      return DepPathsOfType('android_resources', config['deps_configs'])

    return config.get('public_deps_configs', [])

  return build_utils.GetSortedTransitiveDependencies(config_paths, helper)


def _DepsFromPaths(dep_paths,
                   target_type,
                   filter_root_targets=True,
                   recursive_resource_deps=False):
  """Resolves all groups and trims dependency branches that we never want.

  E.g. When a resource or asset depends on an apk target, the intent is to
  include the .apk as a resource/asset, not to have the apk's classpath added.

  This method is meant to be called to get the top nodes (i.e. closest to
  current target) that we could then use to get a full transitive dependants
  list (eg using Deps#all). So filtering single elements out of this list,
  filters whole branches of dependencies. By resolving groups (i.e. expanding
  them to their constituents), depending on a group is equivalent to directly
  depending on each element of that group.
  """
  blocklist = []
  allowlist = []

  # Don't allow root targets to be considered as a dep.
  if filter_root_targets:
    blocklist.extend(_ROOT_TYPES)

  # Don't allow java libraries to cross through assets/resources.
  if target_type in _RESOURCE_TYPES:
    allowlist.extend(_RESOURCE_TYPES)
    # Pretend that this target directly depends on all of its transitive
    # dependencies.
    if recursive_resource_deps:
      dep_paths = GetAllDepsConfigsInOrder(dep_paths)
      # Exclude assets if recursive_resource_deps is set. The
      # recursive_resource_deps arg is used to pull resources into the base
      # module to workaround bugs accessing resources in isolated DFMs, but
      # assets should be kept in the DFMs.
      blocklist.append('android_assets')

  return _DepsFromPathsWithFilters(dep_paths, blocklist, allowlist)


def _FilterConfigPaths(dep_paths, blocklist=None, allowlist=None):
  if not blocklist and not allowlist:
    return dep_paths
  configs = [GetDepConfig(p) for p in dep_paths]
  if blocklist:
    configs = [c for c in configs if c['type'] not in blocklist]
  if allowlist:
    configs = [c for c in configs if c['type'] in allowlist]

  return [c['path'] for c in configs]


def _DepsFromPathsWithFilters(dep_paths, blocklist=None, allowlist=None):
  """Resolves all groups and trims dependency branches that we never want.

  See _DepsFromPaths.

  |blocklist| if passed, are the types of direct dependencies we do not care
  about (i.e. tips of branches that we wish to prune).

  |allowlist| if passed, are the only types of direct dependencies we care
  about (i.e. we wish to prune all other branches that do not start from one of
  these).
  """
  # Filter both before and after so that public_deps of blocked targets are not
  # added.
  allowlist_with_groups = None
  if allowlist:
    allowlist_with_groups = set(allowlist)
    allowlist_with_groups.add('group')
  dep_paths = _FilterConfigPaths(dep_paths, blocklist, allowlist_with_groups)
  dep_paths = _ResolveGroupsAndPublicDeps(dep_paths)
  dep_paths = _FilterConfigPaths(dep_paths, blocklist, allowlist)

  return Deps(dep_paths)


def _ExtractSharedLibsFromRuntimeDeps(runtime_deps_file):
  ret = []
  with open(runtime_deps_file) as f:
    for line in f:
      line = line.rstrip()
      if not line.endswith('.so'):
        continue
      # Only unstripped .so files are listed in runtime deps.
      # Convert to the stripped .so by going up one directory.
      ret.append(os.path.normpath(line.replace('lib.unstripped/', '')))
  ret.reverse()
  return ret


def _CreateJavaLibrariesList(library_paths):
  """Returns a java literal array with the "base" library names:
  e.g. libfoo.so -> foo
  """
  names = ['"%s"' % os.path.basename(s)[3:-3] for s in library_paths]
  return ('{%s}' % ','.join(sorted(set(names))))


def _CreateJavaLocaleListFromAssets(assets, locale_paks):
  """Returns a java literal array from a list of locale assets.

  Args:
    assets: A list of all APK asset paths in the form 'src:dst'
    locale_paks: A list of asset paths that correponds to the locale pak
      files of interest. Each |assets| entry will have its 'dst' part matched
      against it to determine if they are part of the result.
  Returns:
    A string that is a Java source literal array listing the locale names
    of the corresponding asset files, without directory or .pak suffix.
    E.g. '{"en-GB", "en-US", "es-ES", "fr", ... }'
  """
  assets_paths = [a.split(':')[1] for a in assets]
  locales = [os.path.basename(a)[:-4] for a in assets_paths if a in locale_paks]
  return '{%s}' % ','.join('"%s"' % l for l in sorted(locales))


def _AddJarMapping(jar_to_target, configs):
  for config in configs:
    jar = config.get('unprocessed_jar_path')
    if jar:
      jar_to_target[jar] = config['gn_target']
    for jar in config.get('extra_classpath_jars', []):
      jar_to_target[jar] = config['gn_target']


def _CompareClasspathPriority(dep):
  return 1 if dep.get('low_classpath_priority') else 0


def _DedupFeatureModuleSharedCode(uses_split_arg, modules,
                                  field_names_to_dedup):
  child_to_ancestors = collections.defaultdict(list)
  if uses_split_arg:
    for split_pair in uses_split_arg:
      child, parent = split_pair.split(':')
      assert child in modules
      assert parent in modules
      child_to_ancestors[child] = [parent]

  # Create a full list of ancestors for each module.
  for name in modules:
    if name == 'base':
      continue
    curr_name = name
    while curr_name in child_to_ancestors:
      parent = child_to_ancestors[curr_name][0]
      if parent not in child_to_ancestors[name]:
        child_to_ancestors[name].append(parent)
      curr_name = parent

    if curr_name != 'base':
      child_to_ancestors[name].append('base')

  # Strip out duplicates from ancestors.
  for name, module in modules.items():
    if name == 'base':
      continue
    # Make sure we get all ancestors, not just direct parent.
    for ancestor in child_to_ancestors[name]:
      for f in field_names_to_dedup:
        if f in module:
          RemoveObjDups(module, modules[ancestor], f)

  # Strip out duplicates from siblings/cousins.
  for f in field_names_to_dedup:
    _PromoteToCommonAncestor(modules, child_to_ancestors, f)


def _PromoteToCommonAncestor(modules, child_to_ancestors, field_name):
  module_to_fields_set = {}
  for module_name, module in modules.items():
    if field_name in module:
      module_to_fields_set[module_name] = set(module[field_name])

  seen = set()
  dupes = set()
  for fields in module_to_fields_set.values():
    new_dupes = seen & fields
    if new_dupes:
      dupes |= new_dupes
    seen |= fields

  for d in dupes:
    owning_modules = []
    for module_name, fields in module_to_fields_set.items():
      if d in fields:
        owning_modules.append(module_name)
    assert len(owning_modules) >= 2
    # Rely on the fact that ancestors are inserted from closest to
    # farthest, where "base" should always be the last element.
    # Arbitrarily using the first owning module - any would work.
    for ancestor in child_to_ancestors[owning_modules[0]]:
      ancestor_is_shared_with_all = True
      for o in owning_modules[1:]:
        if ancestor not in child_to_ancestors[o]:
          ancestor_is_shared_with_all = False
          break
      if ancestor_is_shared_with_all:
        common_ancestor = ancestor
        break
    for o in owning_modules:
      module_to_fields_set[o].remove(d)
    module_to_fields_set[common_ancestor].add(d)

  for module_name, module in modules.items():
    if field_name in module:
      module[field_name] = sorted(list(module_to_fields_set[module_name]))


def _CopyBuildConfigsForDebugging(debug_dir):
  shutil.rmtree(debug_dir, ignore_errors=True)
  os.makedirs(debug_dir)
  for src_path in _dep_config_cache:
    dst_path = os.path.join(debug_dir, src_path)
    assert dst_path.startswith(debug_dir), dst_path
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    shutil.copy(src_path, dst_path)
  print(f'Copied {len(_dep_config_cache)} .build_config.json into {debug_dir}')


def main(argv):
  parser = optparse.OptionParser()
  action_helpers.add_depfile_arg(parser)
  parser.add_option('--build-config', help='Path to build_config output.')
  parser.add_option('--store-deps-for-debugging-to',
                    help='Path to copy all transitive build config files to.')
  parser.add_option(
      '--type',
      help='Type of this target (e.g. android_library).')
  parser.add_option('--gn-target', help='GN label for this target')
  parser.add_option(
      '--deps-configs',
      help='GN-list of dependent build_config files.')
  parser.add_option(
      '--annotation-processor-configs',
      help='GN-list of build_config files for annotation processors.')

  # android_resources options
  parser.add_option('--srcjar', help='Path to target\'s resources srcjar.')
  parser.add_option('--resources-zip', help='Path to target\'s resources zip.')
  parser.add_option('--package-name',
      help='Java package name for these resources.')
  parser.add_option('--android-manifest',
                    help='Path to the root android manifest.')
  parser.add_option('--merged-android-manifest',
                    help='Path to the merged android manifest.')
  parser.add_option('--resource-dirs', action='append', default=[],
                    help='GYP-list of resource dirs')
  parser.add_option(
      '--res-sources-path',
      help='Path to file containing a list of paths to resources.')
  parser.add_option(
      '--resource-overlay',
      action='store_true',
      help='Whether resources passed in via --resources-zip should override '
      'resources with the same name')
  parser.add_option(
      '--recursive-resource-deps',
      action='store_true',
      help='Whether deps should be walked recursively to find resource deps.')

  # android_assets options
  parser.add_option('--asset-sources', help='List of asset sources.')
  parser.add_option('--asset-renaming-sources',
                    help='List of asset sources with custom destinations.')
  parser.add_option('--asset-renaming-destinations',
                    help='List of asset custom destinations.')
  parser.add_option('--disable-asset-compression', action='store_true',
                    help='Whether to disable asset compression.')
  parser.add_option('--treat-as-locale-paks', action='store_true',
      help='Consider the assets as locale paks in BuildConfig.java')

  # java library and group options
  parser.add_option('--preferred-dep',
                    action='store_true',
                    help='Whether the target should be preferred as a dep.')

  # java library options
  parser.add_option('--public-deps-configs',
                    help='GN list of config files of deps which are exposed as '
                    'part of the target\'s public API.')
  parser.add_option('--aar-path', help='Path to containing .aar file.')
  parser.add_option('--device-jar-path', help='Path to .jar for dexing.')
  parser.add_option('--host-jar-path', help='Path to .jar for java_binary.')
  parser.add_option('--unprocessed-jar-path',
      help='Path to the .jar to use for javac classpath purposes.')
  parser.add_option(
      '--interface-jar-path',
      help='Path to the interface .jar to use for javac classpath purposes.')
  parser.add_option('--is-prebuilt', action='store_true',
                    help='Whether the jar was compiled or pre-compiled.')
  parser.add_option('--target-sources-file', help='Path to .sources file')
  parser.add_option('--bundled-srcjars',
      help='GYP-list of .srcjars that have been included in this java_library.')
  parser.add_option('--supports-android', action='store_true',
      help='Whether this library supports running on the Android platform.')
  parser.add_option('--requires-android', action='store_true',
      help='Whether this library requires running on the Android platform.')
  parser.add_option('--bypass-platform-checks', action='store_true',
      help='Bypass checks for support/require Android platform.')
  parser.add_option('--extra-classpath-jars',
      help='GYP-list of .jar files to include on the classpath when compiling, '
           'but not to include in the final binary.')
  parser.add_option(
      '--low-classpath-priority',
      action='store_true',
      help='Indicates that the library should be placed at the end of the '
      'classpath.')
  parser.add_option(
      '--mergeable-android-manifests',
      help='GN-list of AndroidManifest.xml to include in manifest merging.')
  parser.add_option('--gradle-treat-as-prebuilt', action='store_true',
      help='Whether this library should be treated as a prebuilt library by '
           'generate_gradle.py.')
  parser.add_option('--main-class',
      help='Main class for java_binary or java_annotation_processor targets.')
  parser.add_option('--java-resources-jar-path',
                    help='Path to JAR that contains java resources. Everything '
                    'from this JAR except meta-inf/ content and .class files '
                    'will be added to the final APK.')
  parser.add_option(
      '--non-chromium-code',
      action='store_true',
      help='True if a java library is not chromium code, used for lint.')

  # robolectric_library options
  parser.add_option('--is-robolectric',
                    action='store_true',
                    help='Whether this is a host side android test library.')

  # android library options
  parser.add_option('--dex-path', help='Path to target\'s dex output.')

  # native library options
  parser.add_option('--shared-libraries-runtime-deps',
                    help='Path to file containing runtime deps for shared '
                         'libraries.')
  parser.add_option(
      '--loadable-modules',
      action='append',
      help='GN-list of native libraries for primary '
      'android-abi. Can be specified multiple times.',
      default=[])
  parser.add_option('--secondary-abi-shared-libraries-runtime-deps',
                    help='Path to file containing runtime deps for secondary '
                         'abi shared libraries.')
  parser.add_option(
      '--secondary-abi-loadable-modules',
      action='append',
      help='GN-list of native libraries for secondary '
      'android-abi. Can be specified multiple times.',
      default=[])
  parser.add_option(
      '--native-lib-placeholders',
      action='append',
      help='GN-list of native library placeholders to add.',
      default=[])
  parser.add_option(
      '--secondary-native-lib-placeholders',
      action='append',
      help='GN-list of native library placeholders to add '
      'for the secondary android-abi.',
      default=[])
  parser.add_option('--uncompress-shared-libraries', default=False,
                    action='store_true',
                    help='Whether to store native libraries uncompressed')
  parser.add_option(
      '--library-always-compress',
      help='The list of library files that we always compress.')

  # apk options
  parser.add_option('--apk-path', help='Path to the target\'s apk output.')
  parser.add_option('--incremental-apk-path',
                    help="Path to the target's incremental apk output.")
  parser.add_option('--incremental-install-json-path',
                    help="Path to the target's generated incremental install "
                    "json.")
  parser.add_option(
      '--suffix-apk-assets-used-by',
      help='Path to the build config of the apk whose asset list should be '
      'suffixed.')
  parser.add_option(
      '--tested-apk-config',
      help='Path to the build config of the tested apk (for an instrumentation '
      'test apk).')
  parser.add_option(
      '--proguard-enabled',
      action='store_true',
      help='Whether proguard is enabled for this apk or bundle module.')
  parser.add_option(
      '--proguard-configs',
      help='GN-list of proguard flag files to use in final apk.')
  parser.add_option(
      '--proguard-mapping-path', help='Path to jar created by ProGuard step')

  # apk options that are static library specific
  parser.add_option(
      '--static-library-dependent-configs',
      help='GN list of .build_configs of targets that use this target as a '
      'static library.')

  # options shared between android_resources and apk targets
  parser.add_option('--r-text-path', help='Path to target\'s R.txt file.')

  parser.add_option('--fail',
      help='GN-list of error message lines to fail with.')

  parser.add_option('--final-dex-path',
                    help='Path to final input classes.dex (or classes.zip) to '
                    'use in final apk.')
  parser.add_option('--res-size-info', help='Path to .ap_.info')
  parser.add_option('--apk-proto-resources',
                    help='Path to resources compiled in protocol buffer format '
                         ' for this apk.')
  parser.add_option(
      '--module-pathmap-path',
      help='Path to pathmap file for resource paths in a bundle module.')
  parser.add_option(
      '--base-allowlist-rtxt-path',
      help='Path to R.txt file for the base resources allowlist.')

  parser.add_option('--generate-markdown-format-doc', action='store_true',
                    help='Dump the Markdown .build_config format documentation '
                    'then exit immediately.')

  parser.add_option('--module-name', help='The name of this feature module.')
  parser.add_option(
      '--base-module-build-config',
      help='Path to the base module\'s build config '
      'if this is a feature module.')
  parser.add_option('--parent-module-build-config',
                    help='Path to the parent module\'s build config '
                    'when not using base module as parent.')

  parser.add_option(
      '--module-build-configs',
      help='For bundles, the paths of all non-async module .build_configs '
      'for modules that are part of the bundle.')
  parser.add_option(
      '--uses-split',
      action='append',
      help='List of name pairs separated by : mapping a feature module to a '
      'dependent feature module.')

  parser.add_option(
      '--trace-events-jar-dir',
      help='Directory of rewritten .jar files for trace event rewriting.')

  parser.add_option('--version-name', help='Version name for this APK.')
  parser.add_option('--version-code', help='Version code for this APK.')

  # dist_jar options
  parser.add_option('--use-interface-jars', action='store_true')
  parser.add_option('--direct-deps-only', action='store_true')

  options, args = parser.parse_args(argv)

  if args:
    parser.error('No positional arguments should be given.')

  if options.generate_markdown_format_doc:
    doc_lines = _ExtractMarkdownDocumentation(__doc__)
    for line in doc_lines:
      print(line)
    return 0

  if options.fail:
    parser.error('\n'.join(action_helpers.parse_gn_list(options.fail)))

  lib_options = ['unprocessed_jar_path', 'interface_jar_path']
  device_lib_options = ['device_jar_path', 'dex_path']
  required_options_map = {
      'android_apk': ['build_config'] + lib_options + device_lib_options,
      'android_app_bundle_module':
      ['build_config', 'res_size_info'] + lib_options + device_lib_options,
      'android_assets': ['build_config'],
      'android_resources': ['build_config', 'resources_zip'],
      'dist_aar': ['build_config'],
      'dist_jar': ['build_config'],
      'group': ['build_config'],
      'java_annotation_processor': ['build_config', 'main_class'],
      'java_binary': ['build_config'],
      'java_library': ['build_config', 'host_jar_path'] + lib_options,
      'robolectric_binary': ['build_config'],
      'system_java_library': ['build_config', 'unprocessed_jar_path'],
      'android_app_bundle': ['build_config', 'module_build_configs'],
  }
  required_options = required_options_map.get(options.type)
  if not required_options:
    raise Exception('Unknown type: <%s>' % options.type)

  build_utils.CheckOptions(options, parser, required_options)

  if options.type != 'android_app_bundle_module':
    if options.apk_proto_resources:
      raise Exception('--apk-proto-resources can only be used with '
                      '--type=android_app_bundle_module')
    if options.module_pathmap_path:
      raise Exception('--module-pathmap-path can only be used with '
                      '--type=android_app_bundle_module')
    if options.base_allowlist_rtxt_path:
      raise Exception('--base-allowlist-rtxt-path can only be used with '
                      '--type=android_app_bundle_module')
    if options.module_name:
      raise Exception('--module-name can only be used with '
                      '--type=android_app_bundle_module')

  is_apk_or_module_target = options.type in ('android_apk',
      'android_app_bundle_module')

  if not is_apk_or_module_target:
    if options.library_always_compress:
      raise Exception(
          '--library-always-compress can only be used with --type=android_apk '
          'or --type=android_app_bundle_module')

  if options.device_jar_path and not options.dex_path:
    raise Exception('java_library that supports Android requires a dex path.')
  if any(getattr(options, x) for x in lib_options):
    for attr in lib_options:
      if not getattr(options, attr):
        raise('Expected %s to be set.' % attr)

  if options.requires_android and not options.supports_android:
    raise Exception(
        '--supports-android is required when using --requires-android')

  is_java_target = options.type in ('java_binary', 'robolectric_binary',
                                    'java_annotation_processor', 'java_library',
                                    'android_apk', 'dist_aar', 'dist_jar',
                                    'system_java_library',
                                    'android_app_bundle_module')

  deps_configs_paths = action_helpers.parse_gn_list(options.deps_configs)
  public_deps_configs_paths = action_helpers.parse_gn_list(
      options.public_deps_configs)
  deps_configs_paths += public_deps_configs_paths
  deps = _DepsFromPaths(deps_configs_paths,
                        options.type,
                        recursive_resource_deps=options.recursive_resource_deps)
  public_deps = _DepsFromPaths(public_deps_configs_paths, options.type)
  processor_deps = _DepsFromPaths(action_helpers.parse_gn_list(
      options.annotation_processor_configs or ''),
                                  options.type,
                                  filter_root_targets=False)

  all_inputs = (deps.AllConfigPaths() + processor_deps.AllConfigPaths())

  if options.recursive_resource_deps:
    # Include java_library targets since changes to these targets can remove
    # resource deps from the build, which would require rebuilding this target's
    # build config file: crbug.com/1168655.
    recursive_java_deps = _DepsFromPathsWithFilters(
        GetAllDepsConfigsInOrder(deps_configs_paths),
        allowlist=['java_library'])
    all_inputs.extend(recursive_java_deps.AllConfigPaths())

  system_library_deps = deps.Direct('system_java_library')
  all_deps = deps.All()
  all_library_deps = deps.All('java_library')

  direct_resources_deps = deps.Direct('android_resources')
  if options.type == 'java_library':
    # For Java libraries, restrict to resource targets that are direct deps, or
    # are indirect via other resource targets.
    # The indirect-through-other-targets ones are picked up because
    # _ResolveGroupsAndPublicDeps() treats resource deps of resource targets as
    # public_deps.
    all_resources_deps = direct_resources_deps
  else:
    all_resources_deps = deps.All('android_resources')

  if options.type == 'android_resources' and options.recursive_resource_deps:
    # android_resources targets that want recursive resource deps also need to
    # collect package_names from all library deps. This ensures the R.java files
    # for these libraries will get pulled in along with the resources.
    android_resources_library_deps = _DepsFromPathsWithFilters(
        deps_configs_paths, allowlist=['java_library']).All('java_library')

  base_module_build_config = None
  if options.base_module_build_config:
    base_module_build_config = GetDepConfigRoot(
        options.base_module_build_config)
  parent_module_build_config = base_module_build_config
  if options.parent_module_build_config:
    parent_module_build_config = GetDepConfigRoot(
        options.parent_module_build_config)

  # Initialize some common config.
  # Any value that needs to be queryable by dependents must go within deps_info.
  config = {
      'deps_info': {
          'name': os.path.basename(options.build_config),
          'path': options.build_config,
          'type': options.type,
          'gn_target': options.gn_target,
          'chromium_code': not options.non_chromium_code,
      },
      # Info needed only by generate_gradle.py.
      'gradle': {}
  }
  deps_info = config['deps_info']
  gradle = config['gradle']

  # The paths we record as deps can differ from deps_config_paths:
  # 1) Paths can be removed when blocked by _ROOT_TYPES / _RESOURCE_TYPES.
  # 2) Paths can be added when promoted from group deps or public_deps of deps.
  #    Deps are promoted from groups/public_deps in order to make the filtering
  #    of 1) work through group() targets (which themselves are not resource
  #    targets, but should be treated as such when depended on by a resource
  #    target. A more involved filtering implementation could work to maintain
  #    the semantics of 1) without the need to promote deps, but we've avoided
  #    such an undertaking so far.
  public_deps_set = set()
  if public_deps_configs_paths:
    deps_info['public_deps_configs'] = [d['path'] for d in public_deps.Direct()]
    public_deps_set = set(deps_info['public_deps_configs'])

  deps_info['deps_configs'] = [
      d['path'] for d in deps.Direct() if d['path'] not in public_deps_set
  ]

  if options.type == 'android_apk' and options.tested_apk_config:
    tested_apk_deps = Deps([options.tested_apk_config])
    tested_apk_config = tested_apk_deps.Direct()[0]
    gradle['apk_under_test'] = tested_apk_config['name']

  if options.type == 'android_app_bundle_module':
    deps_info['module_name'] = options.module_name

  # Required for generating gradle files.
  if options.type == 'java_library':
    deps_info['is_prebuilt'] = bool(options.is_prebuilt)
    deps_info['gradle_treat_as_prebuilt'] = options.gradle_treat_as_prebuilt

  if options.preferred_dep:
    deps_info['preferred_dep'] = bool(options.preferred_dep)

  if options.android_manifest:
    deps_info['android_manifest'] = options.android_manifest

  if options.merged_android_manifest:
    deps_info['merged_android_manifest'] = options.merged_android_manifest

  if options.bundled_srcjars:
    deps_info['bundled_srcjars'] = action_helpers.parse_gn_list(
        options.bundled_srcjars)

  if options.target_sources_file:
    deps_info['target_sources_file'] = options.target_sources_file

  if is_java_target:
    if options.main_class:
      deps_info['main_class'] = options.main_class

    dependent_prebuilt_jars = deps.GradlePrebuiltJarPaths()
    dependent_prebuilt_jars.sort()
    if dependent_prebuilt_jars:
      gradle['dependent_prebuilt_jars'] = dependent_prebuilt_jars

    dependent_android_projects = []
    dependent_java_projects = []
    for c in deps.GradleLibraryProjectDeps():
      if c['requires_android']:
        dependent_android_projects.append(c['path'])
      else:
        dependent_java_projects.append(c['path'])

    gradle['dependent_android_projects'] = dependent_android_projects
    gradle['dependent_java_projects'] = dependent_java_projects

  if options.r_text_path:
    deps_info['r_text_path'] = options.r_text_path

  if is_apk_or_module_target or options.type in ('group', 'java_library',
                                                 'robolectric_binary'):
    if options.apk_proto_resources:
      deps_info['proto_resources_path'] = options.apk_proto_resources

    deps_info['version_name'] = options.version_name
    deps_info['version_code'] = options.version_code
    if options.module_pathmap_path:
      deps_info['module_pathmap_path'] = options.module_pathmap_path
    else:
      # Ensure there is an entry, even if it is empty, for modules
      # that have not enabled resource path shortening. Otherwise
      # build_utils.ExpandFileArgs fails.
      deps_info['module_pathmap_path'] = ''

    if options.base_allowlist_rtxt_path:
      deps_info['base_allowlist_rtxt_path'] = options.base_allowlist_rtxt_path
    else:
      # Ensure there is an entry, even if it is empty, for modules
      # that don't need such a allowlist.
      deps_info['base_allowlist_rtxt_path'] = ''

  if is_java_target:
    deps_info['requires_android'] = bool(options.requires_android)
    deps_info['supports_android'] = bool(options.supports_android)

    # robolectric is special in that its an android target that runs on host.
    # You are allowed to depend on both android |deps_require_android| and
    # non-android |deps_not_support_android| targets.
    if not options.bypass_platform_checks and not options.is_robolectric:
      deps_require_android = direct_resources_deps + [
          d for d in deps.Direct() if d.get('requires_android', False)
      ]
      deps_not_support_android = [
          d for d in deps.Direct() if not d.get('supports_android', True)
      ]

      if deps_require_android and not options.requires_android:
        raise Exception(
            'Some deps require building for the Android platform:\n' +
            '\n'.join('* ' + d['gn_target'] for d in deps_require_android))

      if deps_not_support_android and options.supports_android:
        raise Exception('Not all deps support the Android platform:\n' +
                        '\n'.join('* ' + d['gn_target']
                                  for d in deps_not_support_android))

  if is_apk_or_module_target or options.type == 'dist_jar':
    all_dex_files = [c['dex_path'] for c in all_library_deps]

  if is_java_target:
    # Classpath values filled in below (after applying tested_apk_config).
    config['javac'] = {}
    if options.aar_path:
      deps_info['aar_path'] = options.aar_path
    if options.unprocessed_jar_path:
      deps_info['unprocessed_jar_path'] = options.unprocessed_jar_path
      deps_info['interface_jar_path'] = options.interface_jar_path
    if options.device_jar_path:
      deps_info['device_jar_path'] = options.device_jar_path
    if options.host_jar_path:
      deps_info['host_jar_path'] = options.host_jar_path
    if options.dex_path:
      deps_info['dex_path'] = options.dex_path
      if is_apk_or_module_target:
        all_dex_files.append(options.dex_path)
    if options.low_classpath_priority:
      deps_info['low_classpath_priority'] = True
    if options.type == 'android_apk':
      deps_info['apk_path'] = options.apk_path
      deps_info['incremental_apk_path'] = options.incremental_apk_path
      deps_info['incremental_install_json_path'] = (
          options.incremental_install_json_path)

  if options.type == 'android_assets':
    all_asset_sources = []
    if options.asset_renaming_sources:
      all_asset_sources.extend(
          action_helpers.parse_gn_list(options.asset_renaming_sources))
    if options.asset_sources:
      all_asset_sources.extend(
          action_helpers.parse_gn_list(options.asset_sources))

    deps_info['assets'] = {
        'sources': all_asset_sources
    }
    if options.asset_renaming_destinations:
      deps_info['assets']['outputs'] = (action_helpers.parse_gn_list(
          options.asset_renaming_destinations))
    if options.disable_asset_compression:
      deps_info['assets']['disable_compression'] = True
    if options.treat_as_locale_paks:
      deps_info['assets']['treat_as_locale_paks'] = True

  if options.type == 'android_resources':
    deps_info['resources_zip'] = options.resources_zip
    if options.resource_overlay:
      deps_info['resource_overlay'] = True
    if options.srcjar:
      deps_info['srcjar'] = options.srcjar
    if options.android_manifest:
      manifest = AndroidManifest(options.android_manifest)
      deps_info['package_name'] = manifest.GetPackageName()
    if options.package_name:
      deps_info['package_name'] = options.package_name
    deps_info['res_sources_path'] = ''
    if options.res_sources_path:
      deps_info['res_sources_path'] = options.res_sources_path

  if (options.requires_android
      and options.type == 'java_library') or options.is_robolectric:
    if options.package_name:
      deps_info['package_name'] = options.package_name

  if options.type in ('android_resources', 'android_apk', 'robolectric_binary',
                      'dist_aar', 'android_app_bundle_module', 'java_library'):
    dependency_zips = []
    dependency_zip_overlays = []
    for c in all_resources_deps:
      if not c['resources_zip']:
        continue

      dependency_zips.append(c['resources_zip'])
      if c.get('resource_overlay'):
        dependency_zip_overlays.append(c['resources_zip'])

    extra_package_names = []

    if options.type != 'android_resources':
      extra_package_names = [
          c['package_name'] for c in all_resources_deps if 'package_name' in c
      ]
      if options.package_name:
        extra_package_names += [options.package_name]

      # android_resources targets which specified recursive_resource_deps may
      # have extra_package_names.
      for resources_dep in all_resources_deps:
        extra_package_names.extend(resources_dep['extra_package_names'])

      # In final types (i.e. apks and modules) that create real R.java files,
      # they must collect package names from java_libraries as well.
      # https://crbug.com/1073476
      if options.type != 'java_library':
        extra_package_names.extend([
            c['package_name'] for c in all_library_deps if 'package_name' in c
        ])
    elif options.recursive_resource_deps:
      # Pull extra_package_names from library deps if recursive resource deps
      # are required.
      extra_package_names = [
          c['package_name'] for c in android_resources_library_deps
          if 'package_name' in c
      ]
      config['deps_info']['includes_recursive_resources'] = True

    if options.type in ('dist_aar', 'java_library'):
      r_text_files = [
          c['r_text_path'] for c in all_resources_deps if 'r_text_path' in c
      ]
      deps_info['dependency_r_txt_files'] = r_text_files

    if options.type == 'android_apk' and options.tested_apk_config:
      config['deps_info']['arsc_package_name'] = (
          tested_apk_config['package_name'])
      # We should not shadow the actual R.java files of the apk_under_test by
      # creating new R.java files with the same package names in the tested apk.
      extra_package_names = [
          package for package in extra_package_names
          if package not in tested_apk_config['extra_package_names']
      ]
    if options.res_size_info:
      config['deps_info']['res_size_info'] = options.res_size_info

    # Safe to sort: Build checks that non-overlay resource have no overlap.
    dependency_zips.sort()
    config['deps_info']['dependency_zips'] = dependency_zips
    config['deps_info']['dependency_zip_overlays'] = dependency_zip_overlays
    # Order doesn't matter, so make stable.
    extra_package_names.sort()
    config['deps_info']['extra_package_names'] = extra_package_names

  # These are .jars to add to javac classpath but not to runtime classpath.
  extra_classpath_jars = action_helpers.parse_gn_list(
      options.extra_classpath_jars)
  if extra_classpath_jars:
    extra_classpath_jars.sort()
    deps_info['extra_classpath_jars'] = extra_classpath_jars

  mergeable_android_manifests = action_helpers.parse_gn_list(
      options.mergeable_android_manifests)
  mergeable_android_manifests.sort()
  if mergeable_android_manifests:
    deps_info['mergeable_android_manifests'] = mergeable_android_manifests

  extra_proguard_classpath_jars = []
  proguard_configs = action_helpers.parse_gn_list(options.proguard_configs)
  if proguard_configs:
    # Make a copy of |proguard_configs| since it's mutated below.
    deps_info['proguard_configs'] = list(proguard_configs)


  if is_java_target:
    classpath_direct_deps = deps.Direct()
    classpath_direct_library_deps = deps.Direct('java_library')

    # The classpath used to compile this target when annotation processors are
    # present.
    javac_classpath = set(c['unprocessed_jar_path']
                          for c in classpath_direct_library_deps)
    # The classpath used to compile this target when annotation processors are
    # not present. These are also always used to know when a target needs to be
    # rebuilt.
    javac_interface_classpath = set(c['interface_jar_path']
                                    for c in classpath_direct_library_deps)

    # Preserve order of |all_library_deps|. Move low priority libraries to the
    # end of the classpath.
    all_library_deps_sorted_for_classpath = sorted(
        all_library_deps[::-1], key=_CompareClasspathPriority)

    # The classpath used for bytecode-rewritting.
    javac_full_classpath = OrderedSet.fromkeys(
        c['unprocessed_jar_path']
        for c in all_library_deps_sorted_for_classpath)
    # The classpath used for error prone.
    javac_full_interface_classpath = OrderedSet.fromkeys(
        c['interface_jar_path'] for c in all_library_deps_sorted_for_classpath)

    # Adding base module to classpath to compile against its R.java file
    if base_module_build_config:
      javac_full_classpath.add(
          base_module_build_config['deps_info']['unprocessed_jar_path'])
      javac_full_interface_classpath.add(
          base_module_build_config['deps_info']['interface_jar_path'])
      # Turbine now compiles headers against only the direct classpath, so the
      # base module's interface jar must be on the direct interface classpath.
      javac_interface_classpath.add(
          base_module_build_config['deps_info']['interface_jar_path'])

    for dep in classpath_direct_deps:
      if 'extra_classpath_jars' in dep:
        javac_classpath.update(dep['extra_classpath_jars'])
        javac_interface_classpath.update(dep['extra_classpath_jars'])
    for dep in all_deps:
      if 'extra_classpath_jars' in dep:
        javac_full_classpath.update(dep['extra_classpath_jars'])
        javac_full_interface_classpath.update(dep['extra_classpath_jars'])

    # TODO(agrieve): Might be less confusing to fold these into bootclasspath.
    # Deps to add to the compile-time classpath (but not the runtime classpath).
    # These are jars specified by input_jars_paths that almost never change.
    # Just add them directly to all the classpaths.
    if options.extra_classpath_jars:
      javac_classpath.update(extra_classpath_jars)
      javac_interface_classpath.update(extra_classpath_jars)
      javac_full_classpath.update(extra_classpath_jars)
      javac_full_interface_classpath.update(extra_classpath_jars)

  if is_java_target or options.type == 'android_app_bundle':
    # The classpath to use to run this target (or as an input to ProGuard).
    device_classpath = []
    # dist_jar configs should not list their device jar in their own classpath
    # since the classpath is used to create the device jar itself.
    if (is_java_target and options.device_jar_path
        and options.type != 'dist_jar'):
      device_classpath.append(options.device_jar_path)
    device_classpath.extend(
        c.get('device_jar_path') for c in all_library_deps
        if c.get('device_jar_path'))
    if options.type == 'android_app_bundle':
      for d in deps.Direct('android_app_bundle_module'):
        device_classpath.extend(c for c in d.get('device_classpath', [])
                                if c not in device_classpath)

  all_dist_jar_deps = deps.All('dist_jar')

  # We allow lint to be run on android_apk targets, so we collect lint
  # artifacts for them.
  # We allow lint to be run on android_app_bundle targets, so we need to
  # collect lint artifacts for the android_app_bundle_module targets that the
  # bundle includes. Different android_app_bundle targets may include different
  # android_app_bundle_module targets, so the bundle needs to be able to
  # de-duplicate these lint artifacts.
  if options.type in ('android_app_bundle_module', 'android_apk'):
    # Collect all sources and resources at the apk/bundle_module level.
    lint_aars = set()
    lint_srcjars = set()
    lint_sources = set()
    lint_resource_sources = set()
    lint_resource_zips = set()

    if options.target_sources_file:
      lint_sources.add(options.target_sources_file)
    if options.bundled_srcjars:
      lint_srcjars.update(deps_info['bundled_srcjars'])
    for c in all_library_deps:
      if c['chromium_code'] and c['requires_android']:
        if 'target_sources_file' in c:
          lint_sources.add(c['target_sources_file'])
        lint_srcjars.update(c['bundled_srcjars'])
      if 'aar_path' in c:
        lint_aars.add(c['aar_path'])

    if options.res_sources_path:
      lint_resource_sources.add(options.res_sources_path)
    if options.resources_zip:
      lint_resource_zips.add(options.resources_zip)
    for c in all_resources_deps:
      if c['chromium_code']:
        # Prefer res_sources_path to resources_zips so that lint errors have
        # real paths and to avoid needing to extract during lint.
        if c['res_sources_path']:
          lint_resource_sources.add(c['res_sources_path'])
        else:
          lint_resource_zips.add(c['resources_zip'])

    deps_info['lint_aars'] = sorted(lint_aars)
    deps_info['lint_srcjars'] = sorted(lint_srcjars)
    deps_info['lint_sources'] = sorted(lint_sources)
    deps_info['lint_resource_sources'] = sorted(lint_resource_sources)
    deps_info['lint_resource_zips'] = sorted(lint_resource_zips)
    deps_info['lint_extra_android_manifests'] = []

    if options.type == 'android_apk':
      assert options.android_manifest, 'Android APKs must define a manifest'
      deps_info['lint_android_manifest'] = options.android_manifest

  if options.type == 'android_app_bundle':
    module_config_paths = action_helpers.parse_gn_list(
        options.module_build_configs)
    module_configs = [GetDepConfig(c) for c in module_config_paths]
    module_configs_by_name = {d['module_name']: d for d in module_configs}
    per_module_fields = [
        'device_classpath', 'trace_event_rewritten_device_classpath',
        'all_dex_files', 'assets'
    ]
    lint_aars = set()
    lint_srcjars = set()
    lint_sources = set()
    lint_resource_sources = set()
    lint_resource_zips = set()
    lint_extra_android_manifests = set()
    config['modules'] = {}
    modules = config['modules']
    for n, c in module_configs_by_name.items():
      if n == 'base':
        assert 'base_module_config' not in deps_info, (
            'Must have exactly 1 base module!')
        deps_info['package_name'] = c['package_name']
        deps_info['version_code'] = c['version_code']
        deps_info['version_name'] = c['version_name']
        deps_info['base_module_config'] = c['path']
        deps_info['lint_android_manifest'] = c['android_manifest']
      else:
        # All manifests nodes are merged into the main manfiest by lint.py.
        lint_extra_android_manifests.add(c['android_manifest'])

      lint_extra_android_manifests.update(c['extra_android_manifests'])
      lint_aars.update(c['lint_aars'])
      lint_srcjars.update(c['lint_srcjars'])
      lint_sources.update(c['lint_sources'])
      lint_resource_sources.update(c['lint_resource_sources'])
      lint_resource_zips.update(c['lint_resource_zips'])
      module = modules[n] = {}
      for f in per_module_fields:
        if f in c:
          module[f] = c[f]
    deps_info['lint_aars'] = sorted(lint_aars)
    deps_info['lint_srcjars'] = sorted(lint_srcjars)
    deps_info['lint_sources'] = sorted(lint_sources)
    deps_info['lint_resource_sources'] = sorted(lint_resource_sources)
    deps_info['lint_resource_zips'] = sorted(lint_resource_zips)
    deps_info['lint_extra_android_manifests'] = sorted(
        lint_extra_android_manifests)

    _DedupFeatureModuleSharedCode(options.uses_split, modules,
                                  per_module_fields)

  system_jars = [c['unprocessed_jar_path'] for c in system_library_deps]
  system_interface_jars = [c['interface_jar_path'] for c in system_library_deps]
  if system_library_deps:
    config['android'] = {}
    config['android']['sdk_interface_jars'] = system_interface_jars
    config['android']['sdk_jars'] = system_jars

  if options.type in ('android_apk', 'dist_aar',
      'dist_jar', 'android_app_bundle_module', 'android_app_bundle'):
    for c in all_deps:
      proguard_configs.extend(c.get('proguard_configs', []))
      extra_proguard_classpath_jars.extend(c.get('extra_classpath_jars', []))
    if options.type == 'android_app_bundle':
      for c in deps.Direct('android_app_bundle_module'):
        proguard_configs.extend(p for p in c.get('proguard_configs', []))
    if options.type == 'android_app_bundle':
      for d in deps.Direct('android_app_bundle_module'):
        extra_proguard_classpath_jars.extend(
            c for c in d.get('proguard_classpath_jars', [])
            if c not in extra_proguard_classpath_jars)

    if options.type == 'android_app_bundle':
      deps_proguard_enabled = []
      deps_proguard_disabled = []
      for d in deps.Direct('android_app_bundle_module'):
        if not d['device_classpath']:
          # We don't care about modules that have no Java code for proguarding.
          continue
        if d['proguard_enabled']:
          deps_proguard_enabled.append(d['name'])
        else:
          deps_proguard_disabled.append(d['name'])
      if deps_proguard_enabled and deps_proguard_disabled:
        raise Exception('Deps %s have proguard enabled while deps %s have '
                        'proguard disabled' % (deps_proguard_enabled,
                                               deps_proguard_disabled))
    deps_info['proguard_enabled'] = bool(options.proguard_enabled)

    if options.proguard_mapping_path:
      deps_info['proguard_mapping_path'] = options.proguard_mapping_path

  # The java code for an instrumentation test apk is assembled differently for
  # ProGuard vs. non-ProGuard.
  #
  # Without ProGuard: Each library's jar is dexed separately and then combined
  # into a single classes.dex. A test apk will include all dex files not already
  # present in the apk-under-test. At runtime all test code lives in the test
  # apk, and the program code lives in the apk-under-test.
  #
  # With ProGuard: Each library's .jar file is fed into ProGuard, which outputs
  # a single .jar, which is then dexed into a classes.dex. A test apk includes
  # all jar files from the program and the tests because having them separate
  # doesn't work with ProGuard's whole-program optimizations. Although the
  # apk-under-test still has all of its code in its classes.dex, none of it is
  # used at runtime because the copy of it within the test apk takes precidence.

  if options.type == 'android_apk' and options.tested_apk_config:
    if tested_apk_config['proguard_enabled']:
      assert options.proguard_enabled, ('proguard must be enabled for '
          'instrumentation apks if it\'s enabled for the tested apk.')
      # Mutating lists, so no need to explicitly re-assign to dict.
      proguard_configs.extend(
          p for p in tested_apk_config['proguard_all_configs'])
      extra_proguard_classpath_jars.extend(
          p for p in tested_apk_config['proguard_classpath_jars'])
      tested_apk_config = GetDepConfig(options.tested_apk_config)
      deps_info['proguard_under_test_mapping'] = (
          tested_apk_config['proguard_mapping_path'])
    elif options.proguard_enabled:
      # Not sure why you'd want to proguard the test apk when the under-test apk
      # is not proguarded, but it's easy enough to support.
      deps_info['proguard_under_test_mapping'] = ''

    # Add all tested classes to the test's classpath to ensure that the test's
    # java code is a superset of the tested apk's java code
    device_classpath_extended = list(device_classpath)
    device_classpath_extended.extend(
        p for p in tested_apk_config['device_classpath']
        if p not in device_classpath)
    # Include in the classpath classes that are added directly to the apk under
    # test (those that are not a part of a java_library).
    javac_classpath.add(tested_apk_config['unprocessed_jar_path'])
    javac_interface_classpath.add(tested_apk_config['interface_jar_path'])
    javac_full_classpath.add(tested_apk_config['unprocessed_jar_path'])
    javac_full_interface_classpath.add(tested_apk_config['interface_jar_path'])
    javac_full_classpath.update(tested_apk_config['javac_full_classpath'])
    javac_full_interface_classpath.update(
        tested_apk_config['javac_full_interface_classpath'])

    # Exclude .jar files from the test apk that exist within the apk under test.
    tested_apk_library_deps = tested_apk_deps.All('java_library')
    tested_apk_dex_files = {c['dex_path'] for c in tested_apk_library_deps}
    all_dex_files = [p for p in all_dex_files if p not in tested_apk_dex_files]
    tested_apk_jar_files = set(tested_apk_config['device_classpath'])
    device_classpath = [
        p for p in device_classpath if p not in tested_apk_jar_files
    ]

  if options.type in ('android_apk', 'dist_aar', 'dist_jar',
                      'android_app_bundle_module', 'android_app_bundle'):
    deps_info['proguard_all_configs'] = sorted(set(proguard_configs))
    deps_info['proguard_classpath_jars'] = sorted(
        set(extra_proguard_classpath_jars))

  if options.type in ('dist_jar', 'java_binary', 'robolectric_binary'):
    # The classpath to use to run this target.
    host_classpath = []
    if options.host_jar_path:
      host_classpath.append(options.host_jar_path)
    host_classpath.extend(c['host_jar_path'] for c in all_library_deps)
    # Collect all the dist_jar host jars.
    dist_jar_host_jars = [
        c['host_jar_path'] for c in all_dist_jar_deps if 'host_jar_path' in c
    ]
    # Collect all the jars that went into the dist_jar host jars.
    dist_jar_host_classpath = set()
    for c in all_dist_jar_deps:
      dist_jar_host_classpath.update(c['host_classpath'])
    # Remove the jars that went into the dist_jar host jars.
    host_classpath = [
        p for p in host_classpath if p not in dist_jar_host_classpath
    ]
    # Add the dist_jar host jars themselves instead.
    host_classpath += dist_jar_host_jars
    deps_info['host_classpath'] = host_classpath

  if is_java_target:

    def _CollectListsFromDeps(deps, key_name):
      combined = set()
      for config in deps:
        combined.update(config.get(key_name, []))
      return combined

    dist_jar_device_classpath = _CollectListsFromDeps(all_dist_jar_deps,
                                                      'device_classpath')
    dist_jar_javac_full_classpath = _CollectListsFromDeps(
        all_dist_jar_deps, 'javac_full_classpath')
    dist_jar_javac_full_interface_classpath = _CollectListsFromDeps(
        all_dist_jar_deps, 'javac_full_interface_classpath')
    dist_jar_child_dex_files = _CollectListsFromDeps(all_dist_jar_deps,
                                                     'all_dex_files')

    def _CollectSinglesFromDeps(deps, key_name):
      return [config[key_name] for config in deps if key_name in config]

    dist_jar_device_jars = _CollectSinglesFromDeps(all_dist_jar_deps,
                                                   'device_jar_path')
    dist_jar_combined_dex_files = _CollectSinglesFromDeps(
        all_dist_jar_deps, 'dex_path')
    dist_jar_interface_jars = _CollectSinglesFromDeps(all_dist_jar_deps,
                                                      'interface_jar_path')
    dist_jar_unprocessed_jars = _CollectSinglesFromDeps(all_dist_jar_deps,
                                                        'unprocessed_jar_path')

    device_classpath = [
        p for p in device_classpath if p not in dist_jar_device_classpath
    ]
    device_classpath += dist_jar_device_jars

    javac_full_classpath.difference_update(dist_jar_javac_full_classpath)
    javac_full_classpath.update(dist_jar_unprocessed_jars)

    javac_full_interface_classpath.difference_update(
        dist_jar_javac_full_interface_classpath)
    javac_full_interface_classpath.update(dist_jar_interface_jars)

    javac_interface_classpath.update(dist_jar_interface_jars)
    javac_classpath.update(dist_jar_unprocessed_jars)

    if is_apk_or_module_target or options.type == 'dist_jar':
      all_dex_files = [
          p for p in all_dex_files if p not in dist_jar_child_dex_files
      ]
      all_dex_files += dist_jar_combined_dex_files

  if options.final_dex_path:
    config['final_dex'] = {'path': options.final_dex_path}
  if is_apk_or_module_target or options.type == 'dist_jar':
    # Dependencies for the final dex file of an apk.
    deps_info['all_dex_files'] = all_dex_files

  if is_java_target:
    config['javac']['classpath'] = sorted(javac_classpath)
    config['javac']['interface_classpath'] = sorted(javac_interface_classpath)
    # Direct() will be of type 'java_annotation_processor', and so not included
    # in All('java_library').
    # Annotation processors run as part of the build, so need host_jar_path.
    config['javac']['processor_classpath'] = [
        c['host_jar_path'] for c in processor_deps.Direct()
        if c.get('host_jar_path')
    ]
    config['javac']['processor_classpath'] += [
        c['host_jar_path'] for c in processor_deps.All('java_library')
    ]
    config['javac']['processor_classes'] = sorted(
        c['main_class'] for c in processor_deps.Direct())
    deps_info['javac_full_classpath'] = list(javac_full_classpath)
    deps_info['javac_full_interface_classpath'] = list(
        javac_full_interface_classpath)
  elif options.type == 'android_app_bundle':
    # bundles require javac_full_classpath to create .aab.jar.info and require
    # javac_full_interface_classpath for lint.
    javac_full_classpath = OrderedSet()
    javac_full_interface_classpath = OrderedSet()
    for d in deps.Direct('android_app_bundle_module'):
      javac_full_classpath.update(d['javac_full_classpath'])
      javac_full_interface_classpath.update(d['javac_full_interface_classpath'])
      javac_full_classpath.add(d['unprocessed_jar_path'])
      javac_full_interface_classpath.add(d['interface_jar_path'])
    deps_info['javac_full_classpath'] = list(javac_full_classpath)
    deps_info['javac_full_interface_classpath'] = list(
        javac_full_interface_classpath)

  if options.type in ('android_apk', 'android_app_bundle',
                      'android_app_bundle_module', 'dist_aar', 'dist_jar'):
    deps_info['device_classpath'] = device_classpath
    if options.trace_events_jar_dir:
      trace_event_rewritten_device_classpath = []
      for jar_path in device_classpath:
        file_path = jar_path.replace('../', '')
        file_path = file_path.replace('obj/', '')
        file_path = file_path.replace('gen/', '')
        file_path = file_path.replace('.jar', '.tracing_rewritten.jar')
        rewritten_jar_path = os.path.join(options.trace_events_jar_dir,
                                          file_path)
        trace_event_rewritten_device_classpath.append(rewritten_jar_path)

      deps_info['trace_event_rewritten_device_classpath'] = (
          trace_event_rewritten_device_classpath)

    if options.tested_apk_config:
      deps_info['device_classpath_extended'] = device_classpath_extended

  if options.type == 'dist_jar':
    if options.direct_deps_only:
      if options.use_interface_jars:
        dist_jars = config['javac']['interface_classpath']
      else:
        dist_jars = sorted(c['device_jar_path']
                           for c in classpath_direct_library_deps)
    else:
      if options.use_interface_jars:
        dist_jars = [c['interface_jar_path'] for c in all_library_deps]
      else:
        dist_jars = deps_info['device_classpath']

    config['dist_jar'] = {
        'jars': dist_jars,
    }

  if is_apk_or_module_target:
    manifest = AndroidManifest(options.android_manifest)
    deps_info['package_name'] = manifest.GetPackageName()
    if not options.tested_apk_config and manifest.GetInstrumentationElements():
      # This must then have instrumentation only for itself.
      manifest.CheckInstrumentationElements(manifest.GetPackageName())

    library_paths = []
    java_libraries_list = None
    if options.shared_libraries_runtime_deps:
      library_paths = _ExtractSharedLibsFromRuntimeDeps(
          options.shared_libraries_runtime_deps)
      java_libraries_list = _CreateJavaLibrariesList(library_paths)
      all_inputs.append(options.shared_libraries_runtime_deps)

    secondary_abi_library_paths = []
    if options.secondary_abi_shared_libraries_runtime_deps:
      secondary_abi_library_paths = _ExtractSharedLibsFromRuntimeDeps(
          options.secondary_abi_shared_libraries_runtime_deps)
      secondary_abi_library_paths.sort()
      paths_without_parent_dirs = [
          p for p in secondary_abi_library_paths if os.path.sep not in p
      ]
      if paths_without_parent_dirs:
        sys.stderr.write('Found secondary native libraries from primary '
                         'toolchain directory. This is a bug!\n')
        sys.stderr.write('\n'.join(paths_without_parent_dirs))
        sys.stderr.write('\n\nIt may be helpful to run: \n')
        sys.stderr.write('    gn path out/Default //chrome/android:'
                         'monochrome_secondary_abi_lib //base:base\n')
        sys.exit(1)

      all_inputs.append(options.secondary_abi_shared_libraries_runtime_deps)

    native_library_placeholder_paths = action_helpers.parse_gn_list(
        options.native_lib_placeholders)
    native_library_placeholder_paths.sort()

    secondary_native_library_placeholder_paths = action_helpers.parse_gn_list(
        options.secondary_native_lib_placeholders)
    secondary_native_library_placeholder_paths.sort()

    loadable_modules = action_helpers.parse_gn_list(options.loadable_modules)
    loadable_modules.sort()
    secondary_abi_loadable_modules = action_helpers.parse_gn_list(
        options.secondary_abi_loadable_modules)
    secondary_abi_loadable_modules.sort()

    config['native'] = {
        'libraries':
        library_paths,
        'native_library_placeholders':
        native_library_placeholder_paths,
        'secondary_abi_libraries':
        secondary_abi_library_paths,
        'secondary_native_library_placeholders':
        secondary_native_library_placeholder_paths,
        'java_libraries_list':
        java_libraries_list,
        'library_always_compress':
        options.library_always_compress,
        'loadable_modules':
        loadable_modules,
        'secondary_abi_loadable_modules':
        secondary_abi_loadable_modules,
    }

    # Collect java resources
    java_resources_jars = [d['java_resources_jar'] for d in all_library_deps
                          if 'java_resources_jar' in d]
    if options.tested_apk_config:
      tested_apk_resource_jars = [d['java_resources_jar']
                                  for d in tested_apk_library_deps
                                  if 'java_resources_jar' in d]
      java_resources_jars = [jar for jar in java_resources_jars
                             if jar not in tested_apk_resource_jars]
    java_resources_jars.sort()
    config['java_resources_jars'] = java_resources_jars

  if is_apk_or_module_target or options.type == 'robolectric_binary':
    # android_resources deps which had recursive_resource_deps set should not
    # have the manifests from the recursively collected deps added to this
    # module. This keeps the manifest declarations in the child DFMs, since they
    # will have the Java implementations.
    def ExcludeRecursiveResourcesDeps(config):
      return not config.get('includes_recursive_resources', False)

    extra_manifest_deps = [
        GetDepConfig(p) for p in GetAllDepsConfigsInOrder(
            deps_configs_paths, filter_func=ExcludeRecursiveResourcesDeps)
    ]
    # Manifests are listed from highest priority to lowest priority.
    # Ensure directly manfifests come first, and then sort the rest by name.
    # https://developer.android.com/build/manage-manifests#merge_priorities
    deps_info['extra_android_manifests'] = list(mergeable_android_manifests)
    manifests_from_deps = []
    for c in extra_manifest_deps:
      manifests_from_deps += c.get('mergeable_android_manifests', [])
    manifests_from_deps.sort(key=lambda p: (os.path.basename(p), p))
    deps_info['extra_android_manifests'] += manifests_from_deps

    assets, uncompressed_assets, locale_paks = _MergeAssets(
        deps.All('android_assets'))
    deps_info['assets'] = assets
    deps_info['uncompressed_assets'] = uncompressed_assets
    deps_info['locales_java_list'] = _CreateJavaLocaleListFromAssets(
        uncompressed_assets, locale_paks)
    if options.suffix_apk_assets_used_by:
      if options.suffix_apk_assets_used_by == options.build_config:
        target_config = deps_info
      else:
        target_config = GetDepConfig(options.suffix_apk_assets_used_by)
      all_assets = (target_config['assets'] +
                    target_config['uncompressed_assets'])
      suffix = '+' + target_config['package_name'] + '+'
      suffix_names = {
          x.split(':', 1)[1].replace(suffix, '')
          for x in all_assets
      }
      deps_info['assets'] = _SuffixAssets(suffix_names, suffix, assets)
      deps_info['uncompressed_assets'] = _SuffixAssets(suffix_names, suffix,
                                                       uncompressed_assets)
      config['apk_assets_suffixed_list'] = ','.join(
          f'"assets/{x}"' for x in sorted(suffix_names))
      config['apk_assets_suffix'] = suffix

  if options.java_resources_jar_path:
    deps_info['java_resources_jar'] = options.java_resources_jar_path

  # DYNAMIC FEATURE MODULES:
  # There are two approaches to dealing with modules dependencies:
  # 1) Perform steps in android_apk_or_module(), with only the knowledge of
  #    ancesstor splits. Our implementation currently allows only for 2 levels:
  #        base -> parent -> leaf
  #    Bundletool normally fails if two leaf nodes merge the same manifest or
  #    resources. The fix is to add the common dep to the chrome or base module
  #    so that our deduplication logic will work.
  #    RemoveObjDups() implements this approach.
  # 2) Perform steps in android_app_bundle(), with knowledge of full set of
  #    modules. This is required for dex because it can handle the case of two
  #    leaf nodes having the same dep, and promoting that dep to their common
  #    parent.
  #    _DedupFeatureModuleSharedCode() implements this approach.
  if base_module_build_config:
    ancestors = [base_module_build_config]
    if parent_module_build_config is not base_module_build_config:
      ancestors += [parent_module_build_config]
    for ancestor in ancestors:
      RemoveObjDups(config, ancestor, 'deps_info', 'dependency_zips')
      RemoveObjDups(config, ancestor, 'deps_info', 'dependency_zip_overlays')
      RemoveObjDups(config, ancestor, 'deps_info', 'extra_android_manifests')
      RemoveObjDups(config, ancestor, 'deps_info', 'extra_package_names')

  if is_java_target:
    jar_to_target = {}
    _AddJarMapping(jar_to_target, [deps_info])
    _AddJarMapping(jar_to_target, all_deps)
    if base_module_build_config:
      _AddJarMapping(jar_to_target, [base_module_build_config['deps_info']])
      if parent_module_build_config is not base_module_build_config:
        _AddJarMapping(jar_to_target, [parent_module_build_config['deps_info']])
    if options.tested_apk_config:
      _AddJarMapping(jar_to_target, [tested_apk_config])
      for jar, target in zip(tested_apk_config['javac_full_classpath'],
                             tested_apk_config['javac_full_classpath_targets']):
        jar_to_target[jar] = target

    # Used by check_for_missing_direct_deps.py to give better error message
    # when missing deps are found. Both javac_full_classpath_targets and
    # javac_full_classpath must be in identical orders, as they get passed as
    # separate arrays and then paired up based on index.
    config['deps_info']['javac_full_classpath_targets'] = [
        jar_to_target[x] for x in deps_info['javac_full_classpath']
    ]

  build_utils.WriteJson(config, options.build_config, only_if_changed=True)

  if options.depfile:
    action_helpers.write_depfile(options.depfile, options.build_config,
                                 sorted(set(all_inputs)))

  if options.store_deps_for_debugging_to:
    GetDepConfig(options.build_config)  # Add it to cache.
    _CopyBuildConfigsForDebugging(options.store_deps_for_debugging_to)

  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
