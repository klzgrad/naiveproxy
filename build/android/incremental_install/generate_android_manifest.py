#!/usr/bin/env python
#
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Creates an AndroidManifest.xml for an incremental APK.

Given the manifest file for the real APK, generates an AndroidManifest.xml with
the application class changed to IncrementalApplication.
"""

import argparse
import os
import sys
from xml.etree import ElementTree

sys.path.append(os.path.join(os.path.dirname(__file__), os.path.pardir, 'gyp'))
from util import build_utils

_ANDROID_NAMESPACE = 'http://schemas.android.com/apk/res/android'
ElementTree.register_namespace('android', _ANDROID_NAMESPACE)

_INCREMENTAL_APP_NAME = 'org.chromium.incrementalinstall.BootstrapApplication'
_META_DATA_APP_NAME = 'incremental-install-real-app'
_DEFAULT_APPLICATION_CLASS = 'android.app.Application'
_META_DATA_INSTRUMENTATION_NAMES = [
    'incremental-install-real-instrumentation-0',
    'incremental-install-real-instrumentation-1',
]
_INCREMENTAL_INSTRUMENTATION_CLASSES = [
    'android.app.Instrumentation',
    'org.chromium.incrementalinstall.SecondInstrumentation',
]


def _AddNamespace(name):
  """Adds the android namespace prefix to the given identifier."""
  return '{%s}%s' % (_ANDROID_NAMESPACE, name)

def _ParseArgs():
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--src-manifest',
                      help='The main manifest of the app',
                      required=True)
  parser.add_argument('--out-manifest',
                      help='The output manifest',
                      required=True)
  parser.add_argument('--disable-isolated-processes',
                      help='Changes all android:isolatedProcess to false. '
                           'This is required on Android M+',
                      action='store_true')
  return parser.parse_args()


def _CreateMetaData(parent, name, value):
  meta_data_node = ElementTree.SubElement(parent, 'meta-data')
  meta_data_node.set(_AddNamespace('name'), name)
  meta_data_node.set(_AddNamespace('value'), value)


def _ProcessManifest(main_manifest, disable_isolated_processes):
  """Returns a transformed AndroidManifest.xml for use with _incremental apks.

  Args:
    main_manifest: Manifest contents to transform.
    disable_isolated_processes: Whether to set all isolatedProcess attributes to
        false

  Returns:
    The transformed AndroidManifest.xml.
  """
  if disable_isolated_processes:
    main_manifest = main_manifest.replace('isolatedProcess="true"',
                                          'isolatedProcess="false"')

  # Disable check for page-aligned native libraries.
  main_manifest = main_manifest.replace('extractNativeLibs="false"',
                                        'extractNativeLibs="true"')

  doc = ElementTree.fromstring(main_manifest)
  app_node = doc.find('application')
  if app_node is None:
    app_node = ElementTree.SubElement(doc, 'application')

  real_app_class = app_node.get(_AddNamespace('name'),
                                _DEFAULT_APPLICATION_CLASS)
  app_node.set(_AddNamespace('name'), _INCREMENTAL_APP_NAME)
  _CreateMetaData(app_node, _META_DATA_APP_NAME, real_app_class)

  # Seems to be a bug in ElementTree, as doc.find() doesn't work here.
  instrumentation_nodes = doc.findall('instrumentation')
  assert len(instrumentation_nodes) <= 2, (
      'Need to update incremental install to support >2 <instrumentation> tags')
  for i, instrumentation_node in enumerate(instrumentation_nodes):
    real_instrumentation_class = instrumentation_node.get(_AddNamespace('name'))
    instrumentation_node.set(_AddNamespace('name'),
                             _INCREMENTAL_INSTRUMENTATION_CLASSES[i])
    _CreateMetaData(app_node, _META_DATA_INSTRUMENTATION_NAMES[i],
                    real_instrumentation_class)

  return ElementTree.tostring(doc, encoding='UTF-8')


def main():
  options = _ParseArgs()
  with open(options.src_manifest) as f:
    main_manifest_data = f.read()
  new_manifest_data = _ProcessManifest(main_manifest_data,
                                       options.disable_isolated_processes)
  with open(options.out_manifest, 'w') as f:
    f.write(new_manifest_data)

  if options.depfile:
    deps = [options.src_manifest]
    build_utils.WriteDepfile(options.depfile, options.out_manifest, deps)


if __name__ == '__main__':
  main()
