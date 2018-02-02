# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import functools
import logging
import os
import shlex
import sys


def memoize(default=None):
  """This decorator caches the return value of a parameterless pure function"""
  def memoizer(func):
    val = []
    @functools.wraps(func)
    def inner():
      if not val:
        ret = func()
        val.append(ret if ret is not None else default)
        if logging.getLogger().isEnabledFor(logging.INFO):
          print '%s -> %r' % (func.__name__, val[0])
      return val[0]
    return inner
  return memoizer


@memoize()
def IsWindows():
  return sys.platform in ['win32', 'cygwin']


@memoize()
def IsLinux():
  return sys.platform.startswith(('linux', 'freebsd', 'netbsd', 'openbsd'))


@memoize()
def IsMac():
  return sys.platform == 'darwin'


@memoize()
def gyp_defines():
  """Parses and returns GYP_DEFINES env var as a dictionary."""
  return dict(arg.split('=', 1)
      for arg in shlex.split(os.environ.get('GYP_DEFINES', '')))


@memoize()
def gyp_generator_flags():
  """Parses and returns GYP_GENERATOR_FLAGS env var as a dictionary."""
  return dict(arg.split('=', 1)
      for arg in shlex.split(os.environ.get('GYP_GENERATOR_FLAGS', '')))


@memoize()
def gyp_msvs_version():
  return os.environ.get('GYP_MSVS_VERSION', '')


@memoize()
def distributor():
  """
  Returns a string which is the distributed build engine in use (if any).
  Possible values: 'goma', None
  """
  if 'goma' in gyp_defines():
    return 'goma'


@memoize()
def platform():
  """
  Returns a string representing the platform this build is targeted for.
  Possible values: 'win', 'mac', 'linux', 'ios', 'android'
  """
  if 'OS' in gyp_defines():
    if 'android' in gyp_defines()['OS']:
      return 'android'
    else:
      return gyp_defines()['OS']
  elif IsWindows():
    return 'win'
  elif IsLinux():
    return 'linux'
  else:
    return 'mac'
