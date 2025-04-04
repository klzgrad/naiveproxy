#!/usr/bin/env vpython3
#
# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs all types of tests from one unified interface."""

from __future__ import absolute_import
import argparse
import collections
import contextlib
import io
import itertools
import logging
import os
import re
import shlex
import shutil
import signal
import sys
import tempfile
import threading
import traceback
import unittest

# Import _strptime before threaded code. datetime.datetime.strptime is
# threadsafe except for the initial import of the _strptime module.
# See http://crbug.com/724524 and https://bugs.python.org/issue7980.
import _strptime  # pylint: disable=unused-import

# pylint: disable=ungrouped-imports
from pylib.constants import host_paths

if host_paths.DEVIL_PATH not in sys.path:
  sys.path.append(host_paths.DEVIL_PATH)

from devil import base_error
from devil.utils import reraiser_thread
from devil.utils import run_tests_helper

from pylib import constants
from pylib.base import base_test_result
from pylib.base import environment_factory
from pylib.base import output_manager
from pylib.base import output_manager_factory
from pylib.base import test_instance_factory
from pylib.base import test_run_factory
from pylib.results import json_results
from pylib.results import report_results
from pylib.results.presentation import test_results_presentation
from pylib.utils import local_utils
from pylib.utils import logdog_helper
from pylib.utils import logging_utils
from pylib.utils import test_filter

from py_utils import contextlib_ext

from lib.proto import exception_recorder
from lib.results import result_sink

_DEVIL_STATIC_CONFIG_FILE = os.path.abspath(os.path.join(
    host_paths.DIR_SOURCE_ROOT, 'build', 'android', 'devil_config.json'))

_RERUN_FAILED_TESTS_FILE = 'rerun_failed_tests.filter'


def _RealPath(arg):
  if arg.startswith('//'):
    arg = os.path.abspath(os.path.join(host_paths.DIR_SOURCE_ROOT,
                                       arg[2:].replace('/', os.sep)))
  return os.path.realpath(arg)


def AddTestLauncherOptions(parser):
  """Adds arguments mirroring //base/test/launcher.

  Args:
    parser: The parser to which arguments should be added.
  Returns:
    The given parser.
  """
  parser.add_argument(
      '--test-launcher-retry-limit',
      '--test_launcher_retry_limit',
      '--num_retries', '--num-retries',
      '--isolated-script-test-launcher-retry-limit',
      dest='num_retries', type=int, default=2,
      help='Number of retries for a test before '
           'giving up (default: %(default)s).')
  parser.add_argument(
      '--test-launcher-summary-output',
      '--json-results-file',
      dest='json_results_file', type=os.path.realpath,
      help='If set, will dump results in JSON form to the specified file. '
           'Note that this will also trigger saving per-test logcats to '
           'logdog.')
  parser.add_argument(
      '--test-launcher-shard-index',
      type=int, default=os.environ.get('GTEST_SHARD_INDEX', 0),
      help='Index of the external shard to run.')
  parser.add_argument(
      '--test-launcher-total-shards',
      type=int, default=os.environ.get('GTEST_TOTAL_SHARDS', 1),
      help='Total number of external shards.')

  test_filter.AddFilterOptions(parser)

  return parser


def AddCommandLineOptions(parser):
  """Adds arguments to support passing command-line flags to the device."""
  parser.add_argument(
      '--device-flags-file',
      type=os.path.realpath,
      help='The relative filepath to a file containing '
           'command-line flags to set on the device')
  parser.add_argument(
      '--use-apk-under-test-flags-file',
      action='store_true',
      help='Wether to use the flags file for the apk under test. If set, '
           "the filename will be looked up in the APK's PackageInfo.")
  parser.add_argument('--variations-test-seed-path',
                      type=os.path.relpath,
                      default=None,
                      help='Path to variations seed file.')
  parser.add_argument('--webview-variations-test-seed-path',
                      type=os.path.relpath,
                      default=None,
                      help='Path to variations seed file for WebView.')

  parser.set_defaults(allow_unknown=True)
  parser.set_defaults(command_line_flags=None)


def AddTracingOptions(parser):
  # TODO(shenghuazhang): Move this into AddCommonOptions once it's supported
  # for all test types.
  parser.add_argument(
      '--trace-output',
      metavar='FILENAME', type=os.path.realpath,
      help='Path to save test_runner trace json output to.')

  parser.add_argument(
      '--trace-all',
      action='store_true',
      help='Whether to trace all function calls.')


def AddCommonOptions(parser):
  """Adds all common options to |parser|."""

  default_build_type = os.environ.get('BUILDTYPE', 'Debug')

  debug_or_release_group = parser.add_mutually_exclusive_group()
  debug_or_release_group.add_argument(
      '--debug',
      action='store_const', const='Debug', dest='build_type',
      default=default_build_type,
      help='If set, run test suites under out/Debug. '
           'Default is env var BUILDTYPE or Debug.')
  debug_or_release_group.add_argument(
      '--release',
      action='store_const', const='Release', dest='build_type',
      help='If set, run test suites under out/Release. '
           'Default is env var BUILDTYPE or Debug.')

  parser.add_argument(
      '--break-on-failure', '--break_on_failure',
      dest='break_on_failure', action='store_true',
      help='Whether to break on failure.')

  # TODO(jbudorick): Remove this once everything has switched to platform
  # mode.
  parser.add_argument(
      '--enable-platform-mode',
      action='store_true',
      help='Run the test scripts in platform mode, which '
           'conceptually separates the test runner from the '
           '"device" (local or remote, real or emulated) on '
           'which the tests are running. [experimental]')

  parser.add_argument(
      '-e', '--environment',
      default='local', choices=constants.VALID_ENVIRONMENTS,
      help='Test environment to run in (default: %(default)s).')

  parser.add_argument(
      '--local-output',
      action='store_true',
      help='Whether to archive test output locally and generate '
           'a local results detail page.')

  parser.add_argument('--list-tests',
                      action='store_true',
                      help='List available tests and exit.')

  parser.add_argument('--wrapper-script-args',
                      help='A string of args that were passed to the wrapper '
                      'script. This should probably not be edited by a '
                      'user as it is passed by the wrapper itself.')

  class FastLocalDevAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
      namespace.enable_concurrent_adb = True
      namespace.enable_device_cache = True
      namespace.extract_test_list_from_filter = True
      namespace.local_output = True
      namespace.num_retries = 0
      namespace.skip_clear_data = True
      namespace.use_persistent_shell = True

  parser.add_argument(
      '--fast-local-dev',
      type=bool,
      nargs=0,
      action=FastLocalDevAction,
      help='Alias for: --num-retries=0 --enable-device-cache '
      '--enable-concurrent-adb --skip-clear-data '
      '--extract-test-list-from-filter --use-persistent-shell --local-output')

  # TODO(jbudorick): Remove this once downstream bots have switched to
  # api.test_results.
  parser.add_argument(
      '--flakiness-dashboard-server',
      dest='flakiness_dashboard_server',
      help=argparse.SUPPRESS)
  parser.add_argument(
      '--gs-results-bucket',
      help='Google Storage bucket to upload results to.')

  parser.add_argument(
      '--output-directory',
      dest='output_directory', type=os.path.realpath,
      help='Path to the directory in which build files are'
           ' located (must include build type). This will take'
           ' precedence over --debug and --release')
  parser.add_argument(
      '-v', '--verbose',
      dest='verbose_count', default=0, action='count',
      help='Verbose level (multiple times for more)')

  parser.add_argument(
      '--repeat', '--gtest_repeat', '--gtest-repeat',
      '--isolated-script-test-repeat',
      dest='repeat', type=int, default=0,
      help='Number of times to repeat the specified set of tests.')

  # Not useful for junit tests.
  parser.add_argument(
      '--use-persistent-shell',
      action='store_true',
      help='Uses a persistent shell connection for the adb connection.')

  parser.add_argument('--disable-test-server',
                      action='store_true',
                      help='Disables SpawnedTestServer which doesn'
                      't work with remote adb. '
                      'WARNING: Will break tests which require the server.')

  # This is currently only implemented for gtests and instrumentation tests.
  parser.add_argument(
      '--gtest_also_run_disabled_tests', '--gtest-also-run-disabled-tests',
      '--isolated-script-test-also-run-disabled-tests',
      dest='run_disabled', action='store_true',
      help='Also run disabled tests if applicable.')

  # These are currently only implemented for gtests.
  parser.add_argument('--isolated-script-test-output',
                      help='If present, store test results on this path.')
  parser.add_argument('--isolated-script-test-perf-output',
                      help='If present, store chartjson results on this path.')
  parser.add_argument('--timeout-scale',
                      type=float,
                      help='Factor by which timeouts should be scaled.')

  AddTestLauncherOptions(parser)


def ProcessCommonOptions(args):
  """Processes and handles all common options."""
  run_tests_helper.SetLogLevel(args.verbose_count, add_handler=False)
  if args.verbose_count > 0:
    handler = logging_utils.ColorStreamHandler()
  else:
    handler = logging.StreamHandler(sys.stdout)
  handler.setFormatter(run_tests_helper.CustomFormatter())
  logging.getLogger().addHandler(handler)

  constants.SetBuildType(args.build_type)
  if args.output_directory:
    constants.SetOutputDirectory(args.output_directory)


def AddDeviceOptions(parser):
  """Adds device options to |parser|."""

  parser = parser.add_argument_group('device arguments')

  parser.add_argument(
      '--adb-path',
      type=os.path.realpath,
      help='Specify the absolute path of the adb binary that '
           'should be used.')
  parser.add_argument(
      '--use-local-devil-tools',
      action='store_true',
      help='Use locally built versions of tools used by devil_chromium.')
  parser.add_argument('--denylist-file',
                      type=os.path.realpath,
                      help='Device denylist file.')
  parser.add_argument(
      '-d', '--device', nargs='+',
      dest='test_devices',
      help='Target device(s) for the test suite to run on.')
  parser.add_argument(
      '--enable-concurrent-adb',
      action='store_true',
      help='Run multiple adb commands at the same time, even '
           'for the same device.')
  parser.add_argument(
      '--enable-device-cache',
      action='store_true',
      help='Cache device state to disk between runs')
  parser.add_argument('--list-data',
                      action='store_true',
                      help='List files pushed to device and exit.')
  parser.add_argument('--skip-clear-data',
                      action='store_true',
                      help='Do not wipe app data between tests. Use this to '
                      'speed up local development and never on bots '
                      '(increases flakiness)')
  parser.add_argument(
      '--recover-devices',
      action='store_true',
      help='Attempt to recover devices prior to the final retry. Warning: '
      'this will cause all devices to reboot.')

  parser.add_argument(
      '--upload-logcats-file',
      action='store_true',
      dest='upload_logcats_file',
      help='Whether to upload logcat file to logdog.')

  logcat_output_group = parser.add_mutually_exclusive_group()
  logcat_output_group.add_argument(
      '--logcat-output-dir', type=os.path.realpath,
      help='If set, will dump logcats recorded during test run to directory. '
           'File names will be the device ids with timestamps.')
  logcat_output_group.add_argument(
      '--logcat-output-file', type=os.path.realpath,
      help='If set, will merge logcats recorded during test run and dump them '
           'to the specified file.')

  parser.add_argument(
      '--force-main-user',
      action='store_true',
      help='Force the applicable adb commands to run with "--user" param set '
      'to the id of the main user on device. Only use when the main user is a '
      'secondary user, e.g. Android Automotive OS.')

  parser.add_argument(
      '--connect-over-ethernet',
      action='store_true',
      help='Connect to devices over the network using "adb connect". Only '
      'supported when running on chromeos-swarming')


def AddEmulatorOptions(parser):
  """Adds emulator-specific options to |parser|."""
  parser = parser.add_argument_group('emulator arguments')

  parser.add_argument(
      '--avd-config',
      type=os.path.realpath,
      help='Path to the avd config textpb. '
      '(See //tools/android/avd/proto/ for message definition'
      ' and existing textpb files.)')
  parser.add_argument(
      '--emulator-count',
      type=int,
      default=1,
      help='Number of emulators to use.')
  parser.add_argument(
      '--emulator-window',
      action='store_true',
      default=False,
      help='Enable graphical window display on the emulator.')
  parser.add_argument(
      '--emulator-debug-tags',
      help='Comma-separated list of debug tags. This can be used to enable or '
      'disable debug messages from specific parts of the emulator, e.g. '
      'init,snapshot. See "emulator -help-debug-tags" '
      'for a full list of tags.')
  parser.add_argument(
      '--emulator-enable-network',
      action='store_true',
      help='Enable the network (WiFi and mobile data) on the emulator.')


def AddGTestOptions(parser):
  """Adds gtest options to |parser|."""

  parser = parser.add_argument_group('gtest arguments')

  parser.add_argument(
      '--additional-apk',
      action='append', dest='additional_apks', default=[],
      type=_RealPath,
      help='Additional apk that must be installed on '
           'the device when the tests are run.')
  parser.add_argument(
      '--app-data-file',
      action='append', dest='app_data_files',
      help='A file path relative to the app data directory '
           'that should be saved to the host.')
  parser.add_argument(
      '--app-data-file-dir',
      help='Host directory to which app data files will be'
           ' saved. Used with --app-data-file.')
  parser.add_argument(
      '--enable-xml-result-parsing',
      action='store_true', help=argparse.SUPPRESS)
  parser.add_argument(
      '--executable-dist-dir',
      type=os.path.realpath,
      help="Path to executable's dist directory for native"
           " (non-apk) tests.")
  parser.add_argument(
      '--deploy-mock-openxr-runtime',
      action='store_true',
      help=('Prepares the device by deploying a mock OpenXR runtime to use for '
            'testing. Note that this *may* override a runtime specialization '
            'already present on the device.'))
  parser.add_argument('--extract-test-list-from-filter',
                      action='store_true',
                      help='When a test filter is specified, and the list of '
                      'tests can be determined from it, skip querying the '
                      'device for the list of all tests. Speeds up local '
                      'development, but is not safe to use on bots ('
                      'http://crbug.com/549214')
  parser.add_argument(
      '--gs-test-artifacts-bucket',
      help=('If present, test artifacts will be uploaded to this Google '
            'Storage bucket.'))
  parser.add_argument(
      '--render-test-output-dir',
      help='If present, store rendering artifacts in this path.')
  parser.add_argument(
      '--runtime-deps-path',
      dest='runtime_deps_path', type=os.path.realpath,
      help='Runtime data dependency file from GN.')
  parser.add_argument(
      '-t', '--shard-timeout',
      dest='shard_timeout', type=int, default=120,
      help='Timeout to wait for each test (default: %(default)s).')
  parser.add_argument(
      '--store-tombstones',
      dest='store_tombstones', action='store_true',
      help='Add tombstones in results if crash.')
  parser.add_argument(
      '-s', '--suite',
      dest='suite_name', nargs='+', metavar='SUITE_NAME', required=True,
      help='Executable name of the test suite to run.')
  parser.add_argument(
      '--test-apk-incremental-install-json',
      type=os.path.realpath,
      help='Path to install json for the test apk.')
  parser.add_argument('--test-launcher-batch-limit',
                      dest='test_launcher_batch_limit',
                      type=int,
                      help='The max number of tests to run in a shard. '
                      'Ignores non-positive ints and those greater than '
                      'MAX_SHARDS')
  parser.add_argument(
      '-w', '--wait-for-java-debugger', action='store_true',
      help='Wait for java debugger to attach before running any application '
           'code. Also disables test timeouts and sets retries=0.')
  parser.add_argument(
      '--coverage-dir',
      type=os.path.realpath,
      help='Directory in which to place all generated coverage files.')
  parser.add_argument(
      '--use-existing-test-data',
      action='store_true',
      help='Do not push new files to the device, instead using existing APK '
      'and test data. Only use when running the same test for multiple '
      'iterations.')
  # This is currently only implemented for gtests tests.
  parser.add_argument('--gtest_also_run_pre_tests',
                      '--gtest-also-run-pre-tests',
                      dest='run_pre_tests',
                      action='store_true',
                      help='Also run PRE_ tests if applicable.')


def AddInstrumentationTestOptions(parser):
  """Adds Instrumentation test options to |parser|."""

  parser = parser.add_argument_group('instrumentation arguments')

  parser.add_argument('--additional-apex',
                      action='append',
                      dest='additional_apexs',
                      default=[],
                      type=_RealPath,
                      help='Additional apex that must be installed on '
                      'the device when the tests are run')
  parser.add_argument(
      '--additional-apk',
      action='append', dest='additional_apks', default=[],
      type=_RealPath,
      help='Additional apk that must be installed on '
           'the device when the tests are run')
  parser.add_argument('--forced-queryable-additional-apk',
                      action='append',
                      dest='forced_queryable_additional_apks',
                      default=[],
                      type=_RealPath,
                      help='Configures an additional-apk to be forced '
                      'to be queryable by other APKs.')
  parser.add_argument('--instant-additional-apk',
                      action='append',
                      dest='instant_additional_apks',
                      default=[],
                      type=_RealPath,
                      help='Configures an additional-apk to be an instant APK')
  parser.add_argument(
      '-A', '--annotation',
      dest='annotation_str',
      help='Comma-separated list of annotations. Run only tests with any of '
           'the given annotations. An annotation can be either a key or a '
           'key-values pair. A test that has no annotation is considered '
           '"SmallTest".')
  # TODO(jbudorick): Remove support for name-style APK specification once
  # bots are no longer doing it.
  parser.add_argument(
      '--apk-under-test',
      help='Path or name of the apk under test.')
  parser.add_argument(
      '--store-data-dependencies-in-temp',
      action='store_true',
      help='Store data dependencies in /data/local/tmp/chromium_tests_root')
  parser.add_argument(
      '--module',
      action='append',
      dest='modules',
      help='Specify Android App Bundle modules to install in addition to the '
      'base module.')
  parser.add_argument(
      '--fake-module',
      action='append',
      dest='fake_modules',
      help='Specify Android App Bundle modules to fake install in addition to '
      'the real modules.')
  parser.add_argument(
      '--additional-locale',
      action='append',
      dest='additional_locales',
      help='Specify locales in addition to the device locale to install splits '
      'for when --apk-under-test is an Android App Bundle.')
  parser.add_argument(
      '--coverage-dir',
      type=os.path.realpath,
      help='Directory in which to place all generated '
      'Jacoco coverage files.')
  parser.add_argument('--disable-dalvik-asserts',
                      dest='set_asserts',
                      action='store_false',
                      default=True,
                      help='Removes the dalvik.vm.enableassertions property')
  parser.add_argument(
      '--proguard-mapping-path',
      help='.mapping file to use to Deobfuscate java stack traces in test '
      'output and logcat.')
  parser.add_argument(
      '-E', '--exclude-annotation',
      dest='exclude_annotation_str',
      help='Comma-separated list of annotations. Exclude tests with these '
           'annotations.')
  parser.add_argument(
      '--enable-breakpad-dump',
      action='store_true',
      help='Stores any breakpad dumps till the end of the test.')
  parser.add_argument(
      '--replace-system-package',
      type=_RealPath,
      default=None,
      help='Use this apk to temporarily replace a system package with the same '
      'package name.')
  parser.add_argument(
      '--remove-system-package',
      default=[],
      action='append',
      dest='system_packages_to_remove',
      help='Specifies a system package to remove before testing if it exists '
      'on the system. WARNING: THIS WILL PERMANENTLY REMOVE THE SYSTEM APP. '
      'Unlike --replace-system-package, the app will not be restored after '
      'tests are finished.')
  parser.add_argument(
      '--use-voice-interaction-service',
      help='This can be used to update the voice interaction service to be a '
      'custom one. This is useful for mocking assistants. eg: '
      'android.assist.service/.MainInteractionService')
  parser.add_argument(
      '--use-webview-provider',
      type=_RealPath, default=None,
      help='Use this apk as the webview provider during test. '
           'The original provider will be restored if possible, '
           "on Nougat the provider can't be determined and so "
           'the system will choose the default provider.')
  parser.add_argument(
      '--webview-command-line-arg',
      default=[],
      action='append',
      help="Specifies command line arguments to add to WebView's flag file")
  parser.add_argument(
      '--webview-process-mode',
      choices=['single', 'multiple'],
      help='Run WebView instrumentation tests only in the specified process '
      'mode. If not set, both single and multiple process modes will execute.')
  parser.add_argument(
      '--run-setup-command',
      default=[],
      action='append',
      dest='run_setup_commands',
      help='This can be used to run a custom shell command on the device as a '
      'setup step')
  parser.add_argument(
      '--run-teardown-command',
      default=[],
      action='append',
      dest='run_teardown_commands',
      help='This can be used to run a custom shell command on the device as a '
      'teardown step')
  parser.add_argument(
      '--runtime-deps-path',
      dest='runtime_deps_path', type=os.path.realpath,
      help='Runtime data dependency file from GN.')
  parser.add_argument(
      '--screenshot-directory',
      dest='screenshot_dir', type=os.path.realpath,
      help='Capture screenshots of test failures')
  parser.add_argument(
      '--store-tombstones',
      action='store_true', dest='store_tombstones',
      help='Add tombstones in results if crash.')
  parser.add_argument(
      '--strict-mode',
      dest='strict_mode', default='testing',
      help='StrictMode command-line flag set on the device, '
           'death/testing to kill the process, off to stop '
           'checking, flash to flash only. (default: %(default)s)')
  parser.add_argument(
      '--test-apk',
      required=True,
      help='Path or name of the apk containing the tests.')
  parser.add_argument(
      '--test-apk-as-instant',
      action='store_true',
      help='Install the test apk as an instant app. '
      'Instant apps run in a more restrictive execution environment.')
  parser.add_argument(
      '--test-launcher-batch-limit',
      dest='test_launcher_batch_limit',
      type=int,
      help=('Not actually used for instrumentation tests, but can be used as '
            'a proxy for determining if the current run is a retry without '
            'patch.'))
  parser.add_argument(
      '--is-unit-test',
      action='store_true',
      help=('Specify the test suite as composed of unit tests, blocking '
            'certain operations.'))
  parser.add_argument(
      '-w', '--wait-for-java-debugger', action='store_true',
      help='Wait for java debugger to attach before running any application '
           'code. Also disables test timeouts and sets retries=0.')
  parser.add_argument(
      '--webview-rebaseline-mode',
      action='store_true',
      help=('Run WebView tests in rebaselining mode, updating on-device '
            'expectation files.'))

  # WPR record mode.
  parser.add_argument('--wpr-enable-record',
                      action='store_true',
                      default=False,
                      help='If true, WPR server runs in record mode.'
                      'otherwise, runs in replay mode.')

  parser.add_argument(
      '--approve-app-links',
      help='Force enables Digital Asset Link verification for the provided '
      'package and domain, example usage: --approve-app-links '
      'com.android.package:www.example.com')

  # These arguments are suppressed from the help text because they should
  # only ever be specified by an intermediate script.
  parser.add_argument(
      '--apk-under-test-incremental-install-json',
      help=argparse.SUPPRESS)
  parser.add_argument(
      '--test-apk-incremental-install-json',
      type=os.path.realpath,
      help=argparse.SUPPRESS)


def AddSkiaGoldTestOptions(parser):
  """Adds Skia Gold test options to |parser|."""
  parser = parser.add_argument_group("Skia Gold arguments")
  parser.add_argument(
      '--code-review-system',
      help='A non-default code review system to pass to pass to Gold, if '
      'applicable')
  parser.add_argument(
      '--continuous-integration-system',
      help='A non-default continuous integration system to pass to Gold, if '
      'applicable')
  parser.add_argument(
      '--git-revision', help='The git commit currently being tested.')
  parser.add_argument(
      '--gerrit-issue',
      help='The Gerrit issue this test is being run on, if applicable.')
  parser.add_argument(
      '--gerrit-patchset',
      help='The Gerrit patchset this test is being run on, if applicable.')
  parser.add_argument(
      '--buildbucket-id',
      help='The Buildbucket build ID that this test was triggered from, if '
      'applicable.')
  local_group = parser.add_mutually_exclusive_group()
  local_group.add_argument(
      '--local-pixel-tests',
      action='store_true',
      default=None,
      help='Specifies to run the Skia Gold pixel tests in local mode. When run '
      'in local mode, uploading to Gold is disabled and traditional '
      'generated/golden/diff images are output instead of triage links. '
      'Running in local mode also implies --no-luci-auth. If both this '
      'and --no-local-pixel-tests are left unset, the test harness will '
      'attempt to detect whether it is running on a workstation or not '
      'and set the options accordingly.')
  local_group.add_argument(
      '--no-local-pixel-tests',
      action='store_false',
      dest='local_pixel_tests',
      help='Specifies to run the Skia Gold pixel tests in non-local (bot) '
      'mode. When run in this mode, data is actually uploaded to Gold and '
      'triage links are generated. If both this and --local-pixel-tests '
      'are left unset, the test harness will attempt to detect whether '
      'it is running on a workstation or not and set the options '
      'accordingly.')
  parser.add_argument(
      '--no-luci-auth',
      action='store_true',
      default=False,
      help="Don't use the serve account provided by LUCI for authentication "
      'with Skia Gold, instead relying on gsutil to be pre-authenticated. '
      'Meant for testing locally instead of on the bots.')
  parser.add_argument(
      '--bypass-skia-gold-functionality',
      action='store_true',
      default=False,
      help='Bypass all interaction with Skia Gold, effectively disabling the '
      'image comparison portion of any tests that use Gold. Only meant to be '
      'used in case a Gold outage occurs and cannot be fixed quickly.')


def AddHostsideTestOptions(parser):
  """Adds hostside test options to |parser|."""

  parser = parser.add_argument_group('hostside arguments')

  parser.add_argument(
      '-s', '--test-suite', required=True,
      help='Hostside test suite to run.')
  parser.add_argument(
      '--test-apk-as-instant',
      action='store_true',
      help='Install the test apk as an instant app. '
      'Instant apps run in a more restrictive execution environment.')
  parser.add_argument(
      '--additional-apk',
      action='append',
      dest='additional_apks',
      default=[],
      type=_RealPath,
      help='Additional apk that must be installed on '
           'the device when the tests are run')
  parser.add_argument(
      '--use-webview-provider',
      type=_RealPath, default=None,
      help='Use this apk as the webview provider during test. '
           'The original provider will be restored if possible, '
           "on Nougat the provider can't be determined and so "
           'the system will choose the default provider.')
  parser.add_argument(
      '--tradefed-executable',
      type=_RealPath, default=None,
      help='Location of the cts-tradefed script')
  parser.add_argument(
      '--tradefed-aapt-path',
      type=_RealPath, default=None,
      help='Location of the directory containing aapt binary')
  parser.add_argument(
      '--tradefed-adb-path',
      type=_RealPath, default=None,
      help='Location of the directory containing adb binary')
  # The below arguments are not used, but allow us to pass the same arguments
  # from run_cts.py regardless of type of run (instrumentation/hostside)
  parser.add_argument(
      '--apk-under-test',
      help=argparse.SUPPRESS)
  parser.add_argument(
      '--use-apk-under-test-flags-file',
      action='store_true',
      help=argparse.SUPPRESS)
  parser.add_argument(
      '-E', '--exclude-annotation',
      dest='exclude_annotation_str',
      help=argparse.SUPPRESS)


def AddJUnitTestOptions(parser):
  """Adds junit test options to |parser|."""

  parser = parser.add_argument_group('junit arguments')

  parser.add_argument(
      '--coverage-on-the-fly',
      action='store_true',
      help='Generate coverage data by Jacoco on-the-fly instrumentation.')
  parser.add_argument(
      '--coverage-dir', type=os.path.realpath,
      help='Directory to store coverage info.')
  parser.add_argument(
      '--package-filter',
      help='Filters tests by package.')
  parser.add_argument(
      '--runner-filter',
      help='Filters tests by runner class. Must be fully qualified.')
  parser.add_argument('--json-config',
                      help='Runs only tests listed in this config.')
  parser.add_argument(
      '--shards',
      type=int,
      help='Number of shards to run junit tests in parallel on. Only 1 shard '
      'is supported when test-filter is specified. Values less than 1 will '
      'use auto select.')
  parser.add_argument('--shard-filter',
                      help='Comma separated list of shard indices to run.')
  parser.add_argument(
      '-s', '--test-suite', required=True,
      help='JUnit test suite to run.')
  debug_group = parser.add_mutually_exclusive_group()
  debug_group.add_argument(
      '-w', '--wait-for-java-debugger', action='store_const', const='8701',
      dest='debug_socket', help='Alias for --debug-socket=8701')
  debug_group.add_argument(
      '--debug-socket',
      help='Wait for java debugger to attach at specified socket address '
           'before running any application code. Also disables test timeouts '
           'and sets retries=0.')

  # These arguments are for Android Robolectric tests.
  parser.add_argument(
      '--robolectric-runtime-deps-dir',
      help='Path to runtime deps for Robolectric.')
  parser.add_argument('--native-libs-dir',
                      help='Path to search for native libraries.')
  parser.add_argument(
      '--resource-apk',
      required=True,
      help='Path to .ap_ containing binary resources for Robolectric.')
  parser.add_argument('--shadows-allowlist',
                      help='Path to Allowlist file for Shadows.')


def AddLinkerTestOptions(parser):

  parser = parser.add_argument_group('linker arguments')

  parser.add_argument(
      '--test-apk',
      type=os.path.realpath,
      help='Path to the linker test APK.')


def AddMonkeyTestOptions(parser):
  """Adds monkey test options to |parser|."""

  parser = parser.add_argument_group('monkey arguments')

  parser.add_argument('--browser',
                      required=True,
                      choices=list(constants.PACKAGE_INFO.keys()),
                      metavar='BROWSER',
                      help='Browser under test.')
  parser.add_argument(
      '--category',
      nargs='*', dest='categories', default=[],
      help='A list of allowed categories. Monkey will only visit activities '
           'that are listed with one of the specified categories.')
  parser.add_argument(
      '--event-count',
      default=10000, type=int,
      help='Number of events to generate (default: %(default)s).')
  parser.add_argument(
      '--seed',
      type=int,
      help='Seed value for pseudo-random generator. Same seed value generates '
           'the same sequence of events. Seed is randomized by default.')
  parser.add_argument(
      '--throttle',
      default=100, type=int,
      help='Delay between events (ms) (default: %(default)s). ')


def AddPythonTestOptions(parser):

  parser = parser.add_argument_group('python arguments')

  parser.add_argument('-s',
                      '--suite',
                      dest='suite_name',
                      metavar='SUITE_NAME',
                      choices=list(constants.PYTHON_UNIT_TEST_SUITES.keys()),
                      help='Name of the test suite to run.')


def _CreateClassToFileNameDict(test_apk):
  """Creates a dict mapping classes to file names from size-info apk."""
  constants.CheckOutputDirectory()
  test_apk_size_info = os.path.join(constants.GetOutDirectory(), 'size-info',
                                    os.path.basename(test_apk) + '.jar.info')

  class_to_file_dict = {}
  # Some tests such as webview_cts_tests use a separately downloaded apk to run
  # tests. This means the apk may not have been built by the system and hence
  # no size info file exists.
  if not os.path.exists(test_apk_size_info):
    logging.debug('Apk size file not found. %s', test_apk_size_info)
    return class_to_file_dict

  with open(test_apk_size_info, 'r') as f:
    for line in f:
      file_class, file_name = line.rstrip().split(',', 1)
      # Only want files that are not prebuilt.
      if file_name.startswith('../../'):
        class_to_file_dict[file_class] = str(
            file_name.replace('../../', '//', 1))

  return class_to_file_dict


def _RunPythonTests(args):
  """Subcommand of RunTestsCommand which runs python unit tests."""
  suite_vars = constants.PYTHON_UNIT_TEST_SUITES[args.suite_name]
  suite_path = suite_vars['path']
  suite_test_modules = suite_vars['test_modules']

  sys.path = [suite_path] + sys.path
  try:
    suite = unittest.TestSuite()
    suite.addTests(unittest.defaultTestLoader.loadTestsFromName(m)
                   for m in suite_test_modules)
    runner = unittest.TextTestRunner(verbosity=1+args.verbose_count)
    return 0 if runner.run(suite).wasSuccessful() else 1
  finally:
    sys.path = sys.path[1:]


_DEFAULT_PLATFORM_MODE_TESTS = [
    'gtest', 'hostside', 'instrumentation', 'junit', 'linker', 'monkey'
]


def RunTestsCommand(args, result_sink_client=None):
  """Checks test type and dispatches to the appropriate function.

  Args:
    args: argparse.Namespace object.
    result_sink_client: A ResultSinkClient object.

  Returns:
    Integer indicated exit code.

  Raises:
    Exception: Unknown command name passed in, or an exception from an
        individual test runner.
  """
  command = args.command

  ProcessCommonOptions(args)
  logging.info('command: %s', shlex.join(sys.argv))
  if args.enable_platform_mode or command in _DEFAULT_PLATFORM_MODE_TESTS:
    return RunTestsInPlatformMode(args, result_sink_client)

  if command == 'python':
    return _RunPythonTests(args)
  raise Exception('Unknown test type.')


def _SinkTestResult(test_result, test_file_name, result_sink_client):
  """Upload test result to result_sink.

  Args:
    test_result: A BaseTestResult object
    test_file_name: A string representing the file location of the test
    result_sink_client: A ResultSinkClient object

  Returns:
    N/A
  """
  # Some tests put in non utf-8 char as part of the test
  # which breaks uploads, so need to decode and re-encode.
  log_decoded = test_result.GetLog()
  if isinstance(log_decoded, bytes):
    log_decoded = log_decoded.decode('utf-8', 'replace')
  html_artifact = ''
  https_artifacts = []
  for link_name, link_url in sorted(test_result.GetLinks().items()):
    if link_url.startswith('https:'):
      https_artifacts.append('<li><a target="_blank" href=%s>%s</a></li>' %
                             (link_url, link_name))
    else:
      logging.info('Skipping non-https link %r (%s) for test %s.', link_name,
                   link_url, test_result.GetName())
  if https_artifacts:
    html_artifact += '<ul>%s</ul>' % '\n'.join(https_artifacts)
  result_sink_client.Post(test_result.GetNameForResultSink(),
                          test_result.GetType(),
                          test_result.GetDuration(),
                          log_decoded,
                          test_file_name,
                          variant=test_result.GetVariantForResultSink(),
                          failure_reason=test_result.GetFailureReason(),
                          html_artifact=html_artifact)


_SUPPORTED_IN_PLATFORM_MODE = [
  # TODO(jbudorick): Add support for more test types.
  'gtest',
  'hostside',
  'instrumentation',
  'junit',
  'linker',
  'monkey',
]


def UploadExceptions(result_sink_client, exc_recorder):
  if not result_sink_client or not exc_recorder.size():
    return

  try_count_max = 3
  for try_count in range(1, try_count_max + 1):
    logging.info('Uploading exception records to RDB. (TRY %d/%d)', try_count,
                 try_count_max)
    try:
      record_dict = exc_recorder.to_dict()
      result_sink_client.UpdateInvocationExtendedProperties(
          {exc_recorder.EXCEPTION_OCCURRENCES_KEY: record_dict})
      exc_recorder.clear()
      break
    except Exception as e:  # pylint: disable=W0703
      logging.error("Got error %s when uploading exception records.", e)
      # Upload can fail due to record size being too big.
      # In this case, let's try to reduce the size.
      if try_count == try_count_max - 2:
        # Clear all the stackstrace to reduce size.
        exc_recorder.clear_stacktrace()
      elif try_count == try_count_max - 1:
        # Clear all the records and just report the upload failure.
        exc_recorder.clear()
        exc_recorder.register(e)
      elif try_count == try_count_max:
        # Swallow the exception if the upload fails again and hit the max
        # try so that it won't fail the test task (and it shouldn't).
        exc_recorder.clear()
        logging.error("Hit max retry. Skip uploading exception records.")


def RunTestsInPlatformMode(args, result_sink_client=None):

  def infra_error(message):
    logging.fatal(message)
    sys.exit(constants.INFRA_EXIT_CODE)

  if args.command not in _SUPPORTED_IN_PLATFORM_MODE:
    infra_error('%s is not yet supported in platform mode' % args.command)

  ### Set up sigterm handler.

  contexts_to_notify_on_sigterm = []
  def unexpected_sigterm(_signum, _frame):
    msg = [
      'Received SIGTERM. Shutting down.',
    ]
    for live_thread in threading.enumerate():
      # pylint: disable=protected-access
      thread_stack = ''.join(traceback.format_stack(
          sys._current_frames()[live_thread.ident]))
      msg.extend([
        'Thread "%s" (ident: %s) is currently running:' % (
            live_thread.name, live_thread.ident),
        thread_stack])

    for context in contexts_to_notify_on_sigterm:
      context.ReceivedSigterm()

    infra_error('\n'.join(msg))

  signal.signal(signal.SIGTERM, unexpected_sigterm)

  ### Set up results handling.
  # TODO(jbudorick): Rewrite results handling.

  # all_raw_results is a list of lists of
  # base_test_result.TestRunResults objects. Each instance of
  # TestRunResults contains all test results produced by a single try,
  # while each list of TestRunResults contains all tries in a single
  # iteration.
  all_raw_results = []

  # all_iteration_results is a list of base_test_result.TestRunResults
  # objects. Each instance of TestRunResults contains the last test
  # result for each test run in that iteration.
  all_iteration_results = []

  global_results_tags = set()

  json_file = tempfile.NamedTemporaryFile(delete=False)
  json_file.close()

  @contextlib.contextmanager
  def json_finalizer():
    try:
      yield
    finally:
      if args.json_results_file and os.path.exists(json_file.name):
        shutil.move(json_file.name, args.json_results_file)
      elif args.isolated_script_test_output and os.path.exists(json_file.name):
        shutil.move(json_file.name, args.isolated_script_test_output)
      else:
        os.remove(json_file.name)

  @contextlib.contextmanager
  def json_writer():
    try:
      yield
    except Exception:
      global_results_tags.add('UNRELIABLE_RESULTS')
      raise
    finally:
      if args.isolated_script_test_output:
        interrupted = 'UNRELIABLE_RESULTS' in global_results_tags
        json_results.GenerateJsonTestResultFormatFile(all_raw_results,
                                                      interrupted,
                                                      json_file.name,
                                                      indent=2)
      else:
        json_results.GenerateJsonResultsFile(
            all_raw_results,
            json_file.name,
            global_tags=list(global_results_tags),
            indent=2)

      test_class_to_file_name_dict = {}
      # Test Location is only supported for instrumentation tests as it
      # requires the size-info file.
      if test_instance.TestType() == 'instrumentation':
        test_class_to_file_name_dict = _CreateClassToFileNameDict(args.test_apk)

      if result_sink_client:
        for run in all_raw_results:
          for results in run:
            for r in results.GetAll():
              # Matches chrome.page_info.PageInfoViewTest#testChromePage
              match = re.search(r'^(.+\..+)#', r.GetName())
              test_file_name = test_class_to_file_name_dict.get(
                  match.group(1)) if match else None
              _SinkTestResult(r, test_file_name, result_sink_client)

  @contextlib.contextmanager
  def upload_logcats_file():
    try:
      yield
    finally:
      if not args.logcat_output_file:
        logging.critical('Cannot upload logcat file: no file specified.')
      elif not os.path.exists(args.logcat_output_file):
        logging.critical("Cannot upload logcat file: file doesn't exist.")
      else:
        with open(args.logcat_output_file) as src:
          dst = logdog_helper.open_text('unified_logcats')
          if dst:
            shutil.copyfileobj(src, dst)
            dst.close()
            logging.critical(
                'Logcat: %s', logdog_helper.get_viewer_url('unified_logcats'))


  logcats_uploader = contextlib_ext.Optional(
      upload_logcats_file(),
      'upload_logcats_file' in args and args.upload_logcats_file)

  save_detailed_results = (args.local_output or not local_utils.IsOnSwarming()
                           ) and not args.isolated_script_test_output

  @contextlib.contextmanager
  def exceptions_uploader():
    try:
      yield
    finally:
      UploadExceptions(result_sink_client, exception_recorder)

  ### Set up test objects.

  out_manager = output_manager_factory.CreateOutputManager(args)
  env = environment_factory.CreateEnvironment(
      args, out_manager, infra_error)
  test_instance = test_instance_factory.CreateTestInstance(args, infra_error)
  test_run = test_run_factory.CreateTestRun(env, test_instance, infra_error)

  contexts_to_notify_on_sigterm.append(env)
  contexts_to_notify_on_sigterm.append(test_run)

  if args.list_tests:
    try:
      with out_manager, env, test_instance, test_run:
        test_names = test_run.GetTestsForListing()
      print('There are {} tests:'.format(len(test_names)))
      for n in test_names:
        print(n)
      return 0
    except NotImplementedError:
      sys.stderr.write('Test does not support --list-tests (type={}).\n'.format(
          args.command))
      return 1

  if getattr(args, 'list_data', False):
    with out_manager, env, test_instance, test_run:
      data_deps = test_run.GetDataDepsForListing()

    print('There are {} data files:'.format(len(data_deps)))
    for d in data_deps:
      print(d)
    return 0

  ### Run.
  with out_manager, json_finalizer():
    # |raw_logs_fh| is only used by Robolectric tests.
    raw_logs_fh = io.StringIO() if save_detailed_results else None

    with json_writer(), exceptions_uploader(), logcats_uploader, \
         env, test_instance, test_run:

      repetitions = (range(args.repeat +
                           1) if args.repeat >= 0 else itertools.count())
      result_counts = collections.defaultdict(
          lambda: collections.defaultdict(int))
      iteration_count = 0
      for _ in repetitions:
        # raw_results will be populated with base_test_result.TestRunResults by
        # test_run.RunTests(). It is immediately added to all_raw_results so
        # that in the event of an exception, all_raw_results will already have
        # the up-to-date results and those can be written to disk.
        raw_results = []
        all_raw_results.append(raw_results)

        test_run.RunTests(raw_results, raw_logs_fh=raw_logs_fh)
        if not raw_results:
          all_raw_results.pop()
          continue

        iteration_results = base_test_result.TestRunResults()
        for r in reversed(raw_results):
          iteration_results.AddTestRunResults(r)
        all_iteration_results.append(iteration_results)
        iteration_count += 1

        for r in iteration_results.GetAll():
          result_counts[r.GetName()][r.GetType()] += 1

        report_results.LogFull(
            results=iteration_results,
            test_type=test_instance.TestType(),
            test_package=test_run.TestPackage(),
            annotation=getattr(args, 'annotations', None),
            flakiness_server=getattr(args, 'flakiness_dashboard_server',
                                     None))

        failed_tests = (iteration_results.GetNotPass() -
                        iteration_results.GetSkip())
        if failed_tests:
          _LogRerunStatement(failed_tests, args.wrapper_script_args)

        if args.break_on_failure and not iteration_results.DidRunPass():
          break

      if iteration_count > 1:
        # display summary results
        # only display results for a test if at least one test did not pass
        all_pass = 0
        tot_tests = 0
        for test_name in result_counts:
          tot_tests += 1
          if any(result_counts[test_name][x] for x in (
              base_test_result.ResultType.FAIL,
              base_test_result.ResultType.CRASH,
              base_test_result.ResultType.TIMEOUT,
              base_test_result.ResultType.UNKNOWN)):
            logging.critical(
                '%s: %s',
                test_name,
                ', '.join('%s %s' % (str(result_counts[test_name][i]), i)
                          for i in base_test_result.ResultType.GetTypes()))
          else:
            all_pass += 1

        logging.critical('%s of %s tests passed in all %s runs',
                         str(all_pass),
                         str(tot_tests),
                         str(iteration_count))

    if save_detailed_results:
      assert raw_logs_fh
      raw_logs_fh.seek(0)
      raw_logs = raw_logs_fh.read()
      if raw_logs:
        with out_manager.ArchivedTempfile(
            'raw_logs.txt', 'raw_logs',
            output_manager.Datatype.TEXT) as raw_logs_file:
          raw_logs_file.write(raw_logs)
        logging.critical('RAW LOGS: %s', raw_logs_file.Link())

      with out_manager.ArchivedTempfile(
          'test_results_presentation.html',
          'test_results_presentation',
          output_manager.Datatype.HTML) as results_detail_file:
        result_html_string, _, _ = test_results_presentation.result_details(
            json_path=json_file.name,
            test_name=args.command,
            cs_base_url='http://cs.chromium.org',
            local_output=True)
        results_detail_file.write(result_html_string)
        results_detail_file.flush()
      logging.critical('TEST RESULTS: %s', results_detail_file.Link())

      ui_screenshots = test_results_presentation.ui_screenshot_set(
          json_file.name)
      if ui_screenshots:
        with out_manager.ArchivedTempfile(
            'ui_screenshots.json',
            'ui_capture',
            output_manager.Datatype.JSON) as ui_screenshot_file:
          ui_screenshot_file.write(ui_screenshots)
        logging.critical('UI Screenshots: %s', ui_screenshot_file.Link())

  return (0 if all(r.DidRunPass() for r in all_iteration_results)
          else constants.ERROR_EXIT_CODE)


def _LogRerunStatement(failed_tests, wrapper_arg_str):
  """Logs a message that can rerun the failed tests.

  Logs a copy/pasteable message that filters tests so just the failing tests
  are run.

  Args:
    failed_tests: A set of test results that did not pass.
    wrapper_arg_str: A string of args that were passed to the called wrapper
        script.
  """
  rerun_arg_list = []
  try:
    constants.CheckOutputDirectory()
  # constants.CheckOutputDirectory throws bare exceptions.
  except:  # pylint: disable=bare-except
    logging.exception('Output directory not found. Unable to generate failing '
                      'test filter file.')
    return

  output_directory = constants.GetOutDirectory()
  if not os.path.exists(output_directory):
    logging.error('Output directory not found. Unable to generate failing '
                  'test filter file.')
    return

  test_filter_file = os.path.join(os.path.relpath(output_directory),
                                  _RERUN_FAILED_TESTS_FILE)
  arg_list = shlex.split(wrapper_arg_str) if wrapper_arg_str else sys.argv
  index = 0
  while index < len(arg_list):
    arg = arg_list[index]
    # Skip adding the filter=<file> and/or the filter arg as we're replacing
    # it with the new filter arg.
    # This covers --test-filter=, --test-launcher-filter-file=, --gtest-filter=,
    # --test-filter *Foobar.baz, -f *foobar, --package-filter <package>,
    # --runner-filter <runner>.
    if 'filter' in arg or arg == '-f':
      index += 1 if '=' in arg else 2
      continue

    rerun_arg_list.append(arg)
    index += 1

  failed_test_list = [str(t) for t in failed_tests]
  with open(test_filter_file, 'w') as fp:
    for t in failed_test_list:
      # Test result names can have # in them that don't match when applied as
      # a test name filter.
      fp.write('%s\n' % t.replace('#', '.'))

  rerun_arg_list.append('--test-launcher-filter-file=%s' % test_filter_file)
  msg = """
    %d Test(s) failed.
    Rerun failed tests with copy and pastable command:
        %s
    """
  logging.critical(msg, len(failed_tests), shlex.join(rerun_arg_list))


def DumpThreadStacks(_signal, _frame):
  for thread in threading.enumerate():
    reraiser_thread.LogThreadStack(thread)


def main():
  signal.signal(signal.SIGUSR1, DumpThreadStacks)

  parser = argparse.ArgumentParser()
  command_parsers = parser.add_subparsers(
      title='test types', dest='command')

  subp = command_parsers.add_parser(
      'gtest',
      help='googletest-based C++ tests')
  AddCommonOptions(subp)
  AddDeviceOptions(subp)
  AddEmulatorOptions(subp)
  AddGTestOptions(subp)
  AddTracingOptions(subp)
  AddCommandLineOptions(subp)

  subp = command_parsers.add_parser(
      'hostside',
      help='Webview CTS host-side tests')
  AddCommonOptions(subp)
  AddDeviceOptions(subp)
  AddEmulatorOptions(subp)
  AddHostsideTestOptions(subp)

  subp = command_parsers.add_parser(
      'instrumentation',
      help='InstrumentationTestCase-based Java tests')
  AddCommonOptions(subp)
  AddDeviceOptions(subp)
  AddEmulatorOptions(subp)
  AddInstrumentationTestOptions(subp)
  AddSkiaGoldTestOptions(subp)
  AddTracingOptions(subp)
  AddCommandLineOptions(subp)

  subp = command_parsers.add_parser(
      'junit',
      help='JUnit4-based Java tests')
  AddCommonOptions(subp)
  AddJUnitTestOptions(subp)

  subp = command_parsers.add_parser(
      'linker',
      help='linker tests')
  AddCommonOptions(subp)
  AddDeviceOptions(subp)
  AddEmulatorOptions(subp)
  AddLinkerTestOptions(subp)

  subp = command_parsers.add_parser(
      'monkey',
      help="tests based on Android's monkey command")
  AddCommonOptions(subp)
  AddDeviceOptions(subp)
  AddEmulatorOptions(subp)
  AddMonkeyTestOptions(subp)

  subp = command_parsers.add_parser(
      'python',
      help='python tests based on unittest.TestCase')
  AddCommonOptions(subp)
  AddPythonTestOptions(subp)

  args, unknown_args = parser.parse_known_args()

  if unknown_args:
    if getattr(args, 'allow_unknown', None):
      args.command_line_flags = unknown_args
    else:
      parser.error('unrecognized arguments: %s' % ' '.join(unknown_args))

  # --enable-concurrent-adb does not handle device reboots gracefully.
  if getattr(args, 'enable_concurrent_adb', None):
    if getattr(args, 'replace_system_package', None):
      logging.warning(
          'Ignoring --enable-concurrent-adb due to --replace-system-package')
      args.enable_concurrent_adb = False
    elif getattr(args, 'system_packages_to_remove', None):
      logging.warning(
          'Ignoring --enable-concurrent-adb due to --remove-system-package')
      args.enable_concurrent_adb = False
    elif getattr(args, 'use_webview_provider', None):
      logging.warning(
          'Ignoring --enable-concurrent-adb due to --use-webview-provider')
      args.enable_concurrent_adb = False

  if (getattr(args, 'coverage_on_the_fly', False)
      and not getattr(args, 'coverage_dir', '')):
    parser.error('--coverage-on-the-fly requires --coverage-dir')

  if (getattr(args, 'debug_socket', None)
      or getattr(args, 'wait_for_java_debugger', None)):
    args.num_retries = 0

  # Result-sink may not exist in the environment if rdb stream is not enabled.
  result_sink_client = result_sink.TryInitClient()

  try:
    return RunTestsCommand(args, result_sink_client)
  except base_error.BaseError as e:
    logging.exception('Error occurred.')
    if e.is_infra_error:
      return constants.INFRA_EXIT_CODE
    return constants.ERROR_EXIT_CODE
  except Exception:  # pylint: disable=W0703
    logging.exception('Unrecognized error occurred.')
    return constants.ERROR_EXIT_CODE


if __name__ == '__main__':
  exit_code = main()
  if exit_code == constants.INFRA_EXIT_CODE:
    # This exit code is returned in case of missing, unreachable,
    # or otherwise not fit for purpose test devices.
    # When this happens, the graceful cleanup triggered by sys.exit()
    # hangs indefinitely (on swarming - until it hits 20min timeout).
    # Skip cleanup (other than flushing output streams) and exit forcefully
    # to avoid the hang.
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(exit_code)  # pylint: disable=protected-access
  else:
    sys.exit(exit_code)
