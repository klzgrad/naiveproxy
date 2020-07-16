#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import shutil
import sys
import tempfile
import zipfile

from util import build_utils
from util import diff_utils

_API_LEVEL_VERSION_CODE = [
    (21, 'L'),
    (22, 'LolliopoMR1'),
    (23, 'M'),
    (24, 'N'),
    (25, 'NMR1'),
    (26, 'O'),
    (27, 'OMR1'),
    (28, 'P'),
    (29, 'Q'),
]
_CHECKDISCARD_RE = re.compile(r'-checkdiscard[\s\S]*?}')
_DIRECTIVE_RE = re.compile(r'^-', re.MULTILINE)


class _ProguardOutputFilter(object):
  """ProGuard outputs boring stuff to stdout (ProGuard version, jar path, etc)
  as well as interesting stuff (notes, warnings, etc). If stdout is entirely
  boring, this class suppresses the output.
  """

  IGNORE_RE = re.compile(
      r'Pro.*version|Note:|Reading|Preparing|Printing|ProgramClass:|Searching|'
      r'jar \[|\d+ class path entries checked')

  def __init__(self):
    self._last_line_ignored = False
    self._ignore_next_line = False

  def __call__(self, output):
    ret = []
    for line in output.splitlines(True):
      if self._ignore_next_line:
        self._ignore_next_line = False
        continue

      if '***BINARY RUN STATS***' in line:
        self._last_line_ignored = True
        self._ignore_next_line = True
      elif not line.startswith(' '):
        self._last_line_ignored = bool(self.IGNORE_RE.match(line))
      elif 'You should check if you need to specify' in line:
        self._last_line_ignored = True

      if not self._last_line_ignored:
        ret.append(line)
    return ''.join(ret)


class ProguardProcessError(build_utils.CalledProcessError):
  """Wraps CalledProcessError and enables adding extra output to failures."""

  def __init__(self, cpe, output):
    super(ProguardProcessError, self).__init__(cpe.cwd, cpe.args,
                                               cpe.output + output)


def _ValidateAndFilterCheckDiscards(configs):
  """Check for invalid -checkdiscard rules and filter out -checkdiscards.

  -checkdiscard assertions often don't work for test APKs and are not actually
  helpful. Additionally, test APKs may pull in dependency proguard configs which
  makes filtering out these rules difficult in GN. Instead, we enforce that
  configs that use -checkdiscard do not contain any other rules so that we can
  filter out the undesired -checkdiscard rule files here.

  Args:
    configs: List of paths to proguard configuration files.

  Returns:
    A list of configs with -checkdiscard-containing-configs removed.
  """
  valid_configs = []
  for config_path in configs:
    with open(config_path) as f:
      contents = f.read()
      if _CHECKDISCARD_RE.search(contents):
        contents = _CHECKDISCARD_RE.sub('', contents)
        if _DIRECTIVE_RE.search(contents):
          raise Exception('Proguard configs containing -checkdiscards cannot '
                          'contain other directives so that they can be '
                          'disabled in test APKs ({}).'.format(config_path))
      else:
        valid_configs.append(config_path)

  return valid_configs


def _ParseOptions():
  args = build_utils.ExpandFileArgs(sys.argv[1:])
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  group = parser.add_mutually_exclusive_group(required=True)
  group.add_argument('--proguard-path', help='Path to the proguard.jar to use.')
  group.add_argument('--r8-path', help='Path to the R8.jar to use.')
  parser.add_argument(
      '--desugar-jdk-libs-json', help='Path to desugar_jdk_libs.json.')
  parser.add_argument('--input-paths',
                      action='append',
                      required=True,
                      help='GN-list of .jar files to optimize.')
  parser.add_argument('--output-path', help='Path to the generated .jar file.')
  parser.add_argument(
      '--proguard-configs',
      action='append',
      required=True,
      help='GN-list of configuration files.')
  parser.add_argument(
      '--apply-mapping', help='Path to ProGuard mapping to apply.')
  parser.add_argument(
      '--mapping-output',
      required=True,
      help='Path for ProGuard to output mapping file to.')
  parser.add_argument(
      '--extra-mapping-output-paths',
      help='GN-list of additional paths to copy output mapping file to.')
  parser.add_argument(
      '--output-config',
      help='Path to write the merged ProGuard config file to.')
  parser.add_argument(
      '--expected-configs-file',
      help='Path to a file containing the expected merged ProGuard configs')
  parser.add_argument(
      '--proguard-expectations-failure-file',
      help='Path to file written to if the expected merged ProGuard configs '
      'differ from the generated merged ProGuard configs.')
  parser.add_argument(
      '--fail-on-expectations',
      action="store_true",
      help='When passed fails the build on proguard config expectation '
      'mismatches.')
  parser.add_argument(
      '--only-verify-expectations',
      action='store_true',
      help='If passed only verifies that the proguard configs match '
      'expectations but does not do any optimization with proguard/R8.')
  parser.add_argument(
      '--classpath',
      action='append',
      help='GN-list of .jar files to include as libraries.')
  parser.add_argument(
      '--main-dex-rules-path',
      action='append',
      help='Path to main dex rules for multidex'
      '- only works with R8.')
  parser.add_argument(
      '--min-api', help='Minimum Android API level compatibility.')
  parser.add_argument(
      '--verbose', '-v', action='store_true', help='Print all ProGuard output')
  parser.add_argument(
      '--repackage-classes', help='Package all optimized classes are put in.')
  parser.add_argument(
      '--disable-outlining',
      action='store_true',
      help='Disable the outlining optimization provided by R8.')
  parser.add_argument(
      '--disable-checkdiscard',
      action='store_true',
      help='Disable -checkdiscard directives')
  parser.add_argument('--sourcefile', help='Value for source file attribute')
  parser.add_argument(
      '--force-enable-assertions',
      action='store_true',
      help='Forcefully enable javac generated assertion code.')
  parser.add_argument(
      '--feature-jars',
      action='append',
      help='GN list of path to jars which comprise the corresponding feature.')
  parser.add_argument(
      '--dex-dest',
      action='append',
      dest='dex_dests',
      help='Destination for dex file of the corresponding feature.')
  parser.add_argument(
      '--feature-name',
      action='append',
      dest='feature_names',
      help='The name of the feature module.')
  parser.add_argument(
      '--stamp',
      help='File to touch upon success. Mutually exclusive with --output-path')

  options = parser.parse_args(args)

  if options.feature_names:
    if options.output_path:
      parser.error('Feature splits cannot specify an output in GN.')
    if not options.stamp:
      parser.error('Feature splits require a stamp file as output.')
  elif not options.output_path:
    parser.error('Output path required when feature splits aren\'t used')

  if options.main_dex_rules_path and not options.r8_path:
    parser.error('R8 must be enabled to pass main dex rules.')

  if options.expected_configs_file and not options.output_config:
    parser.error('--expected-configs-file requires --output-config')

  if options.proguard_path and options.disable_outlining:
    parser.error('--disable-outlining requires --r8-path')

  if options.only_verify_expectations and not options.stamp:
    parser.error('--only-verify-expectations requires --stamp')

  options.classpath = build_utils.ParseGnList(options.classpath)
  options.proguard_configs = build_utils.ParseGnList(options.proguard_configs)
  options.input_paths = build_utils.ParseGnList(options.input_paths)
  options.extra_mapping_output_paths = build_utils.ParseGnList(
      options.extra_mapping_output_paths)

  if options.feature_names:
    if 'base' not in options.feature_names:
      parser.error('"base" feature required when feature arguments are used.')
    if len(options.feature_names) != len(options.feature_jars) or len(
        options.feature_names) != len(options.dex_dests):
      parser.error('Invalid feature argument lengths.')

    options.feature_jars = [
        build_utils.ParseGnList(x) for x in options.feature_jars
    ]

  return options


def _VerifyExpectedConfigs(expected_path, actual_path, failure_file_path,
                           fail_on_mismatch):
  msg = diff_utils.DiffFileContents(expected_path, actual_path)
  if not msg:
    return

  msg_header = """\
ProGuard flag expectations file needs updating. For details see:
https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/android/java/README.md
"""
  sys.stderr.write(msg_header)
  sys.stderr.write(msg)
  if failure_file_path:
    build_utils.MakeDirectory(os.path.dirname(failure_file_path))
    with open(failure_file_path, 'w') as f:
      f.write(msg_header)
      f.write(msg)
  if fail_on_mismatch:
    sys.exit(1)


class _DexPathContext(object):
  def __init__(self, name, output_path, input_jars, work_dir):
    self.name = name
    self.input_paths = input_jars
    self._final_output_path = output_path
    self.staging_dir = os.path.join(work_dir, name)
    os.mkdir(self.staging_dir)

  def CreateOutput(self):
    found_files = build_utils.FindInDirectory(self.staging_dir)
    if not found_files:
      raise Exception('Missing dex outputs in {}'.format(self.staging_dir))

    if self._final_output_path.endswith('.dex'):
      if len(found_files) != 1:
        raise Exception('Expected exactly 1 dex file output, found: {}'.format(
            '\t'.join(found_files)))
      shutil.move(found_files[0], self._final_output_path)
      return

    # Add to .jar using Python rather than having R8 output to a .zip directly
    # in order to disable compression of the .jar, saving ~500ms.
    tmp_jar_output = self.staging_dir + '.jar'
    build_utils.DoZip(found_files, tmp_jar_output, base_dir=self.staging_dir)
    shutil.move(tmp_jar_output, self._final_output_path)


def _OptimizeWithR8(options,
                    config_paths,
                    libraries,
                    dynamic_config_data,
                    print_stdout=False):
  with build_utils.TempDir() as tmp_dir:
    if dynamic_config_data:
      tmp_config_path = os.path.join(tmp_dir, 'proguard_config.txt')
      with open(tmp_config_path, 'w') as f:
        f.write(dynamic_config_data)
      config_paths = config_paths + [tmp_config_path]

    tmp_mapping_path = os.path.join(tmp_dir, 'mapping.txt')
    # If there is no output (no classes are kept), this prevents this script
    # from failing.
    build_utils.Touch(tmp_mapping_path)

    tmp_output = os.path.join(tmp_dir, 'r8out')
    os.mkdir(tmp_output)

    feature_contexts = []
    if options.feature_names:
      for name, dest_dex, input_paths in zip(
          options.feature_names, options.dex_dests, options.feature_jars):
        feature_context = _DexPathContext(name, dest_dex, input_paths,
                                          tmp_output)
        if name == 'base':
          base_dex_context = feature_context
        else:
          feature_contexts.append(feature_context)
    else:
      base_dex_context = _DexPathContext('base', options.output_path,
                                         options.input_paths, tmp_output)

    cmd = [
        build_utils.JAVA_PATH,
        '-jar',
        options.r8_path,
        '--no-data-resources',
        '--output',
        base_dex_context.staging_dir,
        '--pg-map-output',
        tmp_mapping_path,
    ]

    if options.desugar_jdk_libs_json:
      cmd += ['--desugared-lib', options.desugar_jdk_libs_json]

    if options.min_api:
      cmd += ['--min-api', options.min_api]

    if options.force_enable_assertions:
      cmd += ['--force-enable-assertions']

    for lib in libraries:
      cmd += ['--lib', lib]

    for config_file in config_paths:
      cmd += ['--pg-conf', config_file]

    if options.main_dex_rules_path:
      for main_dex_rule in options.main_dex_rules_path:
        cmd += ['--main-dex-rules', main_dex_rule]

    module_input_jars = set(base_dex_context.input_paths)
    for feature in feature_contexts:
      feature_input_jars = [
          p for p in feature.input_paths if p not in module_input_jars
      ]
      module_input_jars.update(feature_input_jars)
      cmd += [
          '--feature-jar',
          feature.staging_dir + ':' + ':'.join(feature_input_jars)
      ]

    cmd += base_dex_context.input_paths
    # Add any extra input jars to the base module (e.g. desugar runtime).
    extra_jars = set(options.input_paths) - module_input_jars
    cmd += sorted(extra_jars)

    env = os.environ.copy()
    stderr_filter = lambda l: re.sub(r'.*_JAVA_OPTIONS.*\n?', '', l)
    env['_JAVA_OPTIONS'] = '-Dcom.android.tools.r8.allowTestProguardOptions=1'
    if options.disable_outlining:
      env['_JAVA_OPTIONS'] += ' -Dcom.android.tools.r8.disableOutlining=1'

    try:
      build_utils.CheckOutput(
          cmd, env=env, print_stdout=print_stdout, stderr_filter=stderr_filter)
    except build_utils.CalledProcessError as err:
      debugging_link = ('R8 failed. Please see {}.'.format(
          'https://chromium.googlesource.com/chromium/src/+/HEAD/build/'
          'android/docs/java_optimization.md#Debugging-common-failures\n'))
      raise ProguardProcessError(err, debugging_link)

    base_dex_context.CreateOutput()
    for feature in feature_contexts:
      feature.CreateOutput()

    with open(options.mapping_output, 'w') as out_file, \
        open(tmp_mapping_path) as in_file:
      # Mapping files generated by R8 include comments that may break
      # some of our tooling so remove those (specifically: apkanalyzer).
      out_file.writelines(l for l in in_file if not l.startswith('#'))


def _OptimizeWithProguard(options,
                          config_paths,
                          libraries,
                          dynamic_config_data,
                          print_stdout=False):
  with build_utils.TempDir() as tmp_dir:
    combined_injars_path = os.path.join(tmp_dir, 'injars.jar')
    combined_libjars_path = os.path.join(tmp_dir, 'libjars.jar')
    combined_proguard_configs_path = os.path.join(tmp_dir, 'includes.txt')
    tmp_mapping_path = os.path.join(tmp_dir, 'mapping.txt')
    tmp_output_jar = os.path.join(tmp_dir, 'output.jar')

    build_utils.MergeZips(combined_injars_path, options.input_paths)
    build_utils.MergeZips(combined_libjars_path, libraries)
    with open(combined_proguard_configs_path, 'w') as f:
      f.write(_CombineConfigs(config_paths, dynamic_config_data))

    if options.proguard_path.endswith('.jar'):
      cmd = [
          build_utils.JAVA_PATH, '-jar', options.proguard_path, '-include',
          combined_proguard_configs_path
      ]
    else:
      cmd = [options.proguard_path, '@' + combined_proguard_configs_path]

    cmd += [
        '-forceprocessing',
        '-libraryjars',
        combined_libjars_path,
        '-injars',
        combined_injars_path,
        '-outjars',
        tmp_output_jar,
        '-printmapping',
        tmp_mapping_path,
    ]

    # Warning: and Error: are sent to stderr, but messages and Note: are sent
    # to stdout.
    stdout_filter = None
    stderr_filter = None
    if print_stdout:
      stdout_filter = _ProguardOutputFilter()
      stderr_filter = _ProguardOutputFilter()
    build_utils.CheckOutput(
        cmd,
        print_stdout=True,
        print_stderr=True,
        stdout_filter=stdout_filter,
        stderr_filter=stderr_filter)

    # ProGuard will skip writing if the file would be empty.
    build_utils.Touch(tmp_mapping_path)

    # Copy output files to correct locations.
    shutil.move(tmp_output_jar, options.output_path)
    shutil.move(tmp_mapping_path, options.mapping_output)


def _CombineConfigs(configs, dynamic_config_data, exclude_generated=False):
  ret = []

  def add_header(name):
    ret.append('#' * 80)
    ret.append('# ' + name)
    ret.append('#' * 80)

  for config in sorted(configs):
    if exclude_generated and config.endswith('.resources.proguard.txt'):
      continue

    add_header(config)
    with open(config) as config_file:
      contents = config_file.read().rstrip()

    # Fix up line endings (third_party configs can have windows endings).
    contents = contents.replace('\r', '')
    # Remove numbers from generated rule comments to make file more
    # diff'able.
    contents = re.sub(r' #generated:\d+', '', contents)
    ret.append(contents)
    ret.append('')

  if dynamic_config_data:
    add_header('Dynamically generated from build/android/gyp/proguard.py')
    ret.append(dynamic_config_data)
    ret.append('')
  return '\n'.join(ret)


def _CreateDynamicConfig(options):
  ret = []
  if not options.r8_path and options.min_api:
    # R8 adds this option automatically, and uses -assumenosideeffects instead
    # (which ProGuard doesn't support doing).
    ret.append("""\
-assumevalues class android.os.Build$VERSION {
  public static final int SDK_INT return %s..9999;
}""" % options.min_api)

  if options.sourcefile:
    ret.append("-renamesourcefileattribute '%s' # OMIT FROM EXPECTATIONS" %
               options.sourcefile)

  if options.apply_mapping:
    ret.append("-applymapping '%s'" % os.path.abspath(options.apply_mapping))
  if options.repackage_classes:
    ret.append("-repackageclasses '%s'" % options.repackage_classes)

  _min_api = int(options.min_api) if options.min_api else 0
  for api_level, version_code in _API_LEVEL_VERSION_CODE:
    annotation_name = 'org.chromium.base.annotations.VerifiesOn' + version_code
    if api_level > _min_api:
      ret.append('-keep @interface %s' % annotation_name)
      ret.append("""\
-keep,allowobfuscation,allowoptimization @%s class ** {
  <methods>;
}""" % annotation_name)
      ret.append("""\
-keepclassmembers,allowobfuscation,allowoptimization class ** {
  @%s <methods>;
}""" % annotation_name)
  return '\n'.join(ret)


def _VerifyNoEmbeddedConfigs(jar_paths):
  failed = False
  for jar_path in jar_paths:
    with zipfile.ZipFile(jar_path) as z:
      for name in z.namelist():
        if name.startswith('META-INF/proguard/'):
          failed = True
          sys.stderr.write("""\
Found embedded proguard config within {}.
Embedded configs are not permitted (https://crbug.com/989505)
""".format(jar_path))
          break
  if failed:
    sys.exit(1)


def _ContainsDebuggingConfig(config_str):
  debugging_configs = ('-whyareyoukeeping', '-whyareyounotinlining')
  return any(config in config_str for config in debugging_configs)


def _MaybeWriteStampAndDepFile(options, inputs):
  output = options.output_path
  if options.stamp:
    build_utils.Touch(options.stamp)
    output = options.stamp
  if options.depfile:
    build_utils.WriteDepfile(
        options.depfile, output, inputs=inputs, add_pydeps=False)


def main():
  options = _ParseOptions()

  libraries = []
  for p in options.classpath:
    # TODO(bjoyce): Remove filter once old android support libraries are gone.
    # Fix for having Library class extend program class dependency problem.
    if 'com_android_support' in p or 'android_support_test' in p:
      continue
    # If a jar is part of input no need to include it as library jar.
    if p not in libraries and p not in options.input_paths:
      libraries.append(p)

  _VerifyNoEmbeddedConfigs(options.input_paths + libraries)

  proguard_configs = options.proguard_configs
  if options.disable_checkdiscard:
    proguard_configs = _ValidateAndFilterCheckDiscards(proguard_configs)

  # ProGuard configs that are derived from flags.
  dynamic_config_data = _CreateDynamicConfig(options)

  # ProGuard configs that are derived from flags.
  merged_configs = _CombineConfigs(
      proguard_configs, dynamic_config_data, exclude_generated=True)
  print_stdout = _ContainsDebuggingConfig(merged_configs) or options.verbose


  if options.expected_configs_file:
    with tempfile.NamedTemporaryFile() as f:
      f.write(merged_configs)
      f.flush()
      _VerifyExpectedConfigs(options.expected_configs_file, f.name,
                             options.proguard_expectations_failure_file,
                             options.fail_on_expectations)
  if options.only_verify_expectations:
    _MaybeWriteStampAndDepFile(options, options.proguard_configs)
    return

  # Writing the config output before we know ProGuard is going to succeed isn't
  # great, since then a failure will result in one of the outputs being updated.
  # We do it anyways though because the error message prints out the path to the
  # config. Ninja will still know to re-run the command because of the other
  # stale outputs.
  if options.output_config:
    with open(options.output_config, 'w') as f:
      f.write(merged_configs)

  if options.r8_path:
    _OptimizeWithR8(options, proguard_configs, libraries, dynamic_config_data,
                    print_stdout)
  else:
    _OptimizeWithProguard(options, proguard_configs, libraries,
                          dynamic_config_data, print_stdout)

  # After ProGuard / R8 has run:
  for output in options.extra_mapping_output_paths:
    shutil.copy(options.mapping_output, output)

  inputs = options.proguard_configs + options.input_paths + libraries
  if options.apply_mapping:
    inputs.append(options.apply_mapping)

  _MaybeWriteStampAndDepFile(options, inputs)


if __name__ == '__main__':
  main()
