#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for java_deobfuscate."""

import argparse
import subprocess
import sys
import tempfile


LINE_PREFIXES = [
    '',
    # logcat -v threadtime
    '09-08 14:38:35.535 18029 18084 E qcom_sensors_hal: ',
    # logcat
    'W/GCM     (15158): ',
    'W/GCM     (  158): ',
]

TEST_MAP = """\
this.was.Deobfuscated -> FOO:
    int[] FontFamily -> a
    1:3:void someMethod(int,android.os.Bundle):65:65 -> bar
"""

TEST_DATA = """\
Here is a FOO
Here is a FOO baz
Here is a "FOO" baz
Here is a "FOO.bar" baz
Here it is: FOO
Here it is: FOO.bar
SomeError: SomeFrameworkClass in isTestClass for FOO
Here is a FOO.bar
Here is a FOO.bar baz
END FOO#bar
new-instance 3810 (LSome/Framework/Class;) in LFOO;
FOO: Error message
\tat FOO.bar(PG:1)
""".splitlines(True)

EXPECTED_OUTPUT = """\
Here is a this.was.Deobfuscated
Here is a FOO baz
Here is a "this.was.Deobfuscated" baz
Here is a "this.was.Deobfuscated.someMethod" baz
Here it is: this.was.Deobfuscated
Here it is: this.was.Deobfuscated.someMethod
SomeError: SomeFrameworkClass in isTestClass for this.was.Deobfuscated
Here is a this.was.Deobfuscated.someMethod
Here is a FOO.bar baz
END this.was.Deobfuscated#someMethod
new-instance 3810 (LSome/Framework/Class;) in Lthis/was/Deobfuscated;
this.was.Deobfuscated: Error message
\tat this.was.Deobfuscated.someMethod(Deobfuscated.java:65)
""".splitlines(True)


def _RunTest(bin_path, map_file, prefix):
  cmd = [bin_path, map_file]
  payload = TEST_DATA
  expected_output = EXPECTED_OUTPUT

  payload = [prefix + x for x in payload]
  expected_output = [prefix + x for x in expected_output]

  proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
  actual_output = proc.communicate(''.join(payload))[0].splitlines(True)
  any_unexpected_failures = False
  for actual, expected in zip(actual_output, expected_output):
    if actual == expected:
      sys.stdout.write('Good: ' + actual)
    elif actual.replace('bar', 'someMethod') == expected:
      # TODO(agrieve): Fix ReTrace's ability to deobfuscated methods.
      sys.stdout.write('Fine: ' + actual)
    else:
      sys.stdout.write('BAD:  ' + actual)
      any_unexpected_failures = True
  return not any_unexpected_failures


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('path_to_java_deobfuscate')
  args = parser.parse_args()

  with tempfile.NamedTemporaryFile() as map_file:
    map_file.write(TEST_MAP)
    map_file.flush()
    passed = True
    for prefix in LINE_PREFIXES:
      if not _RunTest(args.path_to_java_deobfuscate, map_file.name, prefix):
        passed = False
  print 'Result:', 'PASS' if passed else 'FAIL'
  sys.exit(int(not passed))


if __name__ == '__main__':
  main()
