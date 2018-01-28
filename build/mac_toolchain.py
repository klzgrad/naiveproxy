#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
If should_use_hermetic_xcode.py emits "1", and the current toolchain is out of
date:
  * Downloads the hermetic mac toolchain
    * Requires gsutil to be configured.
  * Accepts the license.
    * If xcode-select and xcodebuild are not passwordless in sudoers, requires
      user interaction.

The toolchain version can be overridden by setting IOS_TOOLCHAIN_REVISION or
MAC_TOOLCHAIN_REVISION with the full revision, e.g. 9A235-1.
"""

from distutils.version import LooseVersion
import os
import platform
import plistlib
import shutil
import subprocess
import sys
import tarfile
import time
import tempfile
import urllib2

# This can be changed after running /build/package_mac_toolchain.py.
MAC_TOOLCHAIN_VERSION = '8E2002'
MAC_TOOLCHAIN_SUB_REVISION = 3
MAC_TOOLCHAIN_VERSION = '%s-%s' % (MAC_TOOLCHAIN_VERSION,
                                   MAC_TOOLCHAIN_SUB_REVISION)
# The toolchain will not be downloaded if the minimum OS version is not met.
# 16 is the major version number for macOS 10.12.
MAC_MINIMUM_OS_VERSION = 16

IOS_TOOLCHAIN_VERSION = '9A235'
IOS_TOOLCHAIN_SUB_REVISION = 1
IOS_TOOLCHAIN_VERSION = '%s-%s' % (IOS_TOOLCHAIN_VERSION,
                                   IOS_TOOLCHAIN_SUB_REVISION)

# Absolute path to src/ directory.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Absolute path to a file with gclient solutions.
GCLIENT_CONFIG = os.path.join(os.path.dirname(REPO_ROOT), '.gclient')

BASE_DIR = os.path.abspath(os.path.dirname(__file__))
TOOLCHAIN_BUILD_DIR = os.path.join(BASE_DIR, '%s_files', 'Xcode.app')
STAMP_FILE = os.path.join(BASE_DIR, '%s_files', 'toolchain_build_revision')
TOOLCHAIN_URL = 'gs://chrome-mac-sdk/'


def PlatformMeetsHermeticXcodeRequirements(target_os):
  if target_os == 'ios':
    return True
  return int(platform.release().split('.')[0]) >= MAC_MINIMUM_OS_VERSION


def GetPlatforms():
  target_os = set(['mac'])
  try:
    env = {}
    execfile(GCLIENT_CONFIG, env, env)
    target_os |= set(env.get('target_os', target_os))
  except:
    pass
  return target_os


def ReadStampFile(target_os):
  """Return the contents of the stamp file, or '' if it doesn't exist."""
  try:
    with open(STAMP_FILE % target_os, 'r') as f:
      return f.read().rstrip()
  except IOError:
    return ''


def WriteStampFile(target_os, s):
  """Write s to the stamp file."""
  EnsureDirExists(os.path.dirname(STAMP_FILE % target_os))
  with open(STAMP_FILE % target_os, 'w') as f:
    f.write(s)
    f.write('\n')


def EnsureDirExists(path):
  if not os.path.exists(path):
    os.makedirs(path)


def DownloadAndUnpack(url, output_dir):
  """Decompresses |url| into a cleared |output_dir|."""
  temp_name = tempfile.mktemp(prefix='mac_toolchain')
  try:
    print 'Downloading new toolchain.'
    subprocess.check_call(['gsutil.py', 'cp', url, temp_name])
    if os.path.exists(output_dir):
      print 'Deleting old toolchain.'
      shutil.rmtree(output_dir)
    EnsureDirExists(output_dir)
    print 'Unpacking new toolchain.'
    tarfile.open(mode='r:gz', name=temp_name).extractall(path=output_dir)
  finally:
    if os.path.exists(temp_name):
      os.unlink(temp_name)


def CanAccessToolchainBucket():
  """Checks whether the user has access to |TOOLCHAIN_URL|."""
  proc = subprocess.Popen(['gsutil.py', 'ls', TOOLCHAIN_URL],
                           stdout=subprocess.PIPE)
  proc.communicate()
  return proc.returncode == 0


def LoadPlist(path):
  """Loads Plist at |path| and returns it as a dictionary."""
  fd, name = tempfile.mkstemp()
  try:
    subprocess.check_call(['plutil', '-convert', 'xml1', '-o', name, path])
    with os.fdopen(fd, 'r') as f:
      return plistlib.readPlist(f)
  finally:
    os.unlink(name)


def FinalizeUnpack(output_dir, target_os):
  """Use xcodebuild to accept new toolchain license and run first launch
  installers if necessary.  Don't accept the license if a newer license has
  already been accepted. This only works if xcodebuild and xcode-select are
  passwordless in sudoers."""

  # Check old license
  try:
    target_license_plist_path = os.path.join(
        output_dir, 'Contents','Resources','LicenseInfo.plist')
    target_license_plist = LoadPlist(target_license_plist_path)
    build_type = target_license_plist['licenseType']
    build_version = target_license_plist['licenseID']

    accepted_license_plist = LoadPlist(
        '/Library/Preferences/com.apple.dt.Xcode.plist')
    agreed_to_key = 'IDELast%sLicenseAgreedTo' % build_type
    last_license_agreed_to = accepted_license_plist[agreed_to_key]

    # Historically all Xcode build numbers have been in the format of AANNNN, so
    # a simple string compare works.  If Xcode's build numbers change this may
    # need a more complex compare.
    if build_version <= last_license_agreed_to:
      # Don't accept the license of older toolchain builds, this will break the
      # license of newer builds.
      return
  except (subprocess.CalledProcessError, KeyError):
    # If there's never been a license of type |build_type| accepted,
    # |target_license_plist_path| or |agreed_to_key| may not exist.
    pass

  print "Accepting license."
  target_version_plist_path = os.path.join(
      output_dir, 'Contents','version.plist')
  target_version_plist = LoadPlist(target_version_plist_path)
  short_version_string = target_version_plist['CFBundleShortVersionString']
  old_path = subprocess.Popen(['/usr/bin/xcode-select', '-p'],
                               stdout=subprocess.PIPE).communicate()[0].strip()
  try:
    build_dir = os.path.join(output_dir, 'Contents/Developer')
    subprocess.check_call(['sudo', '/usr/bin/xcode-select', '-s', build_dir])
    subprocess.check_call(['sudo', '/usr/bin/xcodebuild', '-license', 'accept'])

    if target_os == 'ios' and \
        LooseVersion(short_version_string) >= LooseVersion("9.0"):
      print "Installing packages."
      subprocess.check_call(['sudo', '/usr/bin/xcodebuild', '-runFirstLaunch'])
  finally:
    subprocess.check_call(['sudo', '/usr/bin/xcode-select', '-s', old_path])


def _UseHermeticToolchain(target_os):
  current_dir = os.path.dirname(os.path.realpath(__file__))
  script_path = os.path.join(current_dir, 'mac/should_use_hermetic_xcode.py')
  proc = subprocess.Popen([script_path, target_os], stdout=subprocess.PIPE)
  return '1' in proc.stdout.readline()


def RequestGsAuthentication():
  """Requests that the user authenticate to be able to access gs://.
  """
  print 'Access to ' + TOOLCHAIN_URL + ' not configured.'
  print '-----------------------------------------------------------------'
  print
  print 'You appear to be a Googler.'
  print
  print 'I\'m sorry for the hassle, but you need to do a one-time manual'
  print 'authentication. Please run:'
  print
  print '    download_from_google_storage --config'
  print
  print 'and follow the instructions.'
  print
  print 'NOTE 1: Use your google.com credentials, not chromium.org.'
  print 'NOTE 2: Enter 0 when asked for a "project-id".'
  print
  print '-----------------------------------------------------------------'
  print
  sys.stdout.flush()
  sys.exit(1)


def DownloadHermeticBuild(target_os, toolchain_version, toolchain_filename):
  if not _UseHermeticToolchain(target_os):
    return 0

  toolchain_output_path = TOOLCHAIN_BUILD_DIR % target_os
  if ReadStampFile(target_os) == toolchain_version:
    FinalizeUnpack(toolchain_output_path, target_os)
    return 0

  if not CanAccessToolchainBucket():
    RequestGsAuthentication()
    return 1

  # Reset the stamp file in case the build is unsuccessful.
  WriteStampFile(target_os, '')

  toolchain_file = '%s.tgz' % toolchain_version
  toolchain_full_url = TOOLCHAIN_URL + toolchain_file

  print 'Updating toolchain to %s...' % toolchain_version
  try:
    toolchain_file = toolchain_filename % toolchain_version
    toolchain_full_url = TOOLCHAIN_URL + toolchain_file
    DownloadAndUnpack(toolchain_full_url, toolchain_output_path)
    FinalizeUnpack(toolchain_output_path, target_os)

    print 'Toolchain %s unpacked.' % toolchain_version
    WriteStampFile(target_os, toolchain_version)
    return 0
  except Exception as e:
    print 'Failed to download toolchain %s.' % toolchain_file
    print 'Exception %s' % e
    print 'Exiting.'
    return 1


def main():
  if sys.platform != 'darwin':
    return 0

  for target_os in GetPlatforms():
    if not PlatformMeetsHermeticXcodeRequirements(target_os):
      print 'OS version does not support toolchain.'
      continue

    if target_os == 'ios':
      toolchain_version = os.environ.get('IOS_TOOLCHAIN_REVISION',
                                          IOS_TOOLCHAIN_VERSION)
      toolchain_filename = 'ios-toolchain-%s.tgz'
    else:
      toolchain_version = os.environ.get('MAC_TOOLCHAIN_REVISION',
                                          MAC_TOOLCHAIN_VERSION)
      toolchain_filename = 'toolchain-%s.tgz'

    return_value = DownloadHermeticBuild(
        target_os, toolchain_version, toolchain_filename)
    if return_value:
      return return_value

  return 0


if __name__ == '__main__':
  sys.exit(main())
