#!/usr/bin/env python

# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Merges dependency Android manifests into a root manifest."""

import argparse
import contextlib
import os
import shlex
import sys
import tempfile
import xml.dom.minidom as minidom
import xml.etree.ElementTree as ElementTree

from util import build_utils
from util import diff_utils

# Tools library directory - relative to Android SDK root
_SDK_TOOLS_LIB_DIR = os.path.join('tools', 'lib')

_MANIFEST_MERGER_MAIN_CLASS = 'com.android.manifmerger.Merger'
_MANIFEST_MERGER_JARS = [
    'common{suffix}.jar',
    'manifest-merger{suffix}.jar',
    'sdk-common{suffix}.jar',
    'sdklib{suffix}.jar',
]

_TOOLS_NAMESPACE_PREFIX = 'tools'
_TOOLS_NAMESPACE = 'http://schemas.android.com/tools'
_ANDROID_NAMESPACE = 'http://schemas.android.com/apk/res/android'

# Without registering namespaces ElementTree converts them to "ns0" and "ns1"
ElementTree.register_namespace('tools', _TOOLS_NAMESPACE)
ElementTree.register_namespace('android', _ANDROID_NAMESPACE)


@contextlib.contextmanager
def _ProcessManifest(manifest_path):
  """Patches an Android manifest to always include the 'tools' namespace
  declaration, as it is not propagated by the manifest merger from the SDK.

  See https://issuetracker.google.com/issues/63411481
  """
  doc = minidom.parse(manifest_path)
  manifests = doc.getElementsByTagName('manifest')
  assert len(manifests) == 1
  manifest = manifests[0]
  package = manifest.getAttribute('package')

  manifest.setAttribute('xmlns:%s' % _TOOLS_NAMESPACE_PREFIX, _TOOLS_NAMESPACE)

  tmp_prefix = os.path.basename(manifest_path)
  with tempfile.NamedTemporaryFile(prefix=tmp_prefix) as patched_manifest:
    doc.writexml(patched_manifest)
    patched_manifest.flush()
    yield patched_manifest.name, package


def _BuildManifestMergerClasspath(build_vars):
  return ':'.join([
      os.path.join(
          build_vars['android_sdk_root'], _SDK_TOOLS_LIB_DIR,
          jar.format(suffix=build_vars['android_sdk_tools_version_suffix']))
      for jar in _MANIFEST_MERGER_JARS
  ])


def _SortAndStripElementTree(tree, reverse_toplevel=False):
  for node in tree:
    if node.text and node.text.isspace():
      node.text = None
    _SortAndStripElementTree(node)
  tree[:] = sorted(tree, key=ElementTree.tostring, reverse=reverse_toplevel)


def _NormalizeManifest(path):
  with open(path) as f:
    # This also strips comments and sorts node attributes alphabetically.
    root = ElementTree.fromstring(f.read())

  # Sort nodes alphabetically, recursively.
  _SortAndStripElementTree(root, reverse_toplevel=True)

  # Fix up whitespace/indentation.
  dom = minidom.parseString(ElementTree.tostring(root))
  lines = []
  for l in dom.toprettyxml(indent='  ').splitlines():
    if l.strip():
      if len(l) > 100:
        indent = ' ' * l.find('<')
        attributes = shlex.split(l, posix=False)
        lines.append('{}{}'.format(indent, attributes[0]))
        for attribute in attributes[1:]:
          lines.append('{}    {}'.format(indent, attribute))
      else:
        lines.append(l)

  return '\n'.join(lines)


def main(argv):
  argv = build_utils.ExpandFileArgs(argv)
  parser = argparse.ArgumentParser(description=__doc__)
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--build-vars',
                      help='Path to GN build vars file',
                      required=True)
  parser.add_argument('--root-manifest',
                      help='Root manifest which to merge into',
                      required=True)
  parser.add_argument(
      '--expected-manifest', help='Expected contents for the merged manifest.')
  parser.add_argument('--normalized-output', help='Normalized merged manifest.')
  parser.add_argument(
      '--verify-expected-manifest',
      action='store_true',
      help='Fail if expected contents do not match merged manifest contents.')
  parser.add_argument('--output', help='Output manifest path', required=True)
  parser.add_argument('--extras',
                      help='GN list of additional manifest to merge')
  args = parser.parse_args(argv)

  classpath = _BuildManifestMergerClasspath(
      build_utils.ReadBuildVars(args.build_vars))

  with build_utils.AtomicOutput(args.output) as output:
    cmd = [
        'java',
        '-cp',
        classpath,
        _MANIFEST_MERGER_MAIN_CLASS,
        '--out',
        output.name,
    ]

    extras = build_utils.ParseGnList(args.extras)
    if extras:
      cmd += ['--libs', ':'.join(extras)]

    with _ProcessManifest(args.root_manifest) as tup:
      root_manifest, package = tup
      cmd += ['--main', root_manifest, '--property', 'PACKAGE=' + package]
      build_utils.CheckOutput(cmd,
        # https://issuetracker.google.com/issues/63514300:
        # The merger doesn't set a nonzero exit code for failures.
        fail_func=lambda returncode, stderr: returncode != 0 or
          build_utils.IsTimeStale(output.name, [root_manifest] + extras))

  if args.expected_manifest:
    with build_utils.AtomicOutput(args.normalized_output) as normalized_output:
      normalized_output.write(_NormalizeManifest(args.output))
    msg = diff_utils.DiffFileContents(args.expected_manifest,
                                      args.normalized_output)
    if msg:
      sys.stderr.write("""\
AndroidManifest.xml expectations file needs updating. For details see:
https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/android/java/README.md
""")
      sys.stderr.write(msg)
      if args.verify_expected_manifest:
        sys.exit(1)

  if args.depfile:
    inputs = extras + classpath.split(':')
    build_utils.WriteDepfile(args.depfile, args.output, inputs=inputs,
                             add_pydeps=False)


if __name__ == '__main__':
  main(sys.argv[1:])
