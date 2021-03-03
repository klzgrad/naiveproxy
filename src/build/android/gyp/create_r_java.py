#!/usr/bin/env python2
# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Writes a dummy R.java file from a list of resources."""

import argparse
import sys

from util import build_utils
from util import resource_utils
from util import resources_parser


def _CreateRJava(resource_zips, package_name, srcjar_out, rtxt_out):
  with resource_utils.BuildContext() as build:
    # TODO(mheikal): maybe we can rewrite resources_parser to just read out of a
    # zipfile without needing to extract first.
    dep_subdirs = resource_utils.ExtractDeps(resource_zips, build.deps_dir)
    resources_parser.RTxtGenerator(dep_subdirs).WriteRTxtFile(rtxt_out)

    rjava_build_options = resource_utils.RJavaBuildOptions()
    rjava_build_options.ExportAllResources()
    rjava_build_options.ExportAllStyleables()
    rjava_build_options.GenerateOnResourcesLoaded(fake=True)
    resource_utils.CreateRJavaFiles(build.srcjar_dir,
                                    package_name,
                                    rtxt_out,
                                    extra_res_packages=[],
                                    rjava_build_options=rjava_build_options,
                                    srcjar_out=srcjar_out)
    build_utils.ZipDir(srcjar_out, build.srcjar_dir)


def main(args):
  parser = argparse.ArgumentParser(description='Create an R.java srcjar.')
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--srcjar-out',
                      required=True,
                      help='Path to output srcjar.')
  parser.add_argument('--rtxt-out',
                      required=True,
                      help='Path to output R.txt file.')
  parser.add_argument('--resource-zips',
                      required=True,
                      help='Path to input resource zips.')
  parser.add_argument('--r-package',
                      required=True,
                      help='R.java package to use.')
  options = parser.parse_args(build_utils.ExpandFileArgs(args))
  options.resource_zips = build_utils.ParseGnList(options.resource_zips)

  _CreateRJava(options.resource_zips, options.r_package, options.srcjar_out,
               options.rtxt_out)
  build_utils.WriteDepfile(options.depfile,
                           options.srcjar_out,
                           inputs=options.resource_zips)


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
