# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Deals with loading & saving .size files.

The .size file is written in the following format. There are no section
delimeters, instead the end of a section is usually determined by a row count
on the first line of a section, followed by that amount of rows. In other
cases, the sections have a known size.

Header
------
4 lines long.
Line 0 of the file is a header comment.
Line 1 is the serialization version of the file.
Line 2 is the number of characters in the metadata string.
Line 3 is the metadata string, a stringified JSON object.

Path list
---------
A list of paths. The first line is the size of the list,
and the next N lines that follow are items in the list. Each item is a tuple
of (object_path, source_path) where the two parts are tab seperated.

Component list
--------------
A list of components. The first line is the size of the list,
and the next N lines that follow are items in the list. Each item is a unique
COMPONENT which is referenced later.
This section is only present if 'has_components' is True in the metadata.

Symbol counts
-------------
2 lines long.
The first line is a tab seperated list of section names.
The second line is a tab seperated list of symbol group lengths, in the same
order as the previous line.

Numeric values
--------------
In each section, the number of rows is the same as the number of section names
in Symbol counts. The values on a row are space seperated, in the order of the
symbols in each group.

Addressses
~~~~~~~~~~
Symbol start addresses which are delta-encoded.

Sizes
~~~~~
The number of bytes this symbol takes up.

Path indicies
~~~~~~~~~~~~~
Indicies that reference paths in the prior Path list section. Delta-encoded.

Component indicies
~~~~~~~~~~~~~~~~~~
Indicies that reference components in the prior Component list section.
Delta-encoded.

Symbols
-------
The final section contains details info on each symbol. Each line represents
a single symbol. Values are tab seperated and follow this format:
symbol.full_name, symbol.num_aliases, symbol.flags
|num_aliases| will be omitted if the aliases of the symbol are the same as the
previous line. |flags| will be omitted if there are no flags.
"""

import cStringIO
import contextlib
import gzip
import itertools
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
  """Saves size info to a .size file.

  Args:
    size_info: Data to write to the file
    file_object: File opened for writing
  """
  # Created by supersize header
  file_obj.write('# Created by //tools/binary_size\n')
  file_obj.write('%s\n' % _SERIALIZATION_VERSION)
  # JSON metadata
  headers = {
      'metadata': size_info.metadata,
      'section_sizes': size_info.section_sizes,
      'has_components': True,
  }
  metadata_str = json.dumps(headers, file_obj, indent=2, sort_keys=True)
  file_obj.write('%d\n' % len(metadata_str))
  file_obj.write(metadata_str)
  file_obj.write('\n')
  _LogSize(file_obj, 'header')  # For libchrome: 570 bytes.

  # Store a single copy of all paths and have them referenced by index.
  unique_path_tuples = sorted(set(
      (s.object_path, s.source_path) for s in size_info.raw_symbols))
  path_tuples = {tup: i for i, tup in enumerate(unique_path_tuples)}
  file_obj.write('%d\n' % len(unique_path_tuples))
  file_obj.writelines('%s\t%s\n' % pair for pair in unique_path_tuples)
  _LogSize(file_obj, 'paths')  # For libchrome, adds 200kb.

  # Store a single copy of all components and have them referenced by index.
  unique_components = sorted(set(s.component for s in size_info.raw_symbols))
  components = {comp: i for i, comp in enumerate(unique_components)}
  file_obj.write('%d\n' % len(unique_components))
  file_obj.writelines('%s\n' % comp for comp in unique_components)
  _LogSize(file_obj, 'components')

  # Symbol counts by section.
  by_section = size_info.raw_symbols.GroupedBySectionName()
  file_obj.write('%s\n' % '\t'.join(g.name for g in by_section))
  file_obj.write('%s\n' % '\t'.join(str(len(g)) for g in by_section))

  # Addresses, sizes, path indicies, component indicies
  def write_numeric(func, delta=False):
    """Write the result of func(symbol) for each symbol in each symbol group.

    Each line written represents one symbol group in |by_section|.
    The values in each line are space seperated and are the result of calling
    |func| with the Nth symbol in the group.

    If |delta| is True, the differences in values are written instead.
    """
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
  write_numeric(lambda s: components[s.component], delta=True)
  _LogSize(file_obj, 'component indices')

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


def _ReadLine(file_iter):
  """Read a line from a file object iterator and remove the newline character.

  Args:
    file_iter: File object iterator

  Returns:
    String
  """
  # str[:-1] removes the last character from a string, specifically the newline
  return next(file_iter)[:-1]


def _ReadValuesFromLine(file_iter, split):
  """Read a list of values from a line in a file object iterator.

  Args:
    file_iter: File object iterator
    split: Splits the line with the given string

  Returns:
    List of string values
  """
  return _ReadLine(file_iter).split(split)


def _LoadSizeInfoFromFile(file_obj, size_path):
  """Loads a size_info from the given file.

  See _SaveSizeInfoToFile for details on the .size file format.

  Args:
    file_obj: File to read, should be a GzipFile
  """
  lines = iter(file_obj)
  _ReadLine(lines)  # Line 0: Created by supersize header
  actual_version = _ReadLine(lines)
  assert actual_version == _SERIALIZATION_VERSION, (
      'Version mismatch. Need to write some upgrade code.')
  # JSON metadata
  json_len = int(_ReadLine(lines))
  json_str = file_obj.read(json_len)

  headers = json.loads(json_str)
  section_sizes = headers['section_sizes']
  metadata = headers.get('metadata')
  has_components = headers.get('has_components', False)
  lines = iter(file_obj)
  _ReadLine(lines)

  # Path list
  num_path_tuples = int(_ReadLine(lines))  # Line 4 - number of paths in list
  # Read the path list values and store for later
  path_tuples = [_ReadValuesFromLine(lines, split='\t')
                 for _ in xrange(num_path_tuples)]

  # Component list
  if has_components:
    num_components = int(_ReadLine(lines))  # number of components in list
    components = [_ReadLine(lines) for _ in xrange(num_components)]

  # Symbol counts by section.
  section_names = _ReadValuesFromLine(lines, split='\t')
  section_counts = [int(c) for c in _ReadValuesFromLine(lines, split='\t')]

  # Addresses, sizes, path indicies, component indicies
  def read_numeric(delta=False):
    """Read numeric values, where each line corresponds to a symbol group.

    The values in each line are space seperated.
    If |delta| is True, the numbers are read as a value to add to the sum of the
    prior values in the line, or as the amount to change by.
    """
    ret = []
    delta_multiplier = int(delta)
    for _ in section_counts:
      value = 0
      fields = []
      for f in _ReadValuesFromLine(lines, split=' '):
        value = value * delta_multiplier + int(f)
        fields.append(value)
      ret.append(fields)
    return ret

  addresses = read_numeric(delta=True)
  sizes = read_numeric(delta=False)
  path_indices = read_numeric(delta=True)
  if has_components:
    component_indices = read_numeric(delta=True)
  else:
    component_indices = [None] * len(section_names)

  raw_symbols = [None] * sum(section_counts)
  symbol_idx = 0
  for (cur_section_name, cur_section_count, cur_addresses, cur_sizes,
       cur_path_indicies, cur_component_indices) in itertools.izip(
       section_names, section_counts, addresses, sizes, path_indices,
       component_indices):
    alias_counter = 0
    for i in xrange(cur_section_count):
      parts = _ReadValuesFromLine(lines, split='\t')
      full_name = parts[0]
      flags_part = None
      aliases_part = None

      # aliases_part or flags_part may have been omitted.
      if len(parts) == 3:
        # full_name  aliases_part  flags_part
        aliases_part = parts[1]
        flags_part = parts[2]
      elif len(parts) == 2:
        if parts[1][0] == '0':
          # full_name  aliases_part
          aliases_part = parts[1]
        else:
          # full_name  flags_part
          flags_part = parts[1]

      # Use a bit less RAM by using the same instance for this common string.
      if full_name == models.STRING_LITERAL_NAME:
        full_name = models.STRING_LITERAL_NAME
      flags = int(flags_part, 16) if flags_part else 0
      num_aliases = int(aliases_part, 16) if aliases_part else 0

      # Skip the constructor to avoid default value checks
      new_sym = models.Symbol.__new__(models.Symbol)
      new_sym.section_name = cur_section_name
      new_sym.full_name = full_name
      new_sym.address = cur_addresses[i]
      new_sym.size = cur_sizes[i]
      paths = path_tuples[cur_path_indicies[i]]
      new_sym.object_path, new_sym.source_path = paths
      component = components[cur_component_indices[i]] if has_components else ''
      new_sym.component = component
      new_sym.flags = flags
      # Derived
      new_sym.padding = 0
      new_sym.template_name = ''
      new_sym.name = ''

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
def _OpenGzipForWrite(path, file_obj=None):
  # Open in a way that doesn't set any gzip header fields.
  if file_obj:
    with gzip.GzipFile(filename='', mode='wb', fileobj=file_obj, mtime=0) as fz:
      yield fz
  else:
    with open(path, 'wb') as f:
      with gzip.GzipFile(filename='', mode='wb', fileobj=f, mtime=0) as fz:
        yield fz


def SaveSizeInfo(size_info, path, file_obj=None):
  """Saves |size_info| to |path}."""
  if os.environ.get('SUPERSIZE_MEASURE_GZIP') == '1':
    with _OpenGzipForWrite(path, file_obj=file_obj) as f:
      _SaveSizeInfoToFile(size_info, f)
  else:
    # It is seconds faster to do gzip in a separate step. 6s -> 3.5s.
    stringio = cStringIO.StringIO()
    _SaveSizeInfoToFile(size_info, stringio)

    logging.debug('Serialization complete. Gzipping...')
    stringio.seek(0)
    with _OpenGzipForWrite(path, file_obj=file_obj) as f:
      shutil.copyfileobj(stringio, f)


def LoadSizeInfo(filename, file_obj=None):
  """Returns a SizeInfo loaded from |filename|."""
  with gzip.GzipFile(filename=filename, fileobj=file_obj) as f:
    return _LoadSizeInfoFromFile(f, filename)
