#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Bootstrapping for GRIT.
'''

import sys

import grit.grit_runner


if __name__ == '__main__':
  grit.grit_runner.Main(sys.argv[1:])

