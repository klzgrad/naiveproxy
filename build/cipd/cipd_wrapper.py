#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Updates dependencies managed by CIPD."""

import argparse
import os
import subprocess
import sys
import tempfile

_SRC_ROOT = os.path.realpath(
    os.path.join(os.path.dirname(__file__), '..', '..'))


def cipd_ensure(root, service_url, ensure_file):

  def is_windows():
    return sys.platform in ('cygwin', 'win32')

  cipd_binary = 'cipd'
  if is_windows():
    cipd_binary = 'cipd.bat'

  with tempfile.NamedTemporaryFile() as tmp_stdouterr:
    retcode = subprocess.call(
        [cipd_binary, 'ensure',
         '-ensure-file', ensure_file,
         '-root', root,
         '-service-url', service_url],
        shell=is_windows(),
        stderr=subprocess.STDOUT,
        stdout=tmp_stdouterr)
    if retcode:
      tmp_stdouterr.seek(0)
      for line in tmp_stdouterr:
        print line,
    return retcode


def main():
  parser = argparse.ArgumentParser(
      description='Updates CIPD-managed dependencies based on the given OS.')

  parser.add_argument(
      '--chromium-root',
      required=True,
      help='Root directory for dependency.')
  parser.add_argument(
      '--service-url',
      help='The url of the CIPD service.',
      default='https://chrome-infra-packages.appspot.com')
  parser.add_argument(
      '--ensure-file',
      type=os.path.realpath,
      required=True,
      help='The path to the ensure file.')
  args = parser.parse_args()
  return cipd_ensure(args.chromium_root, args.service_url, args.ensure_file)


if __name__ == '__main__':
  sys.exit(main())
