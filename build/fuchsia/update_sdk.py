#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Updates the Fuchsia SDK to the given revision. Should be used in a 'hooks_os'
entry so that it only runs when .gclient's target_os includes 'fuchsia'."""

import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

REPOSITORY_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))
sys.path.append(os.path.join(REPOSITORY_ROOT, 'build'))

import find_depot_tools


def EnsureDirExists(path):
  if not os.path.exists(path):
    print 'Creating directory %s' % path
    os.makedirs(path)


def main():
  if len(sys.argv) != 2:
    print >>sys.stderr, 'usage: %s <sdk_hash>' % sys.argv[0]
    return 1

  sdk_hash = sys.argv[1]
  output_dir = os.path.join(REPOSITORY_ROOT, 'third_party', 'fuchsia-sdk')

  hash_filename = os.path.join(output_dir, '.hash')
  if os.path.exists(hash_filename):
    with open(hash_filename, 'r') as f:
      if f.read().strip() == sdk_hash:
        # Nothing to do.
        return 0

  print 'Downloading SDK %s...' % sdk_hash

  if os.path.isdir(output_dir):
    shutil.rmtree(output_dir)

  bucket = 'gs://fuchsia/sdk/linux-amd64/'
  with tempfile.NamedTemporaryFile() as f:
    cmd = [os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gsutil.py'),
           'cp', bucket + sdk_hash, f.name]
    subprocess.check_call(cmd)
    f.seek(0)
    EnsureDirExists(output_dir)
    tarfile.open(mode='r:gz', fileobj=f).extractall(path=output_dir)

  with open(hash_filename, 'w') as f:
    f.write(sdk_hash)

  return 0


if __name__ == '__main__':
  sys.exit(main())
