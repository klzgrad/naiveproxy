#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit tests for grit.gather.igoogle_strings'''

import os
import sys
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

import unittest
import StringIO

from grit.gather import igoogle_strings

class IgoogleStringsUnittest(unittest.TestCase):
  def testParsing(self):
    original = '''<messagebundle><msg test="hello_world">Hello World</msg></messagebundle>'''
    gatherer = igoogle_strings.IgoogleStrings(StringIO.StringIO(original))
    gatherer.Parse()
    print len(gatherer.GetCliques())
    print gatherer.GetCliques()[0].GetMessage().GetRealContent()
    self.failUnless(len(gatherer.GetCliques()) == 1)
    self.failUnless(gatherer.Translate('en').replace('\n', '') == original)

if __name__ == '__main__':
  unittest.main()
