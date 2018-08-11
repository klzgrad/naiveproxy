# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities for demangling C++ symbols."""

import collections
import logging
import subprocess

import path_util

def _DemangleNames(names, tool_prefix):
  """Uses c++filt to demangle a list of names."""
  proc = subprocess.Popen([path_util.GetCppFiltPath(tool_prefix)],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
  stdout = proc.communicate('\n'.join(names))[0]
  assert proc.returncode == 0
  ret = stdout.splitlines()
  if logging.getLogger().isEnabledFor(logging.INFO):
    fail_count = sum(1 for s in ret if s.startswith('_Z'))
    if fail_count:
      logging.info('* Failed to demangle %d/%d items', fail_count, len(ret))
  return ret


def DemangleRemainingSymbols(raw_symbols, tool_prefix):
  """Demangles any symbols that need it."""
  to_process = [s for s in raw_symbols if s.full_name.startswith('_Z')]
  if not to_process:
    return

  logging.info('Demangling %d symbols', len(to_process))
  names = _DemangleNames((s.full_name for s in to_process), tool_prefix)
  for i, name in enumerate(names):
    to_process[i].full_name = name


def DemangleSetsInDicts(key_to_names, tool_prefix):
  """Demangles values as sets, and returns the result.

  |key_to_names| is a dict from key to sets (or lists) of mangled names.
  """
  all_names = []
  for names in key_to_names.itervalues():
    all_names.extend(n for n in names if n.startswith('_Z'))
  if not all_names:
    return key_to_names

  logging.info('Demangling %d values', len(all_names))
  it = iter(_DemangleNames(all_names, tool_prefix))
  ret = {}
  for key, names in key_to_names.iteritems():
    ret[key] = set(next(it) if n.startswith('_Z') else n for n in names)
  assert(next(it, None) is None)
  return ret


def DemangleKeysAndMergeLists(name_to_list, tool_prefix):
  """Demangles keys of a dict of lists, and returns the result.

  Keys may demangle to a common name. When this happens, the corresponding lists
  are merged in arbitrary order.
  """
  keys = [key for key in name_to_list if key.startswith('_Z')]
  if not keys:
    return name_to_list

  logging.info('Demangling %d keys', len(keys))
  key_iter = iter(_DemangleNames(keys, tool_prefix))
  ret = collections.defaultdict(list)
  for key, val in name_to_list.iteritems():
    ret[next(key_iter) if key.startswith('_Z') else key] += val
  assert(next(key_iter, None) is None)
  logging.info('* %d keys become %d keys' % (len(name_to_list), len(ret)))
  return ret
