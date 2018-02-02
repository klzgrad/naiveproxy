#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Support for ALL_ALL.xml format used by Igoogle plug-ins in Google Desktop.'''

import StringIO
import re
import xml.sax
import xml.sax.handler
import xml.sax.saxutils

from grit.gather import regexp
from grit import util
from grit import tclib

# Placeholders can be defined in strings.xml files by putting the name of the
# placeholder between [![ and ]!] e.g. <MSG>Hello [![USER]!] how are you<MSG>
PLACEHOLDER_RE = re.compile('(\[!\[|\]!\])')


class IgoogleStringsContentHandler(xml.sax.handler.ContentHandler):
  '''A very dumb parser for splitting the strings.xml file into translateable
  and nontranslateable chunks.'''

  def __init__(self, parent):
    self.curr_elem = ''
    self.curr_text = ''
    self.parent = parent
    self.resource_name = ''
    self.meaning = ''
    self.translateable = True

  def startElement(self, name, attrs):
    if (name != 'messagebundle'):
      self.curr_elem = name

      attr_names = attrs.getQNames()
      if 'name' in attr_names:
        self.resource_name = attrs.getValueByQName('name')

      att_text = []
      for attr_name in attr_names:
        att_text.append(' ')
        att_text.append(attr_name)
        att_text.append('=')
        att_text.append(
          xml.sax.saxutils.quoteattr(attrs.getValueByQName(attr_name)))

      self.parent._AddNontranslateableChunk("<%s%s>" %
                                            (name, ''.join(att_text)))

  def characters(self, content):
    if self.curr_elem != '':
      self.curr_text += content

  def endElement(self, name):
    if name != 'messagebundle':
      self.parent.AddMessage(self.curr_text, self.resource_name,
                             self.meaning, self.translateable)
      self.parent._AddNontranslateableChunk("</%s>\n" % name)
      self.curr_elem = ''
      self.curr_text = ''
      self.resource_name = ''
      self.meaning = ''
      self.translateable = True

  def ignorableWhitespace(self, whitespace):
    pass


class IgoogleStrings(regexp.RegexpGatherer):
  '''Supports the ALL_ALL.xml format used by Igoogle gadgets.'''

  def AddMessage(self, msgtext, description, meaning, translateable):
    if msgtext == '':
      return

    msg = tclib.Message(description=description, meaning=meaning)

    unescaped_text = self.UnEscape(msgtext)
    parts = PLACEHOLDER_RE.split(unescaped_text)
    in_placeholder = False
    for part in parts:
      if part == '':
        continue
      elif part == '[![':
        in_placeholder = True
      elif part == ']!]':
        in_placeholder = False
      else:
        if in_placeholder:
          msg.AppendPlaceholder(tclib.Placeholder(part, '[![%s]!]' % part,
                                                  '(placeholder)'))
        else:
          msg.AppendText(part)

    self.skeleton_.append(
      self.uberclique.MakeClique(msg, translateable=translateable))

    # if statement needed because this is supposed to be idempotent (so never
    # set back to false)
    if translateable:
      self.translatable_chunk_ = True

  # Although we use the RegexpGatherer base class, we do not use the
  # _RegExpParse method of that class to implement Parse().  Instead, we
  # parse using a SAX parser.
  def Parse(self):
    if self.have_parsed_:
      return
    self.have_parsed_ = True

    self.text_ = self._LoadInputFile().strip()
    self._AddNontranslateableChunk(u'<messagebundle>\n')
    stream = StringIO.StringIO(self.text_)
    handler = IgoogleStringsContentHandler(self)
    xml.sax.parse(stream, handler)
    self._AddNontranslateableChunk(u'</messagebundle>\n')

  def Escape(self, text):
    return util.EncodeCdata(text)
