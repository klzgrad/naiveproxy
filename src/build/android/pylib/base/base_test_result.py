# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module containing base test results classes."""


import functools
import re
import sys
import threading

from lib.results import result_types

# This must match the source adding the suffix: bit.ly/3Zmwwyx
MULTIPROCESS_SUFFIX = '__multiprocess_mode'

# This must match the source adding the suffix: bit.ly/3Qt0Ww4
_NULL_MUTATION_SUFFIX = '__null_'
_MUTATION_SUFFIX_PATTERN = re.compile(r'^(.*)__([a-zA-Z]+)\.\.([a-zA-Z]+)_$')


class ResultType:
  """Class enumerating test types.

  Wraps the results defined in //build/util/lib/results/.
  """
  PASS = result_types.PASS
  SKIP = result_types.SKIP
  FAIL = result_types.FAIL
  CRASH = result_types.CRASH
  TIMEOUT = result_types.TIMEOUT
  UNKNOWN = result_types.UNKNOWN
  NOTRUN = result_types.NOTRUN

  @staticmethod
  def GetTypes():
    """Get a list of all test types."""
    return [ResultType.PASS, ResultType.SKIP, ResultType.FAIL,
            ResultType.CRASH, ResultType.TIMEOUT, ResultType.UNKNOWN,
            ResultType.NOTRUN]


@functools.total_ordering
class BaseTestResult:
  """Base class for a single test result."""

  def __init__(self, name, test_type, duration=0, log='', failure_reason=None):
    """Construct a BaseTestResult.

    Args:
      name: Name of the test which defines uniqueness.
      test_type: Type of the test result as defined in ResultType.
      duration: Time it took for the test to run in milliseconds.
      log: An optional string listing any errors.
    """
    assert name
    assert test_type in ResultType.GetTypes()
    self._name = name
    self._test_type = test_type
    self._duration = duration
    self._log = log
    self._failure_reason = failure_reason
    self._links = {}
    self._webview_multiprocess_mode = MULTIPROCESS_SUFFIX in name

  def __str__(self):
    return self._name

  def __repr__(self):
    return self._name

  def __eq__(self, other):
    return self.GetName() == other.GetName()

  def __lt__(self, other):
    return self.GetName() < other.GetName()

  def __hash__(self):
    return hash(self._name)

  def SetName(self, name):
    """Set the test name.

    Because we're putting this into a set, this should only be used if moving
    this test result into another set.
    """
    self._name = name

  def GetName(self):
    """Get the test name."""
    return self._name

  def GetNameForResultSink(self):
    """Get the test name to be reported to resultsink."""
    raw_name = self.GetName()

    # The name can include suffixes encoding Webview variant data:
    # a Webview multiprocess mode suffix and an AwSettings mutation suffix.
    # If both are present, the mutation suffix will come after the multiprocess
    # suffix. The mutation suffix can either be "__null_" or "__{key}..{value}_"
    #
    # Examples:
    # (...)AwSettingsTest#testAssetUrl__multiprocess_mode__allMutations..true_
    # (...)AwSettingsTest#testAssetUrl__multiprocess_mode__null_
    # (...)AwSettingsTest#testAssetUrl__allMutations..true_
    # org.chromium.android_webview.test.AwSettingsTest#testAssetUrl__null_

    # first, strip any AwSettings mutation parameter information
    # from the RHS of the raw_name
    if raw_name.endswith(_NULL_MUTATION_SUFFIX):
      raw_name = raw_name[:-len(_NULL_MUTATION_SUFFIX)]
    elif match := _MUTATION_SUFFIX_PATTERN.search(raw_name):
      raw_name = match.group(1)

    # At this stage, the name will only have the multiprocess suffix appended,
    # if applicable.
    #
    # Examples:
    # (...)AwSettingsTest#testAssetUrl__multiprocess_mode
    # org.chromium.android_webview.test.AwSettingsTest#testAssetUrl

    # then check for multiprocess mode suffix and strip it, if present
    if self._webview_multiprocess_mode:
      assert raw_name.endswith(
          MULTIPROCESS_SUFFIX
      ), 'multiprocess mode test raw name should have the corresponding suffix'
      return raw_name[:-len(MULTIPROCESS_SUFFIX)]
    return raw_name

  def SetType(self, test_type):
    """Set the test result type."""
    assert test_type in ResultType.GetTypes()
    self._test_type = test_type

  def GetType(self):
    """Get the test result type."""
    return self._test_type

  def GetDuration(self):
    """Get the test duration."""
    return self._duration

  def SetLog(self, log):
    """Set the test log."""
    self._log = log

  def GetLog(self):
    """Get the test log."""
    return self._log

  def SetFailureReason(self, failure_reason):
    """Set the reason the test failed.

    This should be the first failure the test encounters and exclude any stack
    trace.
    """
    self._failure_reason = failure_reason

  def GetFailureReason(self):
    """Get the reason the test failed.

    Returns None if the test did not fail or if the reason the test failed is
    unknown.
    """
    return self._failure_reason

  def SetLink(self, name, link_url):
    """Set link with test result data."""
    self._links[name] = link_url

  def GetLinks(self):
    """Get dict containing links to test result data."""
    return self._links

  def GetVariantForResultSink(self):
    """Get the variant dict to be reported to result sink."""
    variants = {}
    if match := _MUTATION_SUFFIX_PATTERN.search(self.GetName()):
      # variant keys need to be lowercase
      variants[match.group(2).lower()] = match.group(3)
    if self._webview_multiprocess_mode:
      variants['webview_multiprocess_mode'] = 'Yes'
    return variants or None


class TestRunResults:
  """Set of results for a test run."""

  def __init__(self):
    self._links = {}
    self._results = set()
    self._results_lock = threading.RLock()

  def SetLink(self, name, link_url):
    """Add link with test run results data."""
    self._links[name] = link_url

  def GetLinks(self):
    """Get dict containing links to test run result data."""
    return self._links

  def GetLogs(self):
    """Get the string representation of all test logs."""
    with self._results_lock:
      s = []
      for test_type in ResultType.GetTypes():
        if test_type != ResultType.PASS:
          for t in sorted(self._GetType(test_type)):
            log = t.GetLog()
            if log:
              s.append('[%s] %s:' % (test_type, t))
              s.append(log)
      if sys.version_info.major == 2:
        decoded = [u.decode(encoding='utf-8', errors='ignore') for u in s]
        return '\n'.join(decoded)
      return '\n'.join(s)

  def GetGtestForm(self):
    """Get the gtest string representation of this object."""
    with self._results_lock:
      s = []
      plural = lambda n, s, p: '%d %s' % (n, p if n != 1 else s)
      tests = lambda n: plural(n, 'test', 'tests')

      s.append('[==========] %s ran.' % (tests(len(self.GetAll()))))
      s.append('[  PASSED  ] %s.' % (tests(len(self.GetPass()))))

      skipped = self.GetSkip()
      if skipped:
        s.append('[  SKIPPED ] Skipped %s, listed below:' % tests(len(skipped)))
        for t in sorted(skipped):
          s.append('[  SKIPPED ] %s' % str(t))

      all_failures = self.GetFail().union(self.GetCrash(), self.GetTimeout(),
          self.GetUnknown())
      if all_failures:
        s.append('[  FAILED  ] %s, listed below:' % tests(len(all_failures)))
        for t in sorted(self.GetFail()):
          s.append('[  FAILED  ] %s' % str(t))
        for t in sorted(self.GetCrash()):
          s.append('[  FAILED  ] %s (CRASHED)' % str(t))
        for t in sorted(self.GetTimeout()):
          s.append('[  FAILED  ] %s (TIMEOUT)' % str(t))
        for t in sorted(self.GetUnknown()):
          s.append('[  FAILED  ] %s (UNKNOWN)' % str(t))
        s.append('')
        s.append(plural(len(all_failures), 'FAILED TEST', 'FAILED TESTS'))
      return '\n'.join(s)

  def GetShortForm(self):
    """Get the short string representation of this object."""
    with self._results_lock:
      s = []
      s.append('ALL: %d' % len(self._results))
      for test_type in ResultType.GetTypes():
        s.append('%s: %d' % (test_type, len(self._GetType(test_type))))
      return ''.join([x.ljust(15) for x in s])

  def __str__(self):
    return self.GetGtestForm()

  def AddResult(self, result):
    """Add |result| to the set.

    Args:
      result: An instance of BaseTestResult.
    """
    assert isinstance(result, BaseTestResult)
    with self._results_lock:
      self._results.discard(result)
      self._results.add(result)

  def AddResults(self, results):
    """Add |results| to the set.

    Args:
      results: An iterable of BaseTestResult objects.
    """
    with self._results_lock:
      for t in results:
        self.AddResult(t)

  def AddTestRunResults(self, results):
    """Add the set of test results from |results|.

    Args:
      results: An instance of TestRunResults.
    """
    assert isinstance(results, TestRunResults), (
           'Expected TestRunResult object: %s' % type(results))
    with self._results_lock:
      # pylint: disable=W0212
      self._results.update(results._results)

  def GetAll(self):
    """Get the set of all test results."""
    with self._results_lock:
      return self._results.copy()

  def _GetType(self, test_type):
    """Get the set of test results with the given test type."""
    with self._results_lock:
      return set(t for t in self._results if t.GetType() == test_type)

  def GetPass(self):
    """Get the set of all passed test results."""
    return self._GetType(ResultType.PASS)

  def GetSkip(self):
    """Get the set of all skipped test results."""
    return self._GetType(ResultType.SKIP)

  def GetFail(self):
    """Get the set of all failed test results."""
    return self._GetType(ResultType.FAIL)

  def GetCrash(self):
    """Get the set of all crashed test results."""
    return self._GetType(ResultType.CRASH)

  def GetTimeout(self):
    """Get the set of all timed out test results."""
    return self._GetType(ResultType.TIMEOUT)

  def GetUnknown(self):
    """Get the set of all unknown test results."""
    return self._GetType(ResultType.UNKNOWN)

  def GetNotPass(self):
    """Get the set of all non-passed test results."""
    return self.GetAll() - self.GetPass()

  def DidRunPass(self):
    """Return whether the test run was successful."""
    return not self.GetNotPass() - self.GetSkip()
