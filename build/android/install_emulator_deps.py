#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Installs deps for using SDK emulator for testing.

The script will download the SDK and system images, if they are not present, and
install and enable KVM, if virtualization has been enabled in the BIOS.
"""


import logging
import optparse
import os
import re
import sys

import devil_chromium
from devil.utils import cmd_helper
from devil.utils import run_tests_helper
from pylib import constants
from pylib import pexpect

# Android API level
DEFAULT_ANDROID_API_LEVEL = constants.ANDROID_SDK_VERSION
# Android ABI/Arch
DEFAULT_ABI = 'x86'

# Default Time out for downloading SDK component
DOWNLOAD_SYSTEM_IMAGE_TIMEOUT = 300
DOWNLOAD_SDK_PLATFORM_TIMEOUT = 300

def CheckSDK():
  """Check if SDK is already installed.

  Returns:
    True if the emulator SDK directory (src/android_emulator_sdk/) exists.
  """
  return os.path.exists(constants.ANDROID_SDK_ROOT)


def CheckSDKPlatform(api_level=DEFAULT_ANDROID_API_LEVEL, google=False):
  """Check if the "SDK Platform" for the specified API level is installed.
     This is necessary in order for the emulator to run when the target
     is specified.

  Args:
    abi: target abi, x86 or arm
    api_level: the Android API level to check; defaults to the latest API.
    google: use Google build system image instead of AOSP build

  Returns:
    True if the platform is already installed.
  """
  android_binary = os.path.join(constants.ANDROID_SDK_ROOT, 'tools', 'android')
  if google:
    pattern = re.compile('id: [0-9]+ or "Google Inc.:Google APIs:%s"' %
                         api_level)
  else:
    pattern = re.compile('id: [0-9]+ or "android-%d"' % api_level)

  try:
    exit_code, stdout = cmd_helper.GetCmdStatusAndOutput(
        [android_binary, 'list'])
    if exit_code != 0:
      raise Exception('\'android list\' command failed')
    for line in stdout.split('\n'):
      if pattern.match(line):
        return True
    return False
  except OSError:
    logging.exception('Unable to execute \'android list\'')
    return False


def CheckSystemImage(abi, api_level=DEFAULT_ANDROID_API_LEVEL, google=False):
  """Check if Android system images have been installed.

  Args:
    abi: target abi, x86 or arm
    api_level: the Android API level to check for; defaults to the latest API.
    google: use Google build system image instead of AOSP build

  Returns:
    True if x86 image has been previously downloaded.
  """
  api_target = 'android-%d' % api_level
  system_image_root = os.path.join(constants.ANDROID_SDK_ROOT,
                                   'system-images', api_target)
  if abi == 'x86':
    if google:
      return os.path.exists(os.path.join(system_image_root, 'google_apis',
                                         'x86'))
    else:
      return os.path.exists(os.path.join(system_image_root, 'default', 'x86'))
  elif abi == 'arm':
    if google:
      return os.path.exists(os.path.join(system_image_root, 'google_apis',
                                         'armeabi-v7a'))
    else:
      return os.path.exists(os.path.join(system_image_root, 'default',
                                         'armeabi-v7a'))
  else:
    raise Exception("abi option invalid")

def CheckKVM():
  """Quickly check whether KVM is enabled.

  Returns:
    True iff /dev/kvm exists (Linux only).
  """
  return os.path.exists('/dev/kvm')

def RunKvmOk():
  """Run kvm-ok as root to check that KVM is properly enabled after installation
     of the required packages.

  Returns:
    True iff KVM is enabled (/dev/kvm exists). On failure, returns False
    but also print detailed information explaining why KVM isn't enabled
    (e.g. CPU doesn't support it, or BIOS disabled it).
  """
  try:
    # Note: kvm-ok is in /usr/sbin, so always use 'sudo' to run it.
    return not cmd_helper.RunCmd(['sudo', 'kvm-ok'])
  except OSError:
    logging.info('kvm-ok not installed')
    return False


def InstallKVM():
  """Installs KVM packages."""
  rc = cmd_helper.RunCmd(['sudo', 'apt-get', 'install', 'kvm'])
  if rc:
    logging.critical('ERROR: Did not install KVM. Make sure hardware '
                     'virtualization is enabled in BIOS (i.e. Intel VT-x or '
                     'AMD SVM).')
  # TODO(navabi): Use modprobe kvm-amd on AMD processors.
  rc = cmd_helper.RunCmd(['sudo', 'modprobe', 'kvm-intel'])
  if rc:
    logging.critical('ERROR: Did not add KVM module to Linux Kernel. Make sure '
                     'hardware virtualization is enabled in BIOS.')
  # Now check to ensure KVM acceleration can be used.
  if not RunKvmOk():
    logging.critical('ERROR: Can not use KVM acceleration. Make sure hardware '
                     'virtualization is enabled in BIOS (i.e. Intel VT-x or '
                     'AMD SVM).')


def UpdateSDK(api_level, package_name, package_pattern, timeout):
  """This function update SDK with a filter index.

  Args:
    api_level: the Android API level to download for.
    package_name: logging name of package that is being updated.
    package_pattern: the pattern to match the filter index from.
    timeout: the amount of time wait for update command.
  """
  android_binary = os.path.join(constants.ANDROID_SDK_ROOT, 'tools', 'android')

  list_sdk_repo_command = [android_binary, 'list', 'sdk', '--all']

  exit_code, stdout = cmd_helper.GetCmdStatusAndOutput(list_sdk_repo_command)

  if exit_code != 0:
    raise Exception('\'android list sdk --all\' command return %d' % exit_code)

  for line in stdout.split('\n'):
    match = package_pattern.match(line)
    if match:
      index = match.group(1)
      logging.info('package %s corresponds to %s with api level %d',
                   index, package_name, api_level)
      update_command = [android_binary, 'update', 'sdk', '--no-ui', '--all',
                         '--filter', index]
      update_command_str = ' '.join(update_command)
      logging.info('running update command: %s', update_command_str)
      update_process = pexpect.spawn(update_command_str)

      if update_process.expect('Do you accept the license') != 0:
        raise Exception('License agreement check failed')
      update_process.sendline('y')
      if update_process.expect(
        'Done. 1 package installed.', timeout=timeout) == 0:
        logging.info('Successfully installed %s for API level %d',
                      package_name, api_level)
        return
      else:
        raise Exception('Failed to install platform update')
  raise Exception('Could not find android-%d update for the SDK!' % api_level)

def GetSystemImage(abi, api_level=DEFAULT_ANDROID_API_LEVEL, google=False):
  """Download system image files

  Args:
    abi: target abi, x86 or arm
    api_level: the Android API level to download for.
    google: use Google build system image instead of AOSP build
  """
  logging.info('Download x86 system image directory into sdk directory.')

  if abi == 'x86':
    if google:
      package_name = 'Google Intel x86 Atom System Image'
      pattern = re.compile(
         r'\s*([0-9]+)- Google APIs Intel x86 Atom System Image, Google Inc.'
        ' API %d.*' % api_level)
    else:
      package_name = 'Intel x86 system image'
      pattern = re.compile(
        r'\s*([0-9]+)- Intel x86 Atom System Image, Android API %d.*'
        % api_level)
  elif abi == 'arm':
    if google:
      package_name = 'Google arm system image'
      pattern = re.compile(
        r'\s*([0-9]+)- Google APIs ARM EABI v7a System Image, Google Inc. API '
        '%d.*' % api_level)
    else:
      package_name = 'Android arm system image'
      pattern = re.compile(
        r'\s*([0-9]+)- ARM EABI v7a System Image, Android API %d.*' % api_level)
  else:
    raise Exception('abi option is invalid')

  UpdateSDK(api_level, package_name, pattern, DOWNLOAD_SYSTEM_IMAGE_TIMEOUT)

def GetSDKPlatform(api_level=DEFAULT_ANDROID_API_LEVEL, google=False):
  """Update the SDK to include the platform specified.

  Args:
    api_level: the Android API level to download
    google: use Google build system image instead of AOSP build
  """
  logging.info('Download SDK Platform directory into sdk directory.')

  platform_package_pattern = re.compile(
      r'\s*([0-9]+)- SDK Platform Android [\.,0-9]+, API %d.*' % api_level)

  UpdateSDK(api_level, 'SDK Platform', platform_package_pattern,
            DOWNLOAD_SDK_PLATFORM_TIMEOUT)

  if google:
    google_api_package_pattern = re.compile(
      r'\s*([0-9]+)- Google APIs, Android API %d.*' % api_level)
    UpdateSDK(api_level, 'Google APIs', google_api_package_pattern,
              DOWNLOAD_SDK_PLATFORM_TIMEOUT)


def main(argv):
  opt_parser = optparse.OptionParser(
      description='Install dependencies for running the Android emulator')
  opt_parser.add_option('--abi',
                        dest='abi',
                        help='The targeted abi for emulator system image',
                        type='string',
                        default=DEFAULT_ABI)
  opt_parser.add_option('--api-level',
                        dest='api_level',
                        help=('The API level (e.g., 19 for Android 4.4) to '
                              'ensure is available'),
                        type='int',
                        default=DEFAULT_ANDROID_API_LEVEL)
  opt_parser.add_option('-v',
                        dest='verbosity',
                        default=1,
                        action='count',
                        help='Verbose level (multiple times for more)')
  opt_parser.add_option('--google',
                        dest='google',
                        action='store_true',
                        default=False,
                        help='Install Google System Image instead of AOSP')

  options, _ = opt_parser.parse_args(argv[1:])

  run_tests_helper.SetLogLevel(verbose_count=options.verbosity)

  devil_chromium.Initialize()

  # Calls below will download emulator SDK and/or system images only if needed.
  if CheckSDK():
    logging.info('android_emulator_sdk/ exists')
  else:
    logging.critical('ERROR: Emulator SDK not installed in %s'
                     , constants.ANDROID_SDK_ROOT)
    return 1

  # Check target. The target has to be installed in order to run the emulator.
  if CheckSDKPlatform(options.api_level, options.google):
    logging.info('SDK platform %s %s android-%d already present, skipping.',
                 'Google' if options.google else 'AOSP', options.abi,
                 options.api_level)
  else:
    logging.info('SDK platform %s %s android-%d not present, installing.',
                 'Google' if options.google else 'AOSP', options.abi,
                 options.api_level)
    GetSDKPlatform(options.api_level, options.google)

  # Download the system image needed
  if CheckSystemImage(options.abi, options.api_level, options.google):
    logging.info('system image for %s %s android-%d already present, skipping.',
                 'Google' if options.google else 'AOSP', options.abi,
                 options.api_level)
  else:
    GetSystemImage(options.abi, options.api_level, options.google)

  # Make sure KVM packages are installed and enabled.
  if options.abi == 'x86':
    if CheckKVM():
      logging.info('KVM already installed and enabled.')
    else:
      logging.warning('KVM is not installed or enabled.')


if __name__ == '__main__':
  sys.exit(main(sys.argv))
