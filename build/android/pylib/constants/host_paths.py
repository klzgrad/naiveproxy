# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import contextlib
import os
import sys

DIR_SOURCE_ROOT = os.environ.get(
    'CHECKOUT_SOURCE_ROOT',
    os.path.abspath(os.path.join(os.path.dirname(__file__),
                                 os.pardir, os.pardir, os.pardir, os.pardir)))

BUILD_COMMON_PATH = os.path.join(
    DIR_SOURCE_ROOT, 'build', 'util', 'lib', 'common')

# third-party libraries
ANDROID_PLATFORM_DEVELOPMENT_SCRIPTS_PATH = os.path.join(
    DIR_SOURCE_ROOT, 'third_party', 'android_platform', 'development',
    'scripts')
DEVIL_PATH = os.path.join(
    DIR_SOURCE_ROOT, 'third_party', 'catapult', 'devil')
PYMOCK_PATH = os.path.join(
    DIR_SOURCE_ROOT, 'third_party', 'pymock')

@contextlib.contextmanager
def SysPath(path, position=None):
  if position is None:
    sys.path.append(path)
  else:
    sys.path.insert(position, path)
  try:
    yield
  finally:
    if sys.path[-1] == path:
      sys.path.pop()
    else:
      sys.path.remove(path)
