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

_LINT_MD_URL = 'https://chromium.googlesource.com/chromium/src/+/master/build/android/docs/lint.md'  # pylint: disable=line-too-long

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

_RES_ZIP_DIR = 'RESZIPS'
_SRCJAR_DIR = 'SRCJARS'


def _SrcRelative(path):
  """Returns relative path to top-level src dir."""
  return os.path.relpath(path, build_utils.DIR_SOURCE_ROOT)


def _GenerateProjectFile(android_manifest,
                         android_sdk_root,
                         cache_dir,
                         sources=None,
                         srcjar_sources=None,
                         resource_sources=None,
                         android_sdk_version=None):
  project = ElementTree.Element('project')
  root = ElementTree.SubElement(project, 'root')
  # An absolute path helps error paths to be shorter.
  root.set('dir', os.path.abspath(build_utils.DIR_SOURCE_ROOT))
  sdk = ElementTree.SubElement(project, 'sdk')
  # Lint requires that the sdk path be an absolute path.
  sdk.set('dir', os.path.abspath(android_sdk_root))
  cache = ElementTree.SubElement(project, 'cache')
  cache.set('dir', _SrcRelative(cache_dir))
  main_module = ElementTree.SubElement(project, 'module')
  main_module.set('name', 'main')
  main_module.set('android', 'true')
  main_module.set('library', 'false')
  if android_sdk_version:
    main_module.set('compile_sdk_version', android_sdk_version)
  manifest = ElementTree.SubElement(main_module, 'manifest')
  manifest.set('file', _SrcRelative(android_manifest))
  if srcjar_sources:
    for srcjar_file in srcjar_sources:
      src = ElementTree.SubElement(main_module, 'src')
      src.set('file', _SrcRelative(srcjar_file))
  if sources:
    for source in sources:
      src = ElementTree.SubElement(main_module, 'src')
      src.set('file', _SrcRelative(source))
  if resource_sources:
    for resource_file in resource_sources:
      resource = ElementTree.SubElement(main_module, 'resource')
      resource.set('file', _SrcRelative(resource_file))
  return project


def _GenerateAndroidManifest(original_manifest_path,
                             min_sdk_version,
                             manifest_package=None):
  # Set minSdkVersion and package in the manifest to the correct values.
  doc, manifest, _ = manifest_utils.ParseManifest(original_manifest_path)
  uses_sdk = manifest.find('./uses-sdk')
  if uses_sdk is None:
    uses_sdk = ElementTree.Element('uses-sdk')
    manifest.insert(0, uses_sdk)
  uses_sdk.set('{%s}minSdkVersion' % manifest_utils.ANDROID_NAMESPACE,
               min_sdk_version)
  if manifest_package:
    manifest.set('package', manifest_package)
  return doc


def _WriteXmlFile(root, path):
  build_utils.MakeDirectory(os.path.dirname(path))
  with build_utils.AtomicOutput(path) as f:
    # Although we can write it just with ElementTree.tostring, using minidom
    # makes it a lot easier to read as a human (also on code search).
    f.write(
        minidom.parseString(ElementTree.tostring(
            root, encoding='utf-8')).toprettyxml(indent='  '))


def _RunLint(lint_binary_path,
             config_path,
             manifest_path,
             sources,
             cache_dir,
             android_sdk_version,
             srcjars,
             min_sdk_version,
             manifest_package,
             resource_sources,
             resource_zips,
             android_sdk_root,
             lint_gen_dir,
             testonly_target=False,
             can_fail_build=False,
             silent=False):
  logging.info('Lint starting')

  cmd = [
      _SrcRelative(lint_binary_path),
      # Consider all lint warnings as errors. Warnings should either always be
      # fixed or completely suppressed in suppressions.xml. They should not
      # bloat build output if they are not important enough to be fixed.
      '-Werror',
      '--exitcode',  # Sets error code if there are errors.
      '--quiet',  # Silences lint's "." progress updates.
  ]
  if config_path:
    cmd.extend(['--config', _SrcRelative(config_path)])
  if testonly_target:
    cmd.extend(['--disable', ','.join(_DISABLED_FOR_TESTS)])

  if not manifest_path:
    manifest_path = os.path.join(build_utils.DIR_SOURCE_ROOT, 'build',
                                 'android', 'AndroidManifest.xml')

  logging.info('Generating Android manifest file')
  android_manifest_tree = _GenerateAndroidManifest(manifest_path,
                                                   min_sdk_version,
                                                   manifest_package)
  # Include the rebased manifest_path in the lint generated path so that it is
  # clear in error messages where the original AndroidManifest.xml came from.
  lint_android_manifest_path = os.path.join(lint_gen_dir,
                                            _SrcRelative(manifest_path))
  logging.info('Writing xml file %s', lint_android_manifest_path)
  _WriteXmlFile(android_manifest_tree.getroot(), lint_android_manifest_path)

  resource_root_dir = os.path.join(lint_gen_dir, _RES_ZIP_DIR)
  # These are zip files with generated resources (e. g. strings from GRD).
  logging.info('Extracting resource zips')
  for resource_zip in resource_zips:
    # Use a consistent root and name rather than a temporary file so that
    # suppressions can be local to the lint target and the resource target.
    resource_dir = os.path.join(resource_root_dir, resource_zip)
    shutil.rmtree(resource_dir, True)
    os.makedirs(resource_dir)
    resource_sources.extend(
        build_utils.ExtractAll(resource_zip, path=resource_dir))

  logging.info('Extracting srcjars')
  srcjar_root_dir = os.path.join(lint_gen_dir, _SRCJAR_DIR)
  srcjar_sources = []
  if srcjars:
    for srcjar in srcjars:
      # Use path without extensions since otherwise the file name includes
      # .srcjar and lint treats it as a srcjar.
      srcjar_dir = os.path.join(srcjar_root_dir, os.path.splitext(srcjar)[0])
      shutil.rmtree(srcjar_dir, True)
      os.makedirs(srcjar_dir)
      # Sadly lint's srcjar support is broken since it only considers the first
      # srcjar. Until we roll a lint version with that fixed, we need to extract
      # it ourselves.
      srcjar_sources.extend(build_utils.ExtractAll(srcjar, path=srcjar_dir))

  logging.info('Generating project file')
  project_file_root = _GenerateProjectFile(lint_android_manifest_path,
                                           android_sdk_root, cache_dir, sources,
                                           srcjar_sources, resource_sources,
                                           android_sdk_version)

  project_xml_path = os.path.join(lint_gen_dir, 'project.xml')
  logging.info('Writing xml file %s', project_xml_path)
  _WriteXmlFile(project_file_root, project_xml_path)
  cmd += ['--project', _SrcRelative(project_xml_path)]

  logging.info('Preparing environment variables')
  env = os.environ.copy()
  # It is important that lint uses the checked-in JDK11 as it is almost 50%
  # faster than JDK8.
  env['JAVA_HOME'] = os.path.relpath(build_utils.JAVA_HOME,
                                     build_utils.DIR_SOURCE_ROOT)
  # This filter is necessary for JDK11.
  stderr_filter = build_utils.FilterReflectiveAccessJavaWarnings

  try:
    logging.debug('Lint command %s', cmd)
    start = time.time()
    # Lint outputs "No issues found" if it succeeds, and uses stderr when it
    # fails, so we can safely ignore stdout.
    build_utils.CheckOutput(cmd,
                            cwd=build_utils.DIR_SOURCE_ROOT,
                            env=env,
                            stderr_filter=stderr_filter)
    end = time.time() - start
    logging.info('Lint command took %ss', end)
  except build_utils.CalledProcessError as e:
    if not silent:
      print('Lint found new issues.\n'
            ' - Here is the project.xml file passed to lint: {}\n'
            ' - For more information about lint and how to fix lint issues,'
            ' please refer to {}\n'.format(_SrcRelative(project_xml_path),
                                           _LINT_MD_URL))
    if can_fail_build:
      raise
    else:
      print(e)
  else:
    # Lint succeeded, no need to keep generated files for debugging purposes.
    shutil.rmtree(resource_root_dir, ignore_errors=True)
    shutil.rmtree(srcjar_root_dir, ignore_errors=True)

  logging.info('Lint completed')


def _ParseArgs(argv):
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--lint-binary-path',
                      required=True,
                      help='Path to lint executable.')
  parser.add_argument('--cache-dir',
                      required=True,
                      help='Path to the directory in which the android cache '
                      'directory tree should be stored.')
  parser.add_argument('--config-path', help='Path to lint suppressions file.')
  parser.add_argument('--lint-gen-dir',
                      required=True,
                      help='Path to store generated xml files.')
  parser.add_argument('--stamp', help='Path to stamp upon success.')
  parser.add_argument('--android-sdk-version',
                      help='Version (API level) of the Android SDK used for '
                      'building.')
  parser.add_argument('--min-sdk-version',
                      required=True,
                      help='Minimal SDK version to lint against.')
  parser.add_argument('--android-sdk-root',
                      required=True,
                      help='Lint needs an explicit path to the android sdk.')
  parser.add_argument('--testonly',
                      action='store_true',
                      help='If set, some checks like UnusedResources will be '
                      'disabled since they are not helpful for test '
                      'targets.')
  parser.add_argument('--manifest-package',
                      help='Package name of the AndroidManifest.xml.')
  parser.add_argument('--can-fail-build',
                      action='store_true',
                      help='If set, script will exit with nonzero exit status'
                      ' if lint errors are present')
  parser.add_argument('--silent',
                      action='store_true',
                      help='If set, script will not log anything.')
  parser.add_argument('--java-sources',
                      help='File containing a list of java sources files.')
  parser.add_argument('--srcjars', help='GN list of included srcjars.')
  parser.add_argument('--manifest-path',
                      help='Path to original AndroidManifest.xml')
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

  _RunLint(args.lint_binary_path,
           args.config_path,
           args.manifest_path,
           sources,
           args.cache_dir,
           args.android_sdk_version,
           args.srcjars,
           args.min_sdk_version,
           args.manifest_package,
           resource_sources,
           args.resource_zips,
           args.android_sdk_root,
           args.lint_gen_dir,
           testonly_target=args.testonly,
           can_fail_build=args.can_fail_build,
           silent=args.silent)
  logging.info('Creating stamp file')
  build_utils.Touch(args.stamp)

  if args.depfile:
    build_utils.WriteDepfile(args.depfile, args.stamp, depfile_deps)


if __name__ == '__main__':
  sys.exit(main())
