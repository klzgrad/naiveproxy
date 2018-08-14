#!/usr/bin/env python
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Update the Google Cloud Storage bucket hosting the Super Size UI."""

import argparse
import os
import subprocess
import uuid


GS_BUCKET = 'gs://chrome-supersize'


def _SyncStatic():
  """Upload static files from the static directory."""
  static_files = os.path.join(os.path.dirname(__file__), 'static')
  subprocess.check_call([
    'gsutil.py', '-m', 'rsync', '-r', static_files, GS_BUCKET
  ])


def _SyncTemplates():
  """Generate and upload the templates/sw.js file."""
  template_file = os.path.join(os.path.dirname(__file__), 'templates', 'sw.js')
  cache_hash = uuid.uuid4().hex

  p = subprocess.Popen([
    'gsutil.py', 'cp', '-p', '-', '%s/sw.js' % GS_BUCKET
  ], stdin=subprocess.PIPE)
  with open(template_file, 'r') as in_file:
    p.communicate(in_file.read().replace('{{cache_hash}}', cache_hash))


def _SetMetaAndPermissions():
  # sw.js has the wrong type due to being created from a stream
  subprocess.check_call([
    'gsutil.py', 'setmeta', '-h', 'Content-Type:application/javascript',
    '%s/sw.js' % GS_BUCKET
  ])

  # All files in the root of the bucket are user readable
  subprocess.check_call([
    'gsutil.py', '-m', 'acl', 'ch', '-u', 'AllUsers:R', '%s/*' % GS_BUCKET
  ])


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--sync', action='store_true', required=True,
                      help='Sync static and template files to GCS.')

  args = parser.parse_args()

  if args.sync:
    _SyncStatic()
    _SyncTemplates()
    _SetMetaAndPermissions()


if __name__ == '__main__':
  main()
