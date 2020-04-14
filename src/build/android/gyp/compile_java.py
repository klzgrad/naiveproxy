#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import distutils.spawn
import logging
import multiprocessing
import optparse
import os
import shutil
import re
import sys
import zipfile

from util import build_utils
from util import md5_check
from util import jar_info_utils

sys.path.insert(
    0,
    os.path.join(build_utils.DIR_SOURCE_ROOT, 'third_party', 'colorama', 'src'))
import colorama

_JAVAC_WRAPPER = os.path.join(build_utils.DIR_SOURCE_ROOT, 'build', 'android',
                              'gyp', 'javac')

# Full list of checks: https://errorprone.info/bugpatterns
ERRORPRONE_WARNINGS_TO_TURN_OFF = [
    # This one should really be turned on.
    'ParameterNotNullable',
    # TODO(crbug.com/834807): Follow steps in bug
    'DoubleBraceInitialization',
    # TODO(crbug.com/834790): Follow steps in bug.
    'CatchAndPrintStackTrace',
    # TODO(crbug.com/801210): Follow steps in bug.
    'SynchronizeOnNonFinalField',
    # TODO(crbug.com/802073): Follow steps in bug.
    'TypeParameterUnusedInFormals',
    # TODO(crbug.com/803484): Follow steps in bug.
    'CatchFail',
    # TODO(crbug.com/803485): Follow steps in bug.
    'JUnitAmbiguousTestClass',
    # TODO(crbug.com/1027683): Follow steps in bug.
    'UnnecessaryParentheses',
    # TODO(wnwen): Fix issue in JavaUploadDataSinkBase.java
    'PrimitiveAtomicReference',
    # Android platform default is always UTF-8.
    # https://developer.android.com/reference/java/nio/charset/Charset.html#defaultCharset()
    'DefaultCharset',
    # Low priority since the alternatives still work.
    'JdkObsolete',
    # We don't use that many lambdas.
    'FunctionalInterfaceClash',
    # There are lots of times when we just want to post a task.
    'FutureReturnValueIgnored',
    # Nice to be explicit about operators, but not necessary.
    'OperatorPrecedence',
    # Just false positives in our code.
    'ThreadJoinLoop',
    # Low priority corner cases with String.split.
    # Linking Guava and using Splitter was rejected
    # in the https://chromium-review.googlesource.com/c/chromium/src/+/871630.
    'StringSplitter',
    # Preferred to use another method since it propagates exceptions better.
    'ClassNewInstance',
    # Nice to have static inner classes but not necessary.
    'ClassCanBeStatic',
    # Explicit is better than implicit.
    'FloatCast',
    # Results in false positives.
    'ThreadLocalUsage',
    # Also just false positives.
    'Finally',
    # False positives for Chromium.
    'FragmentNotInstantiable',
    # Low priority to fix.
    'HidingField',
    # Low priority.
    'IntLongMath',
    # Low priority.
    'BadComparable',
    # Low priority.
    'EqualsHashCode',
    # Nice to fix but low priority.
    'TypeParameterShadowing',
    # Good to have immutable enums, also low priority.
    'ImmutableEnumChecker',
    # False positives for testing.
    'InputStreamSlowMultibyteRead',
    # Nice to have better primitives.
    'BoxedPrimitiveConstructor',
    # Not necessary for tests.
    'OverrideThrowableToString',
    # Nice to have better type safety.
    'CollectionToArraySafeParameter',
    # Makes logcat debugging more difficult, and does not provide obvious
    # benefits in the Chromium codebase.
    'ObjectToString',
    # Triggers on private methods that are @CalledByNative.
    'UnusedMethod',
    # Triggers on generated R.java files.
    'UnusedVariable',
    # Not that useful.
    'UnsafeReflectiveConstructionCast',
    # Not that useful.
    'MixedMutabilityReturnType',
    # Nice to have.
    'EqualsGetClass',
    # A lot of false-positives from CharSequence.equals().
    'UndefinedEquals',
    # Nice to have.
    'ExtendingJUnitAssert',
    # Nice to have.
    'SystemExitOutsideMain',
    # Nice to have.
    'TypeParameterNaming',
    # Nice to have.
    'UnusedException',
    # Nice to have.
    'UngroupedOverloads',
    # Nice to have.
    'FunctionalInterfaceClash',
    # Nice to have.
    'InconsistentOverloads',
    # Dagger generated code triggers this.
    'SameNameButDifferent',
    # Nice to have.
    'UnnecessaryLambda',
    # Nice to have.
    'UnnecessaryAnonymousClass',
    # Nice to have.
    'LiteProtoToString',
]

# Full list of checks: https://errorprone.info/bugpatterns
# Only those marked as "experimental" need to be listed here in order to be
# enabled. We build with -Werror, so all default checks cause builds to fail.
ERRORPRONE_WARNINGS_TO_ERROR = [
    'BinderIdentityRestoredDangerously',
    'EmptyIf',
    'EqualsBrokenForNull',
    'InvalidThrows',
    'LongLiteralLowerCaseSuffix',
    'MultiVariableDeclaration',
    'RedundantOverride',
    'RemoveUnusedImports',
    'StaticQualifiedUsingExpression',
    'StringEquality',
    'TimeUnitMismatch',
    'UnnecessaryStaticImport',
    'UseBinds',
    'WildcardImport',
]


def ProcessJavacOutput(output):
  fileline_prefix = r'(?P<fileline>(?P<file>[-.\w/\\]+.java):(?P<line>[0-9]+):)'
  warning_re = re.compile(fileline_prefix +
                          r'(?P<full_message> warning: (?P<message>.*))$')
  error_re = re.compile(fileline_prefix +
                        r'(?P<full_message> (?P<message>.*))$')
  marker_re = re.compile(r'\s*(?P<marker>\^)\s*$')

  # These warnings cannot be suppressed even for third party code. Deprecation
  # warnings especially do not help since we must support older android version.
  deprecated_re = re.compile(
      r'(Note: .* uses? or overrides? a deprecated API.)$')
  unchecked_re = re.compile(
      r'(Note: .* uses? unchecked or unsafe operations.)$')
  recompile_re = re.compile(r'(Note: Recompile with -Xlint:.* for details.)$')

  warning_color = ['full_message', colorama.Fore.YELLOW + colorama.Style.DIM]
  error_color = ['full_message', colorama.Fore.MAGENTA + colorama.Style.BRIGHT]
  marker_color = ['marker', colorama.Fore.BLUE + colorama.Style.BRIGHT]

  def Colorize(line, regex, color):
    match = regex.match(line)
    start = match.start(color[0])
    end = match.end(color[0])
    return (line[:start] + color[1] + line[start:end] + colorama.Fore.RESET +
            colorama.Style.RESET_ALL + line[end:])

  def ApplyFilters(line):
    return not (deprecated_re.match(line) or unchecked_re.match(line)
                or recompile_re.match(line))

  def ApplyColors(line):
    if warning_re.match(line):
      line = Colorize(line, warning_re, warning_color)
    elif error_re.match(line):
      line = Colorize(line, error_re, error_color)
    elif marker_re.match(line):
      line = Colorize(line, marker_re, marker_color)
    return line

  return '\n'.join(map(ApplyColors, filter(ApplyFilters, output.split('\n'))))


def _ExtractClassFiles(jar_path, dest_dir, java_files):
  """Extracts all .class files not corresponding to |java_files|."""

  # Two challenges exist here:
  # 1. |java_files| have prefixes that are not represented in the the jar paths.
  # 2. A single .java file results in multiple .class files when it contains
  #    nested classes.
  # Here's an example:
  #   source path: ../../base/android/java/src/org/chromium/Foo.java
  #   jar paths: org/chromium/Foo.class, org/chromium/Foo$Inner.class
  # To extract only .class files not related to the given .java files, we strip
  # off ".class" and "$*.class" and use a substring match against java_files.
  def extract_predicate(path):
    if not path.endswith('.class'):
      return False
    path_without_suffix = re.sub(r'(?:\$|\.)[^/]*class$', '', path)
    partial_java_path = path_without_suffix + '.java'
    return not any(p.endswith(partial_java_path) for p in java_files)

  logging.info('Extracting class files from %s', jar_path)
  build_utils.ExtractAll(jar_path, path=dest_dir, predicate=extract_predicate)
  for path in build_utils.FindInDirectory(dest_dir, '*.class'):
    shutil.copystat(jar_path, path)


def _ParsePackageAndClassNames(java_file):
  package_name = ''
  class_names = []
  with open(java_file) as f:
    for l in f:
      # Strip unindented comments.
      # Considers a leading * as a continuation of a multi-line comment (our
      # linter doesn't enforce a space before it like there should be).
      l = re.sub(r'^(?://.*|/?\*.*?(?:\*/\s*|$))', '', l)

      m = re.match(r'package\s+(.*?);', l)
      if m and not package_name:
        package_name = m.group(1)

      # Not exactly a proper parser, but works for sources that Chrome uses.
      # In order to not match nested classes, it just checks for lack of indent.
      m = re.match(r'(?:\S.*?)?(?:class|@?interface|enum)\s+(.+?)\b', l)
      if m:
        class_names.append(m.group(1))
  return package_name, class_names


def _ProcessJavaFileForInfo(java_file):
  package_name, class_names = _ParsePackageAndClassNames(java_file)
  return java_file, package_name, class_names


class _InfoFileContext(object):
  """Manages the creation of the class->source file .info file."""

  def __init__(self, chromium_code, excluded_globs):
    self._chromium_code = chromium_code
    self._excluded_globs = excluded_globs
    # Map of .java path -> .srcjar/nested/path.java.
    self._srcjar_files = {}
    # List of generators from pool.imap_unordered().
    self._results = []
    # Lazily created multiprocessing.Pool.
    self._pool = None

  def AddSrcJarSources(self, srcjar_path, extracted_paths, parent_dir):
    for path in extracted_paths:
      # We want the path inside the srcjar so the viewer can have a tree
      # structure.
      self._srcjar_files[path] = '{}/{}'.format(
          srcjar_path, os.path.relpath(path, parent_dir))

  def SubmitFiles(self, java_files, close=False):
    if self._pool is None:
      # Restrict to just one process to not slow down compiling. Compiling
      # is always slower.
      self._pool = multiprocessing.Pool(1)
    logging.info('Submitting %d files for info', len(java_files))
    self._results.append(
        self._pool.imap_unordered(
            _ProcessJavaFileForInfo, java_files, chunksize=10))
    if close:
      self._pool.close()

  def _CheckPathMatchesClassName(self, java_file, package_name, class_name):
    parts = package_name.split('.') + [class_name + '.java']
    expected_path_suffix = os.path.sep.join(parts)
    if not java_file.endswith(expected_path_suffix):
      raise Exception(('Java package+class name do not match its path.\n'
                       'Actual path: %s\nExpected path: %s') %
                      (java_file, expected_path_suffix))

  def _ProcessInfo(self, java_file, package_name, class_names, source):
    for class_name in class_names:
      yield '{}.{}'.format(package_name, class_name)
      # Skip aidl srcjars since they don't indent code correctly.
      if '_aidl.srcjar' in source:
        continue
      assert not self._chromium_code or len(class_names) == 1, (
          'Chromium java files must only have one class: {}'.format(source))
      if self._chromium_code:
        # This check is not necessary but nice to check this somewhere.
        self._CheckPathMatchesClassName(java_file, package_name, class_names[0])

  def _ShouldIncludeInJarInfo(self, fully_qualified_name):
    name_as_class_glob = fully_qualified_name.replace('.', '/') + '.class'
    return not build_utils.MatchesGlob(name_as_class_glob, self._excluded_globs)

  def _Collect(self):
    if self._pool is None:
      return {}
    ret = {}
    for result in self._results:
      for java_file, package_name, class_names in result:
        source = self._srcjar_files.get(java_file, java_file)
        for fully_qualified_name in self._ProcessInfo(java_file, package_name,
                                                      class_names, source):
          if self._ShouldIncludeInJarInfo(fully_qualified_name):
            ret[fully_qualified_name] = java_file
    self._pool.terminate()
    return ret

  def __del__(self):
    # Work around for Python 2.x bug with multiprocessing and daemon threads:
    # https://bugs.python.org/issue4106
    if self._pool is not None:
      logging.info('Joining multiprocessing.Pool')
      self._pool.terminate()
      self._pool.join()
      logging.info('Done.')

  def Commit(self, output_path):
    """Writes a .jar.info file.

    Maps fully qualified names for classes to either the java file that they
    are defined in or the path of the srcjar that they came from.
    """
    logging.info('Collecting info file entries')
    entries = self._Collect()

    logging.info('Writing info file: %s', output_path)
    with build_utils.AtomicOutput(output_path) as f:
      jar_info_utils.WriteJarInfoFile(f, entries, self._srcjar_files)
    logging.info('Completed info file: %s', output_path)


def _AddExtraJarFiles(jar_path, provider_configurations=None, file_tuples=None):
  with zipfile.ZipFile(jar_path, 'a') as z:
    for config in provider_configurations or []:
      zip_path = 'META-INF/services/' + os.path.basename(config)
      build_utils.AddToZipHermetic(z, zip_path, src_path=config)

    for src_path, zip_path in file_tuples or []:
      build_utils.AddToZipHermetic(z, zip_path, src_path=src_path)


def _OnStaleMd5(options, javac_cmd, java_files, classpath):
  logging.info('Starting _OnStaleMd5')

  # Compiles with Error Prone take twice as long to run as pure javac. Thus GN
  # rules run both in parallel, with Error Prone only used for checks.
  save_outputs = not options.enable_errorprone

  # Use jar_path's directory to ensure paths are relative (needed for goma).
  temp_dir = options.jar_path + '.staging'
  shutil.rmtree(temp_dir, True)
  os.makedirs(temp_dir)
  try:
    classes_dir = os.path.join(temp_dir, 'classes')

    if save_outputs:
      input_srcjars_dir = os.path.join(options.generated_dir, 'input_srcjars')
      annotation_processor_outputs_dir = os.path.join(
          options.generated_dir, 'annotation_processor_outputs')
      # Delete any stale files in the generated directory. The purpose of
      # options.generated_dir is for codesearch.
      shutil.rmtree(options.generated_dir, True)
      info_file_context = _InfoFileContext(options.chromium_code,
                                           options.jar_info_exclude_globs)
    else:
      input_srcjars_dir = os.path.join(temp_dir, 'input_srcjars')
      annotation_processor_outputs_dir = os.path.join(
          temp_dir, 'annotation_processor_outputs')

    if options.java_srcjars:
      logging.info('Extracting srcjars to %s', input_srcjars_dir)
      build_utils.MakeDirectory(input_srcjars_dir)
      for srcjar in options.java_srcjars:
        extracted_files = build_utils.ExtractAll(
            srcjar, no_clobber=True, path=input_srcjars_dir, pattern='*.java')
        java_files.extend(extracted_files)
        if save_outputs:
          info_file_context.AddSrcJarSources(srcjar, extracted_files,
                                             input_srcjars_dir)
      logging.info('Done extracting srcjars')

    if save_outputs and java_files:
      info_file_context.SubmitFiles(java_files)

    if java_files:
      # Don't include the output directory in the initial set of args since it
      # being in a temp dir makes it unstable (breaks md5 stamping).
      cmd = [_JAVAC_WRAPPER]
      cmd += javac_cmd
      cmd += ['-d', classes_dir]
      cmd += ['-s', annotation_processor_outputs_dir]

      # Pass classpath and source paths as response files to avoid extremely
      # long command lines that are tedius to debug.
      if classpath:
        cmd += ['-classpath', ':'.join(classpath)]

      java_files_rsp_path = os.path.join(temp_dir, 'files_list.txt')
      with open(java_files_rsp_path, 'w') as f:
        f.write(' '.join(java_files))
      cmd += ['@' + java_files_rsp_path]

      logging.debug('Build command %s', cmd)
      os.makedirs(classes_dir)
      os.makedirs(annotation_processor_outputs_dir)
      build_utils.CheckOutput(
          cmd,
          print_stdout=options.chromium_code,
          stderr_filter=ProcessJavacOutput)
      logging.info('Finished build command')

    if save_outputs:
      annotation_processor_srcjar = os.path.join(
          annotation_processor_outputs_dir, 'output.srcjar')
      if os.path.exists(annotation_processor_srcjar):
        annotation_processor_java_files = build_utils.ExtractAll(
            annotation_processor_srcjar,
            no_clobber=True,
            path=annotation_processor_outputs_dir)
        os.unlink(annotation_processor_srcjar)
        if annotation_processor_java_files:
          info_file_context.SubmitFiles(
              annotation_processor_java_files, close=True)

      with build_utils.AtomicOutput(options.jar_path) as f:
        if java_files:
          shutil.move(os.path.join(classes_dir, 'classes.jar'), f.name)
        else:
          zipfile.ZipFile(f.name, 'w').close()
        if options.provider_configurations or options.additional_jar_files:
          _AddExtraJarFiles(f.name, options.provider_configurations,
                            options.additional_jar_files)

      info_file_context.Commit(options.jar_path + '.info')
    else:
      build_utils.Touch(options.jar_path)

    logging.info('Completed all steps in _OnStaleMd5')
  finally:
    shutil.rmtree(temp_dir)


def _ParseOptions(argv):
  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)

  parser.add_option(
      '--java-srcjars',
      action='append',
      default=[],
      help='List of srcjars to include in compilation.')
  parser.add_option(
      '--generated-dir',
      help='Subdirectory within target_gen_dir to place extracted srcjars and '
      'annotation processor output for codesearch to find.')
  parser.add_option(
      '--bootclasspath',
      action='append',
      default=[],
      help='Boot classpath for javac. If this is specified multiple times, '
      'they will all be appended to construct the classpath.')
  parser.add_option(
      '--java-version',
      help='Java language version to use in -source and -target args to javac.')
  parser.add_option('--classpath', action='append', help='Classpath to use.')
  parser.add_option(
      '--processors',
      action='append',
      help='GN list of annotation processor main classes.')
  parser.add_option(
      '--processorpath',
      action='append',
      help='GN list of jars that comprise the classpath used for Annotation '
      'Processors.')
  parser.add_option(
      '--processor-arg',
      dest='processor_args',
      action='append',
      help='key=value arguments for the annotation processors.')
  parser.add_option(
      '--provider-configuration',
      dest='provider_configurations',
      action='append',
      help='File to specify a service provider. Will be included '
      'in the jar under META-INF/services.')
  parser.add_option(
      '--additional-jar-file',
      dest='additional_jar_files',
      action='append',
      help='Additional files to package into jar. By default, only Java .class '
      'files are packaged into the jar. Files should be specified in '
      'format <filename>:<path to be placed in jar>.')
  parser.add_option(
      '--jar-info-exclude-globs',
      help='GN list of exclude globs to filter from generated .info files.')
  parser.add_option(
      '--chromium-code',
      type='int',
      help='Whether code being compiled should be built with stricter '
      'warnings for chromium code.')
  parser.add_option(
      '--gomacc-path', help='When set, prefix javac command with gomacc')
  parser.add_option(
      '--errorprone-path', help='Use the Errorprone compiler at this path.')
  parser.add_option(
      '--enable-errorprone',
      action='store_true',
      help='Enable errorprone checks')
  parser.add_option(
      '--warnings-as-errors',
      action='store_true',
      help='Treat all warnings as errors.')
  parser.add_option('--jar-path', help='Jar output path.')
  parser.add_option(
      '--javac-arg',
      action='append',
      default=[],
      help='Additional arguments to pass to javac.')

  options, args = parser.parse_args(argv)
  build_utils.CheckOptions(options, parser, required=('jar_path', ))

  options.bootclasspath = build_utils.ParseGnList(options.bootclasspath)
  options.classpath = build_utils.ParseGnList(options.classpath)
  options.processorpath = build_utils.ParseGnList(options.processorpath)
  options.processors = build_utils.ParseGnList(options.processors)
  options.java_srcjars = build_utils.ParseGnList(options.java_srcjars)
  options.jar_info_exclude_globs = build_utils.ParseGnList(
      options.jar_info_exclude_globs)

  additional_jar_files = []
  for arg in options.additional_jar_files or []:
    filepath, jar_filepath = arg.split(':')
    additional_jar_files.append((filepath, jar_filepath))
  options.additional_jar_files = additional_jar_files

  java_files = []
  for arg in args:
    # Interpret a path prefixed with @ as a file containing a list of sources.
    if arg.startswith('@'):
      java_files.extend(build_utils.ReadSourcesList(arg[1:]))
    else:
      java_files.append(arg)

  return options, java_files


def main(argv):
  build_utils.InitLogging('JAVAC_DEBUG')
  colorama.init()

  argv = build_utils.ExpandFileArgs(argv)
  options, java_files = _ParseOptions(argv)
  javac_path = build_utils.JAVAC_PATH

  javac_cmd = []
  if options.gomacc_path:
    javac_cmd.append(options.gomacc_path)

  javac_cmd += [
      javac_path,
      '-g',
      # Chromium only allows UTF8 source files.  Being explicit avoids
      # javac pulling a default encoding from the user's environment.
      '-encoding',
      'UTF-8',
      # Prevent compiler from compiling .java files not listed as inputs.
      # See: http://blog.ltgt.net/most-build-tools-misuse-javac/
      '-sourcepath',
      ':',
  ]

  if options.enable_errorprone:
    # All errorprone args are passed space-separated in a single arg.
    errorprone_flags = ['-Xplugin:ErrorProne']
    for warning in ERRORPRONE_WARNINGS_TO_TURN_OFF:
      errorprone_flags.append('-Xep:{}:OFF'.format(warning))
    for warning in ERRORPRONE_WARNINGS_TO_ERROR:
      errorprone_flags.append('-Xep:{}:ERROR'.format(warning))
    if not options.warnings_as_errors:
      errorprone_flags.append('-XepAllErrorsAsWarnings')
    javac_cmd += ['-XDcompilePolicy=simple', ' '.join(errorprone_flags)]

  if options.java_version:
    javac_cmd.extend([
        '-source',
        options.java_version,
        '-target',
        options.java_version,
    ])
  if options.java_version == '1.8':
    # Android's boot jar doesn't contain all java 8 classes.
    options.bootclasspath.append(build_utils.RT_JAR_PATH)

  if options.warnings_as_errors:
    javac_cmd.extend(['-Werror'])
  else:
    # XDignore.symbol.file makes javac compile against rt.jar instead of
    # ct.sym. This means that using a java internal package/class will not
    # trigger a compile warning or error.
    javac_cmd.extend(['-XDignore.symbol.file'])

  if options.processors:
    javac_cmd.extend(['-processor', ','.join(options.processors)])

  if options.bootclasspath:
    javac_cmd.extend(['-bootclasspath', ':'.join(options.bootclasspath)])

  if options.processorpath:
    javac_cmd.extend(['-processorpath', ':'.join(options.processorpath)])
  if options.processor_args:
    for arg in options.processor_args:
      javac_cmd.extend(['-A%s' % arg])

  javac_cmd.extend(options.javac_arg)

  classpath_inputs = (
      options.bootclasspath + options.classpath + options.processorpath)

  # GN already knows of java_files, so listing them just make things worse when
  # they change.
  depfile_deps = classpath_inputs + options.java_srcjars
  input_paths = depfile_deps + java_files
  input_paths += [x[0] for x in options.additional_jar_files]

  output_paths = [
      options.jar_path,
      options.jar_path + '.info',
  ]

  input_strings = javac_cmd + options.classpath + java_files
  if options.jar_info_exclude_globs:
    input_strings.append(options.jar_info_exclude_globs)
  md5_check.CallAndWriteDepfileIfStale(
      lambda: _OnStaleMd5(options, javac_cmd, java_files, options.classpath),
      options,
      depfile_deps=depfile_deps,
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=output_paths)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
