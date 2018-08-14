# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Creates an html report that allows you to view binary size by component."""

import codecs
import collections
import json
import logging
import os

import archive
import diff
import models


_SYMBOL_TYPE_VTABLE = 'v'
_SYMBOL_TYPE_GENERATED = '*'
_SYMBOL_TYPE_DEX_METHOD = 'm'
_SYMBOL_TYPE_OTHER = 'o'

_COMPACT_FILE_PATH_KEY = 'p'
_COMPACT_FILE_COMPONENT_INDEX_KEY = 'c'
_COMPACT_FILE_SYMBOLS_KEY = 's'
_COMPACT_SYMBOL_NAME_KEY = 'n'
_COMPACT_SYMBOL_BYTE_SIZE_KEY = 'b'
_COMPACT_SYMBOL_TYPE_KEY = 't'
_COMPACT_SYMBOL_COUNT_KEY = 'u'
_COMPACT_SYMBOL_FLAGS_KEY = 'f'

_SMALL_SYMBOL_DESCRIPTIONS = {
  'b': 'Other small uninitialized data',
  'd': 'Other small initialized data',
  'r': 'Other small readonly data',
  't': 'Other small code',
  'v': 'Other small vtable entries',
  'x': 'Other small dex non-method entries',
  'm': 'Other small dex methods',
  'p': 'Other small locale pak entries',
  'P': 'Other small non-locale pak entries',
  'o': 'Other small entries',
}

_DEFAULT_SYMBOL_COUNT = 250000


def _GetSymbolType(symbol):
  symbol_type = symbol.section
  if symbol.name.endswith('[vtable]'):
    symbol_type = _SYMBOL_TYPE_VTABLE
  if symbol_type not in _SMALL_SYMBOL_DESCRIPTIONS:
    symbol_type = _SYMBOL_TYPE_OTHER
  return symbol_type


def _GetOrAddFileNode(symbol, file_nodes, components):
  path = symbol.source_path or symbol.object_path
  file_node = file_nodes.get(path)
  if file_node is None:
    component_index = components.GetOrAdd(symbol.component)
    file_node = {
      _COMPACT_FILE_PATH_KEY: path,
      _COMPACT_FILE_COMPONENT_INDEX_KEY: component_index,
      _COMPACT_FILE_SYMBOLS_KEY: [],
    }
    file_nodes[path] = file_node
  return file_node


class IndexedSet(object):
  """Set-like object where values are unique and indexed.

  Values must be immutable.
  """

  def __init__(self):
    self._index_dict = {}  # Value -> Index dict
    self.value_list = []  # List containing all the set items

  def GetOrAdd(self, value):
    """Get the index of the value in the list. Append it if not yet present."""
    index = self._index_dict.get(value)
    if index is None:
      self.value_list.append(value)
      index = len(self.value_list) - 1
      self._index_dict[value] = index
    return index


def _MakeTreeViewList(symbols, include_all_symbols):
  """Builds JSON data of the symbols for the tree view HTML report.

  As the tree is built on the client-side, this function creates a flat list
  of files, where each file object contains symbols that have the same path.

  Args:
    symbols: A SymbolGroup containing all symbols.
    include_all_symbols: If true, include all symbols in the data file.
  """
  file_nodes = {}
  components = IndexedSet()

  # Build a container for symbols smaller than min_symbol_size
  small_symbols = collections.defaultdict(dict)

  # Dex methods (type "m") are whitelisted for the method_count mode on the
  # UI. It's important to see details on all the methods.
  dex_symbols = symbols.WhereIsDex()
  ordered_symbols = dex_symbols.Inverted().Sorted()
  if include_all_symbols:
    symbol_count = len(ordered_symbols)
  else:
    symbol_count = max(_DEFAULT_SYMBOL_COUNT - len(dex_symbols), 0)

  main_symbols = dex_symbols + ordered_symbols[:symbol_count]
  extra_symbols = ordered_symbols[symbol_count:]

  logging.info('Found %d large symbols, %s small symbols',
               len(main_symbols), len(extra_symbols))

  # Bundle symbols by the file they belong to,
  # and add all the file buckets into file_nodes
  for symbol in main_symbols:
    symbol_type = _GetSymbolType(symbol)
    symbol_size = round(symbol.pss, 2)
    if symbol_size.is_integer():
      symbol_size = int(symbol_size)
    symbol_count = 1
    if symbol.IsDelta() and symbol.diff_status == models.DIFF_STATUS_REMOVED:
      symbol_count = -1

    file_node = _GetOrAddFileNode(symbol, file_nodes, components)

    is_dex_method = symbol_type == _SYMBOL_TYPE_DEX_METHOD
    symbol_entry = {
      _COMPACT_SYMBOL_NAME_KEY: symbol.template_name,
      _COMPACT_SYMBOL_TYPE_KEY: symbol_type,
      _COMPACT_SYMBOL_BYTE_SIZE_KEY: symbol_size,
    }
    # We use symbol count for the method count mode in the diff mode report.
    # Negative values are used to indicate a symbol was removed, so it should
    # count as -1 rather than the default, 1.
    # We don't care about accurate counts for other symbol types currently,
    # so this data is only included for methods.
    if is_dex_method and symbol_count != 1:
      symbol_entry[_COMPACT_SYMBOL_COUNT_KEY] = symbol_count
    if symbol.flags:
      symbol_entry[_COMPACT_SYMBOL_FLAGS_KEY] = symbol.flags
    file_node[_COMPACT_FILE_SYMBOLS_KEY].append(symbol_entry)

  for symbol in extra_symbols:
    symbol_type = _GetSymbolType(symbol)

    file_node = _GetOrAddFileNode(symbol, file_nodes, components)
    path = file_node[_COMPACT_FILE_PATH_KEY]

    small_type_symbol = small_symbols[path].get(symbol_type)
    if small_type_symbol is None:
      small_type_symbol = {
        _COMPACT_SYMBOL_NAME_KEY: _SMALL_SYMBOL_DESCRIPTIONS[symbol_type],
        _COMPACT_SYMBOL_TYPE_KEY: symbol_type,
        _COMPACT_SYMBOL_BYTE_SIZE_KEY: 0,
      }
      small_symbols[path][symbol_type] = small_type_symbol
      file_node[_COMPACT_FILE_SYMBOLS_KEY].append(small_type_symbol)

    small_type_symbol[_COMPACT_SYMBOL_BYTE_SIZE_KEY] += symbol.pss

  meta = {
    'components': components.value_list,
    'total': symbols.pss,
  }
  return meta, file_nodes.values()


def BuildReport(out_file, size_file, before_size_file=(None, None),
                all_symbols=False):
  """Builds a .ndjson report for a .size file.

  Args:
    out_file: File object to save JSON report to.
    size_file: Size file to use as input. Tuple of path and file object.
    before_size_file: If used, creates a diff report where |size_file| is the
      newer .size file. Tuple of path and file object.
    all_symbols: If true, all symbols will be included in the report rather
      than truncated.
  """
  logging.info('Reading .size file')
  diff_mode = any(before_size_file)

  size_info = archive.LoadAndPostProcessSizeInfo(*size_file)
  if diff_mode:
    before_size_info = archive.LoadAndPostProcessSizeInfo(*before_size_file)
    after_size_info = size_info

    size_info = diff.Diff(before_size_info, after_size_info)
    symbols = size_info.raw_symbols
    symbols = symbols.WhereDiffStatusIs(models.DIFF_STATUS_UNCHANGED).Inverted()
  else:
    symbols = size_info.raw_symbols

  logging.info('Creating JSON objects')
  meta, tree_nodes = _MakeTreeViewList(symbols, all_symbols)
  meta.update({
    'diff_mode': diff_mode,
    'section_sizes': size_info.section_sizes,
  })
  if diff_mode:
    meta.update({
      'before_metadata': size_info.before.metadata,
      'after_metadata': size_info.after.metadata,
    })
  else:
    meta['metadata'] = size_info.metadata

  # Write newline-delimited JSON file
  logging.info('Serializing JSON')
  # Use separators without whitespace to get a smaller file.
  json_dump_args = {
    'separators': (',', ':'),
    'ensure_ascii': True,
    'check_circular': False,
  }

  json.dump(meta, out_file, **json_dump_args)
  out_file.write('\n')

  for tree_node in tree_nodes:
    json.dump(tree_node, out_file, **json_dump_args)
    out_file.write('\n')


def _MakeDirIfDoesNotExist(rel_path):
  """Ensures a directory exists."""
  abs_path = os.path.abspath(rel_path)
  try:
    os.makedirs(abs_path)
  except OSError:
    if not os.path.isdir(abs_path):
      raise


def AddArguments(parser):
  parser.add_argument('input_file',
                      help='Path to input .size file.')
  parser.add_argument('--report-file', metavar='PATH', required=True,
                      help='Write generated data to the specified '
                           '.ndjson file.')
  parser.add_argument('--all-symbols', action='store_true',
                      help='Include all symbols. Will cause the data file to '
                           'take longer to load.')
  parser.add_argument('--diff-with',
                      help='Diffs the input_file against an older .size file')


def Run(args, parser):
  if not args.input_file.endswith('.size'):
    parser.error('Input must end with ".size"')
  if args.diff_with and not args.diff_with.endswith('.size'):
    parser.error('Diff input must end with ".size"')
  if not args.report_file.endswith('.ndjson'):
    parser.error('Output must end with ".ndjson"')

  with codecs.open(args.report_file, 'w', encoding='ascii') as out_file:
    BuildReport(
      out_file,
      size_file=(args.input_file, None),
      before_size_file=(args.diff_with, None),
      all_symbols=args.all_symbols
    )

  logging.warning('Report saved to %s', args.report_file)
  logging.warning('Open server by running: \n'
                  'tools/binary_size/supersize start_server %s',
                  args.report_file)
