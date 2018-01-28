#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A utility script to help building Syzygy-instrumented Chrome binaries."""

import glob
import logging
import optparse
import os
import shutil
import subprocess
import sys


# The default directory containing the Syzygy toolchain.
_DEFAULT_SYZYGY_DIR = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../../..',
    'third_party/syzygy/binaries/exe/'))

# Basenames of various tools.
_INSTRUMENT_EXE = 'instrument.exe'
_GENFILTER_EXE = 'genfilter.exe'

_LOGGER = logging.getLogger()


def _Shell(*cmd, **kw):
  """Shells out to "cmd". Returns a tuple of cmd's stdout, stderr."""
  _LOGGER.info('Running command "%s".', cmd)
  prog = subprocess.Popen(cmd, **kw)

  stdout, stderr = prog.communicate()
  if prog.returncode != 0:
    raise RuntimeError('Command "%s" returned %d.' % (cmd, prog.returncode))

  return stdout, stderr


def _CompileFilter(syzygy_dir, executable, symbol, filter_file,
                   output_filter_file):
  """Compiles the provided filter writing the compiled filter file to
  output_filter_file.
  """
  cmd = [os.path.abspath(os.path.join(syzygy_dir, _GENFILTER_EXE)),
         '--action=compile',
         '--input-image=%s' % executable,
         '--input-pdb=%s' % symbol,
         '--output-file=%s' % output_filter_file,
         '--overwrite',
         os.path.abspath(filter_file)]

  _Shell(*cmd)
  if not os.path.exists(output_filter_file):
    raise RuntimeError('Compiled filter file missing: %s' % output_filter_file)
  return


def _InstrumentBinary(syzygy_dir, mode, executable, symbol, dst_dir,
                      filter_file, allocation_filter_file):
  """Instruments the executable found in input_dir, and writes the resultant
  instrumented executable and symbol files to dst_dir.
  """
  cmd = [os.path.abspath(os.path.join(syzygy_dir, _INSTRUMENT_EXE)),
         '--overwrite',
         '--mode=%s' % mode,
         '--debug-friendly',
         '--input-image=%s' % executable,
         '--input-pdb=%s' % symbol,
         '--output-image=%s' % os.path.abspath(
             os.path.join(dst_dir, os.path.basename(executable))),
         '--output-pdb=%s' % os.path.abspath(
             os.path.join(dst_dir, os.path.basename(symbol)))]

  if mode == "asan":
    cmd.append('--no-augment-pdb')
    # Disable some of the new SysyASAN features. We're seeing an increase in
    # crash rates and are wondering if they are to blame.
    cmd.append(
        '--asan-rtl-options="--enable_feature_randomization '
        '--prevent_duplicate_corruption_crashes"')

  # If any filters were specified then pass them on to the instrumenter.
  if filter_file:
    cmd.append('--filter=%s' % os.path.abspath(filter_file))
  if allocation_filter_file:
    cmd.append('--allocation-filter-config-file=%s' %
        os.path.abspath(allocation_filter_file))

  return _Shell(*cmd)


def main(options):
  # Make sure the destination directory exists.
  if not os.path.isdir(options.destination_dir):
    _LOGGER.info('Creating destination directory "%s".',
                 options.destination_dir)
    os.makedirs(options.destination_dir)

  # Compile the filter if one was provided.
  if options.filter:
    _CompileFilter(options.syzygy_dir,
                   options.input_executable,
                   options.input_symbol,
                   options.filter,
                   options.output_filter_file)

  # Instruments the binaries into the destination directory.
  _InstrumentBinary(options.syzygy_dir,
                    options.mode,
                    options.input_executable,
                    options.input_symbol,
                    options.destination_dir,
                    options.output_filter_file,
                    options.allocation_filter_file)


def _ParseOptions():
  option_parser = optparse.OptionParser()
  option_parser.add_option('--input_executable',
      help='The path to the input executable.')
  option_parser.add_option('--input_symbol',
      help='The path to the input symbol file.')
  option_parser.add_option('--mode',
      help='Specifies which instrumentation mode is to be used.')
  option_parser.add_option('--syzygy-dir', default=_DEFAULT_SYZYGY_DIR,
      help='Instrumenter executable to use, defaults to "%default".')
  option_parser.add_option('-d', '--destination_dir',
      help='Destination directory for instrumented files.')
  option_parser.add_option('--filter',
      help='An optional filter. This will be compiled and passed to the '
           'instrumentation executable.')
  option_parser.add_option('--output-filter-file',
      help='The path where the compiled filter will be written. This is '
           'required if --filter is specified.')
  option_parser.add_option('--allocation-filter-file',
      help='The path to the SyzyASAN allocation filter to use.')
  options, args = option_parser.parse_args()

  if not options.mode:
    option_parser.error('You must provide an instrumentation mode.')
  if not options.input_executable:
    option_parser.error('You must provide an input executable.')
  if not options.input_symbol:
    option_parser.error('You must provide an input symbol file.')
  if not options.destination_dir:
    option_parser.error('You must provide a destination directory.')
  if options.filter and not options.output_filter_file:
    option_parser.error('You must provide a filter output file.')

  return options


if '__main__' == __name__:
  logging.basicConfig(level=logging.INFO)
  sys.exit(main(_ParseOptions()))
