#!/usr/bin/env python
# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs 'gn help' and various subhelps, and spits out html.
# TODO:
# - Handle numbered and dashed lists -> <ol> <ul>. (See "os" and "toolchain").
# - Handle "Arguments:" blocks a bit better (the argument names could be
#   distinguished).
# - Convert "|blahblah|" to <code>.
# - Spit out other similar formats like wiki, markdown, whatever.

import cgi
import subprocess
import sys


def GetOutput(*args):
  try:
    return subprocess.check_output([sys.argv[1]] + list(args))
  except subprocess.CalledProcessError:
    return ''


def ParseTopLevel(out):
  commands = []
  output = []
  for line in out.splitlines():
    if line.startswith('  '):
      command, sep, rest = line.partition(':')
      command = command.strip()
      is_option = command.startswith('-')
      output_line = ['<li>']
      if not is_option:
        commands.append(command)
        output_line.append('<a href="#' + cgi.escape(command) + '">')
      output_line.append(cgi.escape(command))
      if not is_option:
        output_line.append('</a>')
      output_line.extend([sep + cgi.escape(rest) + '</li>'])
      output.append(''.join(output_line))
    else:
      output.append('<h2>' + cgi.escape(line) + '</h2>')
  return commands, output


def ParseCommand(command, out):
  first_line = True
  got_example = False
  output = []
  for line in out.splitlines():
    if first_line:
      name, sep, rest = line.partition(':')
      name = name.strip()
      output.append('<h3><a name="' + cgi.escape(command) + '">' +
                    cgi.escape(name + sep + rest) + '</a></h3>')
      first_line = False
    else:
      if line.startswith('Example'):
        # Special subsection that's pre-formatted.
        if got_example:
          output.append('</pre>')
        got_example = True
        output.append('<h4>Example</h4>')
        output.append('<pre>')
      elif not line.strip():
        output.append('<p>')
      elif not line.startswith('  ') and line.endswith(':'):
        # Subsection.
        output.append('<h4>' + cgi.escape(line[:-1]) + '</h4>')
      else:
        output.append(cgi.escape(line))
  if got_example:
    output.append('</pre>')
  return output


def main():
  if len(sys.argv) < 2:
    print 'usage: help_as_html.py <gn_binary>'
    return 1
  header = '''<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial, sans-serif; font-size: small; }
      pre { font-family: Consolas, monospace; font-size: small; }
      #container { margin: 0 auto; max-width: 48rem; width: 90%; }
    </style>
  </head>
  <body>
    <div id="container"><h1>GN</h1>
'''
  footer = '</div></body></html>'
  commands, output = ParseTopLevel(GetOutput('help'))
  for command in commands:
    output += ParseCommand(command, GetOutput('help', command))
  print header + '\n'.join(output) + footer
  return 0


if __name__ == '__main__':
  sys.exit(main())
