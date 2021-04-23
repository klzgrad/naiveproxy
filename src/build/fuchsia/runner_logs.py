# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Creates and manages test runner log file objects.

Provides a context manager object for use in a with statement
and a module level FileStreamFor function for use by clients.
"""

import collections
import multiprocessing
import os

from symbolizer import RunSymbolizer

SYMBOLIZED_SUFFIX = '.symbolized'

_RunnerLogEntry = collections.namedtuple(
    '_RunnerLogEntry', ['name', 'log_file', 'path', 'symbolize'])

# Module singleton variable.
_instance = None


class RunnerLogManager(object):
  """ Runner logs object for use in a with statement."""

  def __init__(self, log_dir, build_ids_files):
    global _instance
    if _instance:
      raise Exception('Only one RunnerLogManager can be instantiated')

    self._log_dir = log_dir
    self._build_ids_files = build_ids_files
    self._runner_logs = []

    if self._log_dir and not os.path.isdir(self._log_dir):
      os.makedirs(self._log_dir)

    _instance = self

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_value, traceback):
    pool = multiprocessing.Pool(4)
    for log_entry in self._runner_logs:
      pool.apply_async(_FinalizeLog, (log_entry, self._build_ids_files))
    pool.close()
    pool.join()
    _instance = None


  def _FileStreamFor(self, name, symbolize):
    if any(elem.name == name for elem in self._runner_logs):
      raise Exception('RunnerLogManager can only open "%s" once' % name)

    path = os.path.join(self._log_dir, name) if self._log_dir else os.devnull
    log_file = open(path, 'w')

    self._runner_logs.append(_RunnerLogEntry(name, log_file, path, symbolize))

    return log_file


def _FinalizeLog(log_entry, build_ids_files):
    log_entry.log_file.close()

    if log_entry.symbolize:
      input_file = open(log_entry.path, 'r')
      output_file = open(log_entry.path + SYMBOLIZED_SUFFIX, 'w')
      proc = RunSymbolizer(input_file, output_file, build_ids_files)
      proc.wait()
      output_file.close()
      input_file.close()


def IsEnabled():
  """Returns True if the RunnerLogManager has been created, or False if not."""

  return _instance is not None and _instance._log_dir is not None


def FileStreamFor(name, symbolize=False):
  """Opens a test runner file stream in the test runner log directory.

  If no test runner log directory is specified, output is discarded.

  name: log file name
  symbolize: if True, make a symbolized copy of the log after closing it.

  Returns an opened log file object."""

  return _instance._FileStreamFor(name, symbolize) if IsEnabled() else open(
      os.devnull, 'w')
