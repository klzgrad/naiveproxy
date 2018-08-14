#!/usr/bin/env python
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Unit tests for grit.gather.policy_json'''

import os
import re
import sys
if __name__ == '__main__':
  sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))

import unittest
import StringIO

from grit.gather import policy_json

class PolicyJsonUnittest(unittest.TestCase):

  def GetExpectedOutput(self, original):
    expected = eval(original)
    for key, message in expected['messages'].iteritems():
      del message['desc']
    return expected

  def testEmpty(self):
    original = "{'policy_definitions': [], 'messages': {}}"
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 0)
    self.failUnless(eval(original) == eval(gatherer.Translate('en')))

  def testGeneralPolicy(self):
    original = (
        "{"
        "  'policy_definitions': ["
        "    {"
        "      'name': 'HomepageLocation',"
        "      'type': 'string',"
        "      'supported_on': ['chrome.*:8-'],"
        "      'features': {'dynamic_refresh': 1},"
        "      'example_value': 'http://chromium.org',"
        "      'caption': 'nothing special 1',"
        "      'desc': 'nothing special 2',"
        "      'label': 'nothing special 3',"
        "    },"
        "  ],"
        "  'messages': {"
        "    'msg_identifier': {"
        "      'text': 'nothing special 3',"
        "      'desc': 'nothing special descr 3',"
        "    }"
        "  }"
        "}")
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 4)
    expected = self.GetExpectedOutput(original)
    self.failUnless(expected == eval(gatherer.Translate('en')))

  def testEnum(self):
    original = (
        "{"
        "  'policy_definitions': ["
        "    {"
        "      'name': 'Policy1',"
        "      'items': ["
        "        {"
        "          'name': 'Item1',"
        "          'caption': 'nothing special',"
        "        }"
        "      ]"
        "    },"
        "  ],"
        "  'messages': {}"
        "}")
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 1)
    expected = self.GetExpectedOutput(original)
    self.failUnless(expected == eval(gatherer.Translate('en')))

  # Keeping for backwards compatibility.
  def testSubPolicyOldFormat(self):
    original = (
        "{"
        "  'policy_definitions': ["
        "    {"
        "      'type': 'group',"
        "      'policies': ["
        "        {"
        "          'name': 'Policy1',"
        "          'caption': 'nothing special',"
        "        }"
        "      ]"
        "    }"
        "  ],"
        "  'messages': {}"
        "}")
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 1)
    expected = self.GetExpectedOutput(original)
    self.failUnless(expected == eval(gatherer.Translate('en')))

  def testSubPolicyNewFormat(self):
    original = (
        "{"
        "  'policy_definitions': ["
        "    {"
        "      'type': 'group',"
        "      'policies': ['Policy1']"
        "    },"
        "    {"
        "      'name': 'Policy1',"
        "      'caption': 'nothing special',"
        "    }"
        "  ],"
        "  'messages': {}"
        "}")
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 1)
    expected = self.GetExpectedOutput(original)
    self.failUnless(expected == eval(gatherer.Translate('en')))

  def testEscapingAndLineBreaks(self):
    original = """{
        'policy_definitions': [],
        'messages': {
          'msg1': {
            # The following line will contain two backslash characters when it
            # ends up in eval().
            'text': '''backslashes, Sir? \\\\''',
            'desc': '',
          },
          'msg2': {
            'text': '''quotes, Madam? "''',
            'desc': '',
          },
          'msg3': {
            # The following line will contain two backslash characters when it
            # ends up in eval().
            'text': 'backslashes, Sir? \\\\',
            'desc': '',
          },
          'msg4': {
            'text': "quotes, Madam? '",
            'desc': '',
          },
          'msg5': {
            'text': '''what happens
with a newline?''',
            'desc': ''
          },
          'msg6': {
            # The following line will contain a backslash+n when it ends up in
            # eval().
            'text': 'what happens\\nwith a newline? (Episode 1)',
            'desc': ''
          }
        }
}"""
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 6)
    expected = self.GetExpectedOutput(original)
    self.failUnless(expected == eval(gatherer.Translate('en')))

  def testPlaceholders(self):
    original = """{
        'policy_definitions': [
          {
            'name': 'Policy1',
            'caption': '''Please install
                <ph name="PRODUCT_NAME">$1<ex>Google Chrome</ex></ph>.''',
          },
        ],
        'messages': {}
}"""
    gatherer = policy_json.PolicyJson(StringIO.StringIO(original))
    gatherer.Parse()
    self.failUnless(len(gatherer.GetCliques()) == 1)
    expected = eval(re.sub('<ph.*ph>', '$1', original))
    self.failUnless(expected == eval(gatherer.Translate('en')))
    self.failUnless(gatherer.GetCliques()[0].translateable)
    msg = gatherer.GetCliques()[0].GetMessage()
    self.failUnless(len(msg.GetPlaceholders()) == 1)
    ph = msg.GetPlaceholders()[0]
    self.failUnless(ph.GetOriginal() == '$1')
    self.failUnless(ph.GetPresentation() == 'PRODUCT_NAME')
    self.failUnless(ph.GetExample() == 'Google Chrome')

  def testGetDescription(self):
    gatherer = policy_json.PolicyJson({})
    self.assertEquals(
        gatherer._GetDescription({'name': 'Policy1'}, 'policy', None, 'desc'),
        'Description of the policy named Policy1')
    self.assertEquals(
        gatherer._GetDescription({'name': 'Plcy2'}, 'policy', None, 'caption'),
        'Caption of the policy named Plcy2')
    self.assertEquals(
        gatherer._GetDescription({'name': 'Plcy3'}, 'policy', None, 'label'),
        'Label of the policy named Plcy3')
    self.assertEquals(
        gatherer._GetDescription({'name': 'Item'}, 'enum_item',
                                 {'name': 'Policy'}, 'caption'),
        'Caption of the option named Item in policy Policy')


if __name__ == '__main__':
  unittest.main()
