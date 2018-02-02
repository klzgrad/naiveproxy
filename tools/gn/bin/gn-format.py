# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Based on clang-format.py.
#
# This file is a minimal gn format vim-integration. To install:
# - Change 'binary' if gn is not on the path (see below).
# - Add to your .vimrc:
#
#   map <F1> :pyf <path-to-this-file>/gn-format.py<CR>
#
# gn format currently formats only a complete file so visual ranges, etc. won't
# be used. It operates on the current, potentially unsaved buffer and does not
# create or save any files. To revert a formatting, just undo.

import difflib
import subprocess
import sys
import vim

# Change this to the full path if gn is not on the path.
binary = 'gn'

def main():
  # Get the current text.
  buf = vim.current.buffer
  text = '\n'.join(buf)

  is_win = sys.platform.startswith('win32')
  # Avoid flashing an ugly cmd prompt on Windows when invoking gn.
  startupinfo = None
  if is_win:
    startupinfo = subprocess.STARTUPINFO()
    startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startupinfo.wShowWindow = subprocess.SW_HIDE

  # Call formatter. Needs shell=True on Windows due to gn.bat in depot_tools.
  p = subprocess.Popen([binary, 'format', '--stdin'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       stdin=subprocess.PIPE, startupinfo=startupinfo,
                       shell=is_win, universal_newlines=True)
  stdout, stderr = p.communicate(input=text)
  if p.returncode != 0:
    print 'Formatting failed, please report to gn-dev@chromium.org.'
    print stdout, stderr
  else:
    # Otherwise, replace current buffer.
    lines = stdout.split('\n')
    # Last line should have trailing \n, but we don't want to insert a blank
    # line at the end of the buffer, so remove that.
    if lines[-1] == '':
      lines = lines[:-1]
    sequence = difflib.SequenceMatcher(None, vim.current.buffer, lines)
    for op in reversed(sequence.get_opcodes()):
      if op[0] is not 'equal':
        vim.current.buffer[op[1]:op[2]] = lines[op[3]:op[4]]

main()
