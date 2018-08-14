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

SDK_HASH_FILE = os.path.join(os.path.dirname(__file__), 'sdk.sha1')

REPOSITORY_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))
sys.path.append(os.path.join(REPOSITORY_ROOT, 'build'))

import find_depot_tools

SDK_SUBDIRS = ["arch", "pkg", "qemu", "sysroot", "target",
               "toolchain_libs", "tools"]


def EnsureDirExists(path):
  if not os.path.exists(path):
    print 'Creating directory %s' % path
    os.makedirs(path)


# Removes previous SDK from the specified path if it's detected there.
def Cleanup(path):
  hash_file = os.path.join(path, '.hash')
  if os.path.exists(hash_file):
    print 'Removing old SDK from %s.' % path
    for d in SDK_SUBDIRS:
      to_remove = os.path.join(path, d)
      if os.path.isdir(to_remove):
        shutil.rmtree(to_remove)
    os.remove(hash_file)


# Updates the modification timestamps of |path| and its contents to the
# current time.
def UpdateTimestampsRecursive(path):
  for root, dirs, files in os.walk(path):
    for f in files:
      os.utime(os.path.join(root, f), None)
    for d in dirs:
      os.utime(os.path.join(root, d), None)


def main():
  if len(sys.argv) != 1:
    print >>sys.stderr, 'usage: %s' % sys.argv[0]
    return 1

  # Previously SDK was unpacked in //third_party/fuchsia-sdk instead of
  # //third_party/fuchsia-sdk/sdk . Remove the old files if they are still
  # there.
  Cleanup(os.path.join(REPOSITORY_ROOT, 'third_party', 'fuchsia-sdk'))

  with open(SDK_HASH_FILE, 'r') as f:
    sdk_hash = f.read().strip()

  if not sdk_hash:
    print >>sys.stderr, 'No SHA1 found in %s' % SDK_HASH_FILE
    return 1

  output_dir = os.path.join(REPOSITORY_ROOT, 'third_party', 'fuchsia-sdk',
                            'sdk')

  hash_filename = os.path.join(output_dir, '.hash')
  if os.path.exists(hash_filename):
    with open(hash_filename, 'r') as f:
      if f.read().strip() == sdk_hash:
        # Nothing to do.
        return 0

  print 'Downloading SDK %s...' % sdk_hash

  if os.path.isdir(output_dir):
    shutil.rmtree(output_dir)

  fd, tmp = tempfile.mkstemp()
  os.close(fd)

  try:
    bucket = 'gs://fuchsia/sdk/linux-amd64/'
    cmd = [os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gsutil.py'),
           'cp', bucket + sdk_hash, tmp]
    subprocess.check_call(cmd)
    with open(tmp, 'rb') as f:
      EnsureDirExists(output_dir)
      tarfile.open(mode='r:gz', fileobj=f).extractall(path=output_dir)
  finally:
    os.remove(tmp)

  with open(hash_filename, 'w') as f:
    f.write(sdk_hash)

  UpdateTimestampsRecursive(output_dir)

  return 0


if __name__ == '__main__':
  sys.exit(main())
