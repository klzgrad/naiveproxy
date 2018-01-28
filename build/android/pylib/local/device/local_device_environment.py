# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import contextlib
import datetime
import functools
import logging
import os
import shutil
import tempfile
import threading
import time

import devil_chromium
from devil import base_error
from devil.android import device_blacklist
from devil.android import device_errors
from devil.android import device_utils
from devil.android import logcat_monitor
from devil.android.sdk import adb_wrapper
from devil.utils import file_utils
from devil.utils import parallelizer
from pylib import constants
from pylib.android import logdog_logcat_monitor
from pylib.base import environment
from pylib.utils import instrumentation_tracing
from py_trace_event import trace_event
from py_utils import contextlib_ext


LOGCAT_FILTERS = [
  'chromium:v',
  'cr_*:v',
  'DEBUG:I',
  'StrictMode:D',
]


def _DeviceCachePath(device):
  file_name = 'device_cache_%s.json' % device.adb.GetDeviceSerial()
  return os.path.join(constants.GetOutDirectory(), file_name)


def handle_shard_failures(f):
  """A decorator that handles device failures for per-device functions.

  Args:
    f: the function being decorated. The function must take at least one
      argument, and that argument must be the device.
  """
  return handle_shard_failures_with(None)(f)


# TODO(jbudorick): Refactor this to work as a decorator or context manager.
def handle_shard_failures_with(on_failure):
  """A decorator that handles device failures for per-device functions.

  This calls on_failure in the event of a failure.

  Args:
    f: the function being decorated. The function must take at least one
      argument, and that argument must be the device.
    on_failure: A binary function to call on failure.
  """
  def decorator(f):
    @functools.wraps(f)
    def wrapper(dev, *args, **kwargs):
      try:
        return f(dev, *args, **kwargs)
      except device_errors.CommandTimeoutError:
        logging.exception('Shard timed out: %s(%s)', f.__name__, str(dev))
      except device_errors.DeviceUnreachableError:
        logging.exception('Shard died: %s(%s)', f.__name__, str(dev))
      except base_error.BaseError:
        logging.exception('Shard failed: %s(%s)', f.__name__, str(dev))
      except SystemExit:
        logging.exception('Shard killed: %s(%s)', f.__name__, str(dev))
        raise
      if on_failure:
        on_failure(dev, f.__name__)
      return None

    return wrapper

  return decorator


# TODO(jbudorick): Reconcile this with the output manager logic in
# https://codereview.chromium.org/2933993002/ once that lands.
@contextlib.contextmanager
def OptionalPerTestLogcat(
    device, test_name, condition, additional_filter_specs=None,
    deobfuscate_func=None):
  """Conditionally capture logcat and stream it to logdog.

  Args:
    device: (DeviceUtils) the device from which logcat should be captured.
    test_name: (str) the test name to use in the stream name.
    condition: (bool) whether or not to capture the logcat.
    additional_filter_specs: (list) additional logcat filters.
    deobfuscate_func: (callable) an optional unary function that
      deobfuscates logcat lines. The callable should take an iterable
      of logcat lines and return a list of deobfuscated logcat lines.
  Yields:
    A LogdogLogcatMonitor instance whether condition is true or not,
    though it may not be active.
  """
  stream_name = 'logcat_%s_%s_%s' % (
      test_name,
      time.strftime('%Y%m%dT%H%M%S-UTC', time.gmtime()),
      device.serial)
  filter_specs = LOGCAT_FILTERS + (additional_filter_specs or [])
  logmon = logdog_logcat_monitor.LogdogLogcatMonitor(
      device.adb, stream_name, filter_specs=filter_specs,
      deobfuscate_func=deobfuscate_func)

  with contextlib_ext.Optional(logmon, condition):
    yield logmon


class LocalDeviceEnvironment(environment.Environment):

  def __init__(self, args, _error_func):
    super(LocalDeviceEnvironment, self).__init__()
    self._blacklist = (device_blacklist.Blacklist(args.blacklist_file)
                       if args.blacklist_file
                       else None)
    self._device_serials = args.test_devices
    self._devices_lock = threading.Lock()
    self._devices = None
    self._concurrent_adb = args.enable_concurrent_adb
    self._enable_device_cache = args.enable_device_cache
    self._logcat_monitors = []
    self._logcat_output_dir = args.logcat_output_dir
    self._logcat_output_file = args.logcat_output_file
    self._max_tries = 1 + args.num_retries
    self._skip_clear_data = args.skip_clear_data
    self._tool_name = args.tool
    self._trace_output = None
    if hasattr(args, 'trace_output'):
      self._trace_output = args.trace_output
    self._trace_all = None
    if hasattr(args, 'trace_all'):
      self._trace_all = args.trace_all

    devil_chromium.Initialize(
        output_directory=constants.GetOutDirectory(),
        adb_path=args.adb_path)

    # Some things such as Forwarder require ADB to be in the environment path.
    adb_dir = os.path.dirname(adb_wrapper.AdbWrapper.GetAdbPath())
    if adb_dir and adb_dir not in os.environ['PATH'].split(os.pathsep):
      os.environ['PATH'] = adb_dir + os.pathsep + os.environ['PATH']

  #override
  def SetUp(self):
    if self.trace_output and self._trace_all:
      to_include = [r"pylib\..*", r"devil\..*", "__main__"]
      to_exclude = ["logging"]
      instrumentation_tracing.start_instrumenting(self.trace_output, to_include,
                                                  to_exclude)
    elif self.trace_output:
      self.EnableTracing()

  def _InitDevices(self):
    device_arg = 'default'
    if self._device_serials:
      device_arg = self._device_serials

    self._devices = device_utils.DeviceUtils.HealthyDevices(
        self._blacklist, enable_device_files_cache=self._enable_device_cache,
        default_retries=self._max_tries - 1, device_arg=device_arg)
    if not self._devices:
      raise device_errors.NoDevicesError('No devices were available')

    if self._logcat_output_file:
      self._logcat_output_dir = tempfile.mkdtemp()

    @handle_shard_failures_with(on_failure=self.BlacklistDevice)
    def prepare_device(d):
      d.WaitUntilFullyBooted()

      if self._enable_device_cache:
        cache_path = _DeviceCachePath(d)
        if os.path.exists(cache_path):
          logging.info('Using device cache: %s', cache_path)
          with open(cache_path) as f:
            d.LoadCacheData(f.read())
          # Delete cached file so that any exceptions cause it to be cleared.
          os.unlink(cache_path)

      if self._logcat_output_dir:
        logcat_file = os.path.join(
            self._logcat_output_dir,
            '%s_%s' % (d.adb.GetDeviceSerial(),
                       datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%S')))
        monitor = logcat_monitor.LogcatMonitor(
            d.adb, clear=True, output_file=logcat_file)
        self._logcat_monitors.append(monitor)
        monitor.Start()

    self.parallel_devices.pMap(prepare_device)

  @property
  def blacklist(self):
    return self._blacklist

  @property
  def concurrent_adb(self):
    return self._concurrent_adb

  @property
  def devices(self):
    # Initialize lazily so that host-only tests do not fail when no devices are
    # attached.
    if self._devices is None:
      self._InitDevices()
    return self._devices

  @property
  def max_tries(self):
    return self._max_tries

  @property
  def parallel_devices(self):
    return parallelizer.SyncParallelizer(self.devices)

  @property
  def skip_clear_data(self):
    return self._skip_clear_data

  @property
  def tool(self):
    return self._tool_name

  @property
  def trace_output(self):
    return self._trace_output

  #override
  def TearDown(self):
    if self.trace_output:
      self.DisableTracing()

    if not self._devices:
      return

    @handle_shard_failures_with(on_failure=self.BlacklistDevice)
    def tear_down_device(d):
      # Write the cache even when not using it so that it will be ready the
      # first time that it is enabled. Writing it every time is also necessary
      # so that an invalid cache can be flushed just by disabling it for one
      # run.
      cache_path = _DeviceCachePath(d)
      if os.path.exists(os.path.dirname(cache_path)):
        with open(cache_path, 'w') as f:
          f.write(d.DumpCacheData())
          logging.info('Wrote device cache: %s', cache_path)
      else:
        logging.warning(
            'Unable to write device cache as %s directory does not exist',
            os.path.dirname(cache_path))

    self.parallel_devices.pMap(tear_down_device)

    for m in self._logcat_monitors:
      try:
        m.Stop()
        m.Close()
        _, temp_path = tempfile.mkstemp()
        with open(m.output_file, 'r') as infile:
          with open(temp_path, 'w') as outfile:
            for line in infile:
              outfile.write('Device(%s) %s' % (m.adb.GetDeviceSerial(), line))
        shutil.move(temp_path, m.output_file)
      except base_error.BaseError:
        logging.exception('Failed to stop logcat monitor for %s',
                          m.adb.GetDeviceSerial())
      except IOError:
        logging.exception('Failed to locate logcat for device %s',
                          m.adb.GetDeviceSerial())

    if self._logcat_output_file:
      file_utils.MergeFiles(
          self._logcat_output_file,
          [m.output_file for m in self._logcat_monitors
           if os.path.exists(m.output_file)])
      shutil.rmtree(self._logcat_output_dir)

  def BlacklistDevice(self, device, reason='local_device_failure'):
    device_serial = device.adb.GetDeviceSerial()
    if self._blacklist:
      self._blacklist.Extend([device_serial], reason=reason)
    with self._devices_lock:
      self._devices = [d for d in self._devices if str(d) != device_serial]
    logging.error('Device %s blacklisted: %s', device_serial, reason)
    if not self._devices:
      raise device_errors.NoDevicesError(
          'All devices were blacklisted due to errors')

  @staticmethod
  def DisableTracing():
    if not trace_event.trace_is_enabled():
      logging.warning('Tracing is not running.')
    else:
      trace_event.trace_disable()

  def EnableTracing(self):
    if trace_event.trace_is_enabled():
      logging.warning('Tracing is already running.')
    else:
      trace_event.trace_enable(self._trace_output)
