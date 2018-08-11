# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Creates an html report that allows you to view binary size by component."""

import argparse
import json
import logging
import os
import shutil
import sys

import archive
import path_util


# Node dictionary keys. These are output in json read by the webapp so
# keep them short to save file size.
# Note: If these change, the webapp must also change.
_METHOD_COUNT_MODE_KEY = 'methodCountMode'
_NODE_TYPE_KEY = 'k'
_NODE_TYPE_BUCKET = 'b'
_NODE_TYPE_PATH = 'p'
_NODE_TYPE_SYMBOL = 's'
_NODE_NAME_KEY = 'n'
_NODE_CHILDREN_KEY = 'children'
_NODE_SYMBOL_TYPE_KEY = 't'
_NODE_SYMBOL_TYPE_VTABLE = 'v'
_NODE_SYMBOL_TYPE_GENERATED = '*'
_NODE_SYMBOL_SIZE_KEY = 'value'
_NODE_MAX_DEPTH_KEY = 'maxDepth'
_NODE_LAST_PATH_ELEMENT_KEY = 'lastPathElement'

# The display name of the bucket where we put symbols without path.
_NAME_NO_PATH_BUCKET = '(No Path)'

# Try to keep data buckets smaller than this to avoid killing the
# graphing lib.
_BIG_BUCKET_LIMIT = 3000


def _GetOrMakeChildNode(node, node_type, name):
  child = node[_NODE_CHILDREN_KEY].get(name)
  if child is None:
    child = {
        _NODE_TYPE_KEY: node_type,
        _NODE_NAME_KEY: name,
    }
    if node_type != _NODE_TYPE_SYMBOL:
      child[_NODE_CHILDREN_KEY] = {}
    node[_NODE_CHILDREN_KEY][name] = child
  else:
    assert child[_NODE_TYPE_KEY] == node_type
  return child


def _SplitLargeBucket(bucket):
  """Split the given node into sub-buckets when it's too big."""
  old_children = bucket[_NODE_CHILDREN_KEY]
  count = 0
  for symbol_type, symbol_bucket in old_children.iteritems():
    count += len(symbol_bucket[_NODE_CHILDREN_KEY])
  if count > _BIG_BUCKET_LIMIT:
    new_children = {}
    bucket[_NODE_CHILDREN_KEY] = new_children
    current_bucket = None
    index = 0
    for symbol_type, symbol_bucket in old_children.iteritems():
      for symbol_name, value in symbol_bucket[_NODE_CHILDREN_KEY].iteritems():
        if index % _BIG_BUCKET_LIMIT == 0:
          group_no = (index / _BIG_BUCKET_LIMIT) + 1
          node_name = '%s subgroup %d' % (_NAME_NO_PATH_BUCKET, group_no)
          current_bucket = _GetOrMakeChildNode(
              bucket, _NODE_TYPE_PATH, node_name)
        index += 1
        symbol_size = value[_NODE_SYMBOL_SIZE_KEY]
        _AddSymbolIntoFileNode(current_bucket, symbol_type, symbol_name,
                               symbol_size, True)


def _MakeChildrenDictsIntoLists(node):
  """Recursively converts all children from dicts -> lists."""
  children = node.get(_NODE_CHILDREN_KEY)
  if children:
    children = children.values()  # Convert dict -> list.
    node[_NODE_CHILDREN_KEY] = children
    for child in children:
      _MakeChildrenDictsIntoLists(child)
    if len(children) > _BIG_BUCKET_LIMIT:
      logging.warning('Bucket found with %d entries. Might be unusable.',
                      len(children))


def _CombineSingleChildNodes(node):
  """Collapse "java"->"com"->"google" into ."java/com/google"."""
  children = node.get(_NODE_CHILDREN_KEY)
  if children:
    child = children[0]
    if len(children) == 1 and node[_NODE_TYPE_KEY] == child[_NODE_TYPE_KEY]:
      node[_NODE_NAME_KEY] = '{}/{}'.format(
          node[_NODE_NAME_KEY], child[_NODE_NAME_KEY])
      node[_NODE_CHILDREN_KEY] = child[_NODE_CHILDREN_KEY]
      _CombineSingleChildNodes(node)
    else:
      for child in children:
        _CombineSingleChildNodes(child)


def _AddSymbolIntoFileNode(node, symbol_type, symbol_name, symbol_size,
                           min_symbol_size):
  """Puts symbol into the file path node |node|."""
  node[_NODE_LAST_PATH_ELEMENT_KEY] = True
  # Don't bother with buckets when not including symbols.
  if min_symbol_size == 0:
    node = _GetOrMakeChildNode(node, _NODE_TYPE_BUCKET, symbol_type)
    node[_NODE_SYMBOL_TYPE_KEY] = symbol_type

  # 'node' is now the symbol-type bucket. Make the child entry.
  if not symbol_name or symbol_size >= min_symbol_size:
    node_name = symbol_name or '[Anonymous]'
  elif symbol_name.startswith('*'):
    node_name = symbol_name
  else:
    node_name = symbol_type
  node = _GetOrMakeChildNode(node, _NODE_TYPE_SYMBOL, node_name)
  node[_NODE_SYMBOL_SIZE_KEY] = node.get(_NODE_SYMBOL_SIZE_KEY, 0) + symbol_size
  node[_NODE_SYMBOL_TYPE_KEY] = symbol_type


def _MakeCompactTree(symbols, min_symbol_size, method_count_mode):
  if method_count_mode:
    # Include all symbols and avoid bucket nodes.
    min_symbol_size = -1
  result = {
      _NODE_NAME_KEY: '/',
      _NODE_CHILDREN_KEY: {},
      _NODE_TYPE_KEY: 'p',
      _NODE_MAX_DEPTH_KEY: 0,
      _METHOD_COUNT_MODE_KEY: bool(method_count_mode),
  }
  for symbol in symbols:
    file_path = symbol.source_path or symbol.object_path or _NAME_NO_PATH_BUCKET
    node = result
    depth = 0
    for path_part in file_path.split(os.path.sep):
      if not path_part:
        continue
      depth += 1
      node = _GetOrMakeChildNode(node, _NODE_TYPE_PATH, path_part)

    symbol_type = symbol.section
    if symbol.name.endswith('[vtable]'):
      symbol_type = _NODE_SYMBOL_TYPE_VTABLE
    elif symbol.name.endswith(']'):
      symbol_type = _NODE_SYMBOL_TYPE_GENERATED
    symbol_size = 1 if method_count_mode else symbol.pss
    _AddSymbolIntoFileNode(node, symbol_type, symbol.template_name, symbol_size,
                           min_symbol_size)
    depth += 2
    result[_NODE_MAX_DEPTH_KEY] = max(result[_NODE_MAX_DEPTH_KEY], depth)

  # The (no path) bucket can be extremely large if we failed to get
  # path information. Split it into subgroups if needed.
  no_path_bucket = result[_NODE_CHILDREN_KEY].get(_NAME_NO_PATH_BUCKET)
  if no_path_bucket and min_symbol_size == 0:
    _SplitLargeBucket(no_path_bucket)

  _MakeChildrenDictsIntoLists(result)
  _CombineSingleChildNodes(result)

  return result


def _CopyTemplateFiles(dest_dir):
  d3_out = os.path.join(dest_dir, 'd3')
  if not os.path.exists(d3_out):
    os.makedirs(d3_out, 0755)
  d3_src = os.path.join(path_util.SRC_ROOT, 'third_party', 'd3', 'src')
  template_src = os.path.join(os.path.dirname(__file__), 'template')
  shutil.copy(os.path.join(d3_src, 'LICENSE'), d3_out)
  shutil.copy(os.path.join(d3_src, 'd3.js'), d3_out)
  shutil.copy(os.path.join(template_src, 'index.html'), dest_dir)
  shutil.copy(os.path.join(template_src, 'D3SymbolTreeMap.js'), dest_dir)


def AddArguments(parser):
  parser.add_argument('input_file',
                      help='Path to input .size file.')
  parser.add_argument('--report-dir', metavar='PATH', required=True,
                      help='Write output to the specified directory. An HTML '
                            'report is generated here.')
  parser.add_argument('--include-bss', action='store_true',
                      help='Include symbols from .bss (which consume no real '
                           'space)')
  parser.add_argument('--min-symbol-size', type=float, default=1024,
                      help='Minimum size (PSS) for a symbol to be included as '
                           'an independent node.')
  parser.add_argument('--method-count', action='store_true',
                      help='Show dex method count rather than size')


def Run(args, parser):
  if not args.input_file.endswith('.size'):
    parser.error('Input must end with ".size"')

  logging.info('Reading .size file')
  size_info = archive.LoadAndPostProcessSizeInfo(args.input_file)
  symbols = size_info.raw_symbols
  if args.method_count:
    symbols = symbols.WhereInSection('m')
  elif not args.include_bss:
    symbols = symbols.WhereInSection('b').Inverted()

  # Copy report boilerplate into output directory. This also proves that the
  # output directory is safe for writing, so there should be no problems writing
  # the nm.out file later.
  _CopyTemplateFiles(args.report_dir)

  logging.info('Creating JSON objects')
  tree_root = _MakeCompactTree(symbols, args.min_symbol_size, args.method_count)

  logging.info('Serializing JSON')
  with open(os.path.join(args.report_dir, 'data.js'), 'w') as out_file:
    out_file.write('var tree_data=')
    # Use separators without whitespace to get a smaller file.
    json.dump(tree_root, out_file, ensure_ascii=False, check_circular=False,
              separators=(',', ':'))

  logging.warning('Report saved to %s/index.html', args.report_dir)
