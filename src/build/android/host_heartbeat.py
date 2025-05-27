#!/usr/bin/env vpython3
#
# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Sends a heart beat pulse to the currently online Android devices.
This heart beat lets the devices know that they are connected to a host.
"""
# pylint: disable=W0702

import sys
import time

import devil_chromium
from devil.android import device_utils

PULSE_PERIOD = 20

def main():
  devil_chromium.Initialize()

  while True:
    try:
      devices = device_utils.DeviceUtils.HealthyDevices(denylist=None)
      for d in devices:
        d.RunShellCommand(['touch', '/sdcard/host_heartbeat'],
                          check_return=True)
    except:
      # Keep the heatbeat running bypassing all errors.
      pass
    time.sleep(PULSE_PERIOD)


if __name__ == '__main__':
  sys.exit(main())
