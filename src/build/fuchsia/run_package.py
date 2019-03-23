# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Contains a helper function for deploying and executing a packaged
executable on a Target."""

import common
import hashlib
import json
import logging
import multiprocessing
import os
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time
import threading
import uuid

from symbolizer import FilterStream

FAR = os.path.join(common.SDK_ROOT, 'tools', 'far')
PM = os.path.join(common.SDK_ROOT, 'tools', 'pm')
_REPO_NAME = 'chrome_runner'

# Amount of time to wait for the termination of the system log output thread.
_JOIN_TIMEOUT_SECS = 5


def _AttachKernelLogReader(target):
  """Attaches a kernel log reader as a long-running SSH task."""

  logging.info('Attaching kernel logger.')
  return target.RunCommandPiped(['dlog', '-f'], stdin=open(os.devnull, 'r'),
                                stdout=subprocess.PIPE)


def _ReadMergedLines(streams):
  """Creates a generator which merges the buffered line output from |streams|.
  The generator is terminated when the primary (first in sequence) stream
  signals EOF. Absolute output ordering is not guaranteed."""

  assert len(streams) > 0
  streams_by_fd = {}
  primary_fd = streams[0].fileno()
  for s in streams:
    streams_by_fd[s.fileno()] = s

  while primary_fd != None:
    rlist, _, _ = select.select(streams_by_fd, [], [], 0.1)
    for fileno in rlist:
      line = streams_by_fd[fileno].readline()
      if line:
        yield line
      elif fileno == primary_fd:
        primary_fd = None
      else:
        del streams_by_fd[fileno]


def _GetComponentUri(package_name):
  return 'fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx' % (package_name,
                                                       package_name)


def _UnregisterAmberRepository(target):
  """Unregisters the Amber repository from the target."""

  logging.debug('Unregistering Amber repository.')
  target.RunCommand(['amber_ctl', 'rm_src', '-n', _REPO_NAME])

  # Re-enable 'devhost' repo if it's present. This is useful for devices that
  # were booted with 'fx serve'.
  target.RunCommand(['amber_ctl', 'enable_src', '-n', 'devhost'], silent=True)


def _RegisterAmberRepository(target, tuf_repo, remote_port):
  """Configures a device to use a local TUF repository as an installation source
  for packages.
  |target|: The remote device to configure.
  |tuf_repo|: The host filesystem path to the TUF repository.
  |remote_port|: The reverse-forwarded port used to connect to instance of
                 `pm serve` that is serving the contents of |tuf_repo|."""

  # Extract the public signing key for inclusion in the config file.
  root_keys = []
  root_json = json.load(open(os.path.join(tuf_repo, 'repository', 'root.json'),
                             'r'))
  for root_key_id in root_json['signed']['roles']['root']['keyids']:
    root_keys.append({
        'Type': root_json['signed']['keys'][root_key_id]['keytype'],
        'Value': root_json['signed']['keys'][root_key_id]['keyval']['public']
    })

  # "pm serve" can automatically generate a "config.json" file at query time,
  # but the file is unusable because it specifies URLs with port
  # numbers that are unreachable from across the port forwarding boundary.
  # So instead, we generate our own config file with the forwarded port numbers
  # instead.
  config_file = open(os.path.join(tuf_repo, 'repository', 'repo_config.json'),
                     'w')
  json.dump({
      'ID': _REPO_NAME,
      'RepoURL': "http://127.0.0.1:%d" % remote_port,
      'BlobRepoURL': "http://127.0.0.1:%d/blobs" % remote_port,
      'RatePeriod': 10,
      'RootKeys': root_keys,
      'StatusConfig': {
          'Enabled': True
      },
      'Auto': True
  }, config_file)
  config_file.close()

  # Register the repo.
  return_code = target.RunCommand(
      ['amber_ctl', 'add_src', '-f',
       'http://127.0.0.1:%d/repo_config.json' % remote_port])
  if return_code != 0:
    raise Exception('Error code %d when running amber_ctl.' % return_code)


def _DrainStreamToStdout(stream, quit_event):
  """Outputs the contents of |stream| until |quit_event| is set."""

  while not quit_event.is_set():
    rlist, _, _ = select.select([ stream ], [], [], 0.1)
    if rlist:
      line = rlist[0].readline()
      if not line:
        return
      print line.rstrip()


class RunPackageArgs:
  """RunPackage() configuration arguments structure.

  install_only: If set, skips the package execution step.
  symbolizer_config: A newline delimited list of source files contained
      in the package. Omitting this parameter will disable symbolization.
  system_logging: If set, connects a system log reader to the target.
  target_staging_path: Path to which package FARs will be staged, during
      installation. Defaults to staging into '/data'.
  """
  def __init__(self):
    self.install_only = False
    self.symbolizer_config = None
    self.system_logging = False
    self.target_staging_path = '/data'

  @staticmethod
  def FromCommonArgs(args):
    run_package_args = RunPackageArgs()
    run_package_args.install_only = args.install_only
    run_package_args.symbolizer_config = args.package_manifest
    run_package_args.system_logging = args.include_system_logs
    run_package_args.target_staging_path = args.target_staging_path
    return run_package_args


def GetPackageInfo(package_path):
  """Returns a tuple with the name and version of a package."""

  # Query the metadata file which resides next to the package file.
  package_info = json.load(
      open(os.path.join(os.path.dirname(package_path), 'package')))
  return (package_info['name'], package_info['version'])


def PublishPackage(tuf_root, package_path):
  """Publishes a combined FAR package to a TUF repository root."""

  subprocess.check_call(
      [PM, 'publish', '-a', '-f', package_path, '-r', tuf_root, '-vt', '-v'],
      stderr=subprocess.STDOUT)


def RunPackage(output_dir, target, package_path, package_name, package_deps,
               package_args, args):
  """Copies the Fuchsia package at |package_path| to the target,
  executes it with |package_args|, and symbolizes its output.

  output_dir: The path containing the build output files.
  target: The deployment Target object that will run the package.
  package_path: The path to the .far package file.
  package_name: The name of app specified by package metadata.
  package_args: The arguments which will be passed to the Fuchsia process.
  args: Structure of arguments to configure how the package will be run.

  Returns the exit code of the remote package process."""


  system_logger = (
      _AttachKernelLogReader(target) if args.system_logging else None)
  try:
    if system_logger:
      # Spin up a thread to asynchronously dump the system log to stdout
      # for easier diagnoses of early, pre-execution failures.
      log_output_quit_event = multiprocessing.Event()
      log_output_thread = threading.Thread(
          target=lambda: _DrainStreamToStdout(system_logger.stdout,
                                              log_output_quit_event))
      log_output_thread.daemon = True
      log_output_thread.start()

    tuf_root = tempfile.mkdtemp()
    pm_serve_task = None

    # Publish all packages to the serving TUF repository under |tuf_root|.
    subprocess.check_call([PM, 'newrepo', '-repo', tuf_root])
    all_packages = [package_path] + package_deps
    for next_package_path in all_packages:
      PublishPackage(tuf_root, next_package_path)

    # Serve the |tuf_root| using 'pm serve' and configure the target to pull
    # from it.
    # TODO(kmarshall): Use -q to suppress pm serve output once blob push
    # is confirmed to be running stably on bots.
    serve_port = common.GetAvailableTcpPort()
    pm_serve_task = subprocess.Popen(
        [PM, 'serve', '-d', os.path.join(tuf_root, 'repository'), '-l',
         ':%d' % serve_port, '-q'])
    remote_port = common.ConnectPortForwardingTask(target, serve_port, 0)
    _RegisterAmberRepository(target, tuf_root, remote_port)

    # Install all packages.
    for next_package_path in all_packages:
      install_package_name, package_version = GetPackageInfo(next_package_path)
      logging.info('Installing %s version %s.' %
                   (install_package_name, package_version))
      return_code = target.RunCommand(['amber_ctl', 'get_up', '-n',
                                       install_package_name, '-v',
                                       package_version])
      if return_code != 0:
        raise Exception('Error while installing %s.' % install_package_name)

    if system_logger:
      log_output_quit_event.set()
      log_output_thread.join(timeout=_JOIN_TIMEOUT_SECS)

    if args.install_only:
      logging.info('Installation complete.')
      return

    logging.info('Running application.')
    command = ['run', _GetComponentUri(package_name)] + package_args
    process = target.RunCommandPiped(command,
                                     stdin=open(os.devnull, 'r'),
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)

    if system_logger:
      task_output = _ReadMergedLines([process.stdout, system_logger.stdout])
    else:
      task_output = process.stdout

    if args.symbolizer_config:
      # Decorate the process output stream with the symbolizer.
      output = FilterStream(task_output, package_name, args.symbolizer_config,
                            output_dir)
    else:
      logging.warn('Symbolization is DISABLED.')
      output = process.stdout

    for next_line in output:
      print next_line.rstrip()

    process.wait()
    if process.returncode == 0:
      logging.info('Process exited normally with status code 0.')
    else:
      # The test runner returns an error status code if *any* tests fail,
      # so we should proceed anyway.
      logging.warning('Process exited with status code %d.' %
                      process.returncode)

  finally:
    if system_logger:
      logging.info('Terminating kernel log reader.')
      log_output_quit_event.set()
      log_output_thread.join()
      system_logger.kill()

    _UnregisterAmberRepository(target)
    if pm_serve_task:
      pm_serve_task.kill()
    shutil.rmtree(tuf_root)

  return process.returncode
