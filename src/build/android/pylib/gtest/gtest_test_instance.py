# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.



import html.parser
import json
import logging
import os
import re
import tempfile
import threading
import xml.etree.ElementTree

from devil.android import apk_helper
from pylib import constants
from pylib.constants import host_paths
from pylib.base import base_test_result
from pylib.base import test_instance
from pylib.symbols import stack_symbolizer
from pylib.utils import test_filter

with host_paths.SysPath(host_paths.BUILD_UTIL_PATH):
  from lib.common import unittest_util

BROWSER_TEST_SUITES = [
    'android_browsertests',
    'android_sync_integration_tests',
    'components_browsertests',
    'content_browsertests',
    'weblayer_browsertests',
]

# The max number of tests to run on a shard during the test run.
MAX_SHARDS = 256

RUN_IN_SUB_THREAD_TEST_SUITES = [
    # Multiprocess tests should be run outside of the main thread.
    'base_unittests',  # file_locking_unittest.cc uses a child process.
    'gwp_asan_unittests',
    'ipc_perftests',
    'ipc_tests',
    'mojo_perftests',
    'mojo_unittests',
    'net_unittests'
]


# Used for filtering large data deps at a finer grain than what's allowed in
# isolate files since pushing deps to devices is expensive.
# Wildcards are allowed.
_DEPS_EXCLUSION_LIST = [
    'chrome/test/data/extensions/api_test',
    'chrome/test/data/extensions/secure_shell',
    'chrome/test/data/firefox*',
    'chrome/test/data/gpu',
    'chrome/test/data/image_decoding',
    'chrome/test/data/import',
    'chrome/test/data/page_cycler',
    'chrome/test/data/perf',
    'chrome/test/data/pyauto_private',
    'chrome/test/data/safari_import',
    'chrome/test/data/scroll',
    'chrome/test/data/third_party',
    'third_party/hunspell_dictionaries/*.dic',
    # crbug.com/258690
    'webkit/data/bmp_decoder',
    'webkit/data/ico_decoder',
]


_EXTRA_NATIVE_TEST_ACTIVITY = (
    'org.chromium.native_test.NativeTestInstrumentationTestRunner.'
        'NativeTestActivity')
_EXTRA_RUN_IN_SUB_THREAD = (
    'org.chromium.native_test.NativeTest.RunInSubThread')
EXTRA_SHARD_NANO_TIMEOUT = (
    'org.chromium.native_test.NativeTestInstrumentationTestRunner.'
        'ShardNanoTimeout')
_EXTRA_SHARD_SIZE_LIMIT = (
    'org.chromium.native_test.NativeTestInstrumentationTestRunner.'
        'ShardSizeLimit')

# TODO(jbudorick): Remove these once we're no longer parsing stdout to generate
# results.
_RE_TEST_STATUS = re.compile(
    # Test state.
    r'\[ +((?:RUN)|(?:FAILED)|(?:OK)|(?:CRASHED)|(?:SKIPPED)) +\] ?'
    # Test name.
    r'([^ ]+)?'
    # Optional parameters.
    r'(?:, where'
    #   Type parameter
    r'(?: TypeParam = [^()]*(?: and)?)?'
    #   Value parameter
    r'(?: GetParam\(\) = [^()]*)?'
    # End of optional parameters.
    ')?'
    # Optional test execution time.
    r'(?: \((\d+) ms\))?$')
# Crash detection constants.
_RE_TEST_ERROR = re.compile(r'FAILURES!!! Tests run: \d+,'
                                    r' Failures: \d+, Errors: 1')
_RE_TEST_CURRENTLY_RUNNING = re.compile(
    r'\[.*ERROR:.*?\] Currently running: (.*)')
_RE_TEST_DCHECK_FATAL = re.compile(r'\[.*:FATAL:.*\] (.*)')
_RE_DISABLED = re.compile(r'DISABLED_')
_RE_FLAKY = re.compile(r'FLAKY_')

# Detect a new launcher invocation. When encountered, the output parser will
# stop recording logs for a suddenly crashed test (if one was running) in the
# previous invocation.
_RE_LAUNCHER_MAIN_START = re.compile(r'>>ScopedMainEntryLogger')

# Regex that matches the printout when there are test failures.
# matches "[  FAILED  ] 1 test, listed below:"
_RE_ANY_TESTS_FAILED = re.compile(r'\[ +FAILED +\].*listed below')

# Detect stack line in stdout.
_STACK_LINE_RE = re.compile(r'\s*#\d+')

def ParseGTestListTests(raw_list):
  """Parses a raw test list as provided by --gtest_list_tests.

  Args:
    raw_list: The raw test listing with the following format:

    IPCChannelTest.
      SendMessageInChannelConnected
    IPCSyncChannelTest.
      Simple
      DISABLED_SendWithTimeoutMixedOKAndTimeout

  Returns:
    A list of all tests. For the above raw listing:

    [IPCChannelTest.SendMessageInChannelConnected, IPCSyncChannelTest.Simple,
     IPCSyncChannelTest.DISABLED_SendWithTimeoutMixedOKAndTimeout]
  """
  ret = []
  current = ''
  for test in raw_list:
    if not test:
      continue
    if not test.startswith(' '):
      test_case = test.split()[0]
      if test_case.endswith('.'):
        current = test_case
    else:
      test = test.strip()
      if test and not 'YOU HAVE' in test:
        test_name = test.split()[0]
        ret += [current + test_name]
  return ret


def ParseGTestOutput(output, symbolizer, device_abi):
  """Parses raw gtest output and returns a list of results.

  Args:
    output: A list of output lines.
    symbolizer: The symbolizer used to symbolize stack.
    device_abi: Device abi that is needed for symbolization.
  Returns:
    A list of base_test_result.BaseTestResults.
  """
  duration = 0
  fallback_result_type = None
  log = []
  stack = []
  result_type = None
  results = []
  test_name = None

  def symbolize_stack_and_merge_with_log():
    log_string = '\n'.join(log or [])
    if not stack:
      stack_string = ''
    else:
      stack_string = '\n'.join(
          symbolizer.ExtractAndResolveNativeStackTraces(
              stack, device_abi))
    return '%s\n%s' % (log_string, stack_string)

  def handle_possibly_unknown_test():
    if test_name is not None:
      results.append(
          base_test_result.BaseTestResult(
              TestNameWithoutDisabledPrefix(test_name),
              # If we get here, that means we started a test, but it did not
              # produce a definitive test status output, so assume it crashed.
              # crbug/1191716
              fallback_result_type or base_test_result.ResultType.CRASH,
              duration,
              log=symbolize_stack_and_merge_with_log()))

  for l in output:
    matcher = _RE_TEST_STATUS.match(l)
    launcher_main_start_match = _RE_LAUNCHER_MAIN_START.match(l)
    if matcher:
      if matcher.group(1) == 'RUN':
        handle_possibly_unknown_test()
        duration = 0
        fallback_result_type = None
        log = []
        stack = []
        result_type = None
      elif matcher.group(1) == 'OK':
        result_type = base_test_result.ResultType.PASS
      elif matcher.group(1) == 'SKIPPED':
        result_type = base_test_result.ResultType.SKIP
      elif matcher.group(1) == 'FAILED':
        result_type = base_test_result.ResultType.FAIL
      elif matcher.group(1) == 'CRASHED':
        fallback_result_type = base_test_result.ResultType.CRASH
      # Be aware that test name and status might not appear on same line.
      test_name = matcher.group(2) if matcher.group(2) else test_name
      duration = int(matcher.group(3)) if matcher.group(3) else 0

    else:
      # Can possibly add more matchers, such as different results from DCHECK.
      currently_running_matcher = _RE_TEST_CURRENTLY_RUNNING.match(l)
      dcheck_matcher = _RE_TEST_DCHECK_FATAL.match(l)

      if currently_running_matcher:
        test_name = currently_running_matcher.group(1)
        result_type = base_test_result.ResultType.CRASH
        duration = None  # Don't know. Not using 0 as this is unknown vs 0.
      elif dcheck_matcher or launcher_main_start_match:
        result_type = base_test_result.ResultType.CRASH
        duration = None  # Don't know.  Not using 0 as this is unknown vs 0.

    if not launcher_main_start_match:
      if not matcher and _STACK_LINE_RE.match(l):
        stack.append(l)
      else:
        log.append(l)

    if _RE_ANY_TESTS_FAILED.match(l):
      break

    if result_type and test_name:
      # Don't bother symbolizing output if the test passed.
      if result_type == base_test_result.ResultType.PASS:
        stack = []
      results.append(base_test_result.BaseTestResult(
          TestNameWithoutDisabledPrefix(test_name), result_type, duration,
          log=symbolize_stack_and_merge_with_log()))
      test_name = None

  else:
    # Executing this after tests have finished with a failure causes a
    # duplicate test entry to be added to results. crbug/1380825
    handle_possibly_unknown_test()

  return results


def ParseGTestXML(xml_content):
  """Parse gtest XML result."""
  results = []
  if not xml_content:
    return results

  html_parser = html.parser.HTMLParser()

  testsuites = xml.etree.ElementTree.fromstring(xml_content)
  for testsuite in testsuites:
    suite_name = testsuite.attrib['name']
    for testcase in testsuite:
      case_name = testcase.attrib['name']
      result_type = base_test_result.ResultType.PASS
      log = []
      for failure in testcase:
        result_type = base_test_result.ResultType.FAIL
        log.append(html_parser.unescape(failure.attrib['message']))

      results.append(base_test_result.BaseTestResult(
          '%s.%s' % (suite_name, TestNameWithoutDisabledPrefix(case_name)),
          result_type,
          int(float(testcase.attrib['time']) * 1000),
          log=('\n'.join(log) if log else '')))

  return results


def ParseGTestJSON(json_content):
  """Parse results in the JSON Test Results format."""
  results = []
  if not json_content:
    return results

  json_data = json.loads(json_content)

  openstack = list(json_data['tests'].items())

  while openstack:
    name, value = openstack.pop()

    if 'expected' in value and 'actual' in value:
      if value['actual'] == 'PASS':
        result_type = base_test_result.ResultType.PASS
      elif value['actual'] == 'SKIP':
        result_type = base_test_result.ResultType.SKIP
      elif value['actual'] == 'CRASH':
        result_type = base_test_result.ResultType.CRASH
      elif value['actual'] == 'TIMEOUT':
        result_type = base_test_result.ResultType.TIMEOUT
      else:
        result_type = base_test_result.ResultType.FAIL
      results.append(base_test_result.BaseTestResult(name, result_type))
    else:
      openstack += [("%s.%s" % (name, k), v) for k, v in value.items()]

  return results


def TestNameWithoutDisabledPrefix(test_name):
  """Modify the test name without disabled prefix if prefix 'DISABLED_' or
  'FLAKY_' presents.

  Args:
    test_name: The name of a test.
  Returns:
    A test name without prefix 'DISABLED_' or 'FLAKY_'.
  """
  disabled_prefixes = [_RE_DISABLED, _RE_FLAKY]
  for dp in disabled_prefixes:
    test_name = dp.sub('', test_name)
  return test_name

class GtestTestInstance(test_instance.TestInstance):

  def __init__(self, args, data_deps_delegate, error_func):
    super().__init__()
    # TODO(jbudorick): Support multiple test suites.
    if len(args.suite_name) > 1:
      raise ValueError('Platform mode currently supports only 1 gtest suite')
    self._additional_apks = []
    self._coverage_dir = args.coverage_dir
    self._exe_dist_dir = None
    self._external_shard_index = args.test_launcher_shard_index
    self._extract_test_list_from_filter = args.extract_test_list_from_filter
    self._filter_tests_lock = threading.Lock()
    self._gs_test_artifacts_bucket = args.gs_test_artifacts_bucket
    self._isolated_script_test_output = args.isolated_script_test_output
    self._isolated_script_test_perf_output = (
        args.isolated_script_test_perf_output)
    self._render_test_output_dir = args.render_test_output_dir
    self._shard_timeout = args.shard_timeout
    self._store_tombstones = args.store_tombstones
    self._suite = args.suite_name[0]
    self._symbolizer = stack_symbolizer.Symbolizer(None)
    self._total_external_shards = args.test_launcher_total_shards
    self._wait_for_java_debugger = args.wait_for_java_debugger
    self._use_existing_test_data = args.use_existing_test_data
    self._deploy_mock_openxr_runtime = args.deploy_mock_openxr_runtime

    # GYP:
    if args.executable_dist_dir:
      self._exe_dist_dir = os.path.abspath(args.executable_dist_dir)
    else:
      # TODO(agrieve): Remove auto-detection once recipes pass flag explicitly.
      exe_dist_dir = os.path.join(constants.GetOutDirectory(),
                                  '%s__dist' % self._suite)

      if os.path.exists(exe_dist_dir):
        self._exe_dist_dir = exe_dist_dir

    incremental_part = ''
    if args.test_apk_incremental_install_json:
      incremental_part = '_incremental'

    self._test_launcher_batch_limit = MAX_SHARDS
    if (args.test_launcher_batch_limit
        and 0 < args.test_launcher_batch_limit < MAX_SHARDS):
      self._test_launcher_batch_limit = args.test_launcher_batch_limit

    apk_path = os.path.join(
        constants.GetOutDirectory(), '%s_apk' % self._suite,
        '%s-debug%s.apk' % (self._suite, incremental_part))
    self._test_apk_incremental_install_json = (
        args.test_apk_incremental_install_json)
    if not os.path.exists(apk_path):
      self._apk_helper = None
    else:
      self._apk_helper = apk_helper.ApkHelper(apk_path)
      self._extras = {
          _EXTRA_NATIVE_TEST_ACTIVITY: self._apk_helper.GetActivityName(),
      }
      if args.timeout_scale and args.timeout_scale != 1:
        self._extras[_EXTRA_RUN_IN_SUB_THREAD] = 1

      if self._suite in RUN_IN_SUB_THREAD_TEST_SUITES:
        self._extras[_EXTRA_RUN_IN_SUB_THREAD] = 1
      if self._suite in BROWSER_TEST_SUITES:
        self._extras[_EXTRA_SHARD_SIZE_LIMIT] = 1
        self._extras[EXTRA_SHARD_NANO_TIMEOUT] = int(1e9 * self._shard_timeout)
        self._shard_timeout = 10 * self._shard_timeout
      if args.wait_for_java_debugger:
        self._extras[EXTRA_SHARD_NANO_TIMEOUT] = int(1e15)  # Forever

    if not self._apk_helper and not self._exe_dist_dir:
      error_func('Could not find apk or executable for %s' % self._suite)

    for x in args.additional_apks:
      if not os.path.exists(x):
        error_func('Could not find additional APK: %s' % x)

      apk = apk_helper.ToHelper(x)
      self._additional_apks.append(apk)

    self._data_deps = []
    self._gtest_filters = test_filter.InitializeFiltersFromArgs(args)
    self._run_disabled = args.run_disabled
    self._run_pre_tests = args.run_pre_tests

    self._data_deps_delegate = data_deps_delegate
    self._runtime_deps_path = args.runtime_deps_path
    if not self._runtime_deps_path:
      logging.warning('No data dependencies will be pushed.')

    if args.app_data_files:
      self._app_data_files = args.app_data_files
      if args.app_data_file_dir:
        self._app_data_file_dir = args.app_data_file_dir
      else:
        self._app_data_file_dir = tempfile.mkdtemp()
        logging.critical('Saving app files to %s', self._app_data_file_dir)
    else:
      self._app_data_files = None
      self._app_data_file_dir = None

    self._flags = None
    self._initializeCommandLineFlags(args)

    # TODO(jbudorick): Remove this once it's deployed.
    self._enable_xml_result_parsing = args.enable_xml_result_parsing

  def _initializeCommandLineFlags(self, args):
    self._flags = []
    if args.command_line_flags:
      self._flags.extend(args.command_line_flags)
    if args.device_flags_file:
      with open(args.device_flags_file) as f:
        stripped_lines = (l.strip() for l in f)
        self._flags.extend(flag for flag in stripped_lines if flag)
    if args.run_disabled:
      self._flags.append('--gtest_also_run_disabled_tests')

  @property
  def activity(self):
    return self._apk_helper and self._apk_helper.GetActivityName()

  @property
  def additional_apks(self):
    return self._additional_apks

  @property
  def apk(self):
    return self._apk_helper and self._apk_helper.path

  @property
  def apk_helper(self):
    return self._apk_helper

  @property
  def app_file_dir(self):
    return self._app_data_file_dir

  @property
  def app_files(self):
    return self._app_data_files

  @property
  def coverage_dir(self):
    return self._coverage_dir

  @property
  def deploy_mock_openxr_runtime(self):
    return self._deploy_mock_openxr_runtime

  @property
  def enable_xml_result_parsing(self):
    return self._enable_xml_result_parsing

  @property
  def exe_dist_dir(self):
    return self._exe_dist_dir

  @property
  def external_shard_index(self):
    return self._external_shard_index

  @property
  def extract_test_list_from_filter(self):
    return self._extract_test_list_from_filter

  @property
  def extras(self):
    return self._extras

  @property
  def flags(self):
    return self._flags

  @property
  def gs_test_artifacts_bucket(self):
    return self._gs_test_artifacts_bucket

  @property
  def gtest_filters(self):
    return self._gtest_filters

  @property
  def isolated_script_test_output(self):
    return self._isolated_script_test_output

  @property
  def isolated_script_test_perf_output(self):
    return self._isolated_script_test_perf_output

  @property
  def render_test_output_dir(self):
    return self._render_test_output_dir

  @property
  def package(self):
    return self._apk_helper and self._apk_helper.GetPackageName()

  @property
  def permissions(self):
    return self._apk_helper and self._apk_helper.GetPermissions()

  @property
  def runner(self):
    return self._apk_helper and self._apk_helper.GetInstrumentationName()

  @property
  def shard_timeout(self):
    return self._shard_timeout

  @property
  def store_tombstones(self):
    return self._store_tombstones

  @property
  def suite(self):
    return self._suite

  @property
  def symbolizer(self):
    return self._symbolizer

  @property
  def test_apk_incremental_install_json(self):
    return self._test_apk_incremental_install_json

  @property
  def test_launcher_batch_limit(self):
    return self._test_launcher_batch_limit

  @property
  def total_external_shards(self):
    return self._total_external_shards

  @property
  def wait_for_java_debugger(self):
    return self._wait_for_java_debugger

  @property
  def use_existing_test_data(self):
    return self._use_existing_test_data

  @property
  def run_pre_tests(self):
    return self._run_pre_tests

  #override
  def TestType(self):
    return 'gtest'

  #override
  def GetPreferredAbis(self):
    if not self._apk_helper:
      return None
    return self._apk_helper.GetAbis()

  #override
  def SetUp(self):
    """Map data dependencies via isolate."""
    self._data_deps.extend(
        self._data_deps_delegate(self._runtime_deps_path))

  def GetDataDependencies(self):
    """Returns the test suite's data dependencies.

    Returns:
      A list of (host_path, device_path) tuples to push. If device_path is
      None, the client is responsible for determining where to push the file.
    """
    return self._data_deps

  def FilterTests(self, test_list, disabled_prefixes=None):
    """Filters |test_list| based on prefixes and, if present, a filter string.

    Args:
      test_list: The list of tests to filter.
      disabled_prefixes: A list of test prefixes to filter. Defaults to
        DISABLED_, FLAKY_, FAILS_, PRE_, and MANUAL_
    Returns:
      A filtered list of tests to run.
    """
    gtest_filter_strings = [
        self._GenerateDisabledFilterString(disabled_prefixes)]
    if self._gtest_filters:
      gtest_filter_strings.extend(self._gtest_filters)

    filtered_test_list = test_list
    # This lock is required because on older versions of Python
    # |unittest_util.FilterTestNames| use of |fnmatch| is not threadsafe.
    with self._filter_tests_lock:
      for gtest_filter_string in gtest_filter_strings:
        logging.debug('Filtering tests using: %s', gtest_filter_string)
        filtered_test_list = unittest_util.FilterTestNames(
            filtered_test_list, gtest_filter_string)

      if self._run_disabled and self._gtest_filters:
        out_filtered_test_list = list(set(test_list)-set(filtered_test_list))
        for test in out_filtered_test_list:
          test_name_no_disabled = TestNameWithoutDisabledPrefix(test)
          if test_name_no_disabled == test:
            continue
          if all(
              unittest_util.FilterTestNames([test_name_no_disabled],
                                            gtest_filter)
              for gtest_filter in self._gtest_filters):
            filtered_test_list.append(test)
    return filtered_test_list

  def _GenerateDisabledFilterString(self, disabled_prefixes):
    disabled_filter_items = []

    if disabled_prefixes is None:
      disabled_prefixes = ['FAILS_']
      if '--run-manual' not in self._flags:
        disabled_prefixes += ['MANUAL_']
      if not self._run_disabled:
        disabled_prefixes += ['DISABLED_', 'FLAKY_']
      if not self._run_pre_tests:
        disabled_prefixes += ['PRE_']

    disabled_filter_items += ['%s*' % dp for dp in disabled_prefixes]
    disabled_filter_items += ['*.%s*' % dp for dp in disabled_prefixes]

    disabled_tests_file_path = os.path.join(
        host_paths.DIR_SOURCE_ROOT, 'build', 'android', 'pylib', 'gtest',
        'filter', '%s_disabled' % self._suite)
    if disabled_tests_file_path and os.path.exists(disabled_tests_file_path):
      with open(disabled_tests_file_path) as disabled_tests_file:
        disabled_filter_items += [
            '%s' % l for l in (line.strip() for line in disabled_tests_file)
            if l and not l.startswith('#')]

    return '*-%s' % ':'.join(disabled_filter_items)

  #override
  def TearDown(self):
    """Do nothing."""
