#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit tests for include.IncludeNode'''

import os
import sys
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

import os
import unittest
import zlib

from grit.node import misc
from grit.node import include
from grit.node import empty
from grit import grd_reader
from grit import util


class IncludeNodeUnittest(unittest.TestCase):
  def testGetPath(self):
    root = misc.GritNode()
    root.StartParsing(u'grit', None)
    root.HandleAttribute(u'latest_public_release', u'0')
    root.HandleAttribute(u'current_release', u'1')
    root.HandleAttribute(u'base_dir', ur'..\resource')
    release = misc.ReleaseNode()
    release.StartParsing(u'release', root)
    release.HandleAttribute(u'seq', u'1')
    root.AddChild(release)
    includes = empty.IncludesNode()
    includes.StartParsing(u'includes', release)
    release.AddChild(includes)
    include_node = include.IncludeNode()
    include_node.StartParsing(u'include', includes)
    include_node.HandleAttribute(u'file', ur'flugel\kugel.pdf')
    includes.AddChild(include_node)
    root.EndParsing()

    self.assertEqual(root.ToRealPath(include_node.GetInputPath()),
                     util.normpath(
                       os.path.join(ur'../resource', ur'flugel/kugel.pdf')))

  def testGetPathNoBasedir(self):
    root = misc.GritNode()
    root.StartParsing(u'grit', None)
    root.HandleAttribute(u'latest_public_release', u'0')
    root.HandleAttribute(u'current_release', u'1')
    root.HandleAttribute(u'base_dir', ur'..\resource')
    release = misc.ReleaseNode()
    release.StartParsing(u'release', root)
    release.HandleAttribute(u'seq', u'1')
    root.AddChild(release)
    includes = empty.IncludesNode()
    includes.StartParsing(u'includes', release)
    release.AddChild(includes)
    include_node = include.IncludeNode()
    include_node.StartParsing(u'include', includes)
    include_node.HandleAttribute(u'file', ur'flugel\kugel.pdf')
    include_node.HandleAttribute(u'use_base_dir', u'false')
    includes.AddChild(include_node)
    root.EndParsing()

    self.assertEqual(root.ToRealPath(include_node.GetInputPath()),
                     util.normpath(
                       os.path.join(ur'../', ur'flugel/kugel.pdf')))

  def testCompressGzip(self):
    root = util.ParseGrdForUnittest('''
        <includes>
          <include name="TEST_TXT" file="test_text.txt"
                   compress="gzip" type="BINDATA"/>
        </includes>''', base_dir = util.PathFromRoot('grit/testdata'))
    inc, = root.GetChildrenOfType(include.IncludeNode)
    throwaway, compressed = inc.GetDataPackPair(lang='en', encoding=1)

    decompressed_data = zlib.decompress(compressed, 16 + zlib.MAX_WBITS)
    self.assertEqual(util.ReadFile(util.PathFromRoot('grit/testdata')
                                   + "/test_text.txt", util.BINARY),
                     decompressed_data)


if __name__ == '__main__':
  unittest.main()
