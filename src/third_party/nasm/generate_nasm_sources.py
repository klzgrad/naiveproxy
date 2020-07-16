#!/usr/bin/env python
#
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A script to parse nasm's file lists out of Makefile.in."""

import os
import sys

def ParseFileLists(path):
  ret = {}
  with open(path) as f:
    in_file_list = False
    split_line = ""
    for line in f:
      line = line.rstrip()
      if not in_file_list:
        if "-- Begin File Lists --" in line:
          in_file_list = True
        continue
      if "-- End File Lists --" in line:
        if split_line:
          raise ValueError("End comment was preceded by split line")
        break
      line = split_line + line
      split_line = ""
      if line.endswith('\\'):
        split_line = line[:-1]
        continue
      line = line.strip()
      if not line:
        continue
      name, value = line.split('=')
      name = name.strip()
      value = value.replace("$(O)", "c")
      files = value.split()
      files.sort()
      files = [file for file in files]
      ret[name] = files
  return ret

def PrintFileList(out, name, files):
  if len(files) == 0:
    print >>out, "%s = []" % (name,)
  elif len(files) == 1:
    print >>out, "%s = [ \"%s\" ]" % (name, files[0])
  else:
    print >>out, "%s = [" % (name,)
    for f in files:
      print >>out, "  \"%s\"," % (f,)
    print >>out, "]"

def main():
  file_lists = ParseFileLists("Makefile.in")
  with open("nasm_sources.gni", "w") as out:
    print >>out, """# Copyright (c) 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is created by generate_nasm_sources.py. Do not edit manually.
"""
    PrintFileList(out, "ndisasm_sources", file_lists['NDISASM'])
    PrintFileList(out, "nasmlib_sources", file_lists['LIBOBJ'])
    PrintFileList(out, "nasm_sources", file_lists['NASM'])

if __name__ == "__main__":
  main()
