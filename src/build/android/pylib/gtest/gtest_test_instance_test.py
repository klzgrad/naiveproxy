#!/usr/bin/env vpython3
# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import unittest

from pylib.base import base_test_result
from pylib.gtest import gtest_test_instance


class GtestTestInstanceTests(unittest.TestCase):

  def testParseGTestListTests_simple(self):
    raw_output = [
      'TestCaseOne.',
      '  testOne',
      '  testTwo',
      'TestCaseTwo.',
      '  testThree',
      '  testFour',
    ]
    actual = gtest_test_instance.ParseGTestListTests(raw_output)
    expected = [
      'TestCaseOne.testOne',
      'TestCaseOne.testTwo',
      'TestCaseTwo.testThree',
      'TestCaseTwo.testFour',
    ]
    self.assertEqual(expected, actual)

  def testParseGTestListTests_typeParameterized_old(self):
    raw_output = [
      'TPTestCase/WithTypeParam/0.',
      '  testOne',
      '  testTwo',
    ]
    actual = gtest_test_instance.ParseGTestListTests(raw_output)
    expected = [
      'TPTestCase/WithTypeParam/0.testOne',
      'TPTestCase/WithTypeParam/0.testTwo',
    ]
    self.assertEqual(expected, actual)

  def testParseGTestListTests_typeParameterized_new(self):
    raw_output = [
      'TPTestCase/WithTypeParam/0.  # TypeParam = TypeParam0',
      '  testOne',
      '  testTwo',
    ]
    actual = gtest_test_instance.ParseGTestListTests(raw_output)
    expected = [
      'TPTestCase/WithTypeParam/0.testOne',
      'TPTestCase/WithTypeParam/0.testTwo',
    ]
    self.assertEqual(expected, actual)

  def testParseGTestListTests_valueParameterized_old(self):
    raw_output = [
      'VPTestCase.',
      '  testWithValueParam/0',
      '  testWithValueParam/1',
    ]
    actual = gtest_test_instance.ParseGTestListTests(raw_output)
    expected = [
      'VPTestCase.testWithValueParam/0',
      'VPTestCase.testWithValueParam/1',
    ]
    self.assertEqual(expected, actual)

  def testParseGTestListTests_valueParameterized_new(self):
    raw_output = [
      'VPTestCase.',
      '  testWithValueParam/0  # GetParam() = 0',
      '  testWithValueParam/1  # GetParam() = 1',
    ]
    actual = gtest_test_instance.ParseGTestListTests(raw_output)
    expected = [
      'VPTestCase.testWithValueParam/0',
      'VPTestCase.testWithValueParam/1',
    ]
    self.assertEqual(expected, actual)

  def testParseGTestListTests_emptyTestName(self):
    raw_output = [
      'TestCase.',
      '  ',
      '  nonEmptyTestName',
    ]
    actual = gtest_test_instance.ParseGTestListTests(raw_output)
    expected = [
      'TestCase.nonEmptyTestName',
    ]
    self.assertEqual(expected, actual)

  def testParseGTestOutput_pass(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
      '[       OK ] FooTest.Bar (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.PASS, actual[0].GetType())

  def testParseGTestOutput_fail(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
      '[   FAILED ] FooTest.Bar (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.FAIL, actual[0].GetType())

  def testParseGTestOutput_crash(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
      '[  CRASHED ] FooTest.Bar (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.CRASH, actual[0].GetType())

  def testParseGTestOutput_errorCrash(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
      '[ERROR:blah] Currently running: FooTest.Bar',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertIsNone(actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.CRASH, actual[0].GetType())

  def testParseGTestOutput_fatalDcheck(self):
    raw_output = [
        '[ RUN      ] FooTest.Bar',
        '[0324/183029.116334:FATAL:test_timeouts.cc(103)] Check failed: !init',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertIsNone(actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.CRASH, actual[0].GetType())

  def testParseGTestOutput_crashWithoutCheckOrErrorMessage(self):
    """Regression test for https://crbug.com/355630342."""
    raw_output = [
        '>>ScopedMainEntryLogger',
        '[ RUN      ] FooTest.WillCrashSuddenly',
        'BrowserTestBase received signal: Segmentation fault. Backtrace:',
        '<fake backtrace line>',
        '>>ScopedMainEntryLogger',
        'Note: Google Test filter = FooTest.DoNotConsume',
        '[ RUN      ] FooTest.DoNotConsume',
        '[       OK ] FooTest.DoNotConsume (1 ms)',
        '<<ScopedMainEntryLogger',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(2, len(actual))

    self.assertEqual('FooTest.WillCrashSuddenly', actual[0].GetName())
    self.assertIsNone(actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.CRASH, actual[0].GetType())
    self.assertEqual([
        '[ RUN      ] FooTest.WillCrashSuddenly',
        'BrowserTestBase received signal: Segmentation fault. Backtrace:',
        '<fake backtrace line>',
    ], actual[0].GetLog().splitlines())

    self.assertEqual('FooTest.DoNotConsume', actual[1].GetName())
    self.assertEqual(1, actual[1].GetDuration())
    self.assertEqual(base_test_result.ResultType.PASS, actual[1].GetType())
    self.assertEqual([
        '[ RUN      ] FooTest.DoNotConsume',
        '[       OK ] FooTest.DoNotConsume (1 ms)',
    ], actual[1].GetLog().splitlines())

  def testParseGTestOutput_unknown(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(0, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.CRASH, actual[0].GetType())

  def testParseGTestOutput_nonterminalUnknown(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
      '[ RUN      ] FooTest.Baz',
      '[       OK ] FooTest.Baz (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(2, len(actual))

    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(0, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.CRASH, actual[0].GetType())

    self.assertEqual('FooTest.Baz', actual[1].GetName())
    self.assertEqual(1, actual[1].GetDuration())
    self.assertEqual(base_test_result.ResultType.PASS, actual[1].GetType())

  def testParseGTestOutput_deathTestCrashOk(self):
    raw_output = [
      '[ RUN      ] FooTest.Bar',
      '[ CRASHED      ]',
      '[       OK ] FooTest.Bar (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))

    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.PASS, actual[0].GetType())

  def testParseGTestOutput_typeParameterized(self):
    raw_output = [
        '[ RUN      ] Baz/FooTest.Bar/0',
        '[   FAILED ] Baz/FooTest.Bar/0, where TypeParam =  (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('Baz/FooTest.Bar/0', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.FAIL, actual[0].GetType())

  def testParseGTestOutput_valueParameterized(self):
    raw_output = [
        '[ RUN      ] Baz/FooTest.Bar/0',
        '[   FAILED ] Baz/FooTest.Bar/0,' +
        ' where GetParam() = 4-byte object <00-00 00-00> (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('Baz/FooTest.Bar/0', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.FAIL, actual[0].GetType())

  def testParseGTestOutput_typeAndValueParameterized(self):
    raw_output = [
        '[ RUN      ] Baz/FooTest.Bar/0',
        '[   FAILED ] Baz/FooTest.Bar/0,' +
        ' where TypeParam =  and GetParam() =  (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('Baz/FooTest.Bar/0', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.FAIL, actual[0].GetType())

  def testParseGTestOutput_skippedTest(self):
    raw_output = [
        '[ RUN      ] FooTest.Bar',
        '[  SKIPPED ] FooTest.Bar (1 ms)',
    ]
    actual = gtest_test_instance.ParseGTestOutput(raw_output, None, None)
    self.assertEqual(1, len(actual))
    self.assertEqual('FooTest.Bar', actual[0].GetName())
    self.assertEqual(1, actual[0].GetDuration())
    self.assertEqual(base_test_result.ResultType.SKIP, actual[0].GetType())

  def testParseGTestXML_none(self):
    actual = gtest_test_instance.ParseGTestXML(None)
    self.assertEqual([], actual)

  def testParseGTestJSON_none(self):
    actual = gtest_test_instance.ParseGTestJSON(None)
    self.assertEqual([], actual)

  def testParseGTestJSON_example(self):
    raw_json = """
      {
        "tests": {
          "mojom_tests": {
            "parse": {
              "ast_unittest": {
                "ASTTest": {
                  "testNodeBase": {
                    "expected": "PASS",
                    "actual": "PASS",
                    "artifacts": {
                      "screenshot": ["screenshots/page.png"]
                    }
                  }
                }
              }
            }
          }
        },
        "interrupted": false,
        "path_delimiter": ".",
        "version": 3,
        "seconds_since_epoch": 1406662283.764424,
        "num_failures_by_type": {
          "FAIL": 0,
          "PASS": 1
        },
        "artifact_types": {
          "screenshot": "image/png"
        }
      }"""
    actual = gtest_test_instance.ParseGTestJSON(raw_json)
    self.assertEqual(1, len(actual))
    self.assertEqual('mojom_tests.parse.ast_unittest.ASTTest.testNodeBase',
                     actual[0].GetName())
    self.assertEqual(base_test_result.ResultType.PASS, actual[0].GetType())

  def testParseGTestJSON_skippedTest_example(self):
    raw_json = """
      {
        "tests": {
          "mojom_tests": {
            "parse": {
              "ast_unittest": {
                "ASTTest": {
                  "testNodeBase": {
                    "expected": "SKIP",
                    "actual": "SKIP"
                  }
                }
              }
            }
          }
        },
        "interrupted": false,
        "path_delimiter": ".",
        "version": 3,
        "seconds_since_epoch": 1406662283.764424,
        "num_failures_by_type": {
          "SKIP": 1
        }
      }"""
    actual = gtest_test_instance.ParseGTestJSON(raw_json)
    self.assertEqual(1, len(actual))
    self.assertEqual('mojom_tests.parse.ast_unittest.ASTTest.testNodeBase',
                     actual[0].GetName())
    self.assertEqual(base_test_result.ResultType.SKIP, actual[0].GetType())

  def testTestNameWithoutDisabledPrefix_disabled(self):
    test_name_list = [
      'A.DISABLED_B',
      'DISABLED_A.B',
      'DISABLED_A.DISABLED_B',
    ]
    for test_name in test_name_list:
      actual = gtest_test_instance \
          .TestNameWithoutDisabledPrefix(test_name)
      expected = 'A.B'
      self.assertEqual(expected, actual)

  def testTestNameWithoutDisabledPrefix_flaky(self):
    test_name_list = [
      'A.FLAKY_B',
      'FLAKY_A.B',
      'FLAKY_A.FLAKY_B',
    ]
    for test_name in test_name_list:
      actual = gtest_test_instance \
          .TestNameWithoutDisabledPrefix(test_name)
      expected = 'A.B'
      self.assertEqual(expected, actual)

  def testTestNameWithoutDisabledPrefix_notDisabledOrFlaky(self):
    test_name = 'A.B'
    actual = gtest_test_instance \
        .TestNameWithoutDisabledPrefix(test_name)
    expected = 'A.B'
    self.assertEqual(expected, actual)


if __name__ == '__main__':
  unittest.main(verbosity=2)
