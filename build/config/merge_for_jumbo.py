#!/usr/bin/env python
#
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script creates a "jumbo" file which merges all incoming files
for compiling.

"""

from __future__ import print_function

import argparse
import cStringIO
import os

# Files that appear in sources lists but have no effect and are
# ignored by the build system. We warn for unexpected files in the
# sources list, but these are ok.
NOOP_FILE_SUFFIXES = (
  ".g",
  ".idl",
  ".inc",
  ".json",
  ".py",
)


def write_jumbo_files(inputs, outputs, written_input_set, written_output_set):
  output_count = len(outputs)
  input_count = len(inputs)

  written_inputs = 0
  for output_index, output_file in enumerate(outputs):
    written_output_set.add(output_file)
    if os.path.isfile(output_file):
      with open(output_file, "r") as current:
        current_jumbo_file = current.read()
    else:
      current_jumbo_file = None

    out = cStringIO.StringIO()
    out.write("/* This is a Jumbo file. Don't edit. */\n\n")
    out.write("/* Generated with merge_for_jumbo.py. */\n\n")
    input_limit = (output_index + 1) * input_count / output_count
    while written_inputs < input_limit:
      filename = inputs[written_inputs]
      written_inputs += 1
      out.write("#include \"%s\"\n" % filename)
      written_input_set.add(filename)
    new_jumbo_file = out.getvalue()
    out.close()

    if new_jumbo_file != current_jumbo_file:
      with open(output_file, "w") as out:
        out.write(new_jumbo_file)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("--outputs", nargs="+", required=True,
                      help='List of output files to split input into')
  parser.add_argument("--file-list", required=True)
  parser.add_argument("--verbose", action="store_true")
  args = parser.parse_args()

  lines = []
  # If written with gn |write_file| each file is on its own line.
  with open(args.file_list) as file_list_file:
    lines = [line.strip() for line in file_list_file if line.strip()]
  # If written with gn |response_file_contents| the files are space separated.
  all_inputs = []
  for line in lines:
    all_inputs.extend(line.split())

  written_output_set = set()  # Just for double checking
  written_input_set = set()  # Just for double checking
  for language_ext in (".cc", ".c", ".mm", ".S"):
    if language_ext == ".cc":
      ext_pattern = (".cc", ".cpp")
    else:
      ext_pattern = tuple([language_ext])

    outputs = [x for x in args.outputs if x.endswith(ext_pattern)]
    inputs = [x for x in all_inputs if x.endswith(ext_pattern)]

    if not outputs:
      assert not inputs
      continue

    write_jumbo_files(inputs, outputs, written_input_set, written_output_set)

  header_files = set([x for x in all_inputs if x.endswith(".h")])
  assert set(args.outputs) == written_output_set, "Did not fill all outputs"
  random_files_to_ignore = set([x for x in all_inputs
                                if x.endswith(NOOP_FILE_SUFFIXES)])
  files_not_included = set(all_inputs) - (written_input_set |
                                          header_files |
                                          random_files_to_ignore)
  assert not files_not_included, ("Jumbo build left out files: %s" %
                                  files_not_included)
  if args.verbose:
    print("Generated %s (%d files) based on %s" % (
      str(args.outputs), len(written_input_set), args.file_list))

if __name__ == "__main__":
  main()
