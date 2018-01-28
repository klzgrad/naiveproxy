#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

''' Toolbar preprocessing code. Turns all IDS_COMMAND macros in the RC file
into simpler constructs that can be understood by GRIT. Also deals with
expansion of $lf; placeholders into the correct linefeed character.
'''

import preprocess_interface

from grit import lazy_re

class ToolbarPreProcessor(preprocess_interface.PreProcessor):
  ''' Toolbar PreProcessing class.
  '''

  _IDS_COMMAND_MACRO = lazy_re.compile(
      r'(.*IDS_COMMAND)\s*\(([a-zA-Z0-9_]*)\s*,\s*([a-zA-Z0-9_]*)\)(.*)')
  _LINE_FEED_PH = lazy_re.compile(r'\$lf;')
  _PH_COMMENT = lazy_re.compile(r'PHRWR')
  _COMMENT = lazy_re.compile(r'^(\s*)//.*')


  def Process(self, rctext, rcpath):
    ''' Processes the data in rctext.
    Args:
      rctext: string containing the contents of the RC file being processed
      rcpath: the path used to access the file.

    Return:
      The processed text.
    '''

    ret = ''
    rclines = rctext.splitlines()
    for line in rclines:

      if self._LINE_FEED_PH.search(line):
        # Replace "$lf;" placeholder comments by an empty line.
        # this will not be put into the processed result
        if self._PH_COMMENT.search(line):
          mm = self._COMMENT.search(line)
          if mm:
            line = '%s//' % mm.group(1)

        else:
          # Replace $lf by the right linefeed character
          line = self._LINE_FEED_PH.sub(r'\\n', line)

      # Deal with IDS_COMMAND_MACRO stuff
      mo = self._IDS_COMMAND_MACRO.search(line)
      if mo:
        line = '%s_%s_%s%s' % (mo.group(1), mo.group(2), mo.group(3), mo.group(4))

      ret += (line + '\n')

    return ret


