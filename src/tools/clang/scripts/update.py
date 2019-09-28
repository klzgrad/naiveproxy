#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is used to download prebuilt clang binaries. It runs as a
"gclient hook" in Chromium checkouts.

It can also be run stand-alone as a convenient way of installing a well-tested
near-tip-of-tree clang version:

  $ curl -s https://raw.githubusercontent.com/chromium/chromium/master/tools/clang/scripts/update.py | python - --clang-dir=.
"""

# TODO: Running stand-alone won't work on Windows due to the dia dll copying.

from __future__ import division
from __future__ import print_function
import argparse
import os
import shutil
import stat
import sys
import tarfile
import tempfile
import time

try:
  from urllib2 import HTTPError, URLError, urlopen
except ImportError: # For Py3 compatibility
  from urllib.error import HTTPError, URLError
  from urllib.request import urlopen

import zipfile


# Do NOT CHANGE this if you don't know what you're doing -- see
# https://chromium.googlesource.com/chromium/src/+/master/docs/updating_clang.md
# Reverting problematic clang rolls is safe, though.
CLANG_REVISION = 'f7e52fbdb5a7af8ea0808e98458b497125a5eca1'
CLANG_SVN_REVISION = '365097'
CLANG_SUB_REVISION = 8

PACKAGE_VERSION = '%s-%s-%s' % (CLANG_SVN_REVISION, CLANG_REVISION[:8],
                                CLANG_SUB_REVISION)
# TODO(crbug.com/985289): Bump when rolling past r366427.
RELEASE_VERSION = '9.0.0'


CDS_URL = os.environ.get('CDS_CLANG_BUCKET_OVERRIDE',
    'https://commondatastorage.googleapis.com/chromium-browser-clang')

# Path constants. (All of these should be absolute paths.)
THIS_DIR = os.path.abspath(os.path.dirname(__file__))
CHROMIUM_DIR = os.path.abspath(os.path.join(THIS_DIR, '..', '..', '..'))
LLVM_BUILD_DIR = os.path.join(CHROMIUM_DIR, 'third_party', 'llvm-build',
                              'Release+Asserts')

STAMP_FILE = os.path.normpath(
    os.path.join(LLVM_BUILD_DIR, 'cr_build_revision'))
FORCE_HEAD_REVISION_FILE = os.path.normpath(os.path.join(LLVM_BUILD_DIR, '..',
                                                   'force_head_revision'))


def RmTree(dir):
  """Delete dir."""
  def ChmodAndRetry(func, path, _):
    # Subversion can leave read-only files around.
    if not os.access(path, os.W_OK):
      os.chmod(path, stat.S_IWUSR)
      return func(path)
    raise
  shutil.rmtree(dir, onerror=ChmodAndRetry)


def ReadStampFile(path):
  """Return the contents of the stamp file, or '' if it doesn't exist."""
  try:
    with open(path, 'r') as f:
      return f.read().rstrip()
  except IOError:
    return ''


def WriteStampFile(s, path):
  """Write s to the stamp file."""
  EnsureDirExists(os.path.dirname(path))
  with open(path, 'w') as f:
    f.write(s)
    f.write('\n')


def DownloadUrl(url, output_file):
  """Download url into output_file."""
  CHUNK_SIZE = 4096
  TOTAL_DOTS = 10
  num_retries = 3
  retry_wait_s = 5  # Doubled at each retry.

  while True:
    try:
      sys.stdout.write('Downloading %s ' % url)
      sys.stdout.flush()
      response = urlopen(url)
      total_size = int(response.info().get('Content-Length').strip())
      bytes_done = 0
      dots_printed = 0
      while True:
        chunk = response.read(CHUNK_SIZE)
        if not chunk:
          break
        output_file.write(chunk)
        bytes_done += len(chunk)
        num_dots = TOTAL_DOTS * bytes_done // total_size
        sys.stdout.write('.' * (num_dots - dots_printed))
        sys.stdout.flush()
        dots_printed = num_dots
      if bytes_done != total_size:
        raise URLError("only got %d of %d bytes" %
                       (bytes_done, total_size))
      print(' Done.')
      return
    except URLError as e:
      sys.stdout.write('\n')
      print(e)
      if num_retries == 0 or isinstance(e, HTTPError) and e.code == 404:
        raise e
      num_retries -= 1
      print('Retrying in %d s ...' % retry_wait_s)
      sys.stdout.flush()
      time.sleep(retry_wait_s)
      retry_wait_s *= 2


def EnsureDirExists(path):
  if not os.path.exists(path):
    os.makedirs(path)


def DownloadAndUnpack(url, output_dir, path_prefix=None):
  """Download an archive from url and extract into output_dir. If path_prefix is
     not None, only extract files whose paths within the archive start with
     path_prefix."""
  with tempfile.TemporaryFile() as f:
    DownloadUrl(url, f)
    f.seek(0)
    EnsureDirExists(output_dir)
    if url.endswith('.zip'):
      assert path_prefix is None
      zipfile.ZipFile(f).extractall(path=output_dir)
    else:
      t = tarfile.open(mode='r:gz', fileobj=f)
      members = None
      if path_prefix is not None:
        members = [m for m in t.getmembers() if m.name.startswith(path_prefix)]
      t.extractall(path=output_dir, members=members)


def GetPlatformUrlPrefix(platform):
  if platform == 'win32' or platform == 'cygwin':
    return CDS_URL + '/Win/'
  if platform == 'darwin':
    return CDS_URL + '/Mac/'
  assert platform.startswith('linux')
  return CDS_URL + '/Linux_x64/'


def DownloadAndUnpackClangPackage(platform, output_dir, runtimes_only=False):
  cds_file = "clang-%s.tgz" %  PACKAGE_VERSION
  cds_full_url = GetPlatformUrlPrefix(platform) + cds_file
  try:
    path_prefix = None
    if runtimes_only:
      path_prefix = 'lib/clang/' + RELEASE_VERSION + '/lib/'
    DownloadAndUnpack(cds_full_url, output_dir, path_prefix)
  except URLError:
    print('Failed to download prebuilt clang %s' % cds_file)
    print('Use --force-local-build if you want to build locally.')
    print('Exiting.')
    sys.exit(1)


win_sdk_dir = None
dia_dll = None
def GetWinSDKDir():
  """Get the location of the current SDK. Sets dia_dll as a side-effect."""
  global win_sdk_dir
  global dia_dll
  if win_sdk_dir:
    return win_sdk_dir

  # Bump after VC updates.
  DIA_DLL = {
    '2013': 'msdia120.dll',
    '2015': 'msdia140.dll',
    '2017': 'msdia140.dll',
    '2019': 'msdia140.dll',
  }

  # Don't let vs_toolchain overwrite our environment.
  environ_bak = os.environ

  sys.path.append(os.path.join(CHROMIUM_DIR, 'build'))
  import vs_toolchain
  win_sdk_dir = vs_toolchain.SetEnvironmentAndGetSDKDir()
  msvs_version = vs_toolchain.GetVisualStudioVersion()

  if bool(int(os.environ.get('DEPOT_TOOLS_WIN_TOOLCHAIN', '1'))):
    dia_path = os.path.join(win_sdk_dir, '..', 'DIA SDK', 'bin', 'amd64')
  else:
    if 'GYP_MSVS_OVERRIDE_PATH' not in os.environ:
      vs_path = vs_toolchain.DetectVisualStudioPath()
    else:
      vs_path = os.environ['GYP_MSVS_OVERRIDE_PATH']
    dia_path = os.path.join(vs_path, 'DIA SDK', 'bin', 'amd64')

  dia_dll = os.path.join(dia_path, DIA_DLL[msvs_version])

  os.environ = environ_bak
  return win_sdk_dir


def CopyFile(src, dst):
  """Copy a file from src to dst."""
  print("Copying %s to %s" % (src, dst))
  shutil.copy(src, dst)


def CopyDiaDllTo(target_dir):
  # This script always wants to use the 64-bit msdia*.dll.
  GetWinSDKDir()
  CopyFile(dia_dll, target_dir)


def UpdateClang():
  GCLIENT_CONFIG = os.path.join(os.path.dirname(CHROMIUM_DIR), '.gclient')

  # Read target_os from .gclient so we know which non-native runtimes we need.
  # TODO(pcc): See if we can download just the runtimes instead of the entire
  # clang package, and do that from DEPS instead of here.
  target_os = []
  try:
    env = {}
    execfile(GCLIENT_CONFIG, env, env)
    target_os = env.get('target_os', target_os)
  except:
    pass

  expected_stamp = ','.join([PACKAGE_VERSION] + target_os)
  if ReadStampFile(STAMP_FILE) == expected_stamp:
    return 0

  if os.path.exists(LLVM_BUILD_DIR):
    RmTree(LLVM_BUILD_DIR)

  DownloadAndUnpackClangPackage(sys.platform, LLVM_BUILD_DIR)
  if 'win' in target_os:
    DownloadAndUnpackClangPackage('win32', LLVM_BUILD_DIR, runtimes_only=True)
  if sys.platform == 'win32':
    CopyDiaDllTo(os.path.join(LLVM_BUILD_DIR, 'bin'))
  WriteStampFile(expected_stamp, STAMP_FILE)

  return 0


def main():
  # TODO: Add an argument to download optional packages and remove the various
  # download_ scripts we currently have for that.

  parser = argparse.ArgumentParser(description='Update clang.')
  parser.add_argument('--clang-dir',
                      help='Where to extract the clang package.')
  parser.add_argument('--force-local-build', action='store_true',
                      help='(no longer used)')
  parser.add_argument('--print-revision', action='store_true',
                      help='Print current clang revision and exit.')
  parser.add_argument('--llvm-force-head-revision', action='store_true',
                      help='Print locally built revision with --print-revision')
  parser.add_argument('--print-clang-version', action='store_true',
                      help=('Print current clang release version (e.g. 9.0.0) '
                            'and exit.'))
  parser.add_argument('--verify-version',
                      help='Verify that clang has the passed-in version.')
  args = parser.parse_args()

  # TODO(crbug.com/985289): Remove when rolling past r366427.
  if args.llvm_force_head_revision:
    global RELEASE_VERSION
    RELEASE_VERSION = '10.0.0'

  if args.force_local_build:
    print(('update.py --force-local-build is no longer used to build clang; '
           'use build.py instead.'))
    return 1

  if args.verify_version and args.verify_version != RELEASE_VERSION:
    print('RELEASE_VERSION is %s but --verify-version argument was %s.' % (
        RELEASE_VERSION, args.verify_version))
    print('clang_version in build/toolchain/toolchain.gni is likely outdated.')
    return 1

  if args.print_clang_version:
    print(RELEASE_VERSION)
    return 0

  if args.print_revision:
    if args.llvm_force_head_revision:
      force_head_revision = ReadStampFile(FORCE_HEAD_REVISION_FILE)
      if force_head_revision == '':
        print('No locally built version found!')
        return 1
      print(force_head_revision)
      return 0

    print(PACKAGE_VERSION)
    return 0

  if args.llvm_force_head_revision:
    print('--llvm-force-head-revision can only be used for --print-revision')
    return 1

  if args.clang_dir:
    global LLVM_BUILD_DIR, STAMP_FILE
    LLVM_BUILD_DIR = os.path.abspath(args.clang_dir)
    STAMP_FILE = os.path.join(LLVM_BUILD_DIR, 'cr_build_revision')

  return UpdateClang()


if __name__ == '__main__':
  sys.exit(main())
