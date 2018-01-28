# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provides an interface to start and stop Android emulator.

  Emulator: The class provides the methods to launch/shutdown the emulator with
            the android virtual device named 'avd_armeabi' .
"""

import logging
import os
import signal
import subprocess
import time

from devil.android import device_errors
from devil.android import device_utils
from devil.android.sdk import adb_wrapper
from devil.utils import cmd_helper
from pylib import constants
from pylib import pexpect
from pylib.utils import time_profile

# Default sdcard size in the format of [amount][unit]
DEFAULT_SDCARD_SIZE = '512M'
# Default internal storage (MB) of emulator image
DEFAULT_STORAGE_SIZE = '1024M'

# Each emulator has 300 secs of wait time for launching
_BOOT_WAIT_INTERVALS = 30
_BOOT_WAIT_INTERVAL_TIME = 10

# Path for avd files and avd dir
_BASE_AVD_DIR = os.path.expanduser(os.path.join('~', '.android', 'avd'))
_TOOLS_ANDROID_PATH = os.path.join(constants.ANDROID_SDK_ROOT,
                                   'tools', 'android')

# Template used to generate config.ini files for the emulator
CONFIG_TEMPLATE = """avd.ini.encoding=ISO-8859-1
hw.dPad=no
hw.lcd.density=320
sdcard.size={sdcard.size}
hw.cpu.arch={hw.cpu.arch}
hw.device.hash=-708107041
hw.camera.back=none
disk.dataPartition.size=800M
hw.gpu.enabled={gpu}
skin.path=720x1280
skin.dynamic=yes
hw.keyboard=yes
hw.ramSize=1024
hw.device.manufacturer=Google
hw.sdCard=yes
hw.mainKeys=no
hw.accelerometer=yes
skin.name=720x1280
abi.type={abi.type}
hw.trackBall=no
hw.device.name=Galaxy Nexus
hw.battery=yes
hw.sensors.proximity=yes
image.sysdir.1=system-images/android-{api.level}/default/{abi.type}/
hw.sensors.orientation=yes
hw.audioInput=yes
hw.camera.front=none
hw.gps=yes
vm.heapSize=128
{extras}"""

CONFIG_REPLACEMENTS = {
  'x86': {
    '{hw.cpu.arch}': 'x86',
    '{abi.type}': 'x86',
    '{extras}': ''
  },
  'arm': {
    '{hw.cpu.arch}': 'arm',
    '{abi.type}': 'armeabi-v7a',
    '{extras}': 'hw.cpu.model=cortex-a8\n'
  },
  'mips': {
    '{hw.cpu.arch}': 'mips',
    '{abi.type}': 'mips',
    '{extras}': ''
  }
}

class EmulatorLaunchException(Exception):
  """Emulator failed to launch."""
  pass

def WaitForEmulatorLaunch(num):
  """Wait for emulators to finish booting

  Emulators on bots are launch with a separate background process, to avoid
  running tests before the emulators are fully booted, this function waits for
  a number of emulators to finish booting

  Arg:
    num: the amount of emulators to wait.
  """
  for _ in range(num*_BOOT_WAIT_INTERVALS):
    emulators = [device_utils.DeviceUtils(a)
                 for a in adb_wrapper.AdbWrapper.Devices()
                 if a.is_emulator]
    if len(emulators) >= num:
      logging.info('All %d emulators launched', num)
      return
    logging.info(
        'Waiting for %d emulators, %d of them already launched', num,
        len(emulators))
    time.sleep(_BOOT_WAIT_INTERVAL_TIME)
  raise Exception("Expected %d emulators, %d launched within time limit" %
                  (num, len(emulators)))

def KillAllEmulators():
  """Kill all running emulators that look like ones we started.

  There are odd 'sticky' cases where there can be no emulator process
  running but a device slot is taken.  A little bot trouble and we're out of
  room forever.
  """
  logging.info('Killing all existing emulators and existing the program')
  emulators = [device_utils.DeviceUtils(a)
               for a in adb_wrapper.AdbWrapper.Devices()
               if a.is_emulator]
  if not emulators:
    return
  for e in emulators:
    e.adb.Emu(['kill'])
  logging.info('Emulator killing is async; give a few seconds for all to die.')
  for _ in range(10):
    if not any(a.is_emulator for a in adb_wrapper.AdbWrapper.Devices()):
      return
    time.sleep(1)


def DeleteAllTempAVDs():
  """Delete all temporary AVDs which are created for tests.

  If the test exits abnormally and some temporary AVDs created when testing may
  be left in the system. Clean these AVDs.
  """
  logging.info('Deleting all the avd files')
  avds = device_utils.GetAVDs()
  if not avds:
    return
  for avd_name in avds:
    if 'run_tests_avd' in avd_name:
      cmd = [_TOOLS_ANDROID_PATH, '-s', 'delete', 'avd', '--name', avd_name]
      cmd_helper.RunCmd(cmd)
      logging.info('Delete AVD %s', avd_name)


class PortPool(object):
  """Pool for emulator port starting position that changes over time."""
  _port_min = 5554
  _port_max = 5585
  _port_current_index = 0

  @classmethod
  def port_range(cls):
    """Return a range of valid ports for emulator use.

    The port must be an even number between 5554 and 5584.  Sometimes
    a killed emulator "hangs on" to a port long enough to prevent
    relaunch.  This is especially true on slow machines (like a bot).
    Cycling through a port start position helps make us resilient."""
    ports = range(cls._port_min, cls._port_max, 2)
    n = cls._port_current_index
    cls._port_current_index = (n + 1) % len(ports)
    return ports[n:] + ports[:n]


def _GetAvailablePort():
  """Returns an available TCP port for the console."""
  used_ports = []
  emulators = [device_utils.DeviceUtils(a)
               for a in adb_wrapper.AdbWrapper.Devices()
               if a.is_emulator]
  for emulator in emulators:
    used_ports.append(emulator.adb.GetDeviceSerial().split('-')[1])
  for port in PortPool.port_range():
    if str(port) not in used_ports:
      return port


def LaunchTempEmulators(emulator_count, abi, api_level, enable_kvm=False,
                        kill_and_launch=True, sdcard_size=DEFAULT_SDCARD_SIZE,
                        storage_size=DEFAULT_STORAGE_SIZE, wait_for_boot=True,
                        headless=False):
  """Create and launch temporary emulators and wait for them to boot.

  Args:
    emulator_count: number of emulators to launch.
    abi: the emulator target platform
    api_level: the api level (e.g., 19 for Android v4.4 - KitKat release)
    wait_for_boot: whether or not to wait for emulators to boot up
    headless: running emulator with no ui

  Returns:
    List of emulators.
  """
  emulators = []
  for n in xrange(emulator_count):
    t = time_profile.TimeProfile('Emulator launch %d' % n)
    # Creates a temporary AVD.
    avd_name = 'run_tests_avd_%d' % n
    logging.info('Emulator launch %d with avd_name=%s and api=%d',
                 n, avd_name, api_level)
    emulator = Emulator(avd_name, abi, enable_kvm=enable_kvm,
                        sdcard_size=sdcard_size, storage_size=storage_size,
                        headless=headless)
    emulator.CreateAVD(api_level)
    emulator.Launch(kill_all_emulators=(n == 0 and kill_and_launch))
    t.Stop()
    emulators.append(emulator)
  # Wait for all emulators to boot completed.
  if wait_for_boot:
    for emulator in emulators:
      emulator.ConfirmLaunch(True)
    logging.info('All emulators are fully booted')
  return emulators


def LaunchEmulator(avd_name, abi, kill_and_launch=True, enable_kvm=False,
                   sdcard_size=DEFAULT_SDCARD_SIZE,
                   storage_size=DEFAULT_STORAGE_SIZE, headless=False):
  """Launch an existing emulator with name avd_name.

  Args:
    avd_name: name of existing emulator
    abi: the emulator target platform
    headless: running emulator with no ui

  Returns:
    emulator object.
  """
  logging.info('Specified emulator named avd_name=%s launched', avd_name)
  emulator = Emulator(avd_name, abi, enable_kvm=enable_kvm,
                      sdcard_size=sdcard_size, storage_size=storage_size,
                      headless=headless)
  emulator.Launch(kill_all_emulators=kill_and_launch)
  emulator.ConfirmLaunch(True)
  return emulator


class Emulator(object):
  """Provides the methods to launch/shutdown the emulator.

  The emulator has the android virtual device named 'avd_armeabi'.

  The emulator could use any even TCP port between 5554 and 5584 for the
  console communication, and this port will be part of the device name like
  'emulator-5554'. Assume it is always True, as the device name is the id of
  emulator managed in this class.

  Attributes:
    emulator: Path of Android's emulator tool.
    popen: Popen object of the running emulator process.
    device: Device name of this emulator.
  """

  # Signals we listen for to kill the emulator on
  _SIGNALS = (signal.SIGINT, signal.SIGHUP)

  # Time to wait for an emulator launch, in seconds.  This includes
  # the time to launch the emulator and a wait-for-device command.
  _LAUNCH_TIMEOUT = 120

  # Timeout interval of wait-for-device command before bouncing to a a
  # process life check.
  _WAITFORDEVICE_TIMEOUT = 5

  # Time to wait for a 'wait for boot complete' (property set on device).
  _WAITFORBOOT_TIMEOUT = 300

  def __init__(self, avd_name, abi, enable_kvm=False,
               sdcard_size=DEFAULT_SDCARD_SIZE,
               storage_size=DEFAULT_STORAGE_SIZE, headless=False):
    """Init an Emulator.

    Args:
      avd_name: name of the AVD to create
      abi: target platform for emulator being created, defaults to x86
    """
    android_sdk_root = constants.ANDROID_SDK_ROOT
    self.emulator = os.path.join(android_sdk_root, 'tools', 'emulator')
    self.android = _TOOLS_ANDROID_PATH
    self.popen = None
    self.device_serial = None
    self.abi = abi
    self.avd_name = avd_name
    self.sdcard_size = sdcard_size
    self.storage_size = storage_size
    self.enable_kvm = enable_kvm
    self.headless = headless

  @staticmethod
  def _DeviceName():
    """Return our device name."""
    port = _GetAvailablePort()
    return ('emulator-%d' % port, port)

  def CreateAVD(self, api_level):
    """Creates an AVD with the given name.

    Args:
      api_level: the api level of the image

    Return avd_name.
    """

    if self.abi == 'arm':
      abi_option = 'armeabi-v7a'
    elif self.abi == 'mips':
      abi_option = 'mips'
    else:
      abi_option = 'x86'

    api_target = 'android-%s' % api_level

    avd_command = [
        self.android,
        '--silent',
        'create', 'avd',
        '--name', self.avd_name,
        '--abi', abi_option,
        '--target', api_target,
        '--sdcard', self.sdcard_size,
        '--force',
    ]
    avd_cmd_str = ' '.join(avd_command)
    logging.info('Create AVD command: %s', avd_cmd_str)
    avd_process = pexpect.spawn(avd_cmd_str)

    # Instead of creating a custom profile, we overwrite config files.
    avd_process.expect('Do you wish to create a custom hardware profile')
    avd_process.sendline('no\n')
    avd_process.expect('Created AVD \'%s\'' % self.avd_name)

    # Replace current configuration with default Galaxy Nexus config.
    ini_file = os.path.join(_BASE_AVD_DIR, '%s.ini' % self.avd_name)
    new_config_ini = os.path.join(_BASE_AVD_DIR, '%s.avd' % self.avd_name,
                                  'config.ini')

    # Remove config files with defaults to replace with Google's GN settings.
    os.unlink(ini_file)
    os.unlink(new_config_ini)

    # Create new configuration files with Galaxy Nexus by Google settings.
    with open(ini_file, 'w') as new_ini:
      new_ini.write('avd.ini.encoding=ISO-8859-1\n')
      new_ini.write('target=%s\n' % api_target)
      new_ini.write('path=%s/%s.avd\n' % (_BASE_AVD_DIR, self.avd_name))
      new_ini.write('path.rel=avd/%s.avd\n' % self.avd_name)

    custom_config = CONFIG_TEMPLATE
    replacements = CONFIG_REPLACEMENTS[self.abi]
    for key in replacements:
      custom_config = custom_config.replace(key, replacements[key])
    custom_config = custom_config.replace('{api.level}', str(api_level))
    custom_config = custom_config.replace('{sdcard.size}', self.sdcard_size)
    custom_config.replace('{gpu}', 'no' if self.headless else 'yes')

    with open(new_config_ini, 'w') as new_config_ini:
      new_config_ini.write(custom_config)

    return self.avd_name


  def _DeleteAVD(self):
    """Delete the AVD of this emulator."""
    avd_command = [
        self.android,
        '--silent',
        'delete',
        'avd',
        '--name', self.avd_name,
    ]
    logging.info('Delete AVD command: %s', ' '.join(avd_command))
    cmd_helper.RunCmd(avd_command)

  def ResizeAndWipeAvd(self, storage_size):
    """Wipes old AVD and creates new AVD of size |storage_size|.

    This serves as a work around for '-partition-size' and '-wipe-data'
    """
    userdata_img = os.path.join(_BASE_AVD_DIR, '%s.avd' % self.avd_name,
                                'userdata.img')
    userdata_qemu_img = os.path.join(_BASE_AVD_DIR, '%s.avd' % self.avd_name,
                                     'userdata-qemu.img')
    resize_cmd = ['resize2fs', userdata_img, '%s' % storage_size]
    logging.info('Resizing userdata.img to ideal size')
    cmd_helper.RunCmd(resize_cmd)
    wipe_cmd = ['cp', userdata_img, userdata_qemu_img]
    logging.info('Replacing userdata-qemu.img with the new userdata.img')
    cmd_helper.RunCmd(wipe_cmd)

  def Launch(self, kill_all_emulators):
    """Launches the emulator asynchronously. Call ConfirmLaunch() to ensure the
    emulator is ready for use.

    If fails, an exception will be raised.
    """
    if kill_all_emulators:
      KillAllEmulators()  # just to be sure
    self._AggressiveImageCleanup()
    (self.device_serial, port) = self._DeviceName()
    self.ResizeAndWipeAvd(storage_size=self.storage_size)
    emulator_command = [
        self.emulator,
        # Speed up emulator launch by 40%.  Really.
        '-no-boot-anim',
        ]
    if self.headless:
      emulator_command.extend([
        '-no-skin',
        '-no-window'
        ])
    else:
      emulator_command.extend([
          '-gpu', 'on'
        ])
    emulator_command.extend([
        # Use a familiar name and port.
        '-avd', self.avd_name,
        '-port', str(port),
        # all the argument after qemu are sub arguments for qemu
        '-qemu', '-m', '1024',
        ])
    if self.abi == 'x86' and self.enable_kvm:
      emulator_command.extend([
          # For x86 emulator --enable-kvm will fail early, avoiding accidental
          # runs in a slow mode (i.e. without hardware virtualization support).
          '--enable-kvm',
          ])

    logging.info('Emulator launch command: %s', ' '.join(emulator_command))
    self.popen = subprocess.Popen(args=emulator_command,
                                  stderr=subprocess.STDOUT)
    self._InstallKillHandler()

  @staticmethod
  def _AggressiveImageCleanup():
    """Aggressive cleanup of emulator images.

    Experimentally it looks like our current emulator use on the bot
    leaves image files around in /tmp/android-$USER.  If a "random"
    name gets reused, we choke with a 'File exists' error.
    TODO(jrg): is there a less hacky way to accomplish the same goal?
    """
    logging.info('Aggressive Image Cleanup')
    emulator_imagedir = '/tmp/android-%s' % os.environ['USER']
    if not os.path.exists(emulator_imagedir):
      return
    for image in os.listdir(emulator_imagedir):
      full_name = os.path.join(emulator_imagedir, image)
      if 'emulator' in full_name:
        logging.info('Deleting emulator image %s', full_name)
        os.unlink(full_name)

  def ConfirmLaunch(self, wait_for_boot=False):
    """Confirm the emulator launched properly.

    Loop on a wait-for-device with a very small timeout.  On each
    timeout, check the emulator process is still alive.
    After confirming a wait-for-device can be successful, make sure
    it returns the right answer.
    """
    seconds_waited = 0
    number_of_waits = 2  # Make sure we can wfd twice

    device = device_utils.DeviceUtils(self.device_serial)
    while seconds_waited < self._LAUNCH_TIMEOUT:
      try:
        device.adb.WaitForDevice(
            timeout=self._WAITFORDEVICE_TIMEOUT, retries=1)
        number_of_waits -= 1
        if not number_of_waits:
          break
      except device_errors.CommandTimeoutError:
        seconds_waited += self._WAITFORDEVICE_TIMEOUT
        device.adb.KillServer()
      self.popen.poll()
      if self.popen.returncode != None:
        raise EmulatorLaunchException('EMULATOR DIED')

    if seconds_waited >= self._LAUNCH_TIMEOUT:
      raise EmulatorLaunchException('TIMEOUT with wait-for-device')

    logging.info('Seconds waited on wait-for-device: %d', seconds_waited)
    if wait_for_boot:
      # Now that we checked for obvious problems, wait for a boot complete.
      # Waiting for the package manager is sometimes problematic.
      device.WaitUntilFullyBooted(timeout=self._WAITFORBOOT_TIMEOUT)
      logging.info('%s is now fully booted', self.avd_name)

  def Shutdown(self):
    """Shuts down the process started by launch."""
    self._DeleteAVD()
    if self.popen:
      self.popen.poll()
      if self.popen.returncode == None:
        self.popen.kill()
      self.popen = None

  def _ShutdownOnSignal(self, _signum, _frame):
    logging.critical('emulator _ShutdownOnSignal')
    for sig in self._SIGNALS:
      signal.signal(sig, signal.SIG_DFL)
    self.Shutdown()
    raise KeyboardInterrupt  # print a stack

  def _InstallKillHandler(self):
    """Install a handler to kill the emulator when we exit unexpectedly."""
    for sig in self._SIGNALS:
      signal.signal(sig, self._ShutdownOnSignal)
