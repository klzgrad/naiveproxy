# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Basic .ini encoding and decoding."""


def loads(ini_str, strict=True):
  ret = {}
  for line in ini_str.splitlines():
    key, val = line.split('=', 1)
    key = key.strip()
    val = val.strip()
    if strict and key in ret:
      raise ValueError('Multiple entries present for key "%s"' % key)
    ret[key] = val

  return ret


def load(fp):
  return loads(fp.read())


def dumps(obj):
  ret = ''
  for k, v in sorted(obj.iteritems()):
    ret += '%s = %s\n' % (k, str(v))
  return ret


def dump(obj, fp):
  fp.write(dumps(obj))
