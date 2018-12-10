#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''GRIT tool that runs the unit test suite for GRIT.'''


import unittest

import grit.test_suite_all
from grit.tool import interface


class UnitTestTool(interface.Tool):
  '''By using this tool (e.g. 'grit unit') you run all the unit tests for GRIT.
This happens in the environment that is set up by the basic GRIT runner.'''

  def ShortDescription(self):
    return 'Use this tool to run all the unit tests for GRIT.'

  def Run(self, opts, args):
    return unittest.TextTestRunner(verbosity=2).run(
      grit.test_suite_all.TestSuiteAll())
