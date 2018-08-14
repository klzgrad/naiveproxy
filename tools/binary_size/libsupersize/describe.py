# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Methods for converting model objects to human-readable formats."""

import abc
import cStringIO
import collections
import csv
import datetime
import itertools
import math
import time

import models


def _PrettySize(size):
  # Arbitrarily chosen cut-off.
  if abs(size) < 2000:
    return '%d bytes' % size
  # Always show 3 digits.
  size /= 1024.0
  if abs(size) < 10:
    return '%.2fkb' % size
  elif abs(size) < 100:
    return '%.1fkb' % size
  elif abs(size) < 1024:
    return '%dkb' % size
  size /= 1024.0
  if abs(size) < 10:
    return '%.2fmb' % size
  # We shouldn't be seeing sizes > 100mb.
  return '%.1fmb' % size


def _FormatPss(pss, force_sign=False):
  # Shows a decimal for small numbers to make it clear that a shared symbol has
  # a non-zero pss.
  if abs(pss) > 10:
    return str(int(pss))
  near_int = abs(pss) % 1 < 0.05
  if near_int and abs(pss) < 1 and pss:
    return '~0'
  if force_sign:
    return ('%+.0f' if near_int else '%+.1f') % pss
  return ('%.0f' if near_int else '%.1f') % pss


def _Divide(a, b):
  return float(a) / b if b else 0


def _IncludeInTotals(section_name):
  return section_name != models.SECTION_BSS and '(' not in section_name


def _GetSectionSizeInfo(section_sizes):
  total_bytes = sum(v for k, v in section_sizes.iteritems()
                    if _IncludeInTotals(k))
  max_bytes = max(abs(v) for k, v in section_sizes.iteritems()
                  if _IncludeInTotals(k))

  def is_relevant_section(name, size):
    # Show all sections containing symbols, plus relocations.
    # As a catch-all, also include any section that comprises > 4% of the
    # largest section. Use largest section rather than total so that it still
    # works out when showing a diff containing +100, -100 (total=0).
    return (name in models.SECTION_TO_SECTION_NAME.values() or
            name in ('.rela.dyn', '.rel.dyn') or
            _IncludeInTotals(name) and abs(_Divide(size, max_bytes)) > .04)

  section_names = sorted(k for k, v  in section_sizes.iteritems()
                         if is_relevant_section(k, v))

  return (total_bytes, section_names)


class Histogram(object):
  BUCKET_NAMES_FOR_SMALL_VALUES = {-1: '(-1,0)', 0: '{0}', 1: '(0,1)'}

  def __init__(self):
    self.data = collections.defaultdict(int)

  # Input:  (-8,-4], (-4,-2], (-2,-1], (-1,0), {0}, (0,1), [1,2), [2,4), [4,8).
  # Output:   -4,      -3,      -2,      -1,    0,    1,     2,     3,     4.
  @staticmethod
  def _Bucket(v):
    absv = abs(v)
    if absv < 1:
      return 0 if v == 0 else (-1 if v < 0 else 1)
    mag = int(math.log(absv, 2.0)) + 2
    return mag if v > 0 else -mag

  @staticmethod
  def _BucketName(k):
    if abs(k) <= 1:
      return Histogram.BUCKET_NAMES_FOR_SMALL_VALUES[k]
    if k < 0:
      return '(-{},-{}]'.format(1 << (-k - 1), 1 << (-k - 2))
    return '[{},{})'.format(1 << (k - 2), 1 << (k - 1))

  def Add(self, v):
    self.data[self._Bucket(v)] += 1

  def Generate(self):
    keys = sorted(self.data.keys())
    bucket_names = [self._BucketName(k) for k in keys]
    bucket_values = [str(self.data[k]) for k in keys]
    num_items = len(keys)
    num_cols = 6
    num_rows = (num_items + num_cols - 1) / num_cols  # Divide and round up.
    # Needed for xrange to not throw due to step by 0.
    if num_rows == 0:
      return
    # Spaces needed by items in each column, to align on ':'.
    name_col_widths = []
    value_col_widths = []
    for i in xrange(0, num_items, num_rows):
      name_col_widths.append(max(len(s) for s in bucket_names[i:][:num_rows]))
      value_col_widths.append(max(len(s) for s in bucket_values[i:][:num_rows]))

    yield 'Histogram of symbols based on PSS:'
    for r in xrange(num_rows):
      row = zip(bucket_names[r::num_rows], name_col_widths,
                bucket_values[r::num_rows], value_col_widths)
      line = '    ' + '   '.join('{:>{}}: {:<{}}'.format(*t) for t in row)
      yield line.rstrip()


class Describer(object):
  def __init__(self):
    pass

  @abc.abstractmethod
  def _DescribeDeltaSizeInfo(self, diff):
    pass

  @abc.abstractmethod
  def _DescribeSizeInfo(self, size_info):
    pass

  @abc.abstractmethod
  def _DescribeDeltaSymbolGroup(self, delta_group):
    pass

  @abc.abstractmethod
  def _DescribeSymbolGroup(self, group):
    pass

  @abc.abstractmethod
  def _DescribeSymbol(self, sym, single_line=False):
    pass

  def _DescribeIterable(self, obj):
    for i, x in enumerate(obj):
      yield '{}: {!r}'.format(i, x)

  def GenerateLines(self, obj):
    if isinstance(obj, models.DeltaSizeInfo):
      return self._DescribeDeltaSizeInfo(obj)
    if isinstance(obj, models.SizeInfo):
      return self._DescribeSizeInfo(obj)
    if isinstance(obj, models.DeltaSymbolGroup):
      return self._DescribeDeltaSymbolGroup(obj)
    if isinstance(obj, models.SymbolGroup):
      return self._DescribeSymbolGroup(obj)
    if isinstance(obj, (models.Symbol, models.DeltaSymbol)):
      return self._DescribeSymbol(obj)
    if hasattr(obj, '__iter__'):
      return self._DescribeIterable(obj)
    return iter((repr(obj),))


class DescriberText(Describer):
  def __init__(self, verbose=False, recursive=False, summarize=True):
    super(DescriberText, self).__init__()
    self.verbose = verbose
    self.recursive = recursive
    self.summarize = summarize

  def _DescribeSectionSizes(self, section_sizes):
    total_bytes, section_names = _GetSectionSizeInfo(section_sizes)
    yield ''
    yield 'Section Sizes (Total={} ({} bytes)):'.format(
        _PrettySize(total_bytes), total_bytes)
    for name in section_names:
      size = section_sizes[name]
      if not _IncludeInTotals(name):
        yield '    {}: {} ({} bytes) (not included in totals)'.format(
            name, _PrettySize(size), size)
      else:
        percent = _Divide(size, total_bytes)
        yield '    {}: {} ({} bytes) ({:.1%})'.format(
            name, _PrettySize(size), size, percent)

    if self.verbose:
      yield ''
      yield 'Other section sizes:'
      section_names = sorted(k for k in section_sizes.iterkeys()
                             if k not in section_names)
      for name in section_names:
        not_included_part = ''
        if not _IncludeInTotals(name):
          not_included_part = ' (not included in totals)'
        yield '    {}: {} ({} bytes){}'.format(
            name, _PrettySize(section_sizes[name]), section_sizes[name],
            not_included_part)

  def _DescribeSymbol(self, sym, single_line=False):
    address = 'Group' if sym.IsGroup() else hex(sym.address)

    last_field = ''
    if sym.IsGroup():
      last_field = 'count=%d' % len(sym)
    else:
      syms = [sym.before_symbol, sym.after_symbol] if sym.IsDelta() else [sym]
      num_aliases = [s.num_aliases for s in syms if not s is None]
      if num_aliases[0] != num_aliases[-1]:  # If 2 distinct values.
        last_field = 'num_aliases=%d->%d' % tuple(num_aliases)
      elif num_aliases[0] > 1 or self.verbose:
        last_field = 'num_aliases=%d' % num_aliases[0]

    pss_field = _FormatPss(sym.pss, sym.IsDelta())
    if sym.IsDelta():
      b = sum(s.before_symbol.pss_without_padding if s.before_symbol else 0
              for s in sym.IterLeafSymbols())
      a = sum(s.after_symbol.pss_without_padding if s.after_symbol else 0
              for s in sym.IterLeafSymbols())
      pss_field = '{} ({}->{})'.format(pss_field, _FormatPss(b), _FormatPss(a))
    elif sym.num_aliases > 1:
      pss_field = '{} (size={})'.format(pss_field, sym.size)

    if self.verbose:
      if last_field:
        last_field = '  ' + last_field
      if sym.IsDelta():
        yield '{}@{:<9s}  {}{}'.format(
            sym.section, address, pss_field, last_field)
      else:
        l = '{}@{:<9s}  pss={}  padding={}{}'.format(
            sym.section, address, pss_field, sym.padding, last_field)
        yield l
      yield '    source_path={} \tobject_path={}'.format(
          sym.source_path, sym.object_path)
      if sym.name:
        yield '    flags={}  name={}'.format(sym.FlagsString(), sym.name)
        if sym.full_name is not sym.name:
          yield '         full_name={}'.format(sym.full_name)
      elif sym.full_name:
        yield '    flags={}  full_name={}'.format(
            sym.FlagsString(), sym.full_name)
    else:
      if last_field:
        last_field = ' ({})'.format(last_field)
      if sym.IsDelta():
        pss_field = '{:<18}'.format(pss_field)
      else:
        pss_field = '{:<14}'.format(pss_field)
      if single_line:
        yield '{}@{:<9s}  {}  {}{}'.format(
            sym.section, address, pss_field, sym.name, last_field)
      else:
        path = sym.source_path or sym.object_path or '{no path}'
        if sym.generated_source:
          path = '$root_gen_dir/' + path
        yield '{}@{:<9s}  {} {}'.format(
            sym.section, address, pss_field, path)
        if sym.name:
          yield '    {}{}'.format(sym.name, last_field)

  def _DescribeSymbolGroupChildren(self, group, indent=0):
    running_total = 0
    running_percent = 0
    is_delta = group.IsDelta()
    all_groups = all(s.IsGroup() for s in group)

    indent_prefix = '> ' * indent
    diff_prefix = ''
    total = group.pss
    for index, s in enumerate(group):
      if group.IsBss() or not s.IsBss():
        running_total += s.pss
        running_percent = _Divide(running_total, total)
      for l in self._DescribeSymbol(s, single_line=all_groups):
        if l[:4].isspace():
          indent_size = 8 + len(indent_prefix) + len(diff_prefix)
          yield '{} {}'.format(' ' * indent_size, l)
        else:
          if is_delta:
            diff_prefix = models.DIFF_PREFIX_BY_STATUS[s.diff_status]
          yield '{}{}{:<4} {:>8} {:7} {}'.format(
              indent_prefix, diff_prefix, str(index) + ')',
              _FormatPss(running_total), '({:.1%})'.format(running_percent), l)

      if self.recursive and s.IsGroup():
        for l in self._DescribeSymbolGroupChildren(s, indent=indent + 1):
          yield l

  def _DescribeSymbolGroup(self, group):
    if self.summarize:
      total_size = group.pss
      section_sizes = collections.defaultdict(float)
      for s in group.IterLeafSymbols():
        section_sizes[s.section_name] += s.pss

    # Apply this filter after calcualating size since an alias being removed
    # causes some symbols to be UNCHANGED, yet have pss != 0.
    if group.IsDelta():
      group = group.WhereDiffStatusIs(models.DIFF_STATUS_UNCHANGED).Inverted()

    if self.summarize:
      histogram = Histogram()
      for s in group:
        histogram.Add(s.pss)
      unique_paths = set()
      for s in group.IterLeafSymbols():
        # Ignore paths like foo/{shared}/2
        if '{' not in s.object_path:
          unique_paths.add(s.object_path)

      if group.IsDelta():
        before_unique, after_unique = group.CountUniqueSymbols()
        unique_part = '{:,} -> {:,} unique'.format(before_unique, after_unique)
      else:
        unique_part = '{:,} unique'.format(group.CountUniqueSymbols())

      relevant_sections = [
          s for s in models.SECTION_TO_SECTION_NAME.itervalues()
          if s in section_sizes]
      if models.SECTION_MULTIPLE in relevant_sections:
        relevant_sections.remove(models.SECTION_MULTIPLE)

      size_summary = ' '.join(
          '{}={:<10}'.format(k, _PrettySize(int(section_sizes[k])))
          for k in relevant_sections)
      size_summary += ' total={:<10}'.format(_PrettySize(int(total_size)))

      section_legend = ', '.join(
          '{}={}'.format(models.SECTION_NAME_TO_SECTION[k], k)
          for k in relevant_sections if k in models.SECTION_NAME_TO_SECTION)

      summary_desc = itertools.chain(
          ['Showing {:,} symbols ({}) with total pss: {} bytes'.format(
              len(group), unique_part, int(total_size))],
          histogram.Generate(),
          [size_summary.rstrip()],
          ['Number of unique paths: {}'.format(len(unique_paths))],
          [''],
          ['Section Legend: {}'.format(section_legend)],
      )
    else:
      summary_desc = ()

    if self.verbose:
      titles = 'Index | Running Total | Section@Address | ...'
    elif group.IsDelta():
      titles = (u'Index | Running Total | Section@Address | \u0394 PSS '
                u'(\u0394 size_without_padding) | Path').encode('utf-8')
    else:
      titles = ('Index | Running Total | Section@Address | PSS | Path')

    header_desc = (titles, '-' * 60)

    children_desc = self._DescribeSymbolGroupChildren(group)
    return itertools.chain(summary_desc, header_desc, children_desc)

  def _DescribeDiffObjectPaths(self, delta_group):
    paths_by_status = [set(), set(), set(), set()]
    for s in delta_group.IterLeafSymbols():
      path = s.source_path or s.object_path
      # Ignore paths like foo/{shared}/2
      if '{' not in path:
        paths_by_status[s.diff_status].add(path)
    # Initial paths sets are those where *any* symbol is
    # unchanged/changed/added/removed.
    unchanged, changed, added, removed = paths_by_status
    # Consider a path with both adds & removes as "changed".
    changed.update(added.intersection(removed))
    # Consider a path added / removed only when all symbols are new/removed.
    added.difference_update(unchanged)
    added.difference_update(changed)
    added.difference_update(removed)
    removed.difference_update(unchanged)
    removed.difference_update(changed)
    removed.difference_update(added)
    yield '{} paths added, {} removed, {} changed'.format(
        len(added), len(removed), len(changed))

    if self.verbose and len(added):
      yield 'Added files:'
      for p in sorted(added):
        yield '  ' + p
    if self.verbose and len(removed):
      yield 'Removed files:'
      for p in sorted(removed):
        yield '  ' + p
    if self.verbose and len(changed):
      yield 'Changed files:'
      for p in sorted(changed):
        yield '  ' + p

  def _DescribeDeltaSymbolGroup(self, delta_group):
    if self.summarize:
      header_template = ('{} symbols added (+), {} changed (~), '
                         '{} removed (-), {} unchanged (not shown)')
      # Apply this filter since an alias being removed causes some symbols to be
      # UNCHANGED, yet have pss != 0.
      changed_delta_group = delta_group.WhereDiffStatusIs(
          models.DIFF_STATUS_UNCHANGED).Inverted()
      num_inc = sum(1 for s in changed_delta_group if s.pss > 0)
      num_dec = sum(1 for s in changed_delta_group if s.pss < 0)
      counts = delta_group.CountsByDiffStatus()
      num_unique_before_symbols, num_unique_after_symbols = (
          delta_group.CountUniqueSymbols())
      diff_summary_desc = [
          header_template.format(
              counts[models.DIFF_STATUS_ADDED],
              counts[models.DIFF_STATUS_CHANGED],
              counts[models.DIFF_STATUS_REMOVED],
              counts[models.DIFF_STATUS_UNCHANGED]),
          'Of changed symbols, {} grew, {} shrank'.format(num_inc, num_dec),
          'Number of unique symbols {} -> {} ({:+})'.format(
              num_unique_before_symbols, num_unique_after_symbols,
              num_unique_after_symbols - num_unique_before_symbols),
          ]
      path_delta_desc = itertools.chain(
          self._DescribeDiffObjectPaths(delta_group), ('',))
    else:
      diff_summary_desc = ()
      path_delta_desc = ()

    group_desc = self._DescribeSymbolGroup(delta_group)
    return itertools.chain(diff_summary_desc, path_delta_desc, group_desc)

  def _DescribeDeltaSizeInfo(self, diff):
    common_metadata = {k: v for k, v in diff.before.metadata.iteritems()
                       if diff.after.metadata[k] == v}
    before_metadata = {k: v for k, v in diff.before.metadata.iteritems()
                       if k not in common_metadata}
    after_metadata = {k: v for k, v in diff.after.metadata.iteritems()
                      if k not in common_metadata}
    metadata_desc = itertools.chain(
        ('Common Metadata:',),
        ('    %s' % line for line in DescribeMetadata(common_metadata)),
        ('Old Metadata:',),
        ('    %s' % line for line in DescribeMetadata(before_metadata)),
        ('New Metadata:',),
        ('    %s' % line for line in DescribeMetadata(after_metadata)))
    section_desc = self._DescribeSectionSizes(diff.section_sizes)
    group_desc = self.GenerateLines(diff.symbols)
    return itertools.chain(metadata_desc, section_desc, ('',), group_desc)

  def _DescribeSizeInfo(self, size_info):
    metadata_desc = itertools.chain(
        ('Metadata:',),
        ('    %s' % line for line in DescribeMetadata(size_info.metadata)))
    section_desc = self._DescribeSectionSizes(size_info.section_sizes)
    coverage_desc = ()
    if self.verbose:
      coverage_desc = itertools.chain(
          ('',), DescribeSizeInfoCoverage(size_info))
    group_desc = self.GenerateLines(size_info.symbols)
    return itertools.chain(metadata_desc, section_desc, coverage_desc, ('',),
                           group_desc)

def DescribeSizeInfoCoverage(size_info):
  """Yields lines describing how accurate |size_info| is."""
  for section, section_name in models.SECTION_TO_SECTION_NAME.iteritems():
    if section_name not in size_info.section_sizes:
      continue
    expected_size = size_info.section_sizes[section_name]

    in_section = size_info.raw_symbols.WhereInSection(section_name)
    actual_size = in_section.size
    size_percent = _Divide(actual_size, expected_size)
    yield ('Section {}: has {:.1%} of {} bytes accounted for from '
           '{} symbols. {} bytes are unaccounted for.').format(
               section_name, size_percent, actual_size, len(in_section),
               expected_size - actual_size)
    star_syms = in_section.WhereNameMatches(r'^\*')
    padding = in_section.padding - star_syms.padding
    anonymous_syms = star_syms.Inverted().WhereHasAnyAttribution().Inverted()
    yield '* Padding accounts for {} bytes ({:.1%})'.format(
        padding, _Divide(padding, in_section.size))
    if len(star_syms):
      yield ('* {} placeholders (symbols that start with **) account for '
             '{} bytes ({:.1%})').format(
                 len(star_syms), star_syms.size,
                 _Divide(star_syms.size,  in_section.size))
    if anonymous_syms:
      yield '* {} anonymous symbols account for {} bytes ({:.1%})'.format(
          len(anonymous_syms), int(anonymous_syms.pss),
          _Divide(star_syms.size, in_section.size))

    if section == 'r':
      string_literals = in_section.Filter(lambda s: s.IsStringLiteral())
      yield '* Contains {} string literals. Total size={}, padding={}'.format(
          len(string_literals), string_literals.size_without_padding,
          string_literals.padding)

    aliased_symbols = in_section.Filter(lambda s: s.aliases)
    if len(aliased_symbols):
      uniques = sum(1 for s in aliased_symbols.IterUniqueSymbols())
      saved = sum(s.size_without_padding * (s.num_aliases - 1)
                  for s in aliased_symbols.IterUniqueSymbols())
      yield ('* Contains {} aliases, mapped to {} unique addresses '
             '({} bytes saved)').format(
                 len(aliased_symbols), uniques, saved)
    else:
      yield '* Contains 0 aliases'

    inlined_symbols = in_section.WhereObjectPathMatches('{shared}')
    if len(inlined_symbols):
      yield '* {} symbols have shared ownership ({} bytes)'.format(
          len(inlined_symbols), inlined_symbols.size)
    else:
      yield '* 0 symbols have shared ownership'


class DescriberCsv(Describer):
  def __init__(self, verbose=False):
    super(DescriberCsv, self).__init__()
    self.verbose = verbose
    self.stringio = cStringIO.StringIO()
    self.csv_writer = csv.writer(self.stringio)

  def _RenderCsv(self, data):
    self.stringio.truncate(0)
    self.csv_writer.writerow(data)
    return self.stringio.getvalue().rstrip()

  def _DescribeSectionSizes(self, section_sizes):
    relevant_section_names = _GetSectionSizeInfo(section_sizes)[1]

    if self.verbose:
      relevant_set = set(relevant_section_names)
      section_names = sorted(section_sizes.iterkeys())
      yield self._RenderCsv(['Name', 'Size', 'IsRelevant'])
      for name in section_names:
        size = section_sizes[name]
        yield self._RenderCsv([name, size, int(name in relevant_set)])
    else:
      yield self._RenderCsv(['Name', 'Size'])
      for name in relevant_section_names:
        size = section_sizes[name]
        yield self._RenderCsv([name, size])

  def _DescribeDeltaSizeInfo(self, diff):
    section_desc = self._DescribeSectionSizes(diff.section_sizes)
    group_desc = self.GenerateLines(diff.symbols)
    return itertools.chain(section_desc, ('',), group_desc)

  def _DescribeSizeInfo(self, size_info):
    section_desc = self._DescribeSectionSizes(size_info.section_sizes)
    group_desc = self.GenerateLines(size_info.symbols)
    return itertools.chain(section_desc, ('',), group_desc)

  def _DescribeDeltaSymbolGroup(self, delta_group):
    yield self._RenderSymbolHeader(True);
    # Apply filter to remove UNCHANGED groups.
    delta_group = delta_group.WhereDiffStatusIs(
        models.DIFF_STATUS_UNCHANGED).Inverted()
    for sym in delta_group:
      yield self._RenderSymbolData(sym)

  def _DescribeSymbolGroup(self, group):
    yield self._RenderSymbolHeader(False);
    for sym in group:
      yield self._RenderSymbolData(sym)

  def _DescribeSymbol(self, sym, single_line=False):
    yield self._RenderSymbolHeader(sym.IsDelta());
    yield self._RenderSymbolData(sym)

  def _RenderSymbolHeader(self, isDelta):
    fields = []
    fields.append('GroupCount')
    fields.append('Address')
    fields.append('SizeWithoutPadding')
    fields.append('Padding')
    if isDelta:
      fields += ['BeforeNumAliases', 'AfterNumAliases']
    else:
      fields.append('NumAliases')
    fields.append('PSS')
    fields.append('Section')
    if self.verbose:
      fields.append('Flags')
      fields.append('SourcePath')
      fields.append('ObjectPath')
    fields.append('Name')
    if self.verbose:
      fields.append('FullName')
    return self._RenderCsv(fields)

  def _RenderSymbolData(self, sym):
    data = []
    data.append(len(sym) if sym.IsGroup() else None)
    data.append(None if sym.IsGroup() else hex(sym.address))
    data.append(sym.size_without_padding)
    data.append(sym.padding)
    if sym.IsDelta():
      b, a = (None, None) if sym.IsGroup() else (sym.before_symbol,
                                                 sym.after_symbol)
      data.append(b.num_aliases if b else None)
      data.append(a.num_aliases if a else None)
    else:
      data.append(sym.num_aliases)
    data.append(round(sym.pss, 3))
    data.append(sym.section)
    if self.verbose:
      data.append(sym.FlagsString())
      data.append(sym.source_path);
      data.append(sym.object_path);
    data.append(sym.name)
    if self.verbose:
      data.append(sym.full_name)
    return self._RenderCsv(data)


def _UtcToLocal(utc):
  epoch = time.mktime(utc.timetuple())
  offset = (datetime.datetime.fromtimestamp(epoch) -
            datetime.datetime.utcfromtimestamp(epoch))
  return utc + offset


def DescribeMetadata(metadata):
  display_dict = metadata.copy()
  timestamp = display_dict.get(models.METADATA_ELF_MTIME)
  if timestamp:
    timestamp_obj = datetime.datetime.utcfromtimestamp(timestamp)
    display_dict[models.METADATA_ELF_MTIME] = (
        _UtcToLocal(timestamp_obj).strftime('%Y-%m-%d %H:%M:%S'))
  gn_args = display_dict.get(models.METADATA_GN_ARGS)
  if gn_args:
    display_dict[models.METADATA_GN_ARGS] = ' '.join(gn_args)
  return sorted('%s=%s' % t for t in display_dict.iteritems())


def GenerateLines(obj, verbose=False, recursive=False, summarize=True,
                  format_name='text'):
  """Returns an iterable of lines (without \n) that describes |obj|."""
  if format_name == 'text':
    d = DescriberText(verbose=verbose, recursive=recursive, summarize=summarize)
  elif format_name == 'csv':
    d = DescriberCsv(verbose=verbose)
  else:
    raise ValueError('Unknown format_name \'{}\''.format(format_name));
  return d.GenerateLines(obj)


def WriteLines(lines, func):
  for l in lines:
    func(l)
    func('\n')
