#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Packages a user.bootfs for a Fuchsia boot image, pulling in the runtime
dependencies of a test binary, and then uses either QEMU from the Fuchsia SDK
to run, or starts the bootserver to allow running on a hardware device."""

import argparse
import json
import os
import socket
import sys
import tempfile
import time

from runner_common import RunFuchsia, BuildBootfs, ReadRuntimeDeps, \
    HOST_IP_ADDRESS

DIR_SOURCE_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
sys.path.append(os.path.join(DIR_SOURCE_ROOT, 'build', 'util', 'lib', 'common'))
import chrome_test_server_spawner

# RunFuchsia() may run qemu with 1 or 4 CPUs. In both cases keep test
# concurrency set to 4.
DEFAULT_TEST_CONCURRENCY = 4


def IsLocalPortAvailable(port):
  s = socket.socket()
  try:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('127.0.0.1', port))
    return True
  except socket.error:
    return False
  finally:
    s.close()


def WaitUntil(predicate, timeout_seconds=1):
  """Blocks until the provided predicate (function) is true.

  Returns:
    Whether the provided predicate was satisfied once (before the timeout).
  """
  start_time = time.clock()
  sleep_time_sec = 0.025
  while True:
    if predicate():
      return True

    if time.clock() - start_time > timeout_seconds:
      return False

    time.sleep(sleep_time_sec)
    sleep_time_sec = min(1, sleep_time_sec * 2)  # Don't wait more than 1 sec.


# Implementation of chrome_test_server_spawner.PortForwarder that doesn't
# forward ports. Instead the tests are expected to connect to the host IP
# address inside the virtual network provided by qemu. qemu will forward
# these connections to the corresponding localhost ports.
class PortForwarderNoop(chrome_test_server_spawner.PortForwarder):
  def Map(self, port_pairs):
    pass

  def GetDevicePortForHostPort(self, host_port):
    return host_port

  def WaitHostPortAvailable(self, port):
    return WaitUntil(lambda: IsLocalPortAvailable(port))

  def WaitPortNotAvailable(self, port):
    return WaitUntil(lambda: not IsLocalPortAvailable(port))

  def WaitDevicePortReady(self, port):
    return self.WaitPortNotAvailable(port)

  def Unmap(self, device_port):
    pass


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--dry-run', '-n', action='store_true', default=False,
                      help='Just print commands, don\'t execute them.')
  parser.add_argument('--output-directory',
                      type=os.path.realpath,
                      help=('Path to the directory in which build files are'
                            ' located (must include build type).'))
  parser.add_argument('--runtime-deps-path',
                      type=os.path.realpath,
                      help='Runtime data dependency file from GN.')
  parser.add_argument('--exe-name',
                      type=os.path.realpath,
                      help='Name of the the test')
  parser.add_argument('--enable-test-server', action='store_true',
                      default=False,
                      help='Enable testserver spawner.')
  parser.add_argument('--gtest_filter',
                      help='GTest filter to use in place of any default.')
  parser.add_argument('--gtest_repeat',
                      help='GTest repeat value to use. This also disables the '
                           'test launcher timeout.')
  parser.add_argument('--gtest_break_on_failure', action='store_true',
                      default=False,
                      help='Should GTest break on failure; useful with '
                           '--gtest_repeat.')
  parser.add_argument('--single-process-tests', action='store_true',
                      default=False,
                      help='Runs the tests and the launcher in the same '
                      'process. Useful for debugging.')
  parser.add_argument('--target-cpu',
                      help='GN target_cpu setting for the build.')
  parser.add_argument('--test-launcher-batch-limit',
                      type=int,
                      help='Sets the limit of test batch to run in a single '
                      'process.')
  # --test-launcher-filter-file is specified relative to --output-directory,
  # so specifying type=os.path.* will break it.
  parser.add_argument('--test-launcher-filter-file',
                      help='Pass filter file through to target process.')
  parser.add_argument('--test-launcher-jobs',
                      type=int,
                      help='Sets the number of parallel test jobs.')
  parser.add_argument('--test-launcher-summary-output',
                      '--test_launcher_summary_output',
                      help='Where the test launcher will output its json.')
  parser.add_argument('child_args', nargs='*',
                      help='Arguments for the test process.')
  parser.add_argument('-d', '--device', action='store_true', default=False,
                      help='Run on hardware device instead of QEMU.')
  args = parser.parse_args()

  child_args = ['--test-launcher-retry-limit=0']

  if args.single_process_tests:
    child_args.append('--single-process-tests')

  if args.test_launcher_batch_limit:
    child_args.append('--test-launcher-batch-limit=%d' %
                       args.test_launcher_batch_limit)

  test_concurrency = args.test_launcher_jobs \
      if args.test_launcher_jobs else DEFAULT_TEST_CONCURRENCY
  child_args.append('--test-launcher-jobs=%d' % test_concurrency)

  if args.gtest_filter:
    child_args.append('--gtest_filter=' + args.gtest_filter)
  if args.gtest_repeat:
    child_args.append('--gtest_repeat=' + args.gtest_repeat)
    child_args.append('--test-launcher-timeout=-1')
  if args.gtest_break_on_failure:
    child_args.append('--gtest_break_on_failure')
  if args.child_args:
    child_args.extend(args.child_args)

  runtime_deps = ReadRuntimeDeps(args.runtime_deps_path, args.output_directory)

  spawning_server = None

  # Start test server spawner for tests that need it.
  if args.enable_test_server:
    spawning_server = chrome_test_server_spawner.SpawningServer(
        0, PortForwarderNoop(), test_concurrency)
    spawning_server.Start()

    # Generate test server config.
    config_file = tempfile.NamedTemporaryFile()
    config_file.write(json.dumps({
      'name': 'testserver',
      'address': HOST_IP_ADDRESS,
      'spawner_url_base': 'http://%s:%d' %
          (HOST_IP_ADDRESS, spawning_server.server_port)
    }))
    config_file.flush()
    runtime_deps.append(('net-test-server-config', config_file.name))

  if args.test_launcher_filter_file:
    # Bundle the filter file in the runtime deps and compose the command-line
    # flag which references it.
    test_launcher_filter_file = os.path.normpath(
        os.path.join(args.output_directory, args.test_launcher_filter_file))
    runtime_deps.append(('test_filter_file', test_launcher_filter_file))
    child_args.append('--test-launcher-filter-file=/system/test_filter_file')

  try:
    bootfs = BuildBootfs(
        args.output_directory, runtime_deps, args.exe_name, child_args,
        args.dry_run, summary_output=args.test_launcher_summary_output,
        power_off=not args.device, target_cpu=args.target_cpu)
    if not bootfs:
      return 2

    return RunFuchsia(bootfs, args.device, args.dry_run,
                      args.test_launcher_summary_output)
  finally:
    # Stop the spawner to make sure it doesn't leave testserver running, in
    # case some tests failed.
    if spawning_server:
      spawning_server.Stop()


if __name__ == '__main__':
  sys.exit(main())
