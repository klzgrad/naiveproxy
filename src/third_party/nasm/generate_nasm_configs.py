#!/usr/bin/env python3
#
# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A script to update and tweak nasm's generated configs post-autoconf."""

import re
import os


def RewriteFile(path, search_replace):
    with open(path) as f:
        contents = f.read()
    with open(path, 'w') as f:
        for search, replace in search_replace:
            contents = re.sub(search, replace, contents)

        # Cleanup trailing newlines.
        f.write(contents.strip() + '\n')


def UpdateLinuxConfig(path):
    RewriteFile(
        path,
        [
            # While glibc has canonicalize_file_name(3), other libcs do not,
            # and we want the source tree not to depend on glibc if we can avoid it,
            # especially for linux distribution tarballs. Since nasm has fallback
            # code for not having canonicalize_file_name(3) anyway, just pretend it
            # doesn't exist.
            (r'#define HAVE_CANONICALIZE_FILE_NAME 1',
             r'/* #undef HAVE_CANONICALIZE_FILE_NAME */ // Controlled by the Chromium build process - see generate_nasm_configs.py'
             )
        ])


def UpdateMacConfig(path):
    pass


def main():
    UpdateLinuxConfig('config/config-linux.h')
    UpdateMacConfig('config/config-mac.h')


if __name__ == "__main__":
    main()
