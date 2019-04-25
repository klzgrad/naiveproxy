# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import subprocess

from common import SDK_ROOT


def SymbolizerFilter(input_fd, build_ids_file):
  """Symbolizes an output stream from a process.

  input_fd: A file descriptor of the stream to be symbolized.
  build_ids_file: Path to the ids.txt file which maps build IDs to
                  unstripped binaries on the filesystem.
  Returns a generator that yields symbolized process output."""

  llvm_symbolizer_path = os.path.join(SDK_ROOT, os.pardir, os.pardir,
                                      'llvm-build', 'Release+Asserts', 'bin',
                                      'llvm-symbolizer')
  symbolizer = os.path.join(SDK_ROOT, 'tools', 'symbolize')
  symbolizer_cmd = [symbolizer, '-ids', build_ids_file,
                    '-ids-rel', '-llvm-symbolizer', llvm_symbolizer_path,
                    '-build-id-dir', os.path.join(SDK_ROOT, '.build-id')]

  logging.info('Running "%s".' % ' '.join(symbolizer_cmd))
  symbolizer_proc = subprocess.Popen(
      symbolizer_cmd,
      stdout=subprocess.PIPE,
      stdin=input_fd,
      close_fds=True)

  for line in symbolizer_proc.stdout:
    yield line

  symbolizer_proc.wait()
