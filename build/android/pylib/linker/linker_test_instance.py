# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pylib.base import test_instance
from pylib.constants import host_paths
from pylib.linker import test_case

with host_paths.SysPath(host_paths.BUILD_COMMON_PATH):
  import unittest_util

_MODERN_LINKER_MINIMUM_SDK_INT = 23

class LinkerTestInstance(test_instance.TestInstance):

  def __init__(self, args):
    super(LinkerTestInstance, self).__init__()
    self._test_apk = args.test_apk
    self._test_filter = args.test_filter

  @property
  def test_apk(self):
    return self._test_apk

  @property
  def test_filter(self):
    return self._test_filter

  def GetTests(self, min_device_sdk):
    tests = [
      test_case.LinkerSharedRelroTest(is_modern_linker=False,
                                      is_low_memory=False),
      test_case.LinkerSharedRelroTest(is_modern_linker=False,
                                      is_low_memory=True)
    ]
    if min_device_sdk >= _MODERN_LINKER_MINIMUM_SDK_INT:
      tests.append(test_case.LinkerSharedRelroTest(is_modern_linker=True))

    if self._test_filter:
      filtered_names = unittest_util.FilterTestNames(
          (t.qualified_name for t in tests), self._test_filter)
      tests = [
          t for t in tests
          if t.qualified_name in filtered_names]

    return tests

  def SetUp(self):
    pass

  def TearDown(self):
    pass

  def TestType(self):
    return 'linker'

