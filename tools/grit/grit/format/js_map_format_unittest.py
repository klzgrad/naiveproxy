#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unittest for js_map_format.py.
"""

import os
import sys
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

import unittest
import StringIO

from grit import util
from grit.tool import build


class JsMapFormatUnittest(unittest.TestCase):

  def testMessages(self):
    root = util.ParseGrdForUnittest(u"""
    <messages>
      <message name="IDS_SIMPLE_MESSAGE">
              Simple message.
      </message>
      <message name="IDS_QUOTES">
              element\u2019s \u201c<ph name="NAME">%s<ex>name</ex></ph>\u201d attribute
      </message>
      <message name="IDS_PLACEHOLDERS">
              <ph name="ERROR_COUNT">%1$d<ex>1</ex></ph> error, <ph name="WARNING_COUNT">%2$d<ex>1</ex></ph> warning
      </message>
      <message name="IDS_STARTS_WITH_SPACE">
              ''' (<ph name="COUNT">%d<ex>2</ex></ph>)
      </message>
      <message name="IDS_DOUBLE_QUOTES">
              A "double quoted" message.
      </message>
      <message name="IDS_BACKSLASH">
              \\
      </message>
    </messages>
    """)

    buf = StringIO.StringIO()
    build.RcBuilder.ProcessNode(root, DummyOutput('js_map_format', 'en'), buf)
    output = util.StripBlankLinesAndComments(buf.getvalue())
    self.assertEqual(u"""\
localizedStrings["Simple message."] = "Simple message.";
localizedStrings["element\u2019s \u201c%s\u201d attribute"] = "element\u2019s \u201c%s\u201d attribute";
localizedStrings["%d error, %d warning"] = "%1$d error, %2$d warning";
localizedStrings[" (%d)"] = " (%d)";
localizedStrings["A \\\"double quoted\\\" message."] = "A \\\"double quoted\\\" message.";
localizedStrings["\\\\"] = "\\\\";""", output)

  def testTranslations(self):
    root = util.ParseGrdForUnittest("""
    <messages>
        <message name="ID_HELLO">Hello!</message>
        <message name="ID_HELLO_USER">Hello <ph name="USERNAME">%s<ex>
          Joi</ex></ph></message>
      </messages>
    """)

    buf = StringIO.StringIO()
    build.RcBuilder.ProcessNode(root, DummyOutput('js_map_format', 'fr'), buf)
    output = util.StripBlankLinesAndComments(buf.getvalue())
    self.assertEqual(u"""\
localizedStrings["Hello!"] = "H\xe9P\xe9ll\xf4P\xf4!";
localizedStrings["Hello %s"] = "H\xe9P\xe9ll\xf4P\xf4 %s";\
""", output)


class DummyOutput(object):

  def __init__(self, type, language):
    self.type = type
    self.language = language

  def GetType(self):
    return self.type

  def GetLanguage(self):
    return self.language

  def GetOutputFilename(self):
    return 'hello.gif'

if __name__ == '__main__':
  unittest.main()
