# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import xml.dom.minidom

from devil.utils import cmd_helper
from pylib.constants import host_paths


_FINDBUGS_HOME = os.path.join(host_paths.DIR_SOURCE_ROOT, 'third_party',
                              'findbugs')
_FINDBUGS_JAR = os.path.join(_FINDBUGS_HOME, 'lib', 'findbugs.jar')
_FINDBUGS_MAX_HEAP = 768
_FINDBUGS_PLUGIN_PATH = os.path.join(
    host_paths.DIR_SOURCE_ROOT, 'tools', 'android', 'findbugs_plugin', 'lib',
    'chromiumPlugin.jar')


def _ParseXmlResults(results_doc):
  errors = set()
  warnings = set()
  for en in (n for n in results_doc.documentElement.childNodes
             if n.nodeType == xml.dom.Node.ELEMENT_NODE):
    if en.tagName == 'Errors':
      errors.update(_ParseErrors(en))
    if en.tagName == 'BugInstance':
      warnings.add(_ParseBugInstance(en))
  return errors, warnings


def _GetMessage(node):
  for c in (n for n in node.childNodes
            if n.nodeType == xml.dom.Node.ELEMENT_NODE):
    if c.tagName == 'Message':
      if (len(c.childNodes) == 1
          and c.childNodes[0].nodeType == xml.dom.Node.TEXT_NODE):
        return c.childNodes[0].data
  return None


def _GetTextContent(node):
  if (len(node.childNodes) == 1
      and node.childNodes[0].nodeType == xml.dom.Node.TEXT_NODE):
    return node.childNodes[0].data
  return ''


def _ParseErrors(node):
  errors = set()
  for error_node in (n for n in node.childNodes
                     if n.nodeType == xml.dom.Node.ELEMENT_NODE):
    error_text = '(unable to determine error text)'
    if error_node.tagName == 'Error':
      error_message_nodes = (
          n for n in error_node.childNodes
          if (n.nodeType == xml.dom.Node.ELEMENT_NODE
              and n.tagName == 'ErrorMessage'))
      text_pieces = [_GetTextContent(n) for n in error_message_nodes]
      if text_pieces:
        error_text = ', '.join(t for t in text_pieces if t)
      errors.add(FindBugsError(error_node.tagName, error_text))
  return errors


def _ParseBugInstance(node):
  bug = FindBugsWarning(node.getAttribute('type'))
  msg_parts = []
  for c in (n for n in node.childNodes
            if n.nodeType == xml.dom.Node.ELEMENT_NODE):
    if c.tagName == 'Class':
      msg_parts.append(_GetMessage(c))
    elif c.tagName == 'Method':
      msg_parts.append(_GetMessage(c))
    elif c.tagName == 'Field':
      msg_parts.append(_GetMessage(c))
    elif c.tagName == 'SourceLine':
      bug.file_name = c.getAttribute('sourcefile')
      if c.hasAttribute('start'):
        bug.start_line = int(c.getAttribute('start'))
      if c.hasAttribute('end'):
        bug.end_line = int(c.getAttribute('end'))
      msg_parts.append(_GetMessage(c))
    elif c.tagName == 'ShortMessage':
      msg_parts.append(_GetTextContent(c))
  bug.message = tuple(m for m in msg_parts if m)
  return bug


class FindBugsError(object):
  def __init__(self, error_type='', error_val=''):
    self.error_type = error_type
    self.error_val = error_val

  def __cmp__(self, other):
    return (cmp(self.error_type, other.error_type)
            or cmp(self.error_val, other.error_val))

  def __eq__(self, other):
    return self.__dict__ == other.__dict__

  def __hash__(self):
    return hash((self.error_type, self.error_val))

  def __ne__(self, other):
    return not self == other

  def __str__(self):
    return '%s: %s' % (self.error_type, self.error_val)


class FindBugsWarning(object):

  def __init__(self, bug_type='', end_line=0, file_name='', message=None,
               start_line=0):
    self.bug_type = bug_type
    self.end_line = end_line
    self.file_name = file_name
    if message is None:
      self.message = tuple()
    else:
      self.message = message
    self.start_line = start_line

  def __cmp__(self, other):
    return (cmp(self.file_name, other.file_name)
            or cmp(self.start_line, other.start_line)
            or cmp(self.end_line, other.end_line)
            or cmp(self.bug_type, other.bug_type)
            or cmp(self.message, other.message))

  def __eq__(self, other):
    return self.__dict__ == other.__dict__

  def __hash__(self):
    return hash((self.bug_type, self.end_line, self.file_name, self.message,
                 self.start_line))

  def __ne__(self, other):
    return not self == other

  def __str__(self):
    return '%s: %s' % (self.bug_type, '\n  '.join(self.message))


def Run(exclude, classes_to_analyze, system_classes, auxiliary_classes,
        output_file, findbug_args, jars):
  """Run FindBugs.

  Args:
    exclude: the exclude xml file, refer to FindBugs's -exclude command option.
    classes_to_analyze: the list of classes need to analyze, refer to FindBugs'
                        -onlyAnalyze command line option.
    system_classes: system classes to help analysis.
    auxiliary_classes: other classes to help analysis. Refer to FindBugs'
                       -auxclasspath command line option.
    output_file: An optional path to dump XML results to.
    findbug_args: A list of additional command line options to pass to Findbugs.
  """
  # TODO(jbudorick): Get this from the build system.
  all_aux_classes = []
  all_aux_classes.extend(system_classes or [])
  all_aux_classes.extend(
      os.path.abspath(classes)
      for classes in auxiliary_classes or [])

  cmd = ['java',
         '-classpath', '%s:' % _FINDBUGS_JAR,
         '-Xmx%dm' % _FINDBUGS_MAX_HEAP,
         '-Dfindbugs.home="%s"' % _FINDBUGS_HOME,
         '-jar', _FINDBUGS_JAR,
         '-textui', '-sortByClass',
         '-pluginList', _FINDBUGS_PLUGIN_PATH, '-xml:withMessages']
  if system_classes:
    cmd.extend(['-auxclasspath', ':'.join(all_aux_classes)])
  if classes_to_analyze:
    cmd.extend(['-onlyAnalyze', classes_to_analyze])
  if exclude:
    cmd.extend(['-exclude', os.path.abspath(exclude)])
  if output_file:
    cmd.extend(['-output', output_file])
  if findbug_args:
    cmd.extend(findbug_args)
  cmd.extend(os.path.abspath(j) for j in jars or [])

  if output_file:
    _, _, stderr = cmd_helper.GetCmdStatusOutputAndError(cmd)

    results_doc = xml.dom.minidom.parse(output_file)
  else:
    _, raw_out, stderr = cmd_helper.GetCmdStatusOutputAndError(cmd)
    results_doc = xml.dom.minidom.parseString(raw_out)

  for line in stderr.splitlines():
    logging.debug('  %s', line)

  current_errors_set, current_warnings_set = _ParseXmlResults(results_doc)

  return (' '.join(cmd), current_errors_set, current_warnings_set)

