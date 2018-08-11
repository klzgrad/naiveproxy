#!/usr/bin/env python
#
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import contextlib
import json
import logging
import os
import re
import stat
import subprocess
import sys


CHROMIUM_SRC_PATH = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))

# Use the android test-runner's gtest results support library for generating
# output json ourselves.
sys.path.insert(0, os.path.join(CHROMIUM_SRC_PATH, 'build', 'android'))
from pylib.base import base_test_result
from pylib.results import json_results

CHROMITE_PATH = os.path.abspath(os.path.join(
    CHROMIUM_SRC_PATH, 'third_party', 'chromite'))
CROS_RUN_VM_TEST_PATH = os.path.abspath(os.path.join(
    CHROMITE_PATH, 'bin', 'cros_run_vm_test'))


_FILE_BLACKLIST = [
  re.compile(r'.*build/chromeos.*'),
  re.compile(r'.*build/cros_cache.*'),
  re.compile(r'.*third_party/chromite.*'),
]


def read_runtime_files(runtime_deps_path, outdir):
  if not runtime_deps_path:
    return []

  abs_runtime_deps_path = os.path.abspath(
      os.path.join(outdir, runtime_deps_path))
  with open(abs_runtime_deps_path) as runtime_deps_file:
    files = [l.strip() for l in runtime_deps_file if l]
  rel_file_paths = []
  for f in files:
    rel_file_path = os.path.relpath(
        os.path.abspath(os.path.join(outdir, f)),
        os.getcwd())
    if not any(regex.match(rel_file_path) for regex in _FILE_BLACKLIST):
      rel_file_paths.append(rel_file_path)

  return rel_file_paths


def host_cmd(args):
  if not args.cmd:
    logging.error('Must specify command to run on the host.')
    return 1

  cros_run_vm_test_cmd = [
      CROS_RUN_VM_TEST_PATH,
      '--start',
      '--board', args.board,
      '--cache-dir', args.cros_cache,
  ]
  if args.verbose:
    cros_run_vm_test_cmd.append('--debug')

  cros_run_vm_test_cmd += [
      '--host-cmd',
      '--',
  ] + args.cmd

  logging.info('Running the following command:')
  logging.info(' '.join(cros_run_vm_test_cmd))

  return subprocess.call(
      cros_run_vm_test_cmd, stdout=sys.stdout, stderr=sys.stderr)


def vm_test(args):
  is_sanity_test = args.test_exe == 'cros_vm_sanity_test'

  cros_run_vm_test_cmd = [
      CROS_RUN_VM_TEST_PATH,
      '--start',
      '--board', args.board,
      '--cache-dir', args.cros_cache,
  ]

  # cros_run_vm_test has trouble with relative paths that go up directories, so
  # cd to src/, which should be the root of all data deps.
  os.chdir(CHROMIUM_SRC_PATH)

  runtime_files = read_runtime_files(
      args.runtime_deps_path, args.path_to_outdir)
  # If we're pushing files, we need to set the cwd.
  if runtime_files:
      cros_run_vm_test_cmd.extend(
          ['--cwd', os.path.relpath(args.path_to_outdir, CHROMIUM_SRC_PATH)])
  for f in runtime_files:
    cros_run_vm_test_cmd.extend(['--files', f])

  if args.test_launcher_summary_output and not is_sanity_test:
    result_dir, result_file = os.path.split(args.test_launcher_summary_output)
    # If args.test_launcher_summary_output is a file in cwd, result_dir will be
    # an empty string, so replace it with '.' when this is the case so
    # cros_run_vm_test can correctly handle it.
    if not result_dir:
      result_dir = '.'
    vm_result_file = '/tmp/%s' % result_file
    cros_run_vm_test_cmd += [
      '--results-src', vm_result_file,
      '--results-dest-dir', result_dir,
    ]

  if is_sanity_test:
    # run_cros_vm_test's default behavior when no cmd is specified is the sanity
    # test that's baked into the VM image. This test smoke-checks the system
    # browser, so deploy our locally-built chrome to the VM before testing.
    cros_run_vm_test_cmd += [
        '--deploy',
        '--build-dir', os.path.relpath(args.path_to_outdir, CHROMIUM_SRC_PATH),
    ]
  else:
    cros_run_vm_test_cmd += [
        '--cmd',
        '--',
        './' + args.test_exe,
        '--test-launcher-shard-index=%d' % args.test_launcher_shard_index,
        '--test-launcher-total-shards=%d' % args.test_launcher_total_shards,
    ]

  if args.test_launcher_summary_output and not is_sanity_test:
    cros_run_vm_test_cmd += [
      '--test-launcher-summary-output=%s' % vm_result_file,
    ]

  logging.info('Running the following command:')
  logging.info(' '.join(cros_run_vm_test_cmd))

  # deploy_chrome needs a set of GN args used to build chrome to determine if
  # certain libraries need to be pushed to the VM. It looks for the args via an
  # env var. To trigger the default deploying behavior, give it a dummy set of
  # args.
  # TODO(crbug.com/823996): Make the GN-dependent deps controllable via cmd-line
  # args.
  env_copy = os.environ.copy()
  if not env_copy.get('GN_ARGS'):
    env_copy['GN_ARGS'] = 'is_chromeos = true'
  env_copy['PATH'] = env_copy['PATH'] + ':' + os.path.join(CHROMITE_PATH, 'bin')
  rc = subprocess.call(
      cros_run_vm_test_cmd, stdout=sys.stdout, stderr=sys.stderr, env=env_copy)

  # Create a simple json results file for the sanity test if needed. The results
  # will contain only one test ('cros_vm_sanity_test'), and will either be a
  # PASS or FAIL depending on the return code of cros_run_vm_test above.
  if args.test_launcher_summary_output and is_sanity_test:
    result = (base_test_result.ResultType.FAIL if rc else
                  base_test_result.ResultType.PASS)
    sanity_test_result = base_test_result.BaseTestResult(
        'cros_vm_sanity_test', result)
    run_results = base_test_result.TestRunResults()
    run_results.AddResult(sanity_test_result)
    with open(args.test_launcher_summary_output, 'w') as f:
      json.dump(json_results.GenerateResultsDict([run_results]), f)

  return rc


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--verbose', '-v', action='store_true')
  # Required args.
  parser.add_argument(
      '--board', type=str, required=True, help='Type of CrOS device.')
  subparsers = parser.add_subparsers(dest='test_type')
  # Host-side test args.
  host_cmd_parser = subparsers.add_parser(
      'host-cmd',
      help='Runs a host-side test. Pass the host-side command to run after '
           '"--". Hostname and port for the VM will be 127.0.0.1:9222.')
  host_cmd_parser.set_defaults(func=host_cmd)
  host_cmd_parser.add_argument(
      '--cros-cache', type=str, required=True, help='Path to cros cache.')
  host_cmd_parser.add_argument('cmd', nargs=argparse.REMAINDER)
  # VM-side test args.
  vm_test_parser = subparsers.add_parser(
      'vm-test',
      help='Runs a vm-side gtest.')
  vm_test_parser.set_defaults(func=vm_test)
  vm_test_parser.add_argument(
      '--cros-cache', type=str, required=True, help='Path to cros cache.')
  vm_test_parser.add_argument(
      '--test-exe', type=str, required=True,
      help='Path to test executable to run inside VM. If the value is '
           '"cros_vm_sanity_test", the sanity test that ships with the VM '
           'image runs instead. This test smokes-check the system browser '
           '(eg: loads a simple webpage, executes some javascript), so a '
           'fully-built Chrome binary that can get deployed to the VM is '
           'expected to available in the out-dir.')
  vm_test_parser.add_argument(
      '--path-to-outdir', type=str, required=True,
      help='Path to output directory, all of whose contents will be deployed '
           'to the device.')
  vm_test_parser.add_argument(
      '--runtime-deps-path', type=str,
      help='Runtime data dependency file from GN.')
  vm_test_parser.add_argument(
      '--test-launcher-summary-output', type=str,
      help='When set, will pass the same option down to the test and retrieve '
           'its result file at the specified location.')
  vm_test_parser.add_argument(
      '--test-launcher-shard-index',
      type=int, default=os.environ.get('GTEST_SHARD_INDEX', 0),
      help='Index of the external shard to run.')
  vm_test_parser.add_argument(
      '--test-launcher-total-shards',
      type=int, default=os.environ.get('GTEST_TOTAL_SHARDS', 1),
      help='Total number of external shards.')
  args = parser.parse_args()

  logging.basicConfig(level=logging.DEBUG if args.verbose else logging.WARN)

  if not os.path.exists('/dev/kvm'):
    logging.error('/dev/kvm is missing. Is KVM installed on this machine?')
    return 1
  elif not os.access('/dev/kvm', os.W_OK):
    logging.error(
        '/dev/kvm is not writable as current user. Perhaps you should be root?')
    return 1

  args.cros_cache = os.path.abspath(args.cros_cache)
  return args.func(args)


if __name__ == '__main__':
  sys.exit(main())
