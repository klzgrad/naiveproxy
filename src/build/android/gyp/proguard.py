#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import cStringIO
import optparse
import os
import shutil
import sys
import tempfile

from util import build_utils
from util import diff_utils
from util import proguard_util

_GENERATED_PROGUARD_HEADER = """
################################################################################
# Dynamically generated from build/android/gyp/proguard.py
################################################################################
"""

# Example:
# android.arch.core.internal.SafeIterableMap$Entry -> b:
#     1:1:java.lang.Object getKey():353:353 -> getKey
#     2:2:java.lang.Object getValue():359:359 -> getValue
def _RemoveMethodMappings(orig_path, out_fd):
  with open(orig_path) as in_fd:
    for line in in_fd:
      if line[:1] != ' ':
        out_fd.write(line)
  out_fd.flush()


def _ParseOptions(args):
  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)
  parser.add_option('--proguard-path',
                    help='Path to the proguard.jar to use.')
  parser.add_option('--r8-path',
                    help='Path to the R8.jar to use.')
  parser.add_option('--input-paths',
                    help='Paths to the .jar files proguard should run on.')
  parser.add_option('--output-path', help='Path to the generated .jar file.')
  parser.add_option('--proguard-configs', action='append',
                    help='Paths to proguard configuration files.')
  parser.add_option('--proguard-config-exclusions',
                    default='',
                    help='GN list of paths to proguard configuration files '
                         'included by --proguard-configs, but that should '
                         'not actually be included.')
  parser.add_option(
      '--apply-mapping', help='Path to proguard mapping to apply.')
  parser.add_option('--mapping-output',
                    help='Path for proguard to output mapping file to.')
  parser.add_option(
      '--extra-mapping-output-paths',
      help='Additional paths to copy output mapping file to.')
  parser.add_option(
      '--output-config',
      help='Path to write the merged proguard config file to.')
  parser.add_option(
      '--expected-configs-file',
      help='Path to a file containing the expected merged proguard configs')
  parser.add_option(
      '--verify-expected-configs',
      action='store_true',
      help='Fail if the expected merged proguard configs differ from the '
      'generated merged proguard configs.')
  parser.add_option('--classpath', action='append',
                    help='Classpath for proguard.')
  parser.add_option('--main-dex-rules-path', action='append',
                    help='Paths to main dex rules for multidex'
                         '- only works with R8.')
  parser.add_option('--min-api', default='',
                    help='Minimum Android API level compatibility.')
  parser.add_option('--verbose', '-v', action='store_true',
                    help='Print all proguard output')
  parser.add_option(
      '--repackage-classes',
      help='Unique package name given to an asynchronously proguarded module')

  options, _ = parser.parse_args(args)

  assert not options.main_dex_rules_path or options.r8_path, \
      'R8 must be enabled to pass main dex rules.'

  classpath = []
  for arg in options.classpath:
    classpath += build_utils.ParseGnList(arg)
  options.classpath = classpath

  configs = []
  for arg in options.proguard_configs:
    configs += build_utils.ParseGnList(arg)
  options.proguard_configs = configs
  options.proguard_config_exclusions = (
      build_utils.ParseGnList(options.proguard_config_exclusions))

  options.input_paths = build_utils.ParseGnList(options.input_paths)

  if not options.mapping_output:
    options.mapping_output = options.output_path + '.mapping'

  if options.apply_mapping:
    options.apply_mapping = os.path.abspath(options.apply_mapping)


  return options


def _VerifyExpectedConfigs(expected_path, actual_path, fail_on_exit):
  msg = diff_utils.DiffFileContents(expected_path, actual_path)
  if not msg:
    return

  sys.stderr.write("""\
Proguard flag expectations file needs updating. For details see:
https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/android/java/README.md
""")
  sys.stderr.write(msg)
  if fail_on_exit:
    sys.exit(1)


def _MoveTempDexFile(tmp_dex_dir, dex_path):
  """Move the temp dex file out of |tmp_dex_dir|.

  Args:
    tmp_dex_dir: Path to temporary directory created with tempfile.mkdtemp().
      The directory should have just a single file.
    dex_path: Target path to move dex file to.

  Raises:
    Exception if there are multiple files in |tmp_dex_dir|.
  """
  tempfiles = os.listdir(tmp_dex_dir)
  if len(tempfiles) > 1:
    raise Exception('%d files created, expected 1' % len(tempfiles))

  tmp_dex_path = os.path.join(tmp_dex_dir, tempfiles[0])
  shutil.move(tmp_dex_path, dex_path)


def _CreateR8Command(options, map_output_path, output_dir, tmp_config_path,
                     libraries):
  cmd = [
    'java', '-jar', options.r8_path,
    '--no-desugaring',
    '--no-data-resources',
    '--output', output_dir,
    '--pg-map-output', map_output_path,
  ]

  for lib in libraries:
    cmd += ['--lib', lib]

  for config_file in options.proguard_configs:
    cmd += ['--pg-conf', config_file]

  temp_config_string = ''
  if options.apply_mapping or options.repackage_classes or options.min_api:
    with open(tmp_config_path, 'w') as f:
      if options.apply_mapping:
        temp_config_string += '-applymapping \'%s\'\n' % (options.apply_mapping)
      if options.repackage_classes:
        temp_config_string += '-repackageclasses \'%s\'\n' % (
            options.repackage_classes)
      if options.min_api:
        temp_config_string += (
            '-assumevalues class android.os.Build$VERSION {\n' +
            '    public static final int SDK_INT return ' + options.min_api +
            '..9999;\n}\n')
      f.write(temp_config_string)
    cmd += ['--pg-conf', tmp_config_path]

  if options.main_dex_rules_path:
    for main_dex_rule in options.main_dex_rules_path:
      cmd += ['--main-dex-rules', main_dex_rule]

  cmd += options.input_paths
  return cmd, temp_config_string


def main(args):
  args = build_utils.ExpandFileArgs(args)
  options = _ParseOptions(args)

  libraries = []
  for p in options.classpath:
    # If a jar is part of input no need to include it as library jar.
    if p not in libraries and p not in options.input_paths:
      libraries.append(p)

  # TODO(agrieve): Remove proguard usages.
  if options.r8_path:
    temp_config_string = ''
    with build_utils.TempDir() as tmp_dir:
      tmp_mapping_path = os.path.join(tmp_dir, 'mapping.txt')
      tmp_proguard_config_path = os.path.join(tmp_dir, 'proguard_config.txt')
      # If there is no output (no classes are kept), this prevents this script
      # from failing.
      build_utils.Touch(tmp_mapping_path)

      f = cStringIO.StringIO()
      proguard_util.WriteFlagsFile(
          options.proguard_configs, f, exclude_generated=True)
      merged_configs = f.getvalue()
      # Fix up line endings (third_party configs can have windows endings)
      merged_configs = merged_configs.replace('\r', '')
      f.close()
      print_stdout = '-whyareyoukeeping' in merged_configs

      if options.output_path.endswith('.dex'):
        with build_utils.TempDir() as tmp_dex_dir:
          cmd, temp_config_string = _CreateR8Command(
              options, tmp_mapping_path, tmp_dex_dir, tmp_proguard_config_path,
              libraries)
          build_utils.CheckOutput(cmd, print_stdout=print_stdout)
          _MoveTempDexFile(tmp_dex_dir, options.output_path)
      else:
        cmd, temp_config_string = _CreateR8Command(
            options, tmp_mapping_path, options.output_path,
            tmp_proguard_config_path, libraries)
        build_utils.CheckOutput(cmd, print_stdout=print_stdout)

      # Copy output files to correct locations.
      with build_utils.AtomicOutput(options.mapping_output) as mapping:
        # Mapping files generated by R8 include comments that may break
        # some of our tooling so remove those.
        with open(tmp_mapping_path) as tmp:
          mapping.writelines(l for l in tmp if not l.startswith('#'))

      for output in build_utils.ParseGnList(options.extra_mapping_output_paths):
        shutil.copy(tmp_mapping_path, output)


    with build_utils.AtomicOutput(options.output_config) as f:
      f.write(merged_configs)
      if temp_config_string:
        f.write(_GENERATED_PROGUARD_HEADER)
        f.write(temp_config_string)

    if options.expected_configs_file:
      _VerifyExpectedConfigs(options.expected_configs_file,
                             options.output_config,
                             options.verify_expected_configs)

    other_inputs = []
    if options.apply_mapping:
      other_inputs += options.apply_mapping

    build_utils.WriteDepfile(
        options.depfile,
        options.output_path,
        inputs=options.proguard_configs + options.input_paths + libraries +
        other_inputs,
        add_pydeps=False)
  else:
    proguard = proguard_util.ProguardCmdBuilder(options.proguard_path)
    proguard.injars(options.input_paths)
    proguard.configs(options.proguard_configs)
    proguard.config_exclusions(options.proguard_config_exclusions)
    proguard.outjar(options.output_path)
    proguard.mapping_output(options.mapping_output)
    proguard.libraryjars(libraries)
    proguard.verbose(options.verbose)
    proguard.min_api(options.min_api)
    # Do not consider the temp file as an input since its name is random.
    input_paths = proguard.GetInputs()

    with tempfile.NamedTemporaryFile() as f:
      if options.apply_mapping:
        input_paths.append(options.apply_mapping)
        # Maintain only class name mappings in the .mapping file in order to
        # work around what appears to be a ProGuard bug in -applymapping:
        #     method 'int close()' is not being kept as 'a', but remapped to 'c'
        _RemoveMethodMappings(options.apply_mapping, f)
        proguard.mapping(f.name)

      input_strings = proguard.build()
      if f.name in input_strings:
        input_strings[input_strings.index(f.name)] = '$M'

      build_utils.CallAndWriteDepfileIfStale(
          proguard.CheckOutput,
          options,
          input_paths=input_paths,
          input_strings=input_strings,
          output_paths=proguard.GetOutputs(),
          depfile_deps=proguard.GetDepfileDeps(),
          add_pydeps=False)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
