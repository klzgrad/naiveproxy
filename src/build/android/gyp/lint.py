#!/usr/bin/env python
#
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs Android's lint tool."""


from __future__ import print_function

import argparse
import logging
import os
import re
import shutil
import sys
import time
import traceback
from xml.dom import minidom
from xml.etree import ElementTree

from util import build_utils
from util import manifest_utils
from util import resource_utils

_LINT_MD_URL = 'https://chromium.googlesource.com/chromium/src/+/master/build/android/docs/lint.md' # pylint: disable=line-too-long

# These checks are not useful for test targets and adds an unnecessary burden
# to suppress them.
_DISABLED_FOR_TESTS = [
    # We should not require test strings.xml files to explicitly add
    # translatable=false since they are not translated and not used in
    # production.
    "MissingTranslation",
    # Test strings.xml files often have simple names and are not translatable,
    # so it may conflict with a production string and cause this error.
    "Untranslatable",
    # Test targets often use the same strings target and resources target as the
    # production targets but may not use all of them.
    "UnusedResources",
]


def _RunLint(lint_path,
             config_path,
             manifest_path,
             result_path,
             product_dir,
             sources,
             cache_dir,
             android_sdk_version,
             srcjars,
             min_sdk_version,
             manifest_package,
             resource_sources,
             resource_zips,
             android_sdk_root,
             testonly_target=False,
             can_fail_build=False,
             include_unexpected=False,
             silent=False):
  logging.info('Lint starting')

  def _RebasePath(path):
    """Returns relative path to top-level src dir.

    Args:
      path: A path relative to cwd.
    """
    ret = os.path.relpath(os.path.abspath(path), build_utils.DIR_SOURCE_ROOT)
    # If it's outside of src/, just use abspath.
    if ret.startswith('..'):
      ret = os.path.abspath(path)
    return ret

  def _ProcessResultFile():
    with open(result_path, 'rb') as f:
      content = f.read().replace(
          _RebasePath(product_dir), 'PRODUCT_DIR')

    with open(result_path, 'wb') as f:
      f.write(content)

  def _ParseAndShowResultFile():
    dom = minidom.parse(result_path)
    issues = dom.getElementsByTagName('issue')
    if not silent:
      print(file=sys.stderr)
      for issue in issues:
        issue_id = issue.attributes['id'].value
        message = issue.attributes['message'].value
        location_elem = issue.getElementsByTagName('location')[0]
        path = location_elem.attributes['file'].value
        line = location_elem.getAttribute('line')
        error = '%s:%s %s: %s [warning]' % (path, line, message, issue_id)
        print(error.encode('utf-8'), file=sys.stderr)
        for attr in ['errorLine1', 'errorLine2']:
          error_line = issue.getAttribute(attr)
          if error_line:
            print(error_line.encode('utf-8'), file=sys.stderr)
    return len(issues)

  with build_utils.TempDir() as temp_dir:
    cmd = [
        _RebasePath(lint_path),
        '-Werror',
        '--exitcode',
        '--showall',
        '--xml',
        _RebasePath(result_path),
        # An explicit sdk root needs to be specified since we have an extra
        # intermediate 'lastest' directory under cmdline-tools which prevents
        # lint from automatically deducing the location of the sdk. The sdk is
        # required for many checks (e.g. NewApi). Lint also requires absolute
        # paths.
        '--sdk-home',
        os.path.abspath(android_sdk_root),
    ]
    if config_path:
      cmd.extend(['--config', _RebasePath(config_path)])
    if testonly_target:
      cmd.extend(['--disable', ','.join(_DISABLED_FOR_TESTS)])

    tmp_dir_counter = [0]
    def _NewTempSubdir(prefix, append_digit=True):
      # Helper function to create a new sub directory based on the number of
      # subdirs created earlier.
      if append_digit:
        tmp_dir_counter[0] += 1
        prefix += str(tmp_dir_counter[0])
      new_dir = os.path.join(temp_dir, prefix)
      os.makedirs(new_dir)
      return new_dir

    resource_dirs = resource_utils.DeduceResourceDirsFromFileList(
        resource_sources)
    # These are zip files with generated resources (e. g. strings from GRD).
    for resource_zip in resource_zips:
      resource_dir = _NewTempSubdir(resource_zip, append_digit=False)
      resource_dirs.append(resource_dir)
      build_utils.ExtractAll(resource_zip, path=resource_dir)

    for resource_dir in resource_dirs:
      cmd.extend(['--resources', _RebasePath(resource_dir)])

    # There may be multiple source files with the same basename (but in
    # different directories). It is difficult to determine what part of the path
    # corresponds to the java package, and so instead just link the source files
    # into temporary directories (creating a new one whenever there is a name
    # conflict).
    def PathInDir(d, src):
      subpath = os.path.join(d, _RebasePath(src))
      subdir = os.path.dirname(subpath)
      if not os.path.exists(subdir):
        os.makedirs(subdir)
      return subpath

    src_dirs = []
    for src in sources:
      src_dir = None
      for d in src_dirs:
        if not os.path.exists(PathInDir(d, src)):
          src_dir = d
          break
      if not src_dir:
        src_dir = _NewTempSubdir('SRC_ROOT')
        src_dirs.append(src_dir)
        cmd.extend(['--sources', _RebasePath(src_dir)])
      # In cases where the build dir is outside of the src dir, this can
      # result in trying to symlink a file to itself for this file:
      # gen/components/version_info/android/java/org/chromium/
      #   components/version_info/VersionConstants.java
      src = os.path.abspath(src)
      dst = PathInDir(src_dir, src)
      if src == dst:
        continue
      os.symlink(src, dst)

    if srcjars:
      srcjar_dir = _NewTempSubdir('GENERATED_SRC_ROOT', append_digit=False)
      cmd.extend(['--sources', _RebasePath(srcjar_dir)])
      for srcjar in srcjars:
        # We choose to allow srcjars that contain java files which have the
        # same package and name to clobber each other. This happens for
        # generated files like BuildConfig.java. It is generated for
        # targets like base_build_config_gen as well as targets like
        # chrome_modern_public_base_bundle_module__build_config_srcjar.
        # Although we could extract each srcjar to a separate folder, that
        # slows down some invocations of lint by 20 seconds or more.
        # TODO(wnwen): Switch lint.py to generate a project.xml file which
        #              supports srcjar inputs by default.
        build_utils.ExtractAll(srcjar, path=srcjar_dir, no_clobber=False)

    project_dir = _NewTempSubdir('PROJECT_ROOT', append_digit=False)
    if android_sdk_version:
      # Create dummy project.properies file in a temporary "project" directory.
      # It is the only way to add Android SDK to the Lint's classpath. Proper
      # classpath is necessary for most source-level checks.
      with open(os.path.join(project_dir, 'project.properties'), 'w') \
          as propfile:
        print('target=android-{}'.format(android_sdk_version), file=propfile)

    # Put the manifest in a temporary directory in order to avoid lint detecting
    # sibling res/ and src/ directories (which should be pass explicitly if they
    # are to be included).
    if not manifest_path:
      manifest_path = os.path.join(
          build_utils.DIR_SOURCE_ROOT, 'build', 'android',
          'AndroidManifest.xml')
    lint_manifest_path = os.path.join(project_dir, 'AndroidManifest.xml')
    shutil.copyfile(os.path.abspath(manifest_path), lint_manifest_path)

    # Check that minSdkVersion and package is correct and add it to the manifest
    # in case it does not exist.
    doc, manifest, _ = manifest_utils.ParseManifest(lint_manifest_path)
    manifest_utils.AssertUsesSdk(manifest, min_sdk_version)
    manifest_utils.AssertPackage(manifest, manifest_package)
    uses_sdk = manifest.find('./uses-sdk')
    if uses_sdk is None:
      uses_sdk = ElementTree.Element('uses-sdk')
      manifest.insert(0, uses_sdk)
    uses_sdk.set('{%s}minSdkVersion' % manifest_utils.ANDROID_NAMESPACE,
                 min_sdk_version)
    if manifest_package:
      manifest.set('package', manifest_package)
    manifest_utils.SaveManifest(doc, lint_manifest_path)

    cmd.append(project_dir)

    if os.path.exists(result_path):
      os.remove(result_path)

    env = os.environ.copy()
    stderr_filter = build_utils.FilterReflectiveAccessJavaWarnings
    if cache_dir:
      env['_JAVA_OPTIONS'] = '-Duser.home=%s' % _RebasePath(cache_dir)
      # When _JAVA_OPTIONS is set, java prints to stderr:
      # Picked up _JAVA_OPTIONS: ...
      #
      # We drop all lines that contain _JAVA_OPTIONS from the output
      stderr_filter = lambda l: re.sub(
          r'.*_JAVA_OPTIONS.*\n?',
          '',
          build_utils.FilterReflectiveAccessJavaWarnings(l))

    def fail_func(returncode, stderr):
      if returncode != 0:
        return True
      if (include_unexpected and
          'Unexpected failure during lint analysis' in stderr):
        return True
      return False

    try:
      env['JAVA_HOME'] = os.path.relpath(build_utils.JAVA_HOME,
                                         build_utils.DIR_SOURCE_ROOT)
      logging.debug('Lint command %s', cmd)
      start = time.time()
      build_utils.CheckOutput(cmd, cwd=build_utils.DIR_SOURCE_ROOT,
                              env=env or None, stderr_filter=stderr_filter,
                              fail_func=fail_func)
      end = time.time() - start
      logging.info('Lint command took %ss', end)
    except build_utils.CalledProcessError:
      # There is a problem with lint usage
      if not os.path.exists(result_path):
        raise

      # Sometimes produces empty (almost) files:
      if os.path.getsize(result_path) < 10:
        if can_fail_build:
          raise
        elif not silent:
          traceback.print_exc()
        return

      # There are actual lint issues
      try:
        num_issues = _ParseAndShowResultFile()
      except Exception: # pylint: disable=broad-except
        if not silent:
          print('Lint created unparseable xml file...')
          print('File contents:')
          with open(result_path) as f:
            print(f.read())
          if can_fail_build:
            traceback.print_exc()
        if can_fail_build:
          raise
        else:
          return

      _ProcessResultFile()
      if num_issues == 0 and include_unexpected:
        msg = 'Please refer to output above for unexpected lint failures.\n'
      else:
        msg = ('\nLint found %d new issues.\n'
               ' - For full explanation, please refer to %s\n'
               ' - For more information about lint and how to fix lint issues,'
               ' please refer to %s\n' %
               (num_issues, _RebasePath(result_path), _LINT_MD_URL))
      if not silent:
        print(msg, file=sys.stderr)
      if can_fail_build:
        raise Exception('Lint failed.')

  logging.info('Lint completed')


def _FindInDirectories(directories, filename_filter):
  all_files = []
  for directory in directories:
    all_files.extend(build_utils.FindInDirectory(directory, filename_filter))
  return all_files


def _ParseArgs(argv):
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--android-sdk-root',
                      required=True,
                      help='Lint needs an explicit path to the android sdk.')
  parser.add_argument('--testonly',
                      action='store_true',
                      help='If set, some checks like UnusedResources will be '
                      'disabled since they are not helpful for test '
                      'targets.')
  parser.add_argument('--lint-path', required=True,
                      help='Path to lint executable.')
  parser.add_argument('--product-dir', required=True,
                      help='Path to product dir.')
  parser.add_argument('--result-path', required=True,
                      help='Path to XML lint result file.')
  parser.add_argument('--cache-dir', required=True,
                      help='Path to the directory in which the android cache '
                           'directory tree should be stored.')
  parser.add_argument('--platform-xml-path', required=True,
                      help='Path to api-platforms.xml')
  parser.add_argument('--android-sdk-version',
                      help='Version (API level) of the Android SDK used for '
                           'building.')
  parser.add_argument('--can-fail-build', action='store_true',
                      help='If set, script will exit with nonzero exit status'
                           ' if lint errors are present')
  parser.add_argument('--include-unexpected-failures', action='store_true',
                      help='If set, script will exit with nonzero exit status'
                           ' if lint itself crashes with unexpected failures.')
  parser.add_argument('--config-path',
                      help='Path to lint suppressions file.')
  parser.add_argument('--java-sources',
                      help='File containing a list of java sources files.')
  parser.add_argument('--manifest-path',
                      help='Path to AndroidManifest.xml')
  parser.add_argument('--resource-sources',
                      default=[],
                      action='append',
                      help='GYP-list of resource sources files, similar to '
                      'java sources files, but for resource files.')
  parser.add_argument('--resource-zips',
                      default=[],
                      action='append',
                      help='GYP-list of resource zips, zip files of generated '
                      'resource files.')
  parser.add_argument('--silent', action='store_true',
                      help='If set, script will not log anything.')
  parser.add_argument('--srcjars',
                      help='GN list of included srcjars.')
  parser.add_argument('--stamp', help='Path to stamp upon success.')
  parser.add_argument(
      '--min-sdk-version',
      required=True,
      help='Minimal SDK version to lint against.')
  parser.add_argument(
      '--manifest-package', help='Package name of the AndroidManifest.xml.')

  args = parser.parse_args(build_utils.ExpandFileArgs(argv))

  args.java_sources = build_utils.ParseGnList(args.java_sources)
  args.srcjars = build_utils.ParseGnList(args.srcjars)
  args.resource_sources = build_utils.ParseGnList(args.resource_sources)
  args.resource_zips = build_utils.ParseGnList(args.resource_zips)

  return args


def main():
  build_utils.InitLogging('LINT_DEBUG')
  args = _ParseArgs(sys.argv[1:])

  sources = []
  for java_sources_file in args.java_sources:
    sources.extend(build_utils.ReadSourcesList(java_sources_file))

  resource_sources = []
  for resource_sources_file in args.resource_sources:
    resource_sources.extend(build_utils.ReadSourcesList(resource_sources_file))

  possible_depfile_deps = (args.srcjars + args.resource_zips + sources +
                           resource_sources + [
                               args.manifest_path,
                           ])

  depfile_deps = [p for p in possible_depfile_deps if p]

  _RunLint(args.lint_path,
           args.config_path,
           args.manifest_path,
           args.result_path,
           args.product_dir,
           sources,
           args.cache_dir,
           args.android_sdk_version,
           args.srcjars,
           args.min_sdk_version,
           args.manifest_package,
           resource_sources,
           args.resource_zips,
           args.android_sdk_root,
           testonly_target=args.testonly,
           can_fail_build=args.can_fail_build,
           include_unexpected=args.include_unexpected_failures,
           silent=args.silent)
  logging.info('Creating stamp file')
  build_utils.Touch(args.stamp)

  if args.depfile:
    build_utils.WriteDepfile(args.depfile,
                             args.stamp,
                             depfile_deps,
                             add_pydeps=False)  # pydeps listed in GN.


if __name__ == '__main__':
  sys.exit(main())
