# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script compares the gtest test list for two different builds.
#
# Usage:
#   compare_test_lists.py <build_dir_1> <build_dir_2> <binary_name>
#
# For example, from the "src" directory:
#   python tools/gn/bin/compare_test_lists.py out/Debug out/gnbuild ipc_tests
#
# This will compile the given binary in both output directories, then extracts
# the test lists and prints missing or extra tests between the first and the
# second build.

import os
import subprocess
import sys

def BuildBinary(build_dir, binary_name):
  """Builds the given binary in the given directory with Ninja.

  Returns True on success."""
  return subprocess.call(["ninja", "-C", build_dir, binary_name]) == 0


def GetTestList(path_to_binary):
  """Returns a set of full test names.

  Each test will be of the form "Case.Test". There will be a separate line
  for each combination of Case/Test (there are often multiple tests in each
  case).

  Throws an exception on failure."""
  raw_output = subprocess.check_output([path_to_binary, "--gtest_list_tests"])
  input_lines = raw_output.split('\n')

  # The format of the gtest_list_tests output is:
  # "Case1."
  # "  Test1  # <Optional extra stuff>"
  # "  Test2"
  # "Case2."
  # "  Test1"
  case_name = ''  # Includes trailing dot.
  test_set = set()
  for line in input_lines:
    if len(line) > 1:
      if line[0] == ' ':
        # Indented means a test in previous case.
        test_set.add(case_name + line[:line.find('#')].strip())
      else:
        # New test case.
        case_name = line.strip()

  return test_set


def PrintSetDiff(a_name, a, b_name, b, binary_name):
  """Prints the test list difference between the given sets a and b.

  a_name and b_name will be used to refer to the directories of the two sets,
  and the binary name will be shown as the source of the output."""

  a_not_b = list(a - b)
  if len(a_not_b):
    print "\n", binary_name, "tests in", a_name, "but not", b_name
    a_not_b.sort()
    for cur in a_not_b:
      print "  ", cur

  b_not_a = list(b - a)
  if len(b_not_a):
    print "\n", binary_name, "tests in", b_name, "but not", a_name
    b_not_a.sort()
    for cur in b_not_a:
      print "  ", cur

  if len(a_not_b) == 0 and len(b_not_a) == 0:
    print "\nTests match!"


def Run(a_dir, b_dir, binary_name):
  if not BuildBinary(a_dir, binary_name):
    print "Building", binary_name, "in", a_dir, "failed"
    return 1
  if not BuildBinary(b_dir, binary_name):
    print "Building", binary_name, "in", b_dir, "failed"
    return 1

  a_tests = GetTestList(os.path.join(a_dir, binary_name))
  b_tests = GetTestList(os.path.join(b_dir, binary_name))

  PrintSetDiff(a_dir, a_tests, b_dir, b_tests, binary_name)


if len(sys.argv) != 4:
  print "Usage: compare_test_lists.py <build_dir_1> <build_dir_2> " \
      "<test_binary_name>"
  sys.exit(1)
sys.exit(Run(sys.argv[1], sys.argv[2], sys.argv[3]))
