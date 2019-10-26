# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pylib import constants
from pylib.local.device import local_device_environment
from pylib.local.emulator import local_emulator_environment
from pylib.local.machine import local_machine_environment

def CreateEnvironment(args, output_manager, error_func):

  if args.environment == 'local':
    if args.command not in constants.LOCAL_MACHINE_TESTS:
      if args.avd_name:
        return local_emulator_environment.LocalEmulatorEnvironment(
            args, output_manager, error_func)
      return local_device_environment.LocalDeviceEnvironment(
          args, output_manager, error_func)
    else:
      return local_machine_environment.LocalMachineEnvironment(
          args, output_manager, error_func)

  error_func('Unable to create %s environment.' % args.environment)
