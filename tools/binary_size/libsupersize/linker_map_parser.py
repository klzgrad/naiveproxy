# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import re

import models

# About linker maps:
# * "Discarded input sections" include symbols merged with other symbols
#   (aliases), so the information there is not actually a list of unused things.
# * Linker maps include symbols that do not have names (with object path),
#   whereas "nm" skips over these (they don't account for much though).
# * The parse time for compressed linker maps is dominated by ungzipping.


class MapFileParserGold(object):
  """Parses a linker map file from gold linker."""
  # Map file writer for gold linker:
  # https://github.com/gittup/binutils/blob/HEAD/gold/mapfile.cc

  def __init__(self):
    self._common_symbols = []
    self._symbols = []
    self._section_sizes = {}
    self._lines = None

  def Parse(self, lines):
    """Parses a linker map file.

    Args:
      lines: Iterable of lines, the first of which has been consumed to
      identify file type.

    Returns:
      A tuple of (section_sizes, symbols).
    """
    self._lines = iter(lines)
    logging.debug('Scanning for Header')

    while True:
      line = self._SkipToLineWithPrefix('Common symbol', 'Memory map')
      if line.startswith('Common symbol'):
        self._common_symbols = self._ParseCommonSymbols()
        logging.debug('.bss common entries: %d', len(self._common_symbols))
        continue
      elif line.startswith('Memory map'):
        self._ParseSections()
      break
    return self._section_sizes, self._symbols

  def _SkipToLineWithPrefix(self, prefix, prefix2=None):
    for l in self._lines:
      if l.startswith(prefix) or (prefix2 and l.startswith(prefix2)):
        return l

  def _ParsePossiblyWrappedParts(self, line, count):
    parts = line.split(None, count - 1)
    if not parts:
      return None
    if len(parts) != count:
      line = next(self._lines)
      parts.extend(line.split(None, count - len(parts) - 1))
      assert len(parts) == count, 'parts: ' + ' '.join(parts)
    parts[-1] = parts[-1].rstrip()
    return parts

  def _ParseCommonSymbols(self):
# Common symbol       size              file
#
# ff_cos_131072       0x40000           obj/third_party/<snip>
# ff_cos_131072_fixed
#                     0x20000           obj/third_party/<snip>
    ret = []
    next(self._lines)  # Skip past blank line

    name, size_str, path = None, None, None
    for l in self._lines:
      parts = self._ParsePossiblyWrappedParts(l, 3)
      if not parts:
        break
      name, size_str, path = parts
      sym = models.Symbol(models.SECTION_BSS,  int(size_str[2:], 16),
                          full_name=name, object_path=path)
      ret.append(sym)
    return ret

  def _ParseSections(self):
# .text           0x0028c600  0x22d3468
#  .text.startup._GLOBAL__sub_I_bbr_sender.cc
#                 0x0028c600       0x38 obj/net/net/bbr_sender.o
#  .text._reset   0x00339d00       0xf0 obj/third_party/icu/icuuc/ucnv.o
#  ** fill        0x0255fb00   0x02
#  .text._ZN4base8AutoLockD2Ev
#                 0x00290710        0xe obj/net/net/file_name.o
#                 0x00290711                base::AutoLock::~AutoLock()
#                 0x00290711                base::AutoLock::~AutoLock()
# .text._ZNK5blink15LayoutBlockFlow31mustSeparateMarginAfterForChildERK...
#                0xffffffffffffffff       0x46 obj/...
#                0x006808e1                blink::LayoutBlockFlow::...
# .bss
#  .bss._ZGVZN11GrProcessor11initClassIDI10LightingFPEEvvE8kClassID
#                0x02d4b294        0x4 obj/skia/skia/SkLightingShader.o
#                0x02d4b294   guard variable for void GrProcessor::initClassID
# .data           0x0028c600  0x22d3468
#  .data.rel.ro._ZTVN3gvr7android19ScopedJavaGlobalRefIP12_jfloatArrayEE
#                0x02d1e668       0x10 ../../third_party/.../libfoo.a(bar.o)
#                0x02d1e668   vtable for gvr::android::GlobalRef<_jfloatArray*>
#  ** merge strings
#                 0x0255fb00   0x1f2424
#  ** merge constants
#                 0x0255fb00   0x8
# ** common      0x02db5700   0x13ab48
    syms = self._symbols
    while True:
      line = self._SkipToLineWithPrefix('.')
      if not line:
        break
      section_name = None
      try:
        # Parse section name and size.
        parts = self._ParsePossiblyWrappedParts(line, 3)
        if not parts:
          break
        section_name, section_address_str, section_size_str = parts
        section_address = int(section_address_str[2:], 16)
        section_size = int(section_size_str[2:], 16)
        self._section_sizes[section_name] = section_size
        if (section_name in (models.SECTION_BSS,
                             models.SECTION_RODATA,
                             models.SECTION_TEXT) or
            section_name.startswith(models.SECTION_DATA)):
          logging.info('Parsing %s', section_name)
          if section_name == models.SECTION_BSS:
            # Common symbols have no address.
            syms.extend(self._common_symbols)
          prefix_len = len(section_name) + 1  # + 1 for the trailing .
          symbol_gap_count = 0
          merge_symbol_start_address = section_address
          sym_count_at_start = len(syms)
          line = next(self._lines)
          # Parse section symbols.
          while True:
            if not line or line.isspace():
              break
            if line.startswith(' **'):
              zero_index = line.find('0')
              if zero_index == -1:
                # Line wraps.
                name = line.strip()
                line = next(self._lines)
              else:
                # Line does not wrap.
                name = line[:zero_index].strip()
                line = line[zero_index:]
              address_str, size_str = self._ParsePossiblyWrappedParts(line, 2)
              line = next(self._lines)
              # These bytes are already accounted for.
              if name == '** common':
                continue
              address = int(address_str[2:], 16)
              size = int(size_str[2:], 16)
              path = None
              sym = models.Symbol(section_name, size, address=address,
                                  full_name=name, object_path=path)
              syms.append(sym)
              if merge_symbol_start_address > 0:
                merge_symbol_start_address += size
            else:
              # A normal symbol entry.
              subsection_name, address_str, size_str, path = (
                  self._ParsePossiblyWrappedParts(line, 4))
              size = int(size_str[2:], 16)
              assert subsection_name.startswith(section_name), (
                  'subsection name was: ' + subsection_name)
              mangled_name = subsection_name[prefix_len:]
              name = None
              address_str2 = None
              while True:
                line = next(self._lines).rstrip()
                if not line or line.startswith(' .'):
                  break
                # clang includes ** fill, but gcc does not.
                if line.startswith(' ** fill'):
                  # Alignment explicitly recorded in map file. Rather than
                  # record padding based on these entries, we calculate it
                  # using addresses. We do this because fill lines are not
                  # present when compiling with gcc (only for clang).
                  continue
                elif line.startswith(' **'):
                  break
                elif name is None:
                  address_str2, name = self._ParsePossiblyWrappedParts(line, 2)

              if address_str == '0xffffffffffffffff':
                # The section needs special handling (e.g., a merge section)
                # It also generally has a large offset after it, so don't
                # penalize the subsequent symbol for this gap (e.g. a 50kb gap).
                # There seems to be no corelation between where these gaps occur
                # and the symbols they come in-between.
                # TODO(agrieve): Learn more about why this happens.
                if address_str2:
                  address = int(address_str2[2:], 16) - 1
                elif syms and syms[-1].address > 0:
                  # Merge sym with no second line showing real address.
                  address = syms[-1].end_address
                else:
                  logging.warning('First symbol of section had address -1')
                  address = 0

                merge_symbol_start_address = address + size
              else:
                address = int(address_str[2:], 16)
                # Finish off active address gap / merge section.
                if merge_symbol_start_address:
                  merge_size = address - merge_symbol_start_address
                  merge_symbol_start_address = 0
                  if merge_size > 0:
                    # merge_size == 0 for the initial symbol generally.
                    logging.debug('Merge symbol of size %d found at:\n  %r',
                                  merge_size, syms[-1])
                    # Set size=0 so that it will show up as padding.
                    sym = models.Symbol(
                        section_name, 0,
                        address=address,
                        full_name='** symbol gap %d' % symbol_gap_count)
                    symbol_gap_count += 1
                    syms.append(sym)

              #  .text.res_findResource_60
              #                 0x00178de8       0x12a obj/...
              #                 0x00178de9                res_findResource_60
              #  .text._ZN3url6ParsedC2Ev
              #                 0x0021ad62       0x2e obj/url/url/url_parse.o
              #                 0x0021ad63                url::Parsed::Parsed()
              #  .text.unlikely._ZN4base3CPUC2Ev
              #                 0x003f9d3c       0x48 obj/base/base/cpu.o
              #                 0x003f9d3d                base::CPU::CPU()
              full_name = name
              if mangled_name and (not name or mangled_name.startswith('_Z') or
                                   '._Z' in mangled_name):
                full_name = mangled_name

              sym = models.Symbol(section_name, size, address=address,
                                  full_name=full_name, object_path=path)
              syms.append(sym)
          section_end_address = section_address + section_size
          if section_name != models.SECTION_BSS and (
              syms[-1].end_address < section_end_address):
            # Set size=0 so that it will show up as padding.
            sym = models.Symbol(
                section_name, 0,
                address=section_end_address,
                full_name=(
                    '** symbol gap %d (end of section)' % symbol_gap_count))
            syms.append(sym)
          logging.debug('Symbol count for %s: %d', section_name,
                        len(syms) - sym_count_at_start)
      except:
        logging.error('Problem line: %r', line)
        logging.error('In section: %r', section_name)
        raise


class _SymbolMaker(object):
  def __init__(self):
    self.syms = []
    self.cur_sym = None

  def Flush(self):
    if self.cur_sym:
      self.syms.append(self.cur_sym)
      self.cur_sym = None

  def Create(self, *args, **kwargs):
    self.Flush()
    self.cur_sym = models.Symbol(*args, **kwargs)


class MapFileParserLld(object):
  """Parses a linker map file from LLD."""
  # TODO(huangs): Add LTO support.
  # Map file writer for LLD linker (for ELF):
  # https://github.com/llvm-mirror/lld/blob/HEAD/ELF/MapFile.cpp
  _LINE_RE_V0 = re.compile(r'([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+) ( *)(.*)')
  _LINE_RE_V1 = re.compile(
      r'\s*[0-9a-f]+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+) ( *)(.*)')
  _LINE_RE = [_LINE_RE_V0, _LINE_RE_V1]

  def __init__(self, linker_name):
    self._linker_name = linker_name
    self._common_symbols = []
    self._section_sizes = {}

  def Parse(self, lines):
    """Parses a linker map file.

    Args:
      lines: Iterable of lines, the first of which has been consumed to
      identify file type.

    Returns:
      A tuple of (section_sizes, symbols).
    """
# Newest format:
#     VMA      LMA     Size Align Out     In      Symbol
#     194      194       13     1 .interp
#     194      194       13     1         <internal>:(.interp)
#     1a8      1a8     22d8     4 .ARM.exidx
#     1b0      1b0        8     4         obj/sandbox/syscall.o:(.ARM.exidx)
# Older format:
# Address          Size             Align Out     In      Symbol
# 00000000002002a8 000000000000001c     1 .interp
# 00000000002002a8 000000000000001c     1         <internal>:(.interp)
# ...
# 0000000000201000 0000000000000202    16 .text
# 0000000000201000 000000000000002a     1         /[...]/crt1.o:(.text)
# 0000000000201000 0000000000000000     0                 _start
# 000000000020102a 0000000000000000     1         /[...]/crti.o:(.text)
# 0000000000201030 00000000000000bd    16         /[...]/crtbegin.o:(.text)
# 0000000000201030 0000000000000000     0                 deregister_tm_clones
# 0000000000201060 0000000000000000     0                 register_tm_clones
# 00000000002010a0 0000000000000000     0                 __do_global_dtors_aux
# 00000000002010c0 0000000000000000     0                 frame_dummy
# 00000000002010ed 0000000000000071     1         a.o:(.text)
# 00000000002010ed 0000000000000071     0                 main
    # Extract e.g., 'lld_v0' -> 0, or 'lld-lto_v1' -> 1.
    map_file_version = int(self._linker_name.split('_v')[1])
    pattern = MapFileParserLld._LINE_RE[map_file_version]

    sym_maker = _SymbolMaker()
    cur_section = None
    cur_section_is_useful = None

    for line in lines:
      m = pattern.match(line)
      if m is None:
        continue
      address = int(m.group(1), 16)
      size = int(m.group(2), 16)
      indent_size = len(m.group(4))
      tok = m.group(5)

      if indent_size == 0:
        sym_maker.Flush()
        self._section_sizes[tok] = size
        cur_section = tok
        # E.g., Want to convert "(.text._name)" -> "_name" later.
        mangled_start_idx = len(cur_section) + 2
        cur_section_is_useful = (
            cur_section in (models.SECTION_BSS,
                            models.SECTION_RODATA,
                            models.SECTION_TEXT) or
            cur_section.startswith(models.SECTION_DATA))
      elif cur_section_is_useful:
        if indent_size == 8:
          sym_maker.Flush()
          # e.g. path.o:(.text._name)
          cur_obj, paren_value = tok.split(':')
          # "(.text._name)" -> "_name".
          mangled_name = paren_value[mangled_start_idx:-1]
          sym_maker.Create(cur_section, size, address=address)
          # As of 2017/11 LLD does not distinguish merged strings from other
          # merged data. Feature request is filed under:
          # https://bugs.llvm.org/show_bug.cgi?id=35248
          if cur_obj == '<internal>':
            if cur_section == '.rodata' and mangled_name == '':
              # Treat all <internal> sections within .rodata as as string
              # literals. Some may hold numeric constants or other data, but
              # there is currently no way to distinguish them.
              sym_maker.cur_sym.full_name = '** lld merge strings'
            else:
              # e.g. <internal>:(.text.thunk)
              sym_maker.cur_sym.full_name = '** ' + mangled_name
          elif cur_obj == 'lto.tmp' or cur_obj.startswith('thinlto-cache'):
            pass
          else:
            sym_maker.cur_sym.object_path = cur_obj

        elif indent_size == 16:
          # If multiple entries exist, take the first on that reports a size.
          # Zero-length symbols look like "$t.4", "$d.5".
          if size and not sym_maker.cur_sym.full_name:
            sym_maker.cur_sym.full_name = tok

        else:
          logging.error('Problem line: %r', line)

    sym_maker.Flush()
    return self._section_sizes, sym_maker.syms


def _DetectLto(lines):
  """Scans LLD linker map file and returns whether LTO was used."""
  # It's assumed that the first line in |lines| was consumed to determine that
  # LLD was used. Seek 'thinlto-cache' prefix within an "indicator section" as
  # indicator for LTO.
  found_indicator_section = False
  # Potential names of "main section". Only one gets used.
  indicator_section_set = set(['.rodata', '.ARM.exidx'])
  start_pos = -1
  for line in lines:
    # Shortcut to avoid regex: The first line seen (second line in file) should
    # start a section, and start with '.', e.g.:
    #     194      194       13     1 .interp
    # Assign |start_pos| as position of '.', and trim everything before!
    if start_pos < 0:
      start_pos = line.index('.')
    if len(line) < start_pos:
      continue
    line = line[start_pos:]
    tok = line.lstrip()  # Allow whitespace at right.
    indent_size = len(line) - len(tok)
    if indent_size == 0:  # Section change.
      if found_indicator_section:  # Exit if just visited "main section".
        break
      if tok.strip() in indicator_section_set:
        found_indicator_section = True
    elif indent_size == 8:
      if found_indicator_section:
        if tok.startswith('thinlto-cache'):
          return True
  return False


def DetectLinkerNameFromMapFile(lines):
  """Scans linker map file, and returns a coded linker name."""
  first_line = next(lines)

  if first_line.startswith('Address'):
    return 'lld-lto_v0' if _DetectLto(lines) else 'lld_v0'

  if first_line.lstrip().startswith('VMA'):
    return 'lld-lto_v1' if _DetectLto(lines) else 'lld_v1'

  if first_line.startswith('Archive member'):
    return 'gold'

  raise Exception('Invalid map file: ' + first_line)


class MapFileParser(object):
  """Parses a linker map file, with heuristic linker detection."""
  def Parse(self, linker_name, lines):
    """Parses a linker map file.

    Args:
      lines: Iterable of lines.

    Returns:
      A tuple of (section_sizes, symbols).
    """
    next(lines)  # Consume the first line of headers.
    if linker_name.startswith('lld'):
      inner_parser = MapFileParserLld(linker_name)
    elif linker_name == 'gold':
      inner_parser = MapFileParserGold()
    else:
      raise Exception('.map file is from a unsupported linker.')

    section_sizes, syms = inner_parser.Parse(lines)
    for sym in syms:
      if sym.object_path and not sym.object_path.endswith(')'):
        # Don't want '' to become '.'.
        # Thin archives' paths will get fixed in |ar.CreateThinObjectPath|.
        sym.object_path = os.path.normpath(sym.object_path)
    return (section_sizes, syms)
