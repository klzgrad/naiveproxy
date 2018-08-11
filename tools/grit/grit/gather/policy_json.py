#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Support for "policy_templates.json" format used by the policy template
generator as a source for generating ADM,ADMX,etc files.'''

import types
import sys

from grit.gather import skeleton_gatherer
from grit import util
from grit import tclib
from xml.dom import minidom
from xml.parsers.expat import ExpatError


class PolicyJson(skeleton_gatherer.SkeletonGatherer):
  '''Collects and translates the following strings from policy_templates.json:
    - captions, descriptions, labels and Android app support details of policies
    - captions of enumeration items
    - misc strings from the 'messages' section
     Translatable strings may have untranslateable placeholders with the same
     format that is used in .grd files.
  '''

  def _ParsePlaceholder(self, placeholder, msg):
    '''Extracts a placeholder from a DOM node and adds it to a tclib Message.

    Args:
      placeholder: A DOM node of the form:
        <ph name="PLACEHOLDER_NAME">Placeholder text<ex>Example value</ex></ph>
      msg: The placeholder is added to this message.
    '''
    text = []
    example_text = []
    for node1 in placeholder.childNodes:
      if (node1.nodeType == minidom.Node.TEXT_NODE):
        text.append(node1.data)
      elif (node1.nodeType == minidom.Node.ELEMENT_NODE and
            node1.tagName == 'ex'):
        for node2 in node1.childNodes:
          example_text.append(node2.toxml())
      else:
         raise Exception('Unexpected element inside a placeholder: ' +
                         node2.toxml())
    if example_text == []:
      # In such cases the original text is okay for an example.
      example_text = text
    msg.AppendPlaceholder(tclib.Placeholder(
        placeholder.attributes['name'].value,
        ''.join(text).strip(),
        ''.join(example_text).strip()))

  def _ParseMessage(self, string, desc):
    '''Parses a given string and adds it to the output as a translatable chunk
    with a given description.

    Args:
      string: The message string to parse.
      desc: The description of the message (for the translators).
    '''
    msg = tclib.Message(description=desc)
    xml = '<msg>' + string + '</msg>'
    try:
      node = minidom.parseString(xml).childNodes[0]
    except ExpatError:
      reason = '''Input isn't valid XML (has < & > been escaped?): ''' + string
      raise Exception, reason, sys.exc_info()[2]

    for child in node.childNodes:
      if child.nodeType == minidom.Node.TEXT_NODE:
        msg.AppendText(child.data)
      elif child.nodeType == minidom.Node.ELEMENT_NODE:
        if child.tagName == 'ph':
          self._ParsePlaceholder(child, msg)
        else:
          raise Exception("Not implemented.")
      else:
        raise Exception("Not implemented.")
    self.skeleton_.append(self.uberclique.MakeClique(msg))

  def _ParseNode(self, node):
    '''Traverses the subtree of a DOM node, and register a tclib message for
    all the <message> nodes.
    '''
    att_text = []
    if node.attributes:
      items = node.attributes.items()
      items.sort()
      for key, value in items:
        att_text.append(' %s=\"%s\"' % (key, value))
    self._AddNontranslateableChunk("<%s%s>" %
                                   (node.tagName, ''.join(att_text)))
    if node.tagName == 'message':
      msg = tclib.Message(description=node.attributes['desc'])
      for child in node.childNodes:
        if child.nodeType == minidom.Node.TEXT_NODE:
          if msg == None:
            self._AddNontranslateableChunk(child.data)
          else:
            msg.AppendText(child.data)
        elif child.nodeType == minidom.Node.ELEMENT_NODE:
          if child.tagName == 'ph':
            self._ParsePlaceholder(child, msg)
        else:
          assert False
      self.skeleton_.append(self.uberclique.MakeClique(msg))
    else:
      for child in node.childNodes:
        if child.nodeType == minidom.Node.TEXT_NODE:
          self._AddNontranslateableChunk(child.data)
        elif node.nodeType == minidom.Node.ELEMENT_NODE:
          self._ParseNode(child)

    self._AddNontranslateableChunk("</%s>" % node.tagName)

  def _AddIndentedNontranslateableChunk(self, depth, string):
    '''Adds a nontranslateable chunk of text to the internally stored output.

    Args:
      depth: The number of double spaces to prepend to the next argument string.
      string: The chunk of text to add.
    '''
    result = []
    while depth > 0:
      result.append('  ')
      depth = depth - 1
    result.append(string)
    self._AddNontranslateableChunk(''.join(result))

  def _GetDescription(self, item, item_type, parent_item, key):
    '''Creates a description for a translatable message. The description gives
    some context for the person who will translate this message.

    Args:
      item: A policy or an enumeration item.
      item_type: 'enum_item' | 'policy'
      parent_item: The owner of item. (A policy of type group or enum.)
      key: The name of the key to parse.
      depth: The level of indentation.
    '''
    key_map = {
      'desc': 'Description',
      'caption': 'Caption',
      'label': 'Label',
      'arc_support': 'Information about the effect on Android apps'
    }
    if item_type == 'policy':
      return '%s of the policy named %s' % (key_map[key], item['name'])
    elif item_type == 'enum_item':
      return ('%s of the option named %s in policy %s' %
              (key_map[key], item['name'], parent_item['name']))
    else:
      raise Exception('Unexpected type %s' % item_type)

  def _AddPolicyKey(self, item, item_type, parent_item, key, depth):
    '''Given a policy/enumeration item and a key, adds that key and its value
    into the output.
    E.g.:
       'example_value': 123
    If key indicates that the value is a translatable string, then it is parsed
    as a translatable string.

    Args:
      item: A policy or an enumeration item.
      item_type: 'enum_item' | 'policy'
      parent_item: The owner of item. (A policy of type group or enum.)
      key: The name of the key to parse.
      depth: The level of indentation.
    '''
    self._AddIndentedNontranslateableChunk(depth, "'%s': " % key)
    if key in ('desc', 'caption', 'label', 'arc_support'):
      self._AddNontranslateableChunk("'''")
      self._ParseMessage(
          item[key],
          self._GetDescription(item, item_type, parent_item, key))
      self._AddNontranslateableChunk("''',\n")
    else:
      str_val = item[key]
      if type(str_val) == types.StringType:
        str_val = "'%s'" % self.Escape(str_val)
      else:
        str_val = str(str_val)
      self._AddNontranslateableChunk(str_val + ',\n')

  def _AddItems(self, items, item_type, parent_item, depth):
    '''Parses and adds a list of items from the JSON file. Items can be policies
    or parts of an enum policy.

    Args:
      items: Either a list of policies or a list of dictionaries.
      item_type: 'enum_item' | 'policy'
      parent_item: If items contains a list of policies, then this is the policy
        group that owns them. If items contains a list of enumeration items,
        then this is the enum policy that holds them.
      depth: Indicates the depth of our position in the JSON hierarchy. Used to
        add nice line-indent to the output.
    '''
    for item1 in items:
      self._AddIndentedNontranslateableChunk(depth, "{\n")
      for key in item1.keys():
        if key == 'items':
          self._AddIndentedNontranslateableChunk(depth + 1, "'items': [\n")
          self._AddItems(item1['items'], 'enum_item', item1, depth + 2)
          self._AddIndentedNontranslateableChunk(depth + 1, "],\n")
        elif key == 'policies' and all(not isinstance(x, str)
                                       for x in item1['policies']):
          self._AddIndentedNontranslateableChunk(depth + 1, "'policies': [\n")
          self._AddItems(item1['policies'], 'policy', item1, depth + 2)
          self._AddIndentedNontranslateableChunk(depth + 1, "],\n")
        else:
          self._AddPolicyKey(item1, item_type, parent_item, key, depth + 1)
      self._AddIndentedNontranslateableChunk(depth, "},\n")

  def _AddMessages(self):
    '''Processed and adds the 'messages' section to the output.'''
    self._AddNontranslateableChunk("  'messages': {\n")
    for name, message in self.data['messages'].iteritems():
      self._AddNontranslateableChunk("      '%s': {\n" % name)
      self._AddNontranslateableChunk("        'text': '''")
      self._ParseMessage(message['text'], message['desc'])
      self._AddNontranslateableChunk("'''\n")
      self._AddNontranslateableChunk("      },\n")
    self._AddNontranslateableChunk("  },\n")

  # Although we use the RegexpGatherer base class, we do not use the
  # _RegExpParse method of that class to implement Parse().  Instead, we
  # parse using a DOM parser.
  def Parse(self):
    if self.have_parsed_:
      return
    self.have_parsed_ = True

    self.text_ = self._LoadInputFile()
    if util.IsExtraVerbose():
      print self.text_

    self.data = eval(self.text_)

    self._AddNontranslateableChunk('{\n')
    self._AddNontranslateableChunk("  'policy_definitions': [\n")
    self._AddItems(self.data['policy_definitions'], 'policy', None, 2)
    self._AddNontranslateableChunk("  ],\n")
    self._AddMessages()
    self._AddNontranslateableChunk('\n}')

  def Escape(self, text):
    # \ -> \\
    # ' -> \'
    # " -> \"
    return text.replace('\\', '\\\\').replace('"', '\\"').replace("'", "\\'")
