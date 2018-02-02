# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Defines a set of constants shared by test runners and other scripts."""

# TODO(jbudorick): Split these constants into coherent modules.

# pylint: disable=W0212

import collections
import glob
import logging
import os
import subprocess

import devil.android.sdk.keyevent
from devil.android.constants import chrome
from devil.android.sdk import version_codes
from devil.constants import exit_codes


keyevent = devil.android.sdk.keyevent


DIR_SOURCE_ROOT = os.environ.get('CHECKOUT_SOURCE_ROOT',
    os.path.abspath(os.path.join(os.path.dirname(__file__),
                                 os.pardir, os.pardir, os.pardir, os.pardir)))

PACKAGE_INFO = dict(chrome.PACKAGE_INFO)
PACKAGE_INFO.update({
    'legacy_browser': chrome.PackageInfo(
        'com.google.android.browser',
        'com.android.browser.BrowserActivity',
        None,
        None),
    'chromecast_shell': chrome.PackageInfo(
        'com.google.android.apps.mediashell',
        'com.google.android.apps.mediashell.MediaShellActivity',
        'castshell-command-line',
        None),
    'android_webview_shell': chrome.PackageInfo(
        'org.chromium.android_webview.shell',
        'org.chromium.android_webview.shell.AwShellActivity',
        'android-webview-command-line',
        None),
    'gtest': chrome.PackageInfo(
        'org.chromium.native_test',
        'org.chromium.native_test.NativeUnitTestActivity',
        'chrome-native-tests-command-line',
        None),
    'components_browsertests': chrome.PackageInfo(
        'org.chromium.components_browsertests_apk',
        ('org.chromium.components_browsertests_apk' +
         '.ComponentsBrowserTestsActivity'),
        'chrome-native-tests-command-line',
        None),
    'content_browsertests': chrome.PackageInfo(
        'org.chromium.content_browsertests_apk',
        'org.chromium.content_browsertests_apk.ContentBrowserTestsActivity',
        'chrome-native-tests-command-line',
        None),
    'chromedriver_webview_shell': chrome.PackageInfo(
        'org.chromium.chromedriver_webview_shell',
        'org.chromium.chromedriver_webview_shell.Main',
        None,
        None),
})


# Ports arrangement for various test servers used in Chrome for Android.
# Lighttpd server will attempt to use 9000 as default port, if unavailable it
# will find a free port from 8001 - 8999.
LIGHTTPD_DEFAULT_PORT = 9000
LIGHTTPD_RANDOM_PORT_FIRST = 8001
LIGHTTPD_RANDOM_PORT_LAST = 8999
TEST_SYNC_SERVER_PORT = 9031
TEST_SEARCH_BY_IMAGE_SERVER_PORT = 9041
TEST_POLICY_SERVER_PORT = 9051


TEST_EXECUTABLE_DIR = '/data/local/tmp'
# Directories for common java libraries for SDK build.
# These constants are defined in build/android/ant/common.xml
SDK_BUILD_JAVALIB_DIR = 'lib.java'
SDK_BUILD_TEST_JAVALIB_DIR = 'test.lib.java'
SDK_BUILD_APKS_DIR = 'apks'

ADB_KEYS_FILE = '/data/misc/adb/adb_keys'

PERF_OUTPUT_DIR = os.path.join(DIR_SOURCE_ROOT, 'out', 'step_results')
# The directory on the device where perf test output gets saved to.
DEVICE_PERF_OUTPUT_DIR = (
    '/data/data/' + PACKAGE_INFO['chrome'].package + '/files')

SCREENSHOTS_DIR = os.path.join(DIR_SOURCE_ROOT, 'out_screenshots')

ANDROID_SDK_VERSION = version_codes.O_MR1
ANDROID_SDK_BUILD_TOOLS_VERSION = '27.0.1'
ANDROID_SDK_ROOT = os.path.join(DIR_SOURCE_ROOT,
                                'third_party', 'android_tools', 'sdk')
ANDROID_SDK_TOOLS = os.path.join(ANDROID_SDK_ROOT,
                                 'build-tools', ANDROID_SDK_BUILD_TOOLS_VERSION)
ANDROID_NDK_ROOT = os.path.join(DIR_SOURCE_ROOT,
                                'third_party', 'android_tools', 'ndk')

PROGUARD_ROOT = os.path.join(DIR_SOURCE_ROOT, 'third_party', 'proguard')

BAD_DEVICES_JSON = os.path.join(DIR_SOURCE_ROOT,
                                os.environ.get('CHROMIUM_OUT_DIR', 'out'),
                                'bad_devices.json')

UPSTREAM_FLAKINESS_SERVER = 'test-results.appspot.com'

# TODO(jbudorick): Remove once unused.
DEVICE_LOCAL_PROPERTIES_PATH = '/data/local.prop'

# TODO(jbudorick): Rework this into testing/buildbot/
PYTHON_UNIT_TEST_SUITES = {
  'pylib_py_unittests': {
    'path': os.path.join(DIR_SOURCE_ROOT, 'build', 'android'),
    'test_modules': [
      'devil.android.device_utils_test',
      'devil.android.md5sum_test',
      'devil.utils.cmd_helper_test',
      'pylib.results.json_results_test',
      'pylib.utils.proguard_test',
    ]
  },
  'gyp_py_unittests': {
    'path': os.path.join(DIR_SOURCE_ROOT, 'build', 'android', 'gyp'),
    'test_modules': [
      'java_cpp_enum_tests',
      'java_google_api_keys_tests',
    ]
  },
}

LOCAL_MACHINE_TESTS = ['junit', 'python']
VALID_ENVIRONMENTS = ['local']
VALID_TEST_TYPES = ['gtest', 'instrumentation', 'junit', 'linker', 'monkey',
                    'perf', 'python']
VALID_DEVICE_TYPES = ['Android', 'iOS']


def GetBuildType():
  try:
    return os.environ['BUILDTYPE']
  except KeyError:
    raise EnvironmentError(
        'The BUILDTYPE environment variable has not been set')


def SetBuildType(build_type):
  os.environ['BUILDTYPE'] = build_type


def SetBuildDirectory(build_directory):
  os.environ['CHROMIUM_OUT_DIR'] = build_directory


def SetOutputDirectory(output_directory):
  os.environ['CHROMIUM_OUTPUT_DIR'] = output_directory


def GetOutDirectory(build_type=None):
  """Returns the out directory where the output binaries are built.

  Args:
    build_type: Build type, generally 'Debug' or 'Release'. Defaults to the
      globally set build type environment variable BUILDTYPE.
  """
  if 'CHROMIUM_OUTPUT_DIR' in os.environ:
    return os.path.abspath(os.path.join(
        DIR_SOURCE_ROOT, os.environ.get('CHROMIUM_OUTPUT_DIR')))

  return os.path.abspath(os.path.join(
      DIR_SOURCE_ROOT, os.environ.get('CHROMIUM_OUT_DIR', 'out'),
      GetBuildType() if build_type is None else build_type))


def CheckOutputDirectory():
  """Checks that CHROMIUM_OUT_DIR or CHROMIUM_OUTPUT_DIR is set.

  If neither are set, but the current working directory is a build directory,
  then CHROMIUM_OUTPUT_DIR is set to the current working directory.

  Raises:
    Exception: If no output directory is detected.
  """
  output_dir = os.environ.get('CHROMIUM_OUTPUT_DIR')
  out_dir = os.environ.get('CHROMIUM_OUT_DIR')
  if not output_dir and not out_dir:
    # If CWD is an output directory, then assume it's the desired one.
    if os.path.exists('build.ninja'):
      output_dir = os.getcwd()
      SetOutputDirectory(output_dir)
    elif os.environ.get('CHROME_HEADLESS'):
      # When running on bots, see if the output directory is obvious.
      dirs = glob.glob(os.path.join(DIR_SOURCE_ROOT, 'out', '*', 'build.ninja'))
      if len(dirs) == 1:
        SetOutputDirectory(dirs[0])
      else:
        raise Exception('Neither CHROMIUM_OUTPUT_DIR nor CHROMIUM_OUT_DIR '
                        'has been set. CHROME_HEADLESS detected, but multiple '
                        'out dirs exist: %r' % dirs)
    else:
      raise Exception('Neither CHROMIUM_OUTPUT_DIR nor CHROMIUM_OUT_DIR '
                      'has been set')


# TODO(jbudorick): Convert existing callers to AdbWrapper.GetAdbPath() and
# remove this.
def GetAdbPath():
  from devil.android.sdk import adb_wrapper
  return adb_wrapper.AdbWrapper.GetAdbPath()


# Exit codes
ERROR_EXIT_CODE = exit_codes.ERROR
INFRA_EXIT_CODE = exit_codes.INFRA
WARNING_EXIT_CODE = exit_codes.WARNING
