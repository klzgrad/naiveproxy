#!/usr/bin/env python3
# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

description = """
Make a symlink.
"""
usage = "%prog [options] source[ source ...] linkname"
epilog = """\
A symlink to source is created at linkname. If multiple sources are specified,
then linkname is assumed to be a directory, and will contain all the links to
the sources (basenames identical to their source).

On Windows, this will use hard links (mklink /H) to avoid requiring elevation.
This means that if the original is deleted and replaced, the link will still
have the old contents.
"""

import errno
import optparse
import os
import shutil
import subprocess
import sys


def Main(argv):
  parser = optparse.OptionParser(usage=usage, description=description,
                                 epilog=epilog)
  parser.add_option('-f', '--force', action='store_true')

  options, args = parser.parse_args(argv[1:])
  if len(args) < 2:
    parser.error('at least two arguments required.')

  target = args[-1]
  sources = args[:-1]
  for s in sources:
    t = os.path.join(target, os.path.basename(s))
    if len(sources) == 1 and not os.path.isdir(target):
      t = target
    t = os.path.expanduser(t)
    if os.path.realpath(t) == os.path.realpath(s):
      continue
    try:
      # N.B. Python 2.x does not have os.symlink for Windows.
      #   Python 3 has os.symlink for Windows, but requires either the admin-
      #   granted privilege SeCreateSymbolicLinkPrivilege or, as of Windows 10
      #   1703, that Developer Mode be enabled. Hard links and junctions do not
      #   require any extra privileges to create.
      if os.name == 'nt':
        # mklink does not tolerate /-delimited path names.
        t = t.replace('/', '\\')
        s = s.replace('/', '\\')
        # N.B. This tool only handles file hardlinks, not directory junctions.
        subprocess.check_output(['cmd.exe', '/c', 'mklink', '/H', t, s],
                                stderr=subprocess.STDOUT)
      else:
        os.symlink(s, t)
    except OSError as e:
      if e.errno == errno.EEXIST and options.force:
        if os.path.isdir(t):
          shutil.rmtree(t, ignore_errors=True)
        else:
          os.remove(t)
        os.symlink(s, t)
      else:
        raise
    except subprocess.CalledProcessError as e:
      # Since subprocess.check_output does not return an easily checked error
      # number, in the 'force' case always assume it is 'file already exists'
      # and retry.
      if options.force:
        if os.path.isdir(t):
          shutil.rmtree(t, ignore_errors=True)
        else:
          os.remove(t)
        subprocess.check_output(e.cmd, stderr=subprocess.STDOUT)
      else:
        raise


if __name__ == '__main__':
  sys.exit(Main(sys.argv))
