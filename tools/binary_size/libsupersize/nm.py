#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs nm on every .o file that comprises an ELF (plus some analysis).

The design of this file is entirely to work around Python's lack of concurrency.

CollectAliasesByAddress:
  Runs "nm" on the elf to collect all symbol names. This reveals symbol names of
  identical-code-folded functions.

CollectAliasesByAddressAsync:
  Runs CollectAliasesByAddress in a subprocess and returns a promise.

_BulkObjectFileAnalyzerMaster:
  Creates a subprocess and sends IPCs to it asking it to do work.

_BulkObjectFileAnalyzerSlave:
  Receives IPCs and delegates logic to _BulkObjectFileAnalyzerWorker.
  Runs _BulkObjectFileAnalyzerWorker on a background thread in order to stay
  responsive to IPCs.

_BulkObjectFileAnalyzerWorker:
  Performs the actual work. Uses Process Pools to shard out per-object-file
  work and then aggregates results.

BulkObjectFileAnalyzer:
  Alias for _BulkObjectFileAnalyzerMaster, but when SUPERSIZE_DISABLE_ASYNC=1,
  alias for _BulkObjectFileAnalyzerWorker.
  * AnalyzePaths: Run "nm" on all .o files to collect symbol names that exist
    within each. Does not work with thin archives (expand them first).
  * SortPaths: Sort results of AnalyzePaths().
  * AnalyzeStringLiterals: Must be run after AnalyzePaths() has completed.
    Extracts string literals from .o files, and then locates them within the
    "** merge strings" sections within an ELF's .rodata section.

This file can also be run stand-alone in order to test out the logic on smaller
sample sizes.
"""

from __future__ import print_function

import argparse
import atexit
import collections
import errno
import itertools
import logging
import os
import multiprocessing
import Queue
import signal
import subprocess
import sys
import threading
import traceback

import ar
import concurrent
import demangle
import models
import path_util

_MSG_ANALYZE_PATHS = 1
_MSG_SORT_PATHS = 2
_MSG_ANALYZE_STRINGS = 3
_MSG_GET_SYMBOL_NAMES = 4
_MSG_GET_STRINGS = 5

_active_pids = None


def _DecodePosition(x):
  # Encoded as "123:123"
  sep_idx = x.index(':')
  return (int(x[:sep_idx]), int(x[sep_idx + 1:]))


def _MakeToolPrefixAbsolute(tool_prefix):
  # Ensure tool_prefix is absolute so that CWD does not affect it
  if os.path.sep in tool_prefix:
    # Use abspath() on the dirname to avoid it stripping a trailing /.
    dirname = os.path.dirname(tool_prefix)
    tool_prefix = os.path.abspath(dirname) + tool_prefix[len(dirname):]
  return tool_prefix


def _IsRelevantNmName(name):
  # Skip lines like:
  # 00000000 t $t
  # 00000000 r $d.23
  # 00000344 N
  return name and not name.startswith('$')


def _IsRelevantObjectFileName(name):
  # Prevent marking compiler-generated symbols as candidates for shared paths.
  # E.g., multiple files might have "CSWTCH.12", but they are different symbols.
  #
  # Find these via:
  #   size_info.symbols.GroupedByFullName(min_count=-2).Filter(
  #       lambda s: s.WhereObjectPathMatches('{')).SortedByCount()
  # and then search for {shared}.
  # List of names this applies to:
  #   startup
  #   __tcf_0  <-- Generated for global destructors.
  #   ._79
  #   .Lswitch.table, .Lswitch.table.12
  #   CSWTCH.12
  #   lock.12
  #   table.12
  #   __compound_literal.12
  #   .L.ref.tmp.1
  #   .L.str, .L.str.3
  #   .L__func__.main:  (when using __func__)
  #   .L__FUNCTION__._ZN6webrtc17AudioDeviceBuffer11StopPlayoutEv
  #   .L__PRETTY_FUNCTION__._Unwind_Resume
  #   .L_ZZ24ScaleARGBFilterCols_NEONE9dx_offset  (an array literal)
  if name in ('__tcf_0', 'startup'):
    return False
  if name.startswith('._') and name[2:].isdigit():
    return False
  if name.startswith('.L') and name.find('.', 2) != -1:
    return False

  dot_idx = name.find('.')
  if dot_idx == -1:
    return True
  name = name[:dot_idx]

  return name not in ('CSWTCH', 'lock', '__compound_literal', 'table')


def CollectAliasesByAddress(elf_path, tool_prefix):
  """Runs nm on |elf_path| and returns a dict of address->[names]"""
  # Constructors often show up twice, so use sets to ensure no duplicates.
  names_by_address = collections.defaultdict(set)

  # About 60mb of output, but piping takes ~30s, and loading it into RAM
  # directly takes 3s.
  args = [path_util.GetNmPath(tool_prefix), '--no-sort', '--defined-only',
          elf_path]
  output = subprocess.check_output(args)
  for line in output.splitlines():
    space_idx = line.find(' ')
    address_str = line[:space_idx]
    section = line[space_idx + 1]
    mangled_name = line[space_idx + 3:]

    # To verify that rodata does not have aliases:
    #   nm --no-sort --defined-only libchrome.so > nm.out
    #   grep -v '\$' nm.out | grep ' r ' | sort | cut -d' ' -f1 > addrs
    #   wc -l < addrs; uniq < addrs | wc -l
    if section not in 'tTW' or not _IsRelevantNmName(mangled_name):
      continue

    address = int(address_str, 16)
    if not address:
      continue
    names_by_address[address].add(mangled_name)

  # Demangle all names.
  names_by_address = demangle.DemangleSetsInDicts(names_by_address, tool_prefix)

  # Since this is run in a separate process, minimize data passing by returning
  # only aliased symbols.
  # Also: Sort to ensure stable ordering.
  return {k: sorted(v) for k, v in names_by_address.iteritems() if len(v) > 1}


def _CollectAliasesByAddressAsyncHelper(elf_path, tool_prefix):
  result = CollectAliasesByAddress(elf_path, tool_prefix)
  return concurrent.EncodeDictOfLists(result, key_transform=str)


def CollectAliasesByAddressAsync(elf_path, tool_prefix):
  """Calls CollectAliasesByAddress in a helper process. Returns a Result."""
  def decode(encoded):
    return concurrent.DecodeDictOfLists(encoded, key_transform=int)
  return concurrent.ForkAndCall(
      _CollectAliasesByAddressAsyncHelper, (elf_path, tool_prefix),
      decode_func=decode)


def _LookupStringSectionPositions(target, tool_prefix, output_directory):
  """Returns a dict of object_path -> [(offset, size)...] of .rodata sections.

  Args:
    target: An archive path string (e.g., "foo.a") or a list of object paths.
  """
  is_archive = isinstance(target, basestring)
  args = [path_util.GetReadElfPath(tool_prefix), '-S', '--wide']
  if is_archive:
    args.append(target)
  else:
    # Assign path for when len(target) == 1, (no File: line exists).
    path = target[0]
    args.extend(target)

  output = subprocess.check_output(args, cwd=output_directory)
  lines = output.splitlines()
  section_positions_by_path = {}
  cur_offsets = []
  for line in lines:
    # File: base/third_party/libevent/libevent.a(buffer.o)
    # [Nr] Name              Type        Addr     Off    Size   ES Flg Lk Inf Al
    # [11] .rodata.str1.1    PROGBITS    00000000 0000b4 000004 01 AMS  0   0  1
    # [11] .rodata.str4.4    PROGBITS    00000000 0000b4 000004 01 AMS  0   0  4
    # [11] .rodata.str8.8    PROGBITS    00000000 0000b4 000004 01 AMS  0   0  8
    # [80] .rodata..L.str    PROGBITS    00000000 000530 000002 00   A  0   0  1
    # The various string sections differ by alignment.
    # The presence of a wchar_t literal (L"asdf") seems to make a str4 section.
    # When multiple sections exist, nm gives us no indication as to which
    # section each string corresponds to.
    if line.startswith('File: '):
      if cur_offsets:
        section_positions_by_path[path] = cur_offsets
        cur_offsets = []
      path = line[6:]
    elif '.rodata.' in line:
      progbits_idx = line.find('PROGBITS ')
      if progbits_idx != -1:
        fields = line[progbits_idx:].split()
        position = (int(fields[2], 16), int(fields[3], 16))
        # The heuristics in _IterStringLiterals rely on str1 coming first.
        if fields[-1] == '1':
          cur_offsets.insert(0, position)
        else:
          cur_offsets.append(position)
  if cur_offsets:
    section_positions_by_path[path] = cur_offsets
  return section_positions_by_path


def LookupElfRodataInfo(elf_path, tool_prefix):
  """Returns (address, offset, size) for the .rodata section."""
  args = [path_util.GetReadElfPath(tool_prefix), '-S', '--wide', elf_path]
  output = subprocess.check_output(args)
  lines = output.splitlines()
  for line in lines:
    # [Nr] Name           Type        Addr     Off     Size   ES Flg Lk Inf Al
    # [07] .rodata        PROGBITS    025e7000 237c000 5ec4f6 00   A  0   0 256
    if '.rodata ' in line:
      fields = line[line.index(models.SECTION_RODATA):].split()
      return int(fields[2], 16), int(fields[3], 16), int(fields[4], 16)
  raise AssertionError('No .rodata for command: ' + repr(args))


def _ReadFileChunks(path, positions):
  """Returns a list of strings corresponding to |positions|.

  Args:
    positions: List of (offset, size).
  """
  ret = []
  if not positions:
    return ret
  with open(path, 'rb') as f:
    for offset, size in positions:
      f.seek(offset)
      ret.append(f.read(size))
  return ret


def _ParseOneObjectFileNmOutput(lines):
  # Constructors are often repeated because they have the same unmangled
  # name, but multiple mangled names. See:
  # https://stackoverflow.com/questions/6921295/dual-emission-of-constructor-symbols
  symbol_names = set()
  string_addresses = []
  for line in lines:
    if not line:
      break
    space_idx = line.find(' ')  # Skip over address.
    section = line[space_idx + 1]
    mangled_name = line[space_idx + 3:]
    if _IsRelevantNmName(mangled_name):
      # Refer to _IsRelevantObjectFileName() for examples of names.
      if section == 'r' and (
          mangled_name.startswith('.L.str') or
          mangled_name.startswith('.L__') and mangled_name.find('.', 3) != -1):
        # Leave as a string for easier marshalling.
        string_addresses.append(line[:space_idx].lstrip('0') or '0')
      elif _IsRelevantObjectFileName(mangled_name):
        symbol_names.add(mangled_name)
  return string_addresses, symbol_names


def _ReadStringSections(target, output_directory, positions_by_path):
  """Returns a dict of object_path -> [string...] of .rodata chunks.

  Args:
    target: An archive path string (e.g., "foo.a") or a list of object paths.
    positions_by_path: A dict of object_path -> [(offset, size)...]
  """
  is_archive = isinstance(target, basestring)
  string_sections_by_path = {}
  if is_archive:
    for subpath, chunk in ar.IterArchiveChunks(
        os.path.join(output_directory, target)):
      path = '{}({})'.format(target, subpath)
      positions = positions_by_path.get(path)
      # No positions if file has no string literals.
      if positions:
        string_sections_by_path[path] = (
            [chunk[offset:offset + size] for offset, size in positions])
  else:
    for path in target:
      positions = positions_by_path.get(path)
      # We already log a warning about this in _IterStringLiterals().
      if positions:
        string_sections_by_path[path] = _ReadFileChunks(
            os.path.join(output_directory, path), positions)
  return string_sections_by_path


def _ExtractArchivePath(path):
  # E.g. foo/bar.a(baz.o)
  if path.endswith(')'):
    start_idx = path.index('(')
    return path[:start_idx]
  return None


def _IterStringLiterals(path, addresses, obj_sections):
  """Yields all string literals (including \0) for the given object path.

  Args:
    path: Object file path.
    addresses: List of string offsets encoded as hex strings.
    obj_sections: List of contents of .rodata.str sections read from the given
        object file.
  """

  next_offsets = sorted(int(a, 16) for a in addresses)
  if not obj_sections:
    # Happens when there is an address for a symbol which is not actually a
    # string literal, or when string_sections_by_path is missing an entry.
    logging.warning('Object has %d strings but no string sections: %s',
                    len(addresses), path)
    return
  for section_data in obj_sections:
    cur_offsets = next_offsets
    # Always assume first element is 0. I'm not entirely sure why this is
    # necessary, but strings get missed without it.
    next_offsets = [0]
    prev_offset = 0
    # TODO(agrieve): Switch to using nm --print-size in order to capture the
    #     address+size of each string rather than just the address.
    for offset in cur_offsets[1:]:
      if offset >= len(section_data):
        # Remaining offsets are for next section.
        next_offsets.append(offset)
        continue
      # Figure out which offsets apply to this section via heuristic of them
      # all ending with a null character.
      if offset == prev_offset or section_data[offset - 1] != '\0':
        next_offsets.append(offset)
        continue
      yield section_data[prev_offset:offset]
      prev_offset = offset

    if prev_offset < len(section_data):
      yield section_data[prev_offset:]


# This is a target for BulkForkAndCall().
def _ResolveStringPieces(encoded_string_addresses_by_path, string_data,
                         tool_prefix, output_directory):
  string_addresses_by_path = concurrent.DecodeDictOfLists(
      encoded_string_addresses_by_path)
  # Assign |target| as archive path, or a list of object paths.
  any_path = next(string_addresses_by_path.iterkeys())
  target = _ExtractArchivePath(any_path)
  if not target:
    target = string_addresses_by_path.keys()

  # Run readelf to find location of .rodata within the .o files.
  section_positions_by_path = _LookupStringSectionPositions(
      target, tool_prefix, output_directory)
  # Load the .rodata sections (from object files) as strings.
  string_sections_by_path = _ReadStringSections(
      target, output_directory, section_positions_by_path)

  # list of elf_positions_by_path.
  ret = [collections.defaultdict(list) for _ in string_data]
  # Brute-force search of strings within ** merge strings sections.
  # This is by far the slowest part of AnalyzeStringLiterals().
  # TODO(agrieve): Pre-process string_data into a dict of literal->address (at
  #     least for ascii strings).
  for path, object_addresses in string_addresses_by_path.iteritems():
    for value in _IterStringLiterals(
        path, object_addresses, string_sections_by_path.get(path)):
      first_match = -1
      first_match_dict = None
      for target_dict, data in itertools.izip(ret, string_data):
        # Set offset so that it will be 0 when len(value) is added to it below.
        offset = -len(value)
        while True:
          offset = data.find(value, offset + len(value))
          if offset == -1:
            break
          # Preferring exact matches (those following \0) over substring matches
          # significantly increases accuracy (although shows that linker isn't
          # being optimal).
          if offset == 0 or data[offset - 1] == '\0':
            break
          if first_match == -1:
            first_match = offset
            first_match_dict = target_dict
        if offset != -1:
          break
      if offset == -1:
        # Exact match not found, so take suffix match if it exists.
        offset = first_match
        target_dict = first_match_dict
      # Missing strings happen when optimization make them unused.
      if offset != -1:
        # Encode tuple as a string for easier mashalling.
        target_dict[path].append(
            str(offset) + ':' + str(len(value)))

  return [concurrent.EncodeDictOfLists(x) for x in ret]


# This is a target for BulkForkAndCall().
def _RunNmOnIntermediates(target, tool_prefix, output_directory):
  """Returns encoded_symbol_names_by_path, encoded_string_addresses_by_path.

  Args:
    target: Either a single path to a .a (as a string), or a list of .o paths.
  """
  is_archive = isinstance(target, basestring)
  args = [path_util.GetNmPath(tool_prefix), '--no-sort', '--defined-only']
  if is_archive:
    args.append(target)
  else:
    args.extend(target)
  output = subprocess.check_output(args, cwd=output_directory)
  lines = output.splitlines()
  # Empty .a file has no output.
  if not lines:
    return concurrent.EMPTY_ENCODED_DICT, concurrent.EMPTY_ENCODED_DICT
  is_multi_file = not lines[0]
  lines = iter(lines)
  if is_multi_file:
    next(lines)
    path = next(lines)[:-1]  # Path ends with a colon.
  else:
    assert not is_archive
    path = target[0]

  string_addresses_by_path = {}
  symbol_names_by_path = {}
  while path:
    if is_archive:
      # E.g. foo/bar.a(baz.o)
      path = '%s(%s)' % (target, path)

    string_addresses, mangled_symbol_names = _ParseOneObjectFileNmOutput(lines)
    symbol_names_by_path[path] = mangled_symbol_names
    if string_addresses:
      string_addresses_by_path[path] = string_addresses
    path = next(lines, ':')[:-1]

  # The multiprocess API uses pickle, which is ridiculously slow. More than 2x
  # faster to use join & split.
  # TODO(agrieve): We could use path indices as keys rather than paths to cut
  #     down on marshalling overhead.
  return (concurrent.EncodeDictOfLists(symbol_names_by_path),
          concurrent.EncodeDictOfLists(string_addresses_by_path))


class _BulkObjectFileAnalyzerWorker(object):
  def __init__(self, tool_prefix, output_directory):
    self._tool_prefix = _MakeToolPrefixAbsolute(tool_prefix)
    self._output_directory = output_directory
    self._paths_by_name = collections.defaultdict(list)
    self._encoded_string_addresses_by_path_chunks = []
    self._list_of_encoded_elf_string_positions_by_path = None

  def AnalyzePaths(self, paths):
    def iter_job_params():
      object_paths = []
      for path in paths:
        # Note: _ResolveStringPieces relies upon .a not being grouped.
        if path.endswith('.a'):
          yield path, self._tool_prefix, self._output_directory
        else:
          object_paths.append(path)

      BATCH_SIZE = 50  # Chosen arbitrarily.
      for i in xrange(0, len(object_paths), BATCH_SIZE):
        batch = object_paths[i:i + BATCH_SIZE]
        yield batch, self._tool_prefix, self._output_directory

    params = list(iter_job_params())
    # Order of the jobs doesn't matter since each job owns independent paths,
    # and our output is a dict where paths are the key.
    results = concurrent.BulkForkAndCall(_RunNmOnIntermediates, params)

    # Names are still mangled.
    all_paths_by_name = self._paths_by_name
    for encoded_syms, encoded_strs in results:
      symbol_names_by_path = concurrent.DecodeDictOfLists(encoded_syms)
      for path, names in symbol_names_by_path.iteritems():
        for name in names:
          all_paths_by_name[name].append(path)

      if encoded_strs != concurrent.EMPTY_ENCODED_DICT:
        self._encoded_string_addresses_by_path_chunks.append(encoded_strs)
    logging.debug('worker: AnalyzePaths() completed.')

  def SortPaths(self):
    # Finally, demangle all names, which can result in some merging of lists.
    self._paths_by_name = demangle.DemangleKeysAndMergeLists(
        self._paths_by_name, self._tool_prefix)
    # Sort and uniquefy.
    for key in self._paths_by_name.iterkeys():
      self._paths_by_name[key] = sorted(set(self._paths_by_name[key]))

  def AnalyzeStringLiterals(self, elf_path, elf_string_positions):
    logging.debug('worker: AnalyzeStringLiterals() started.')
    # Read string_data from elf_path, to be shared by forked processes.
    address, offset, _ = LookupElfRodataInfo(elf_path, self._tool_prefix)
    adjust = address - offset
    abs_string_positions = (
        (addr - adjust, s) for addr, s in elf_string_positions)
    string_data = _ReadFileChunks(elf_path, abs_string_positions)

    params = (
        (chunk, string_data, self._tool_prefix, self._output_directory)
        for chunk in self._encoded_string_addresses_by_path_chunks)
    # Order of the jobs doesn't matter since each job owns independent paths,
    # and our output is a dict where paths are the key.
    results = concurrent.BulkForkAndCall(_ResolveStringPieces, params)
    results = list(results)

    final_result = []
    for i in xrange(len(elf_string_positions)):
      final_result.append(
          concurrent.JoinEncodedDictOfLists([r[i] for r in results]))
    self._list_of_encoded_elf_string_positions_by_path = final_result
    logging.debug('worker: AnalyzeStringLiterals() completed.')

  def GetSymbolNames(self):
    return self._paths_by_name

  def GetStringPositions(self):
    return [concurrent.DecodeDictOfLists(x, value_transform=_DecodePosition)
            for x in self._list_of_encoded_elf_string_positions_by_path]

  def GetEncodedStringPositions(self):
    return self._list_of_encoded_elf_string_positions_by_path

  def Close(self):
    pass


def _TerminateSubprocesses():
  global _active_pids
  if _active_pids:
    for pid in _active_pids:
      os.kill(pid, signal.SIGKILL)
    _active_pids = []


class _BulkObjectFileAnalyzerMaster(object):
  """Runs BulkObjectFileAnalyzer in a subprocess."""
  def __init__(self, tool_prefix, output_directory):
    self._child_pid = None
    self._pipe = None
    self._tool_prefix = tool_prefix
    self._output_directory = output_directory

  def _Spawn(self):
    global _active_pids
    parent_conn, child_conn = multiprocessing.Pipe()
    self._child_pid = os.fork()
    if self._child_pid:
      # We are the parent process.
      if _active_pids is None:
        _active_pids = []
        atexit.register(_TerminateSubprocesses)
      _active_pids.append(self._child_pid)
      self._pipe = parent_conn
    else:
      # We are the child process.
      logging.root.handlers[0].setFormatter(logging.Formatter(
          'nm: %(levelname).1s %(relativeCreated)6d %(message)s'))
      worker_analyzer = _BulkObjectFileAnalyzerWorker(
          self._tool_prefix, self._output_directory)
      slave = _BulkObjectFileAnalyzerSlave(worker_analyzer, child_conn)
      slave.Run()

  def AnalyzePaths(self, paths):
    if self._child_pid is None:
      self._Spawn()

    logging.debug('Sending batch of %d paths to subprocess', len(paths))
    payload = '\x01'.join(paths)
    self._pipe.send((_MSG_ANALYZE_PATHS, payload))

  def SortPaths(self):
    self._pipe.send((_MSG_SORT_PATHS,))

  def AnalyzeStringLiterals(self, elf_path, string_positions):
    self._pipe.send((_MSG_ANALYZE_STRINGS, elf_path, string_positions))

  def GetSymbolNames(self):
    self._pipe.send((_MSG_GET_SYMBOL_NAMES,))
    self._pipe.recv()  # None
    logging.debug('Decoding nm results from forked process')
    encoded_paths_by_name = self._pipe.recv()
    return concurrent.DecodeDictOfLists(encoded_paths_by_name)

  def GetStringPositions(self):
    self._pipe.send((_MSG_GET_STRINGS,))
    self._pipe.recv()  # None
    logging.debug('Decoding string symbol results from forked process')
    result = self._pipe.recv()
    return [concurrent.DecodeDictOfLists(x, value_transform=_DecodePosition)
            for x in result]

  def Close(self):
    self._pipe.close()
    # Child process should terminate gracefully at this point, but leave it in
    # _active_pids to be killed just in case.


class _BulkObjectFileAnalyzerSlave(object):
  """The subprocess entry point."""
  def __init__(self, worker_analyzer, pipe):
    self._worker_analyzer = worker_analyzer
    self._pipe = pipe
    # Use a worker thread so that AnalyzeStringLiterals() is non-blocking. The
    # thread allows the main thread to process a call to GetSymbolNames() while
    # AnalyzeStringLiterals() is in progress.
    self._job_queue = Queue.Queue()
    self._worker_thread = threading.Thread(target=self._WorkerThreadMain)
    self._allow_analyze_paths = True

  def _WorkerThreadMain(self):
    while True:
      # Handle exceptions so test failure will be explicit and not block.
      try:
        func = self._job_queue.get()
        func()
      except Exception:
        traceback.print_exc()
      self._job_queue.task_done()

  def _WaitForAnalyzePathJobs(self):
    if self._allow_analyze_paths:
      self._job_queue.join()
      self._allow_analyze_paths = False

  def Run(self):
    try:
      self._worker_thread.start()
      while True:
        message = self._pipe.recv()
        if message[0] == _MSG_ANALYZE_PATHS:
          assert self._allow_analyze_paths, (
              'Cannot call AnalyzePaths() after AnalyzeStringLiterals()s.')
          paths = message[1].split('\x01')
          self._job_queue.put(lambda: self._worker_analyzer.AnalyzePaths(paths))
        elif message[0] == _MSG_SORT_PATHS:
          assert self._allow_analyze_paths, (
              'Cannot call SortPaths() after AnalyzeStringLiterals()s.')
          self._job_queue.put(self._worker_analyzer.SortPaths)
        elif message[0] == _MSG_ANALYZE_STRINGS:
          self._WaitForAnalyzePathJobs()
          elf_path, string_positions = message[1:]
          self._job_queue.put(
              lambda: self._worker_analyzer.AnalyzeStringLiterals(
                  elf_path, string_positions))
        elif message[0] == _MSG_GET_SYMBOL_NAMES:
          self._WaitForAnalyzePathJobs()
          self._pipe.send(None)
          paths_by_name = self._worker_analyzer.GetSymbolNames()
          self._pipe.send(concurrent.EncodeDictOfLists(paths_by_name))
        elif message[0] == _MSG_GET_STRINGS:
          self._job_queue.join()
          # Send a None packet so that other side can measure IPC transfer time.
          self._pipe.send(None)
          self._pipe.send(self._worker_analyzer.GetEncodedStringPositions())
    except EOFError:
      pass
    except EnvironmentError, e:
      # Parent process exited so don't log.
      if e.errno in (errno.EPIPE, errno.ECONNRESET):
        sys.exit(1)

    logging.debug('nm bulk subprocess finished.')
    sys.exit(0)


BulkObjectFileAnalyzer = _BulkObjectFileAnalyzerMaster
if concurrent.DISABLE_ASYNC:
  BulkObjectFileAnalyzer = _BulkObjectFileAnalyzerWorker


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--multiprocess', action='store_true')
  parser.add_argument('--tool-prefix', required=True)
  parser.add_argument('--output-directory', required=True)
  parser.add_argument('--elf-file', type=os.path.realpath)
  parser.add_argument('--show-names', action='store_true')
  parser.add_argument('--show-strings', action='store_true')
  parser.add_argument('objects', type=os.path.realpath, nargs='+')

  args = parser.parse_args()
  logging.basicConfig(level=logging.DEBUG,
                      format='%(levelname).1s %(relativeCreated)6d %(message)s')

  if args.multiprocess:
    bulk_analyzer = _BulkObjectFileAnalyzerMaster(
        args.tool_prefix, args.output_directory)
  else:
    concurrent.DISABLE_ASYNC = True
    bulk_analyzer = _BulkObjectFileAnalyzerWorker(
        args.tool_prefix, args.output_directory)

  # Pass individually to test multiple calls.
  for path in args.objects:
    bulk_analyzer.AnalyzePaths([path])
  bulk_analyzer.SortPaths()

  names_to_paths = bulk_analyzer.GetSymbolNames()
  print('Found {} names'.format(len(names_to_paths)))
  if args.show_names:
    for name, paths in names_to_paths.iteritems():
      print('{}: {!r}'.format(name, paths))

  if args.elf_file:
    address, offset, size = LookupElfRodataInfo(
        args.elf_file, args.tool_prefix)
    bulk_analyzer.AnalyzeStringLiterals(args.elf_file, ((address, size),))

    positions_by_path = bulk_analyzer.GetStringPositions()[0]
    print('Found {} string literals'.format(sum(
        len(v) for v in positions_by_path.itervalues())))
    if args.show_strings:
      logging.debug('.rodata adjust=%d', address - offset)
      for path, positions in positions_by_path.iteritems():
        strs = _ReadFileChunks(
            args.elf_file, ((offset + addr, size) for addr, size in positions))
        print('{}: {!r}'.format(
            path, [s if len(s) < 20 else s[:20] + '...' for s in strs]))


if __name__ == '__main__':
  main()
