#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Updates the Fuchsia SDK to the given revision. Should be used in a 'hooks_os'
entry so that it only runs when .gclient's target_os includes 'fuchsia'."""

from __future__ import print_function

import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

from common import GetHostOsFromPlatform, GetHostArchFromPlatform

REPOSITORY_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))
sys.path.append(os.path.join(REPOSITORY_ROOT, 'build'))

import find_depot_tools

SDK_SUBDIRS = ["arch", "pkg", "qemu", "sysroot", "target",
               "toolchain_libs", "tools"]

EXTRA_SDK_HASH_PREFIX = ''

def GetSdkGeneration(hash):
  if not hash:
    return None

  cmd = [os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gsutil.py'), 'ls',
         '-L', GetBucketForPlatform() + hash]
  sdk_details = subprocess.check_output(cmd)
  m = re.search('Generation:\s*(\d*)', sdk_details)
  if not m:
    return None
  return int(m.group(1))


def GetSdkHashForPlatform():
  filename = '{platform}.sdk.sha1'.format(platform =  GetHostOsFromPlatform())

  # Get the hash of the SDK in chromium.
  sdk_hash = None
  hash_file = os.path.join(os.path.dirname(__file__), filename)
  with open(hash_file, 'r') as f:
    sdk_hash = f.read().strip()

  # Get the hash of the SDK with the extra prefix.
  extra_sdk_hash = None
  if EXTRA_SDK_HASH_PREFIX:
    extra_hash_file = os.path.join(os.path.dirname(__file__),
                                   EXTRA_SDK_HASH_PREFIX + filename)
    with open(extra_hash_file, 'r') as f:
      extra_sdk_hash = f.read().strip()

  # If both files are empty, return an error.
  if not sdk_hash and not extra_sdk_hash:
    print(
        'No SHA1 found in {} or {}'.format(hash_file, extra_hash_file),
        file=sys.stderr)
    return 1

  # Return the newer SDK based on the generation number.
  sdk_generation = GetSdkGeneration(sdk_hash)
  extra_sdk_generation = GetSdkGeneration(extra_sdk_hash)
  if extra_sdk_generation > sdk_generation:
    return extra_sdk_hash
  return sdk_hash

def GetBucketForPlatform():
  return 'gs://fuchsia/sdk/core/{platform}-amd64/'.format(
      platform = GetHostOsFromPlatform())


def EnsureDirExists(path):
  if not os.path.exists(path):
    print('Creating directory %s' % path)
    os.makedirs(path)


# Removes previous SDK from the specified path if it's detected there.
def Cleanup(path):
  hash_file = os.path.join(path, '.hash')
  if os.path.exists(hash_file):
    print('Removing old SDK from %s.' % path)
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
    print('usage: %s' % sys.argv[0], file=sys.stderr)
    return 1

  # Quietly exit if there's no SDK support for this platform.
  try:
    GetHostOsFromPlatform()
  except:
    return 0

  # Previously SDK was unpacked in //third_party/fuchsia-sdk instead of
  # //third_party/fuchsia-sdk/sdk . Remove the old files if they are still
  # there.
  sdk_root = os.path.join(REPOSITORY_ROOT, 'third_party', 'fuchsia-sdk')
  Cleanup(sdk_root)

  sdk_hash = GetSdkHashForPlatform()
  if not sdk_hash:
    return 1

  output_dir = os.path.join(sdk_root, 'sdk')

  hash_filename = os.path.join(output_dir, '.hash')
  if os.path.exists(hash_filename):
    with open(hash_filename, 'r') as f:
      if f.read().strip() == sdk_hash:
        # Nothing to do. Generate sdk/BUILD.gn anyways, in case the conversion
        # script changed.
        subprocess.check_call([os.path.join(sdk_root, 'gen_build_defs.py')])
        return 0

  print('Downloading SDK %s...' % sdk_hash)

  if os.path.isdir(output_dir):
    shutil.rmtree(output_dir)

  fd, tmp = tempfile.mkstemp()
  os.close(fd)

  try:
    cmd = [os.path.join(find_depot_tools.DEPOT_TOOLS_PATH, 'gsutil.py'),
           'cp', GetBucketForPlatform() + sdk_hash, tmp]
    subprocess.check_call(cmd)
    with open(tmp, 'rb') as f:
      EnsureDirExists(output_dir)
      tarfile.open(mode='r:gz', fileobj=f).extractall(path=output_dir)
  finally:
    os.remove(tmp)

  # Generate sdk/BUILD.gn.
  subprocess.check_call([os.path.join(sdk_root, 'gen_build_defs.py')])

  with open(hash_filename, 'w') as f:
    f.write(sdk_hash)

  UpdateTimestampsRecursive(output_dir)

  return 0


if __name__ == '__main__':
  sys.exit(main())
