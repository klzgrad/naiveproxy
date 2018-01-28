#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A utility script to help building Syzygy-reordered Chrome binaries."""

import logging
import optparse
import os
import subprocess
import sys


# The default relink executable to use to reorder binaries.
_DEFAULT_RELINKER = os.path.join(
    os.path.join(os.path.dirname(__file__), '../../..'),
    'third_party/syzygy/binaries/exe/relink.exe')

_LOGGER = logging.getLogger()

# We use the same seed for all random reorderings to get a deterministic build.
_RANDOM_SEED = 1347344


def _Shell(*cmd, **kw):
  """Shells out to "cmd". Returns a tuple of cmd's stdout, stderr."""
  _LOGGER.info('Running command "%s".', cmd)
  prog = subprocess.Popen(cmd, **kw)

  stdout, stderr = prog.communicate()
  if prog.returncode != 0:
    raise RuntimeError('Command "%s" returned %d.' % (cmd, prog.returncode))

  return stdout, stderr


def _ReorderBinary(relink_exe, executable, symbol, destination_dir):
  """Reorders the executable found in input_dir, and writes the resultant
  reordered executable and symbol files to destination_dir.

  If a file named <executable>-order.json exists, imposes that order on the
  output binaries, otherwise orders them randomly.
  """
  cmd = [relink_exe,
         '--overwrite',
         '--input-image=%s' % executable,
         '--input-pdb=%s' % symbol,
         '--output-image=%s' % os.path.abspath(
             os.path.join(destination_dir, os.path.basename(executable))),
         '--output-pdb=%s' % os.path.abspath(
             os.path.join(destination_dir, os.path.basename(symbol))),]

  # Check whether there's an order file available for the executable.
  order_file = '%s-order.json' % executable
  if os.path.exists(order_file):
    # The ordering file exists, let's use that.
    _LOGGER.info('Reordering "%s" according to "%s".',
                 os.path.basename(executable),
                 os.path.basename(order_file))
    cmd.append('--order-file=%s' % order_file)
  else:
    # No ordering file, we randomize the output.
    _LOGGER.info('Randomly reordering "%s"', executable)
    cmd.append('--seed=%d' % _RANDOM_SEED)

  return _Shell(*cmd)


def main(options):
  logging.basicConfig(level=logging.INFO)

  # Make sure the destination directory exists.
  if not os.path.isdir(options.destination_dir):
    _LOGGER.info('Creating destination directory "%s".',
                 options.destination_dir)
    os.makedirs(options.destination_dir)

  # Reorder the binaries into the destination directory.
  _ReorderBinary(options.relinker,
                 options.input_executable,
                 options.input_symbol,
                 options.destination_dir)


def _ParseOptions():
  option_parser = optparse.OptionParser()
  option_parser.add_option('--input_executable',
      help='The path to the input executable.')
  option_parser.add_option('--input_symbol',
      help='The path to the input symbol file.')
  option_parser.add_option('--relinker', default=_DEFAULT_RELINKER,
      help='Relinker exectuable to use, defaults to "%s"' % _DEFAULT_RELINKER)
  option_parser.add_option('-d', '--destination_dir',
      help='Destination directory for reordered files, defaults to '
           'the subdirectory "reordered" in the output_dir.')
  options, args = option_parser.parse_args()

  if not options.input_executable:
    option_parser.error('You must provide an input executable.')
  if not options.input_symbol:
    option_parser.error('You must provide an input symbol file.')

  if not options.destination_dir:
    options.destination_dir = os.path.join(options.output_dir, 'reordered')

  return options


if '__main__' == __name__:
  sys.exit(main(_ParseOptions()))
