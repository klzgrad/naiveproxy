#!/usr/bin/env python
#
# Copyright 2014 The Chromium Authors. All rights reserved.
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
"""

import itertools
import optparse
import os
import sys
import xml.dom.minidom

from util import build_utils
from util import md5_check


# Types that should never be used as a dependency of another build config.
_ROOT_TYPES = ('android_apk', 'deps_dex', 'java_binary', 'junit_binary',
               'resource_rewriter')
# Types that should not allow code deps to pass through.
_RESOURCE_TYPES = ('android_assets', 'android_resources')


class AndroidManifest(object):
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


dep_config_cache = {}
def GetDepConfig(path):
  if not path in dep_config_cache:
    dep_config_cache[path] = build_utils.ReadJson(path)['deps_info']
  return dep_config_cache[path]


def DepsOfType(wanted_type, configs):
  return [c for c in configs if c['type'] == wanted_type]


def GetAllDepsConfigsInOrder(deps_config_paths):
  def GetDeps(path):
    return set(GetDepConfig(path)['deps_configs'])
  return build_utils.GetSortedTransitiveDependencies(deps_config_paths, GetDeps)


class Deps(object):
  def __init__(self, direct_deps_config_paths):
    self.all_deps_config_paths = GetAllDepsConfigsInOrder(
        direct_deps_config_paths)
    self.direct_deps_configs = [
        GetDepConfig(p) for p in direct_deps_config_paths]
    self.all_deps_configs = [
        GetDepConfig(p) for p in self.all_deps_config_paths]
    self.direct_deps_config_paths = direct_deps_config_paths

  def All(self, wanted_type=None):
    if type is None:
      return self.all_deps_configs
    return DepsOfType(wanted_type, self.all_deps_configs)

  def Direct(self, wanted_type=None):
    if wanted_type is None:
      return self.direct_deps_configs
    return DepsOfType(wanted_type, self.direct_deps_configs)

  def AllConfigPaths(self):
    return self.all_deps_config_paths

  def RemoveNonDirectDep(self, path):
    if path in self.direct_deps_config_paths:
      raise Exception('Cannot remove direct dep.')
    self.all_deps_config_paths.remove(path)
    self.all_deps_configs.remove(GetDepConfig(path))

  def GradlePrebuiltJarPaths(self):
    ret = []

    def helper(cur):
      for config in cur.Direct('java_library'):
        if config['is_prebuilt'] or config['gradle_treat_as_prebuilt']:
          if config['jar_path'] not in ret:
            ret.append(config['jar_path'])

    helper(self)
    return ret

  def GradleLibraryProjectDeps(self):
    ret = []

    def helper(cur):
      for config in cur.Direct('java_library'):
        if config['is_prebuilt']:
          pass
        elif config['gradle_treat_as_prebuilt']:
          helper(Deps(config['deps_configs']))
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
    for src, dest in itertools.izip_longest(entry['sources'], outputs):
      if not dest:
        dest = os.path.basename(src)
      # Merge so that each path shows up in only one of the lists, and that
      # deps of the same target override previous ones.
      other_map.pop(dest, 0)
      dest_map[dest] = src
      if treat_as_locale_paks:
        locale_paks.add(dest)

  def create_list(asset_map):
    ret = ['%s:%s' % (src, dest) for dest, src in asset_map.iteritems()]
    # Sort to ensure deterministic ordering.
    ret.sort()
    return ret

  return create_list(compressed), create_list(uncompressed), locale_paks


def _ResolveGroups(configs):
  """Returns a list of configs with all groups inlined."""
  ret = list(configs)
  while True:
    groups = DepsOfType('group', ret)
    if not groups:
      return ret
    for config in groups:
      index = ret.index(config)
      expanded_configs = [GetDepConfig(p) for p in config['deps_configs']]
      ret[index:index + 1] = expanded_configs


def _FilterDepsPaths(dep_paths, target_type):
  """Resolves all groups and trims dependency branches that we never want.

  E.g. When a resource or asset depends on an apk target, the intent is to
  include the .apk as a resource/asset, not to have the apk's classpath added.
  """
  configs = [GetDepConfig(p) for p in dep_paths]
  configs = _ResolveGroups(configs)
  # Don't allow root targets to be considered as a dep.
  configs = [c for c in configs if c['type'] not in _ROOT_TYPES]

  # Don't allow java libraries to cross through assets/resources.
  if target_type in _RESOURCE_TYPES:
    configs = [c for c in configs if c['type'] in _RESOURCE_TYPES]
  return [c['path'] for c in configs]


def _AsInterfaceJar(jar_path):
  return jar_path[:-3] + 'interface.jar'


def _ExtractSharedLibsFromRuntimeDeps(runtime_deps_files):
  ret = []
  for path in runtime_deps_files:
    with open(path) as f:
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
  return ('{%s}' % ','.join(['"%s"' % s[3:-3] for s in library_paths]))


def _CreateLocalePaksAssetJavaList(assets, locale_paks):
  """Returns a java literal array from a list of assets in the form src:dst."""
  asset_paths = [a.split(':')[1] for a in assets]
  return '{%s}' % ','.join(
      sorted('"%s"' % a[:-4] for a in asset_paths if a in locale_paks))


def main(argv):
  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)
  parser.add_option('--build-config', help='Path to build_config output.')
  parser.add_option(
      '--type',
      help='Type of this target (e.g. android_library).')
  parser.add_option(
      '--deps-configs',
      help='List of paths for dependency\'s build_config files. ')
  parser.add_option(
      '--classpath-deps-configs',
      help='List of paths for classpath dependency\'s build_config files. ')

  # android_resources options
  parser.add_option('--srcjar', help='Path to target\'s resources srcjar.')
  parser.add_option('--resources-zip', help='Path to target\'s resources zip.')
  parser.add_option('--r-text', help='Path to target\'s R.txt file.')
  parser.add_option('--package-name',
      help='Java package name for these resources.')
  parser.add_option('--android-manifest', help='Path to android manifest.')
  parser.add_option('--resource-dirs', action='append', default=[],
                    help='GYP-list of resource dirs')

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

  # java library options
  parser.add_option('--jar-path', help='Path to target\'s jar output.')
  parser.add_option('--java-sources-file', help='Path to .sources file')
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
  parser.add_option('--gradle-treat-as-prebuilt', action='store_true',
      help='Whether this library should be treated as a prebuilt library by '
           'generate_gradle.py.')
  parser.add_option('--main-class', help='Java class for java_binary targets.')
  parser.add_option('--java-resources-jar-path',
                    help='Path to JAR that contains java resources. Everything '
                    'from this JAR except meta-inf/ content and .class files '
                    'will be added to the final APK.')
  parser.add_option('--bootclasspath', help='Path to custom android.jar/rt.jar')

  # android library options
  parser.add_option('--dex-path', help='Path to target\'s dex output.')

  # native library options
  parser.add_option('--shared-libraries-runtime-deps',
                    help='Path to file containing runtime deps for shared '
                         'libraries.')
  parser.add_option('--secondary-abi-shared-libraries-runtime-deps',
                    help='Path to file containing runtime deps for secondary '
                         'abi shared libraries.')
  parser.add_option('--non-native-packed-relocations',
                    action='store_true', default=False,
                    help='Whether relocation packing was applied using the '
                         'Android relocation_packer tool.')

  # apk options
  parser.add_option('--apk-path', help='Path to the target\'s apk output.')
  parser.add_option('--incremental-apk-path',
                    help="Path to the target's incremental apk output.")
  parser.add_option('--incremental-install-json-path',
                    help="Path to the target's generated incremental install "
                    "json.")

  parser.add_option('--tested-apk-config',
      help='Path to the build config of the tested apk (for an instrumentation '
      'test apk).')
  parser.add_option('--proguard-enabled', action='store_true',
      help='Whether proguard is enabled for this apk.')
  parser.add_option('--proguard-configs',
      help='GYP-list of proguard flag files to use in final apk.')
  parser.add_option('--proguard-info',
      help='Path to the proguard .info output for this apk.')
  parser.add_option('--fail',
      help='GYP-list of error message lines to fail with.')

  options, args = parser.parse_args(argv)

  if args:
    parser.error('No positional arguments should be given.')
  if options.fail:
    parser.error('\n'.join(build_utils.ParseGnList(options.fail)))

  required_options_map = {
      'java_binary': ['build_config', 'jar_path'],
      'junit_binary': ['build_config', 'jar_path'],
      'java_library': ['build_config', 'jar_path'],
      'java_prebuilt': ['build_config', 'jar_path'],
      'android_assets': ['build_config'],
      'android_resources': ['build_config', 'resources_zip'],
      'android_apk': ['build_config', 'jar_path', 'dex_path'],
      'deps_dex': ['build_config', 'dex_path'],
      'dist_jar': ['build_config'],
      'resource_rewriter': ['build_config'],
      'group': ['build_config'],
  }
  required_options = required_options_map.get(options.type)
  if not required_options:
    raise Exception('Unknown type: <%s>' % options.type)

  build_utils.CheckOptions(options, parser, required_options)

  # Java prebuilts are the same as libraries except for in gradle files.
  is_java_prebuilt = options.type == 'java_prebuilt'
  if is_java_prebuilt:
    options.type = 'java_library'

  if options.type == 'java_library':
    if options.supports_android and not options.dex_path:
      raise Exception('java_library that supports Android requires a dex path.')

    if options.requires_android and not options.supports_android:
      raise Exception(
          '--supports-android is required when using --requires-android')

  direct_deps_config_paths = build_utils.ParseGnList(options.deps_configs)
  direct_deps_config_paths = _FilterDepsPaths(
      direct_deps_config_paths, options.type)

  deps = Deps(direct_deps_config_paths)
  all_inputs = deps.AllConfigPaths()

  direct_library_deps = deps.Direct('java_library')
  all_library_deps = deps.All('java_library')

  direct_resources_deps = deps.Direct('android_resources')
  all_resources_deps = deps.All('android_resources')
  # Resources should be ordered with the highest-level dependency first so that
  # overrides are done correctly.
  all_resources_deps.reverse()

  # Initialize some common config.
  # Any value that needs to be queryable by dependents must go within deps_info.
  config = {
    'deps_info': {
      'name': os.path.basename(options.build_config),
      'path': options.build_config,
      'type': options.type,
      'deps_configs': direct_deps_config_paths
    },
    # Info needed only by generate_gradle.py.
    'gradle': {}
  }
  deps_info = config['deps_info']
  gradle = config['gradle']

  if options.type == 'android_apk' and options.tested_apk_config:
    tested_apk_deps = Deps([options.tested_apk_config])
    tested_apk_name = tested_apk_deps.Direct()[0]['name']
    tested_apk_resources_deps = tested_apk_deps.All('android_resources')
    gradle['apk_under_test'] = tested_apk_name
    all_resources_deps = [
        d for d in all_resources_deps if not d in tested_apk_resources_deps]

  # Required for generating gradle files.
  if options.type == 'java_library':
    deps_info['is_prebuilt'] = is_java_prebuilt
    deps_info['gradle_treat_as_prebuilt'] = options.gradle_treat_as_prebuilt

  if options.android_manifest:
    deps_info['android_manifest'] = options.android_manifest

  if options.type in (
      'java_binary', 'junit_binary', 'java_library', 'android_apk'):
    if options.java_sources_file:
      deps_info['java_sources_file'] = options.java_sources_file
    if options.bundled_srcjars:
      gradle['bundled_srcjars'] = (
          build_utils.ParseGnList(options.bundled_srcjars))
    else:
      gradle['bundled_srcjars'] = []

    gradle['dependent_android_projects'] = []
    gradle['dependent_java_projects'] = []
    gradle['dependent_prebuilt_jars'] = deps.GradlePrebuiltJarPaths()

    if options.bootclasspath:
      gradle['bootclasspath'] = options.bootclasspath
    if options.main_class:
      gradle['main_class'] = options.main_class

    for c in deps.GradleLibraryProjectDeps():
      if c['requires_android']:
        gradle['dependent_android_projects'].append(c['path'])
      else:
        gradle['dependent_java_projects'].append(c['path'])


  if options.type == 'android_apk':
    config['jni'] = {}
    all_java_sources = [c['java_sources_file'] for c in all_library_deps
                        if 'java_sources_file' in c]
    if options.java_sources_file:
      all_java_sources.append(options.java_sources_file)
    config['jni']['all_source'] = all_java_sources

  if options.type in (
      'java_binary', 'junit_binary', 'java_library', 'dist_jar'):
    deps_info['requires_android'] = options.requires_android
    deps_info['supports_android'] = options.supports_android

    if not options.bypass_platform_checks:
      deps_require_android = (all_resources_deps +
          [d['name'] for d in all_library_deps if d['requires_android']])
      deps_not_support_android = (
          [d['name'] for d in all_library_deps if not d['supports_android']])

      if deps_require_android and not options.requires_android:
        raise Exception('Some deps require building for the Android platform: '
            + str(deps_require_android))

      if deps_not_support_android and options.supports_android:
        raise Exception('Not all deps support the Android platform: '
            + str(deps_not_support_android))

  if options.type in (
      'java_binary', 'junit_binary', 'java_library', 'android_apk'):
    deps_info['jar_path'] = options.jar_path
    if options.type == 'android_apk' or options.supports_android:
      deps_info['dex_path'] = options.dex_path
    if options.type == 'android_apk':
      deps_info['apk_path'] = options.apk_path
      deps_info['incremental_apk_path'] = options.incremental_apk_path
      deps_info['incremental_install_json_path'] = (
          options.incremental_install_json_path)
      deps_info['non_native_packed_relocations'] = str(
          options.non_native_packed_relocations)

  requires_javac_classpath = options.type in (
      'java_binary', 'junit_binary', 'java_library', 'android_apk', 'dist_jar')
  requires_full_classpath = (
      options.type == 'java_prebuilt' or requires_javac_classpath)

  if requires_javac_classpath:
    # Classpath values filled in below (after applying tested_apk_config).
    config['javac'] = {}

  if options.type == 'java_library':
    # android_resources targets use this srcjars field to expose R.java files.
    # Since there is no java_library associated with an android_resources(),
    # Each java_library recompiles the R.java files.
    # junit_binary and android_apk create their own R.java srcjars, so should
    # not pull them in from deps here.
    config['javac']['srcjars'] = [
        c['srcjar'] for c in all_resources_deps if 'srcjar' in c]

    # Used to strip out R.class for android_prebuilt()s.
    config['javac']['resource_packages'] = [
        c['package_name'] for c in all_resources_deps if 'package_name' in c]
  elif options.type in ('android_apk', 'java_binary', 'junit_binary'):
    # Apks will get their resources srcjar explicitly passed to the java step
    config['javac']['srcjars'] = []
    # Gradle may need to generate resources for some apks.
    gradle['srcjars'] = [
        c['srcjar'] for c in direct_resources_deps if 'srcjar' in c]

  if options.type == 'android_assets':
    all_asset_sources = []
    if options.asset_renaming_sources:
      all_asset_sources.extend(
          build_utils.ParseGnList(options.asset_renaming_sources))
    if options.asset_sources:
      all_asset_sources.extend(build_utils.ParseGnList(options.asset_sources))

    deps_info['assets'] = {
        'sources': all_asset_sources
    }
    if options.asset_renaming_destinations:
      deps_info['assets']['outputs'] = (
          build_utils.ParseGnList(options.asset_renaming_destinations))
    if options.disable_asset_compression:
      deps_info['assets']['disable_compression'] = True
    if options.treat_as_locale_paks:
      deps_info['assets']['treat_as_locale_paks'] = True

  if options.type == 'android_resources':
    deps_info['resources_zip'] = options.resources_zip
    if options.srcjar:
      deps_info['srcjar'] = options.srcjar
    if options.android_manifest:
      manifest = AndroidManifest(options.android_manifest)
      deps_info['package_name'] = manifest.GetPackageName()
    if options.package_name:
      deps_info['package_name'] = options.package_name
    if options.r_text:
      deps_info['r_text'] = options.r_text

    deps_info['resources_dirs'] = []
    if options.resource_dirs:
      for gyp_list in options.resource_dirs:
        deps_info['resources_dirs'].extend(build_utils.ParseGnList(gyp_list))

  if options.supports_android and options.type in ('android_apk',
                                                   'java_library'):
    # Lint all resources that are not already linted by a dependent library.
    owned_resource_dirs = set()
    owned_resource_zips = set()
    for c in all_resources_deps:
      # Always use resources_dirs in favour of resources_zips so that lint error
      # messages have paths that are closer to reality (and to avoid needing to
      # extract during lint).
      if c['resources_dirs']:
        owned_resource_dirs.update(c['resources_dirs'])
      else:
        owned_resource_zips.add(c['resources_zip'])

    for c in all_library_deps:
      if c['supports_android']:
        owned_resource_dirs.difference_update(c['owned_resources_dirs'])
        owned_resource_zips.difference_update(c['owned_resources_zips'])
    deps_info['owned_resources_dirs'] = list(owned_resource_dirs)
    deps_info['owned_resources_zips'] = list(owned_resource_zips)

  if options.type in (
      'android_resources', 'android_apk', 'junit_binary', 'resource_rewriter'):
    config['resources'] = {}
    config['resources']['dependency_zips'] = [
        c['resources_zip'] for c in all_resources_deps]
    extra_package_names = []
    extra_r_text_files = []
    if options.type != 'android_resources':
      extra_package_names = [
          c['package_name'] for c in all_resources_deps if 'package_name' in c]
      extra_r_text_files = [
          c['r_text'] for c in all_resources_deps if 'r_text' in c]

    config['resources']['extra_package_names'] = extra_package_names
    config['resources']['extra_r_text_files'] = extra_r_text_files

  if options.type in ['android_apk', 'deps_dex']:
    deps_dex_files = [c['dex_path'] for c in all_library_deps]

  if requires_javac_classpath:
    extra_jars = []
    if options.extra_classpath_jars:
      extra_jars += build_utils.ParseGnList(options.extra_classpath_jars)

    if options.classpath_deps_configs:
      config_paths = build_utils.ParseGnList(options.classpath_deps_configs)
      classpath_deps = Deps(_FilterDepsPaths(config_paths, options.type))
      extra_jars += [
          c['jar_path'] for c in classpath_deps.Direct('java_library')]

    javac_classpath = [c['jar_path'] for c in direct_library_deps]
    if requires_full_classpath:
      java_full_classpath = [c['jar_path'] for c in all_library_deps]

    if extra_jars:
      deps_info['extra_classpath_jars'] = extra_jars
      javac_classpath += [p for p in extra_jars if p not in javac_classpath]
      java_full_classpath += [
          p for p in extra_jars if p not in java_full_classpath]

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
    tested_apk_config = GetDepConfig(options.tested_apk_config)

    expected_tested_package = tested_apk_config['package_name']
    AndroidManifest(options.android_manifest).CheckInstrumentationElements(
        expected_tested_package)
    if options.proguard_enabled:
      # Add all tested classes to the test's classpath to ensure that the test's
      # java code is a superset of the tested apk's java code
      java_full_classpath += [
          jar for jar in tested_apk_config['java']['full_classpath']
          if jar not in java_full_classpath]

    if tested_apk_config['proguard_enabled']:
      assert options.proguard_enabled, ('proguard must be enabled for '
          'instrumentation apks if it\'s enabled for the tested apk.')

    # Include in the classpath classes that are added directly to the apk under
    # test (those that are not a part of a java_library).
    javac_classpath.append(tested_apk_config['jar_path'])
    java_full_classpath.append(tested_apk_config['jar_path'])

    # Exclude dex files from the test apk that exist within the apk under test.
    # TODO(agrieve): When proguard is enabled, this filtering logic happens
    #     within proguard_util.py. Move the logic for the proguard case into
    #     here as well.
    tested_apk_library_deps = tested_apk_deps.All('java_library')
    tested_apk_deps_dex_files = [c['dex_path'] for c in tested_apk_library_deps]
    deps_dex_files = [
        p for p in deps_dex_files if not p in tested_apk_deps_dex_files]

  if options.proguard_configs:
    assert options.type == 'java_library'
    deps_info['proguard_configs'] = (
        build_utils.ParseGnList(options.proguard_configs))

  if options.type in ('android_apk', 'dist_jar'):
    deps_info['proguard_enabled'] = options.proguard_enabled
    deps_info['proguard_info'] = options.proguard_info
    config['proguard'] = {}
    proguard_config = config['proguard']
    proguard_config['input_paths'] = list(java_full_classpath)
    if options.jar_path:
      proguard_config['input_paths'].insert(0, options.jar_path)
    extra_jars = set()
    lib_configs = set()
    for c in all_library_deps:
      extra_jars.update(c.get('extra_classpath_jars', ()))
      lib_configs.update(c.get('proguard_configs', ()))
    proguard_config['lib_paths'] = list(extra_jars)
    proguard_config['lib_configs'] = list(lib_configs)

  # Dependencies for the final dex file of an apk or a 'deps_dex'.
  if options.type in ['android_apk', 'deps_dex']:
    config['final_dex'] = {}
    dex_config = config['final_dex']
    dex_config['dependency_dex_files'] = deps_dex_files

  if requires_javac_classpath:
    config['javac']['classpath'] = javac_classpath
    javac_interface_classpath = [
        _AsInterfaceJar(p) for p in javac_classpath
        if p not in deps_info.get('extra_classpath_jars', [])]
    javac_interface_classpath += deps_info.get('extra_classpath_jars', [])
    config['javac']['interface_classpath'] = javac_interface_classpath

  if requires_full_classpath:
    deps_info['java'] = {
      'full_classpath': java_full_classpath,
    }

  if options.type in ('android_apk', 'dist_jar'):
    dependency_jars = [c['jar_path'] for c in all_library_deps]
    all_interface_jars = [_AsInterfaceJar(p) for p in dependency_jars]
    if options.type == 'android_apk':
      all_interface_jars.append(_AsInterfaceJar(options.jar_path))

    config['dist_jar'] = {
      'dependency_jars': dependency_jars,
      'all_interface_jars': all_interface_jars,
    }

  if options.type == 'android_apk':
    manifest = AndroidManifest(options.android_manifest)
    deps_info['package_name'] = manifest.GetPackageName()
    if not options.tested_apk_config and manifest.GetInstrumentationElements():
      # This must then have instrumentation only for itself.
      manifest.CheckInstrumentationElements(manifest.GetPackageName())

    library_paths = []
    java_libraries_list = None
    runtime_deps_files = build_utils.ParseGnList(
        options.shared_libraries_runtime_deps or '[]')
    if runtime_deps_files:
      library_paths = _ExtractSharedLibsFromRuntimeDeps(runtime_deps_files)
      java_libraries_list = _CreateJavaLibrariesList(library_paths)

    secondary_abi_library_paths = []
    secondary_abi_java_libraries_list = None
    secondary_abi_runtime_deps_files = build_utils.ParseGnList(
        options.secondary_abi_shared_libraries_runtime_deps or '[]')
    if secondary_abi_runtime_deps_files:
      secondary_abi_library_paths = _ExtractSharedLibsFromRuntimeDeps(
          secondary_abi_runtime_deps_files)
      secondary_abi_java_libraries_list = _CreateJavaLibrariesList(
          secondary_abi_library_paths)

    all_inputs.extend(runtime_deps_files)
    config['native'] = {
      'libraries': library_paths,
      'secondary_abi_libraries': secondary_abi_library_paths,
      'java_libraries_list': java_libraries_list,
      'secondary_abi_java_libraries_list': secondary_abi_java_libraries_list,
    }
    config['assets'], config['uncompressed_assets'], locale_paks = (
        _MergeAssets(deps.All('android_assets')))
    config['compressed_locales_java_list'] = _CreateLocalePaksAssetJavaList(
        config['assets'], locale_paks)
    config['uncompressed_locales_java_list'] = _CreateLocalePaksAssetJavaList(
        config['uncompressed_assets'], locale_paks)

    config['extra_android_manifests'] = filter(None, (
        d.get('android_manifest') for d in all_resources_deps))

    # Collect java resources
    java_resources_jars = [d['java_resources_jar'] for d in all_library_deps
                          if 'java_resources_jar' in d]
    if options.tested_apk_config:
      tested_apk_resource_jars = [d['java_resources_jar']
                                  for d in tested_apk_library_deps
                                  if 'java_resources_jar' in d]
      java_resources_jars = [jar for jar in java_resources_jars
                             if jar not in tested_apk_resource_jars]
    config['java_resources_jars'] = java_resources_jars

  if options.type == 'java_library' and options.java_resources_jar_path:
    deps_info['java_resources_jar'] = options.java_resources_jar_path

  build_utils.WriteJson(config, options.build_config, only_if_changed=True)

  if options.depfile:
    build_utils.WriteDepfile(options.depfile, options.build_config, all_inputs)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
