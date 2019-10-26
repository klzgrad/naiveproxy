# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import contextlib
import logging
import os
import socket
import stat

from py_utils import tempfile_ext

from devil.android.sdk import adb_wrapper
from devil.utils import cmd_helper
from devil.utils import timeout_retry

from pylib import constants
from pylib.local.device import local_device_environment


class LocalEmulatorEnvironment(local_device_environment.LocalDeviceEnvironment):

  def __init__(self, args, output_manager, error_func):
    super(LocalEmulatorEnvironment, self).__init__(args, output_manager,
                                                   error_func)
    self._avd_name = args.avd_name
    self._emulator_home = (args.emulator_home
                           or os.path.expanduser(os.path.join('~', '.android')))

    root_ini = os.path.join(self._emulator_home, 'avd',
                            '%s.ini' % self._avd_name)
    if not os.path.exists(root_ini):
      error_func('Unable to find configuration for AVD %s at %s' %
                 (self._avd_name, root_ini))

    self._emulator_path = os.path.join(constants.ANDROID_SDK_ROOT, 'emulator',
                                       'emulator')
    if not os.path.exists(self._emulator_path):
      error_func('%s does not exist.' % self._emulator_path)

    self._emulator_proc = None
    self._emulator_serial = None

  #override
  def SetUp(self):
    # Emulator start-up looks for the adb daemon. Make sure it's running.
    adb_wrapper.AdbWrapper.StartServer()

    # Emulator start-up tries to check for the SDK root by looking for
    # platforms/ and platform-tools/. Ensure they exist.
    # See http://bit.ly/2YAkyFE for context.
    required_dirs = [
        os.path.join(constants.ANDROID_SDK_ROOT, 'platforms'),
        os.path.join(constants.ANDROID_SDK_ROOT, 'platform-tools'),
    ]
    for d in required_dirs:
      if not os.path.exists(d):
        os.makedirs(d)

    # The emulator requires that some files are writable.
    for dirname, _, filenames in os.walk(self._emulator_home):
      for f in filenames:
        path = os.path.join(dirname, f)
        if (os.lstat(path).st_mode &
            (stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO) == stat.S_IRUSR):
          os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)

    self._emulator_proc, self._emulator_serial = self._StartInstance()

    logging.info('Emulator serial: %s', self._emulator_serial)
    self._device_serials = [self._emulator_serial]
    super(LocalEmulatorEnvironment, self).SetUp()

  def _StartInstance(self):
    """Starts an AVD instance.

    Returns:
      A (Popen, str) 2-tuple that includes the process and serial.
    """
    # Start up the AVD.
    with tempfile_ext.TemporaryFileName() as socket_path, (contextlib.closing(
        socket.socket(socket.AF_UNIX))) as sock:
      sock.bind(socket_path)
      emulator_cmd = [
          self._emulator_path,
          '-avd',
          self._avd_name,
          '-report-console',
          'unix:%s' % socket_path,
          '-read-only',
          '-no-window',
      ]
      emulator_env = {}
      if self._emulator_home:
        emulator_env['ANDROID_EMULATOR_HOME'] = self._emulator_home
      sock.listen(1)
      emulator_proc = cmd_helper.Popen(emulator_cmd, env=emulator_env)

      def listen_for_serial(s):
        logging.info('Waiting for connection from emulator.')
        with contextlib.closing(s.accept()[0]) as conn:
          val = conn.recv(1024)
          return 'emulator-%d' % int(val)

      try:
        emulator_serial = timeout_retry.Run(
            listen_for_serial, timeout=30, retries=0, args=[sock])
      except Exception:
        emulator_proc.terminate()
        raise

      return (emulator_proc, emulator_serial)

  #override
  def TearDown(self):
    try:
      super(LocalEmulatorEnvironment, self).TearDown()
    finally:
      if self._emulator_proc:
        self._emulator_proc.terminate()
        self._emulator_proc.wait()
