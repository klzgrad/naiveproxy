# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import boot_data
import common
import json
import logging
import os
import remote_cmd
import shutil
import subprocess
import sys
import tempfile
import time


_SHUTDOWN_CMD = ['dm', 'poweroff']
_ATTACH_MAX_RETRIES = 10
_ATTACH_RETRY_INTERVAL = 1

_PM = os.path.join(common.SDK_ROOT, 'tools', 'pm')
_REPO_NAME = 'chrome_runner'

# Amount of time to wait for Amber to complete package installation, as a
# mitigation against hangs due to amber/network-related failures.
_INSTALL_TIMEOUT_SECS = 5 * 60


def _GetPackageInfo(package_path):
  """Returns a tuple with the name and version of a package."""

  # Query the metadata file which resides next to the package file.
  package_info = json.load(
      open(os.path.join(os.path.dirname(package_path), 'package')))
  return (package_info['name'], package_info['version'])


def _PublishPackage(tuf_root, package_path):
  """Publishes a combined FAR package to a TUF repository root."""

  subprocess.check_call(
      [_PM, 'publish', '-a', '-f', package_path, '-r', tuf_root, '-vt', '-v'],
      stderr=subprocess.STDOUT)


class _MapRemoteDataPathForPackage:
  """Callable object which remaps /data paths to their package-specific
     locations."""

  def __init__(self, package_name, package_version):
    self.data_path = '/data/r/sys/fuchsia.com:{0}:{1}#meta:{0}.cmx'.format(
        package_name, package_version)

  def __call__(self, path):
    if path[:5] == '/data':
      return self.data_path + path[5:]
    return path


class FuchsiaTargetException(Exception):
  def __init__(self, message):
    super(FuchsiaTargetException, self).__init__(message)


class Target(object):
  """Base class representing a Fuchsia deployment target."""

  def __init__(self, output_dir, target_cpu):
    self._output_dir = output_dir
    self._started = False
    self._dry_run = False
    self._target_cpu = target_cpu
    self._command_runner = None

  # Functions used by the Python context manager for teardown.
  def __enter__(self):
    return self
  def __exit__(self, exc_type, exc_val, exc_tb):
    return self

  def Start(self):
    """Handles the instantiation and connection process for the Fuchsia
    target instance."""

    pass

  def IsStarted(self):
    """Returns True if the Fuchsia target instance is ready to accept
    commands."""

    return self._started

  def IsNewInstance(self):
    """Returns True if the connected target instance is newly provisioned."""

    return True

  def GetCommandRunner(self):
    """Returns CommandRunner that can be used to execute commands on the
    target. Most clients should prefer RunCommandPiped() and RunCommand()."""

    self._AssertIsStarted()

    if self._command_runner == None:
      host, port = self._GetEndpoint()
      self._command_runner = \
          remote_cmd.CommandRunner(self._GetSshConfigPath(), host, port)

    return self._command_runner

  def RunCommandPiped(self, command, **kwargs):
    """Starts a remote command and immediately returns a Popen object for the
    command. The caller may interact with the streams, inspect the status code,
    wait on command termination, etc.

    command: A list of strings representing the command and arguments.
    kwargs: A dictionary of parameters to be passed to subprocess.Popen().
            The parameters can be used to override stdin and stdout, for
            example.

    Returns: a Popen object.

    Note: method does not block."""

    logging.debug('running (non-blocking) \'%s\'.' % ' '.join(command))
    return self.GetCommandRunner().RunCommandPiped(command, **kwargs)

  def RunCommand(self, command, silent=False, timeout_secs=None):
    """Executes a remote command and waits for it to finish executing.

    Returns the exit code of the command."""

    logging.debug('running \'%s\'.' % ' '.join(command))
    return self.GetCommandRunner().RunCommand(command, silent,
                                              timeout_secs=timeout_secs)

  def EnsurePackageDataDirectoryExists(self, package_name):
    """Ensures that the specified package's isolated /data directory exists."""
    return self.RunCommand(
      ['mkdir','-p',_MapRemoteDataPathForPackage(package_name, 0)('/data')])

  def PutFile(self, source, dest, recursive=False, for_package=None):
    """Copies a file from the local filesystem to the target filesystem.

    source: The path of the file being copied.
    dest: The path on the remote filesystem which will be copied to.
    recursive: If true, performs a recursive copy.
    for_package: If specified, /data in the |dest| is mapped to the package's
                 isolated /data location.
    """

    assert type(source) is str
    self.PutFiles([source], dest, recursive, for_package)

  def PutFiles(self, sources, dest, recursive=False, for_package=None):
    """Copies files from the local filesystem to the target filesystem.

    sources: List of local file paths to copy from, or a single path.
    dest: The path on the remote filesystem which will be copied to.
    recursive: If true, performs a recursive copy.
    for_package: If specified, /data in the |dest| is mapped to the package's
                 isolated /data location.
    """

    assert type(sources) is tuple or type(sources) is list
    if for_package:
      self.EnsurePackageDataDirectoryExists(for_package)
      dest = _MapRemoteDataPathForPackage(for_package, 0)(dest)
    logging.debug('copy local:%s => remote:%s' % (sources, dest))
    self.GetCommandRunner().RunScp(sources, dest, remote_cmd.COPY_TO_TARGET,
                                   recursive)

  def GetFile(self, source, dest, for_package=None):
    """Copies a file from the target filesystem to the local filesystem.

    source: The path of the file being copied.
    dest: The path on the local filesystem which will be copied to.
    for_package: If specified, /data in paths in |sources| is mapped to the
                 package's isolated /data location.
    """
    assert type(source) is str
    self.GetFiles([source], dest, for_package)

  def GetFiles(self, sources, dest, for_package=None):
    """Copies files from the target filesystem to the local filesystem.

    sources: List of remote file paths to copy.
    dest: The path on the local filesystem which will be copied to.
    for_package: If specified, /data in paths in |sources| is mapped to the
                 package's isolated /data location.
    """
    assert type(sources) is tuple or type(sources) is list
    self._AssertIsStarted()
    if for_package:
      sources = map(_MapRemoteDataPathForPackage(for_package, 0), sources)
    logging.debug('copy remote:%s => local:%s' % (sources, dest))
    return self.GetCommandRunner().RunScp(sources, dest,
                                          remote_cmd.COPY_FROM_TARGET)

  def _GetEndpoint(self):
    """Returns a (host, port) tuple for the SSH connection to the target."""
    raise NotImplementedError

  def _GetTargetSdkArch(self):
    """Returns the Fuchsia SDK architecture name for the target CPU."""
    if self._target_cpu == 'arm64' or self._target_cpu == 'x64':
      return self._target_cpu
    raise FuchsiaTargetException('Unknown target_cpu:' + self._target_cpu)

  def _AssertIsStarted(self):
    assert self.IsStarted()

  def _WaitUntilReady(self, retries=_ATTACH_MAX_RETRIES):
    logging.info('Connecting to Fuchsia using SSH.')

    for retry in xrange(retries + 1):
      host, port = self._GetEndpoint()
      runner = remote_cmd.CommandRunner(self._GetSshConfigPath(), host, port)
      if runner.RunCommand(['true'], True) == 0:
        logging.info('Connected!')
        self._started = True
        return True
      time.sleep(_ATTACH_RETRY_INTERVAL)

    logging.error('Timeout limit reached.')

    raise FuchsiaTargetException('Couldn\'t connect using SSH.')

  def _GetSshConfigPath(self, path):
    raise NotImplementedError

  # TODO: remove this once all instances of architecture names have been
  # converted to the new naming pattern.
  def _GetTargetSdkLegacyArch(self):
    """Returns the Fuchsia SDK architecture name for the target CPU."""
    if self._target_cpu == 'arm64':
      return 'aarch64'
    elif self._target_cpu == 'x64':
      return 'x86_64'
    raise Exception('Unknown target_cpu %s:' % self._target_cpu)


  def InstallPackage(self, package_path, package_name, package_deps):
    """Installs a package and it's dependencies on the device. If the package is
    already installed then it will be updated to the new version.

    package_path: Path to the .far file to be installed.
    package_name: Package name.
    package_deps: List of .far files with the packages that the main package
                  depends on. These packages are installed or updated as well.
    """
    try:
      tuf_root = tempfile.mkdtemp()
      pm_serve_task = None

      # Publish all packages to the serving TUF repository under |tuf_root|.
      subprocess.check_call([_PM, 'newrepo', '-repo', tuf_root])
      all_packages = [package_path] + package_deps
      for next_package_path in all_packages:
        _PublishPackage(tuf_root, next_package_path)

      # Serve the |tuf_root| using 'pm serve' and configure the target to pull
      # from it.
      serve_port = common.GetAvailableTcpPort()
      pm_serve_task = subprocess.Popen(
          [_PM, 'serve', '-d', os.path.join(tuf_root, 'repository'), '-l',
           ':%d' % serve_port, '-q'])
      remote_port = common.ConnectPortForwardingTask(self, serve_port, 0)
      self._RegisterAmberRepository(tuf_root, remote_port)

      # Install all packages.
      for next_package_path in all_packages:
        install_package_name, package_version = \
            _GetPackageInfo(next_package_path)
        logging.info('Installing %s version %s.' %
                     (install_package_name, package_version))
        return_code = self.RunCommand(['amberctl', 'get_up', '-n',
                                       install_package_name, '-v',
                                       package_version],
                                       timeout_secs=_INSTALL_TIMEOUT_SECS)
        if return_code != 0:
          raise Exception('Error while installing %s.' % install_package_name)

    finally:
      self._UnregisterAmberRepository()
      if pm_serve_task:
        pm_serve_task.kill()
      shutil.rmtree(tuf_root)


  def _RegisterAmberRepository(self, tuf_repo, remote_port):
    """Configures a device to use a local TUF repository as an installation
    source for packages.
    |tuf_repo|: The host filesystem path to the TUF repository.
    |remote_port|: The reverse-forwarded port used to connect to instance of
                   `pm serve` that is serving the contents of |tuf_repo|."""

    # Extract the public signing key for inclusion in the config file.
    root_keys = []
    root_json_path = os.path.join(tuf_repo, 'repository', 'root.json')
    root_json = json.load(open(root_json_path, 'r'))
    for root_key_id in root_json['signed']['roles']['root']['keyids']:
      root_keys.append({
          'Type': root_json['signed']['keys'][root_key_id]['keytype'],
          'Value': root_json['signed']['keys'][root_key_id]['keyval']['public']
      })

    # "pm serve" can automatically generate a "config.json" file at query time,
    # but the file is unusable because it specifies URLs with port
    # numbers that are unreachable from across the port forwarding boundary.
    # So instead, we generate our own config file with the forwarded port
    # numbers instead.
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
    return_code = self.RunCommand(
        [('amberctl rm_src -n %s; ' +
          'amberctl add_src -f http://127.0.0.1:%d/repo_config.json')
         % (_REPO_NAME, remote_port)])
    if return_code != 0:
      raise Exception('Error code %d when running amberctl.' % return_code)


  def _UnregisterAmberRepository(self):
    """Unregisters the Amber repository."""

    logging.debug('Unregistering Amber repository.')
    self.RunCommand(['amberctl', 'rm_src', '-n', _REPO_NAME])

    # Re-enable 'devhost' repo if it's present. This is useful for devices that
    # were booted with 'fx serve'.
    self.RunCommand(['amberctl', 'enable_src', '-n', 'devhost'], silent=True)
