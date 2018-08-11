# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Deals with loading & saving .size files."""

import cStringIO
import calendar
import contextlib
import datetime
import gzip
import json
import logging
import os
import shutil

import models


# File format version for .size files.
_SERIALIZATION_VERSION = 'Size File Format v1'


def _LogSize(file_obj, desc):
  if not hasattr(file_obj, 'fileno'):
    return
  file_obj.flush()
  size = os.fstat(file_obj.fileno()).st_size
  logging.debug('File size with %s: %d' % (desc, size))


def _SaveSizeInfoToFile(size_info, file_obj):
  file_obj.write('# Created by //tools/binary_size\n')
  file_obj.write('%s\n' % _SERIALIZATION_VERSION)
  headers = {
      'metadata': size_info.metadata,
      'section_sizes': size_info.section_sizes,
  }
  metadata_str = json.dumps(headers, file_obj, indent=2, sort_keys=True)
  file_obj.write('%d\n' % len(metadata_str))
  file_obj.write(metadata_str)
  file_obj.write('\n')
  _LogSize(file_obj, 'header')  # For libchrome: 570 bytes.

  # Store a single copy of all paths and have them referenced by index.
  unique_path_tuples = sorted(set(
      (s.object_path, s.source_path) for s in size_info.raw_symbols))
  path_tuples = dict.fromkeys(unique_path_tuples)
  for i, tup in enumerate(unique_path_tuples):
    path_tuples[tup] = i
  file_obj.write('%d\n' % len(unique_path_tuples))
  file_obj.writelines('%s\t%s\n' % pair for pair in unique_path_tuples)
  _LogSize(file_obj, 'paths')  # For libchrome, adds 200kb.

  # Symbol counts by section.
  by_section = size_info.raw_symbols.GroupedBySectionName()
  file_obj.write('%s\n' % '\t'.join(g.name for g in by_section))
  file_obj.write('%s\n' % '\t'.join(str(len(g)) for g in by_section))

  def write_numeric(func, delta=False):
    for group in by_section:
      prev_value = 0
      last_sym = group[-1]
      for symbol in group:
        value = func(symbol)
        if delta:
          value, prev_value = value - prev_value, value
        file_obj.write(str(value))
        if symbol is not last_sym:
          file_obj.write(' ')
      file_obj.write('\n')

  write_numeric(lambda s: s.address, delta=True)
  _LogSize(file_obj, 'addresses')  # For libchrome, adds 300kb.
  # Do not write padding except for overhead symbols, it will be recalculated
  # from addresses on load.
  write_numeric(lambda s: s.size if s.IsOverhead() else s.size_without_padding)
  _LogSize(file_obj, 'sizes')  # For libchrome, adds 300kb
  write_numeric(lambda s: path_tuples[(s.object_path, s.source_path)],
                delta=True)
  _LogSize(file_obj, 'path indices')  # For libchrome: adds 125kb.

  prev_aliases = None
  for group in by_section:
    for symbol in group:
      file_obj.write(symbol.full_name)
      if symbol.aliases and symbol.aliases is not prev_aliases:
        file_obj.write('\t0%x' % symbol.num_aliases)
      prev_aliases = symbol.aliases
      if symbol.flags:
        file_obj.write('\t%x' % symbol.flags)
      file_obj.write('\n')
  _LogSize(file_obj, 'names (final)')  # For libchrome: adds 3.5mb.


def _LoadSizeInfoFromFile(file_obj, size_path):
  """Loads a size_info from the given file."""
  lines = iter(file_obj)
  next(lines)  # Comment line.
  actual_version = next(lines)[:-1]
  assert actual_version == _SERIALIZATION_VERSION, (
      'Version mismatch. Need to write some upgrade code.')
  json_len = int(next(lines))
  json_str = file_obj.read(json_len)
  headers = json.loads(json_str)
  section_sizes = headers['section_sizes']
  metadata = headers.get('metadata')
  lines = iter(file_obj)
  next(lines)  # newline after closing } of json.

  num_path_tuples = int(next(lines))
  path_tuples = [None] * num_path_tuples
  for i in xrange(num_path_tuples):
    path_tuples[i] = next(lines)[:-1].split('\t')

  section_names = next(lines)[:-1].split('\t')
  section_counts = [int(c) for c in next(lines)[:-1].split('\t')]

  def read_numeric(delta=False):
    ret = []
    delta_multiplier = int(delta)
    for _ in section_counts:
      value = 0
      fields = next(lines).split(' ')
      for i, f in enumerate(fields):
        value = value * delta_multiplier + int(f)
        fields[i] = value
      ret.append(fields)
    return ret

  addresses = read_numeric(delta=True)
  sizes = read_numeric(delta=False)
  path_indices = read_numeric(delta=True)

  raw_symbols = [None] * sum(section_counts)
  symbol_idx = 0
  for section_index, cur_section_name in enumerate(section_names):
    alias_counter = 0
    for i in xrange(section_counts[section_index]):
      parts = next(lines)[:-1].split('\t')
      flags_part = None
      aliases_part = None

      if len(parts) == 3:
        aliases_part = parts[1]
        flags_part = parts[2]
      elif len(parts) == 2:
        if parts[1][0] == '0':
          aliases_part = parts[1]
        else:
          flags_part = parts[1]

      full_name = parts[0]
      # Use a bit less RAM by using the same instance for this common string.
      if full_name == models.STRING_LITERAL_NAME:
        full_name = models.STRING_LITERAL_NAME
      flags = int(flags_part, 16) if flags_part else 0
      num_aliases = int(aliases_part, 16) if aliases_part else 0

      new_sym = models.Symbol.__new__(models.Symbol)
      new_sym.section_name = cur_section_name
      new_sym.address = addresses[section_index][i]
      new_sym.size = sizes[section_index][i]
      new_sym.full_name = full_name
      paths = path_tuples[path_indices[section_index][i]]
      new_sym.object_path = paths[0]
      new_sym.source_path = paths[1]
      new_sym.flags = flags
      new_sym.padding = 0  # Derived
      new_sym.template_name = ''  # Derived
      new_sym.name = ''  # Derived

      if num_aliases:
        assert alias_counter == 0
        new_sym.aliases = [new_sym]
        alias_counter = num_aliases - 1
      elif alias_counter > 0:
        new_sym.aliases = raw_symbols[symbol_idx - 1].aliases
        new_sym.aliases.append(new_sym)
        alias_counter -= 1
      else:
        new_sym.aliases = None

      raw_symbols[symbol_idx] = new_sym
      symbol_idx += 1

  return models.SizeInfo(section_sizes, raw_symbols, metadata=metadata,
                         size_path=size_path)


@contextlib.contextmanager
def _OpenGzipForWrite(path):
  # Open in a way that doesn't set any gzip header fields.
  with open(path, 'wb') as f:
    with gzip.GzipFile(filename='', mode='wb', fileobj=f, mtime=0) as fz:
      yield fz


def SaveSizeInfo(size_info, path):
  """Saves |size_info| to |path}."""
  if os.environ.get('SUPERSIZE_MEASURE_GZIP') == '1':
    with _OpenGzipForWrite(path) as f:
      _SaveSizeInfoToFile(size_info, f)
  else:
    # It is seconds faster to do gzip in a separate step. 6s -> 3.5s.
    stringio = cStringIO.StringIO()
    _SaveSizeInfoToFile(size_info, stringio)

    logging.debug('Serialization complete. Gzipping...')
    stringio.seek(0)
    with _OpenGzipForWrite(path) as f:
      shutil.copyfileobj(stringio, f)


def LoadSizeInfo(path):
  """Returns a SizeInfo loaded from |path|."""
  with gzip.open(path) as f:
    return _LoadSizeInfoFromFile(f, path)
