#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit tests for the rc_header formatter'''

# GRD samples exceed the 80 character limit.
# pylint: disable-msg=C6310

import os
import sys
import tempfile
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

import StringIO
import unittest

from grit import exception
from grit import grd_reader
from grit import util
from grit.format import rc_header


class RcHeaderFormatterUnittest(unittest.TestCase):
  def FormatAll(self, grd):
    output = rc_header.FormatDefines(grd, grd.ShouldOutputAllResourceDefines())
    return ''.join(output).replace(' ', '')

  def _MakeTempPredeterminedIdsFile(self, content):
    tmp_dir = tempfile.gettempdir()
    predetermined_ids_file = tmp_dir + "/predetermined_ids.txt"
    with open(predetermined_ids_file, 'w') as f:
      f.write(content)
    return predetermined_ids_file

  def testFormatter(self):
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <release seq="3">
          <includes first_id="300" comment="bingo">
            <include type="gif" name="ID_LOGO" file="images/logo.gif" />
          </includes>
          <messages first_id="10000">
            <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
              Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
            </message>
            <message name="IDS_BONGO">
              Bongo!
            </message>
          </messages>
          <structures>
            <structure type="dialog" name="IDD_NARROW_DIALOG" file="rc_files/dialogs.rc" />
            <structure type="version" name="VS_VERSION_INFO" file="rc_files/version.rc" />
          </structures>
        </release>
      </grit>'''), '.')
    output = self.FormatAll(grd)
    self.failUnless(output.count('IDS_GREETING10000'))
    self.failUnless(output.count('ID_LOGO300'))

  def testOnlyDefineResourcesThatSatisfyOutputCondition(self):
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3"
            base_dir="." output_all_resource_defines="false">
        <release seq="3">
          <includes first_id="300" comment="bingo">
            <include type="gif" name="ID_LOGO" file="images/logo.gif" />
          </includes>
          <messages first_id="10000">
            <message name="IDS_FIRSTPRESENTSTRING" desc="Present in .rc file.">
              I will appear in the .rc file.
            </message>
            <if expr="False"> <!--Do not include in the .rc files until used.-->
              <message name="IDS_MISSINGSTRING" desc="Not present in .rc file.">
                I will not appear in the .rc file.
              </message>
            </if>
            <if expr="lang != 'es'">
              <message name="IDS_LANGUAGESPECIFICSTRING" desc="Present in .rc file.">
                Hello.
              </message>
            </if>
            <if expr="lang == 'es'">
              <message name="IDS_LANGUAGESPECIFICSTRING" desc="Present in .rc file.">
                Hola.
              </message>
            </if>
            <message name="IDS_THIRDPRESENTSTRING" desc="Present in .rc file.">
              I will also appear in the .rc file.
            </message>
         </messages>
        </release>
      </grit>'''), '.')
    output = self.FormatAll(grd)
    self.failUnless(output.count('IDS_FIRSTPRESENTSTRING10000'))
    self.failIf(output.count('IDS_MISSINGSTRING'))
    self.failIf(output.count('10001'))  # IDS_MISSINGSTRING should get this ID
    self.failUnless(output.count('IDS_LANGUAGESPECIFICSTRING10002'))
    self.failUnless(output.count('IDS_THIRDPRESENTSTRING10003'))

  def testExplicitFirstIdOverlaps(self):
    # second first_id will overlap preexisting range
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <release seq="3">
          <includes first_id="300" comment="bingo">
            <include type="gif" name="ID_LOGO" file="images/logo.gif" />
            <include type="gif" name="ID_LOGO2" file="images/logo2.gif" />
          </includes>
          <messages first_id="301">
            <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
              Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
            </message>
            <message name="IDS_SMURFGEBURF">Frubegfrums</message>
          </messages>
        </release>
      </grit>'''), '.')
    self.assertRaises(exception.IdRangeOverlap, self.FormatAll, grd)

  def testImplicitOverlapsPreexisting(self):
    # second message in <messages> will overlap preexisting range
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <release seq="3">
          <includes first_id="301" comment="bingo">
            <include type="gif" name="ID_LOGO" file="images/logo.gif" />
            <include type="gif" name="ID_LOGO2" file="images/logo2.gif" />
          </includes>
          <messages first_id="300">
            <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
              Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
            </message>
            <message name="IDS_SMURFGEBURF">Frubegfrums</message>
          </messages>
        </release>
      </grit>'''), '.')
    self.assertRaises(exception.IdRangeOverlap, self.FormatAll, grd)

  def testEmit(self):
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <outputs>
          <output type="rc_all" filename="dummy">
            <emit emit_type="prepend">Wrong</emit>
          </output>
          <if expr="False">
            <output type="rc_header" filename="dummy">
              <emit emit_type="prepend">No</emit>
            </output>
          </if>
          <output type="rc_header" filename="dummy">
            <emit emit_type="append">Error</emit>
          </output>
          <output type="rc_header" filename="dummy">
            <emit emit_type="prepend">Bingo</emit>
          </output>
        </outputs>
      </grit>'''), '.')
    output = ''.join(rc_header.Format(grd, 'en', '.'))
    output = util.StripBlankLinesAndComments(output)
    self.assertEqual('#pragma once\nBingo', output)

  def testRcHeaderFormat(self):
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <release seq="3">
          <includes first_id="300" comment="bingo">
            <include type="gif" name="IDR_LOGO" file="images/logo.gif" />
          </includes>
          <messages first_id="10000">
            <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
              Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
            </message>
            <message name="IDS_BONGO">
              Bongo!
            </message>
          </messages>
        </release>
      </grit>'''), '.')

    # Using the default rc_header format string.
    output = rc_header.FormatDefines(grd, grd.ShouldOutputAllResourceDefines(),
                                     grd.GetRcHeaderFormat())
    self.assertEqual(('#define IDR_LOGO 300\n'
                      '#define IDS_GREETING 10000\n'
                      '#define IDS_BONGO 10001\n'), ''.join(output))

    # Using a custom rc_header format string.
    grd.AssignRcHeaderFormat(
        '#define {textual_id} _Pragma("{textual_id}") {numeric_id}')
    output = rc_header.FormatDefines(grd, grd.ShouldOutputAllResourceDefines(),
                                     grd.GetRcHeaderFormat())
    self.assertEqual(('#define IDR_LOGO _Pragma("IDR_LOGO") 300\n'
                      '#define IDS_GREETING _Pragma("IDS_GREETING") 10000\n'
                      '#define IDS_BONGO _Pragma("IDS_BONGO") 10001\n'),
                     ''.join(output))

  def testPredeterminedIds(self):
    predetermined_ids_file = self._MakeTempPredeterminedIdsFile(
        'IDS_BONGO 101\nID_LOGO 102\n')
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <release seq="3">
          <includes first_id="300" comment="bingo">
            <include type="gif" name="ID_LOGO" file="images/logo.gif" />
          </includes>
          <messages first_id="10000">
            <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
              Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
            </message>
            <message name="IDS_BONGO">
              Bongo!
            </message>
          </messages>
        </release>
      </grit>'''), '.', predetermined_ids_file=predetermined_ids_file)
    output = rc_header.FormatDefines(grd, grd.ShouldOutputAllResourceDefines(),
                                     grd.GetRcHeaderFormat())
    self.assertEqual(('#define ID_LOGO 102\n'
                      '#define IDS_GREETING 10000\n'
                      '#define IDS_BONGO 101\n'), ''.join(output))

  def testPredeterminedIdsOverlap(self):
    predetermined_ids_file = self._MakeTempPredeterminedIdsFile(
        'ID_LOGO 10000\n')
    grd = grd_reader.Parse(StringIO.StringIO('''<?xml version="1.0" encoding="UTF-8"?>
      <grit latest_public_release="2" source_lang_id="en" current_release="3" base_dir=".">
        <release seq="3">
          <includes first_id="300" comment="bingo">
            <include type="gif" name="ID_LOGO" file="images/logo.gif" />
          </includes>
          <messages first_id="10000">
            <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
              Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
            </message>
            <message name="IDS_BONGO">
              Bongo!
            </message>
          </messages>
        </release>
      </grit>'''), '.', predetermined_ids_file=predetermined_ids_file)
    output = rc_header.FormatDefines(grd, grd.ShouldOutputAllResourceDefines(),
                                     grd.GetRcHeaderFormat())
    self.assertRaises(exception.IdRangeOverlap, self.FormatAll, grd)

if __name__ == '__main__':
  unittest.main()
