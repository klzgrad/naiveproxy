#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool for interacting with .pak files.

For details on the pak file format, see:
https://dev.chromium.org/developers/design-documents/linuxresourcesandlocalizedstrings
"""

import argparse
import hashlib
import os
import sys

from grit.format import data_pack


def _RepackMain(args):
  data_pack.RePack(args.output_pak_file, args.input_pak_files, args.whitelist,
                   args.suppress_removed_key_output)


def _ExtractMain(args):
  pak = data_pack.ReadDataPack(args.pak_file)

  for resource_id, payload in pak.resources.iteritems():
    path = os.path.join(args.output_dir, str(resource_id))
    with open(path, 'w') as f:
      f.write(payload)


def _PrintMain(args):
  pak = data_pack.ReadDataPack(args.pak_file)
  id_map = {id(v): k for k, v in sorted(pak.resources.items(), reverse=True)}
  encoding = 'binary'
  if pak.encoding == 1:
    encoding = 'utf-8'
  elif pak.encoding == 2:
    encoding = 'utf-16'
  else:
    encoding = '?' + str(pak.encoding)
  print 'Encoding:', encoding

  try_decode = encoding.startswith('utf')
  # Print IDs in ascending order, since that's the order in which they appear in
  # the file (order is lost by Python dict).
  for resource_id in sorted(pak.resources):
    data = pak.resources[resource_id]
    desc = '<binary>'
    if try_decode:
      try:
        desc = unicode(data, encoding)
        if len(desc) > 60:
          desc = desc[:60] + u'...'
        desc = desc.replace('\n', '\\n')
      except UnicodeDecodeError:
        pass
    sha1 = hashlib.sha1(data).hexdigest()[:10]
    canonical_id = id_map[id(data)]
    if resource_id == canonical_id:
      line = u'Entry(id={}, len={}, sha1={}): {}'.format(
          resource_id, len(data), sha1, desc)
    else:
      line = u'Entry(id={}, alias_of={}): {}'.format(
          resource_id, canonical_id, desc)
    print line.encode('utf-8')


def _ListMain(args):
  resources, _ = data_pack.ReadDataPack(args.pak_file)
  for resource_id in sorted(resources.keys()):
    args.output.write('%d\n' % resource_id)


def main():
  parser = argparse.ArgumentParser(
      description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
  sub_parsers = parser.add_subparsers()

  sub_parser = sub_parsers.add_parser('repack',
      help='Combines several .pak files into one.')
  sub_parser.add_argument('output_pak_file', help='File to create.')
  sub_parser.add_argument('input_pak_files', nargs='+',
      help='Input .pak files.')
  sub_parser.add_argument('--whitelist',
      help='Path to a whitelist used to filter output pak file resource IDs.')
  sub_parser.add_argument('--suppress-removed-key-output', action='store_true',
      help='Do not log which keys were removed by the whitelist.')
  sub_parser.set_defaults(func=_RepackMain)

  sub_parser = sub_parsers.add_parser('extract', help='Extracts pak file')
  sub_parser.add_argument('pak_file')
  sub_parser.add_argument('-o', '--output-dir', default='.',
                          help='Directory to extract to.')
  sub_parser.set_defaults(func=_ExtractMain)

  sub_parser = sub_parsers.add_parser('print',
      help='Prints all pak IDs and contents. Useful for diffing.')
  sub_parser.add_argument('pak_file')
  sub_parser.set_defaults(func=_PrintMain)
  
  sub_parser = sub_parsers.add_parser('list-id',
      help='Outputs all resource IDs to a file.')
  sub_parser.add_argument('pak_file')
  sub_parser.add_argument('--output', type=argparse.FileType('w'),
      default=sys.stdout,
      help='The resource list path to write (default stdout)')
  sub_parser.set_defaults(func=_ListMain)

  if len(sys.argv) == 1:
    parser.print_help()
    sys.exit(1)
  elif len(sys.argv) == 2 and sys.argv[1] in actions:
    parser.parse_args(sys.argv[1:] + ['-h'])
    sys.exit(1)

  args = parser.parse_args()
  args.func(args)


if __name__ == '__main__':
  main()
