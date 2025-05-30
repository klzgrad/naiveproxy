#!/usr/bin/env python3
# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit tests for grit.clique'''


import os
import sys
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

import re
import unittest

from io import StringIO

from grit import clique
from grit import constants
from grit import exception
from grit import grd_reader
from grit import pseudo
from grit import tclib
from grit import util

class MessageCliqueUnittest(unittest.TestCase):
  def testClique(self):
    factory = clique.UberClique()
    msg = tclib.Message(text='Hello USERNAME, how are you?',
                        placeholders=[
                          tclib.Placeholder('USERNAME', '%s', 'Joi')])
    c = factory.MakeClique(msg)

    self.assertTrue(c.GetMessage() == msg)
    self.assertTrue(c.GetId() == msg.GetId())

    msg_en_f = tclib.Translation(
        text='Hello USERNAME, how are youe?',
        id=msg.GetId(),
        placeholders=[tclib.Placeholder('USERNAME', '%s', 'Joi')])
    msg_fr = tclib.Translation(text='Bonjour USERNAME, comment ca va?',
                               id=msg.GetId(), placeholders=[
                                tclib.Placeholder('USERNAME', '%s', 'Joi')])
    msg_fr_f = tclib.Translation(
        text='Bonjour USERNAME, comment ca vae?',
        id=msg.GetId(),
        placeholders=[tclib.Placeholder('USERNAME', '%s', 'Joi')])
    msg_de = tclib.Translation(text='Guten tag USERNAME, wie geht es dir?',
                               id=msg.GetId(), placeholders=[
                                tclib.Placeholder('USERNAME', '%s', 'Joi')])
    msg_de_f = tclib.Translation(
        text='Guten tag USERNAME, wie gehten es dir?',
        id=msg.GetId(),
        placeholders=[tclib.Placeholder('USERNAME', '%s', 'Joi')])

    c.AddTranslation(msg_en_f, 'en', constants.GENDER_FEMININE)
    c.AddTranslation(msg_fr, 'fr', constants.DEFAULT_GENDER)
    c.AddTranslation(msg_fr_f, 'fr', constants.GENDER_FEMININE)
    factory.FindCliqueAndAddTranslation(msg_de, 'de', constants.DEFAULT_GENDER)
    factory.FindCliqueAndAddTranslation(msg_de_f, 'de',
                                        constants.GENDER_FEMININE)

    # sort() sorts lists in-place and does not return them
    for lang in ('en', 'fr', 'de'):
      for gender in (constants.DEFAULT_GENDER, constants.GENDER_FEMININE):
        self.assertTrue((lang, gender) in c.clique,
                        msg=f'({lang}, {gender}) is not in c.clique')

    self.assertEqual(
        c.MessageForLanguageAndGender(
            'fr', constants.DEFAULT_GENDER).GetRealContent(),
        msg_fr.GetRealContent())
    self.assertEqual(
        c.MessageForLanguageAndGender(
            'fr', constants.GENDER_FEMININE).GetRealContent(),
        msg_fr_f.GetRealContent())

    try:
      c.MessageForLanguageAndGender('zh-CN', constants.DEFAULT_GENDER, False)
      self.fail('Should have gotten exception')
    except:
      pass

    self.assertIsNotNone(
        c.MessageForLanguageAndGender('zh-CN', constants.DEFAULT_GENDER, True))

    # Missing gender translation falls back to the default gender for the same
    # language.
    self._assertTranslationPartsEqual(
        c.MessageForLanguageAndGender('de', constants.GENDER_MASCULINE, False),
        msg_de)

    rex = re.compile('fr|de|bingo')
    self.assertEqual(len(c.AllMessagesThatMatch(rex, False)), 4)
    self.assertIsNotNone(
        c.AllMessagesThatMatch(rex, True)[(pseudo.PSEUDO_LANG,
                                           constants.DEFAULT_GENDER)])

  def _assertTranslationPartsEqual(self, msg1, msg2):
    for part in zip(msg1.parts, msg2.parts):
      if isinstance(part[0], str):
        self.assertEqual(part[0], part[1])
      elif isinstance(part[0], tclib.Placeholder):
        self.assertEqual(part[0].GetPresentation(), part[1].GetPresentation())
        self.assertEqual(part[0].GetOriginal(), part[1].GetOriginal())
        self.assertEqual(part[0].GetExample(), part[1].GetExample())
      else:
        self.fail(f'unsupported type in translation parts: {part[0]}')

  def testBestClique(self):
    factory = clique.UberClique()
    factory.MakeClique(tclib.Message(text='Alfur', description='alfaholl'))
    factory.MakeClique(tclib.Message(text='Alfur', description=''))
    factory.MakeClique(tclib.Message(text='Vaettur', description=''))
    factory.MakeClique(tclib.Message(text='Vaettur', description=''))
    factory.MakeClique(tclib.Message(text='Troll', description=''))
    factory.MakeClique(tclib.Message(text='Gryla', description='ID: IDS_GRYLA'))
    factory.MakeClique(tclib.Message(text='Gryla', description='vondakerling'))
    factory.MakeClique(tclib.Message(text='Leppaludi', description='ID: IDS_LL'))
    factory.MakeClique(tclib.Message(text='Leppaludi', description=''))

    count_best_cliques = 0
    for c in factory.BestCliquePerId():
      count_best_cliques += 1
      msg = c.GetMessage()
      text = msg.GetRealContent()
      description = msg.GetDescription()
      if text == 'Alfur':
        self.assertTrue(description == 'alfaholl')
      elif text == 'Gryla':
        self.assertTrue(description == 'vondakerling')
      elif text == 'Leppaludi':
        self.assertTrue(description == 'ID: IDS_LL')
    self.assertTrue(count_best_cliques == 5)

  def testAllInUberClique(self):
    resources = grd_reader.Parse(
        StringIO('''<?xml version="1.0" encoding="UTF-8"?>
<grit latest_public_release="2" source_lang_id="en-US" current_release="3" base_dir=".">
  <release seq="3">
    <messages>
      <message name="IDS_GREETING" desc="Printed to greet the currently logged in user">
        Hello <ph name="USERNAME">%s<ex>Joi</ex></ph>, how are you doing today?
      </message>
    </messages>
    <structures>
      <structure type="dialog" name="IDD_ABOUTBOX" encoding="utf-16" file="grit/testdata/klonk.rc" />
      <structure type="tr_html" name="ID_HTML" file="grit/testdata/simple.html" />
    </structures>
  </release>
</grit>'''), util.PathFromRoot('.'))
    resources.SetOutputLanguage('en')
    resources.RunGatherers()
    content_list = []
    for clique_list in resources.UberClique().cliques_.values():
      for clique in clique_list:
        content_list.append(clique.GetMessage().GetRealContent())
    self.assertTrue('Hello %s, how are you doing today?' in content_list)
    self.assertTrue('Jack "Black" Daniels' in content_list)
    self.assertTrue('Hello!' in content_list)

  def testCorrectExceptionIfWrongEncodingOnResourceFile(self):
    '''This doesn't really belong in this unittest file, but what the heck.'''
    resources = grd_reader.Parse(
        StringIO('''<?xml version="1.0" encoding="UTF-8"?>
<grit latest_public_release="2" source_lang_id="en-US" current_release="3" base_dir=".">
  <release seq="3">
    <structures>
      <structure type="dialog" name="IDD_ABOUTBOX" file="grit/testdata/klonk.rc" />
    </structures>
  </release>
</grit>'''), util.PathFromRoot('.'))
    self.assertRaises(exception.SectionNotFound, resources.RunGatherers)

  def testSemiIdenticalCliques(self):
    messages = [
      tclib.Message(text='Hello USERNAME',
                    placeholders=[tclib.Placeholder('USERNAME', '$1', 'Joi')]),
      tclib.Message(text='Hello USERNAME',
                    placeholders=[tclib.Placeholder('USERNAME', '%s', 'Joi')]),
    ]
    self.assertTrue(messages[0].GetId() == messages[1].GetId())

    # Both of the above would share a translation.
    translation = tclib.Translation(id=messages[0].GetId(),
                                    text='Bonjour USERNAME',
                                    placeholders=[tclib.Placeholder(
                                      'USERNAME', '$1', 'Joi')])

    factory = clique.UberClique()
    cliques = [factory.MakeClique(msg) for msg in messages]

    for clq in cliques:
      clq.AddTranslation(translation, 'fr', constants.DEFAULT_GENDER)

    self.assertTrue(cliques[0].MessageForLanguageAndGender(
        'fr', constants.DEFAULT_GENDER).GetRealContent() == 'Bonjour $1')
    self.assertTrue(cliques[1].MessageForLanguageAndGender(
        'fr', constants.DEFAULT_GENDER).GetRealContent() == 'Bonjour %s')

  def testMissingTranslations(self):
    messages = [
        tclib.Message(text='Hello'),
        tclib.Message(text='Goodbye'),
        tclib.Message(text='gender fallback to english example'),
        tclib.Message(text='gender fallback to default gender example'),
    ]
    factory = clique.UberClique()
    cliques = [factory.MakeClique(msg) for msg in messages]

    cliques[3].AddTranslation(
        tclib.Translation(text='Buongiorno',
                          id=messages[3].GetId(),
                          placeholders=[]), 'it', constants.DEFAULT_GENDER)

    cliques[1].MessageForLanguageAndGender('fr', constants.DEFAULT_GENDER,
                                           False, True)
    cliques[2].MessageForLanguageAndGender('es', constants.DEFAULT_GENDER,
                                           False, True)

    # Non-default genders can still fall back to English
    cliques[2].MessageForLanguageAndGender('es', constants.GENDER_MASCULINE,
                                           False, True)

    cliques[3].MessageForLanguageAndGender('it', constants.DEFAULT_GENDER,
                                           False, False)

    # Non-default genders can fall back to the default constants without an error
    # or warning.
    cliques[3].MessageForLanguageAndGender('it', constants.GENDER_MASCULINE,
                                           False, False)

    self.assertFalse(factory.HasMissingTranslations())

    cliques[0].MessageForLanguageAndGender('de', constants.DEFAULT_GENDER,
                                           False, False)
    cliques[0].MessageForLanguageAndGender('de', constants.GENDER_FEMININE,
                                           False, False)

    self.assertTrue(factory.HasMissingTranslations())

    report = factory.MissingTranslationsReport()
    self.assertEqual(report.count('WARNING'), 1)
    self.assertEqual(
        report.count('''8053599568341804890 "Goodbye" ('fr', 'OTHER')'''), 1)
    self.assertEqual(
        report.count(
            '''362457071231374324 "gender fallback to english example" ('es', 'OTHER'),('es', 'MASCULINE')'''
        ), 1)
    self.assertEqual(report.count('ERROR'), 1)
    self.assertEqual(
        report.count(
            '''800120468867715734 "Hello" ('de', 'OTHER'),('de', 'FEMININE')'''
        ), 1)
    self.assertEqual(report.count('gender fallback to default gender example'),
                     0)

  def testCustomTypes(self):
    factory = clique.UberClique()
    message = tclib.Message(text='Bingo bongo')
    c = factory.MakeClique(message)
    try:
      c.SetCustomType(DummyCustomType())
      self.fail()
    except:
      pass  # expected case - 'Bingo bongo' does not start with 'jjj'

    message = tclib.Message(text='jjjBingo bongo')
    c = factory.MakeClique(message)
    c.SetCustomType(util.NewClassInstance(
      'grit.clique_unittest.DummyCustomType', clique.CustomType))
    translation = tclib.Translation(id=message.GetId(), text='Bilingo bolongo')
    c.AddTranslation(translation, 'fr', constants.DEFAULT_GENDER)
    self.assertTrue(
        c.MessageForLanguageAndGender(
            'fr', constants.DEFAULT_GENDER).GetRealContent().startswith('jjj'))

  def testWhitespaceMessagesAreNontranslateable(self):
    factory = clique.UberClique()

    message = tclib.Message(text=' \t')
    c = factory.MakeClique(message, translateable=True)
    self.assertFalse(c.IsTranslateable())

    message = tclib.Message(text='\n \n ')
    c = factory.MakeClique(message, translateable=True)
    self.assertFalse(c.IsTranslateable())

    message = tclib.Message(text='\n hello')
    c = factory.MakeClique(message, translateable=True)
    self.assertTrue(c.IsTranslateable())

  def testEachCliqueKeptSorted(self):
    factory = clique.UberClique()
    msg_a = tclib.Message(text='hello', description='a')
    msg_b = tclib.Message(text='hello', description='b')
    msg_c = tclib.Message(text='hello', description='c')
    # Insert out of order
    clique_b = factory.MakeClique(msg_b, translateable=True)
    clique_a = factory.MakeClique(msg_a, translateable=True)
    clique_c = factory.MakeClique(msg_c, translateable=True)
    clique_list = factory.cliques_[clique_a.GetId()]
    self.assertTrue(len(clique_list) == 3)
    self.assertTrue(clique_list[0] == clique_a)
    self.assertTrue(clique_list[1] == clique_b)
    self.assertTrue(clique_list[2] == clique_c)

  def testBestCliqueSortIsStable(self):
    factory = clique.UberClique()
    text = 'hello'
    msg_no_description = tclib.Message(text=text)
    msg_id_description_a = tclib.Message(text=text, description='ID: a')
    msg_id_description_b = tclib.Message(text=text, description='ID: b')
    msg_description_x = tclib.Message(text=text, description='x')
    msg_description_y = tclib.Message(text=text, description='y')
    clique_id = msg_no_description.GetId()

    # Insert in an order that tests all outcomes.
    clique_no_description = factory.MakeClique(msg_no_description,
                                               translateable=True)
    self.assertTrue(factory.BestClique(clique_id) == clique_no_description)
    clique_id_description_b = factory.MakeClique(msg_id_description_b,
                                                 translateable=True)
    self.assertTrue(factory.BestClique(clique_id) == clique_id_description_b)
    clique_id_description_a = factory.MakeClique(msg_id_description_a,
                                                 translateable=True)
    self.assertTrue(factory.BestClique(clique_id) == clique_id_description_a)
    clique_description_y = factory.MakeClique(msg_description_y,
                                              translateable=True)
    self.assertTrue(factory.BestClique(clique_id) == clique_description_y)
    clique_description_x = factory.MakeClique(msg_description_x,
                                              translateable=True)
    self.assertTrue(factory.BestClique(clique_id) == clique_description_x)


class DummyCustomType(clique.CustomType):
  def Validate(self, message):
    return message.GetRealContent().startswith('jjj')
  def ValidateAndModify(self, lang, translation):
    is_ok = self.Validate(translation)
    self.ModifyEachTextPart(lang, translation)
  def ModifyTextPart(self, lang, text):
    return 'jjj%s' % text


if __name__ == '__main__':
  unittest.main()
