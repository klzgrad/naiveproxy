# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import re
import shutil
import tempfile
import zipfile

from devil.utils import cmd_helper
from pylib import constants

_STACK_TOOL = os.path.join(os.path.dirname(__file__), '..', '..', '..', '..',
                          'third_party', 'android_platform', 'development',
                          'scripts', 'stack')
ABI_REG = re.compile('ABI: \'(.+?)\'')


def _DeviceAbiToArch(device_abi):
    # The order of this list is significant to find the more specific match
    # (e.g., arm64) before the less specific (e.g., arm).
    arches = ['arm64', 'arm', 'x86_64', 'x86_64', 'x86', 'mips']
    for arch in arches:
      if arch in device_abi:
        return arch
    raise RuntimeError('Unknown device ABI: %s' % device_abi)


class Symbolizer(object):
  """A helper class to symbolize stack."""

  def __init__(self, apk_under_test=None,
               enable_relocation_packing=None):
    self._apk_under_test = apk_under_test
    self._enable_relocation_packing = enable_relocation_packing
    self._libs_dir = None
    self._apk_libs = []
    self._has_unzipped = False


  def __del__(self):
    if self._libs_dir:
      logging.warning('Please call stack_symbolizer\'s'
                      ' CleanUp method before it goes out of scope.')
    self.CleanUp()


  def CleanUp(self):
    """Clean up the temporary directory of apk libs."""
    if self._libs_dir:
      shutil.rmtree(self._libs_dir)
      self._libs_dir = None


  def UnzipAPKIfNecessary(self):
    """Unzip apk if packed relocation is enabled."""
    if (self._has_unzipped
        or not self._enable_relocation_packing
        or not self._apk_under_test):
      return
    self._libs_dir = tempfile.mkdtemp()
    with zipfile.ZipFile(self._apk_under_test) as z:
      for name in z.namelist():
        if name.endswith('.so'):
          self._apk_libs.append(z.extract(name, self._libs_dir))

    self._has_unzipped = True


  def ExtractAndResolveNativeStackTraces(self, data_to_symbolize,
                                         device_abi, include_stack=True):
    """Run the stack tool for given input.

    Args:
      data_to_symbolize: a list of strings to symbolize.
      include_stack: boolean whether to include stack data in output.
      device_abi: the default ABI of the device which generated the tombstone.

    Yields:
      A string for each line of resolved stack output.
    """
    self.UnzipAPKIfNecessary()
    arch = _DeviceAbiToArch(device_abi)
    if not arch:
      logging.warning('No device_abi can be found.')
      return

    cmd = [_STACK_TOOL, '--arch', arch, '--output-directory',
           constants.GetOutDirectory(), '--more-info']
    if self._enable_relocation_packing and self._apk_libs:
      for apk_lib in self._apk_libs:
        cmd.extend(['--packed-lib', apk_lib])
    env = dict(os.environ)
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    with tempfile.NamedTemporaryFile() as f:
      f.write('\n'.join(data_to_symbolize))
      f.flush()
      _, output = cmd_helper.GetCmdStatusAndOutput(cmd + [f.name], env=env)
    for line in output.splitlines():
      if not include_stack and 'Stack Data:' in line:
        break
      yield line
