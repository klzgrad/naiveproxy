#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Using colorama.Fore/Back/Style members
# pylint: disable=no-member

import argparse
import collections
import json
import logging
import os
import pipes
import posixpath
import random
import re
import shlex
import sys

import devil_chromium
from devil import devil_env
from devil.android import apk_helper
from devil.android import device_errors
from devil.android import device_utils
from devil.android import flag_changer
from devil.android.sdk import adb_wrapper
from devil.android.sdk import intent
from devil.android.sdk import version_codes
from devil.utils import run_tests_helper

with devil_env.SysPath(os.path.join(os.path.dirname(__file__), '..', '..',
                                    'third_party', 'colorama', 'src')):
  import colorama

from incremental_install import installer
from pylib import constants
from pylib.symbols import deobfuscator


# Matches messages only on pre-L (Dalvik) that are spammy and unimportant.
_DALVIK_IGNORE_PATTERN = re.compile('|'.join([
    r'^Added shared lib',
    r'^Could not find ',
    r'^DexOpt:',
    r'^GC_',
    r'^Late-enabling CheckJNI',
    r'^Link of class',
    r'^No JNI_OnLoad found in',
    r'^Trying to load lib',
    r'^Unable to resolve superclass',
    r'^VFY:',
    r'^WAIT_',
    ]))


def _Colorize(text, style=''):
  return (style
      + text
      + colorama.Style.RESET_ALL)


def _InstallApk(devices, apk, install_dict):
  def install(device):
    if install_dict:
      installer.Install(device, install_dict, apk=apk)
    else:
      device.Install(apk)

  logging.info('Installing %sincremental apk.', '' if install_dict else 'non-')
  device_utils.DeviceUtils.parallel(devices).pMap(install)


def _UninstallApk(devices, install_dict, package_name):
  def uninstall(device):
    if install_dict:
      installer.Uninstall(device, package_name)
    else:
      device.Uninstall(package_name)
  device_utils.DeviceUtils.parallel(devices).pMap(uninstall)


def _LaunchUrl(devices, input_args, device_args_file, url, apk, package_name,
               wait_for_java_debugger):
  if input_args and device_args_file is None:
    raise Exception('This apk does not support any flags.')
  if url:
    # TODO(agrieve): Launch could be changed to require only package name by
    #     parsing "dumpsys package" rather than relying on the apk.
    if not apk:
      raise Exception('Launching with URL is not supported when using '
                      '--package-name. Use --apk-path instead.')
    view_activity = apk.GetViewActivityName()
    if not view_activity:
      raise Exception('APK does not support launching with URLs.')

  def launch(device):
    # Set debug app in order to enable reading command line flags on user
    # builds.
    cmd = ['am', 'set-debug-app', package_name]
    if wait_for_java_debugger:
      # To wait for debugging on a non-primary process:
      #     am set-debug-app org.chromium.chrome:privileged_process0
      cmd[-1:-1] = ['-w']
    # Ignore error since it will fail if apk is not debuggable.
    device.RunShellCommand(cmd, check_return=False)

    # The flags are first updated with input args.
    if device_args_file:
      changer = flag_changer.FlagChanger(device, device_args_file)
      flags = []
      if input_args:
        flags = shlex.split(input_args)
      changer.ReplaceFlags(flags)
    # Then launch the apk.
    if url is None:
      # Simulate app icon click if no url is present.
      cmd = ['monkey', '-p', package_name, '-c',
             'android.intent.category.LAUNCHER', '1']
      device.RunShellCommand(cmd, check_return=True)
    else:
      launch_intent = intent.Intent(action='android.intent.action.VIEW',
                                    activity=view_activity, data=url,
                                    package=package_name)
      device.StartActivity(launch_intent)
  device_utils.DeviceUtils.parallel(devices).pMap(launch)


def _ChangeFlags(devices, input_args, device_args_file):
  if input_args is None:
    _DisplayArgs(devices, device_args_file)
  else:
    flags = shlex.split(input_args)
    def update(device):
      flag_changer.FlagChanger(device, device_args_file).ReplaceFlags(flags)
    device_utils.DeviceUtils.parallel(devices).pMap(update)


def _TargetCpuToTargetArch(target_cpu):
  if target_cpu == 'x64':
    return 'x86_64'
  if target_cpu == 'mipsel':
    return 'mips'
  return target_cpu


def _RunGdb(device, package_name, output_directory, target_cpu, extra_args,
            verbose):
  gdb_script_path = os.path.dirname(__file__) + '/adb_gdb'
  cmd = [
      gdb_script_path,
      '--package-name=%s' % package_name,
      '--output-directory=%s' % output_directory,
      '--adb=%s' % adb_wrapper.AdbWrapper.GetAdbPath(),
      '--device=%s' % device.serial,
      # Use one lib dir per device so that changing between devices does require
      # refetching the device libs.
      '--pull-libs-dir=/tmp/adb-gdb-libs-%s' % device.serial,
  ]
  # Enable verbose output of adb_gdb if it's set for this script.
  if verbose:
    cmd.append('--verbose')
  if target_cpu:
    cmd.append('--target-arch=%s' % _TargetCpuToTargetArch(target_cpu))
  cmd.extend(extra_args)
  logging.warning('Running: %s', ' '.join(pipes.quote(x) for x in cmd))
  print _Colorize(
      'All subsequent output is from adb_gdb script.', colorama.Fore.YELLOW)
  os.execv(gdb_script_path, cmd)


def _PrintPerDeviceOutput(devices, results, single_line=False):
  for d, result in zip(devices, results):
    if not single_line and d is not devices[0]:
      sys.stdout.write('\n')
    sys.stdout.write(
          _Colorize('{} ({}):'.format(d, d.build_description),
                    colorama.Fore.YELLOW))
    sys.stdout.write(' ' if single_line else '\n')
    yield result


def _RunMemUsage(devices, package_name):
  def mem_usage_helper(d):
    ret = []
    proc_map = _GetPackagePids(d, package_name)
    for name in sorted(proc_map.iterkeys()):
      for pid in proc_map[name]:
        ret.append(
            (name, '\n'.join(d.RunShellCommand(['dumpsys', 'meminfo', pid]))))
    return ret

  parallel_devices = device_utils.DeviceUtils.parallel(devices)
  all_results = parallel_devices.pMap(mem_usage_helper).pGet(None)
  for result in _PrintPerDeviceOutput(devices, all_results):
    if not result:
      print 'No processes found.'
    else:
      for name, usage in sorted(result):
        print _Colorize(
            '==== Output of "dumpsys meminfo %s" ====' % name,
            colorama.Fore.GREEN)
        print usage


def _DuHelper(device, path_spec, run_as=None):
  """Runs "du -s -k |path_spec|" on |device| and returns parsed result.

  Args:
    device: A DeviceUtils instance.
    path_spec: The list of paths to run du on. May contain shell expansions
        (will not be escaped).
    run_as: Package name to run as, or None to run as shell user. If not None
        and app is not android:debuggable (run-as fails), then command will be
        run as root.

  Returns:
    A dict of path->size in kb containing all paths in |path_spec| that exist on
    device. Paths that do not exist are silently ignored.
  """
  # Example output for: du -s -k /data/data/org.chromium.chrome/{*,.*}
  # 144     /data/data/org.chromium.chrome/cache
  # 8       /data/data/org.chromium.chrome/files
  # <snip>
  # du: .*: No such file or directory

  # The -d flag works differently across android version, so use -s instead.
  cmd_str = 'du -s -k ' + path_spec
  lines = device.RunShellCommand(cmd_str, run_as=run_as, shell=True,
                                 check_return=False)
  output = '\n'.join(lines)
  # run-as: Package 'com.android.chrome' is not debuggable
  if output.startswith('run-as:'):
    # check_return=False needed for when some paths in path_spec do not exist.
    lines = device.RunShellCommand(cmd_str, as_root=True, shell=True,
                                   check_return=False)
  ret = {}
  try:
    for line in lines:
      # du: .*: No such file or directory
      if line.startswith('du:'):
        continue
      size, subpath = line.split(None, 1)
      ret[subpath] = int(size)
    return ret
  except ValueError:
    logging.error('Failed to parse du output:\n%s', output)


def _RunDiskUsage(devices, package_name, verbose):
  # Measuring dex size is a bit complicated:
  # https://source.android.com/devices/tech/dalvik/jit-compiler
  #
  # For KitKat and below:
  #   dumpsys package contains:
  #     dataDir=/data/data/org.chromium.chrome
  #     codePath=/data/app/org.chromium.chrome-1.apk
  #     resourcePath=/data/app/org.chromium.chrome-1.apk
  #     nativeLibraryPath=/data/app-lib/org.chromium.chrome-1
  #   To measure odex:
  #     ls -l /data/dalvik-cache/data@app@org.chromium.chrome-1.apk@classes.dex
  #
  # For Android L and M (and maybe for N+ system apps):
  #   dumpsys package contains:
  #     codePath=/data/app/org.chromium.chrome-1
  #     resourcePath=/data/app/org.chromium.chrome-1
  #     legacyNativeLibraryDir=/data/app/org.chromium.chrome-1/lib
  #   To measure odex:
  #     # Option 1:
  #  /data/dalvik-cache/arm/data@app@org.chromium.chrome-1@base.apk@classes.dex
  #  /data/dalvik-cache/arm/data@app@org.chromium.chrome-1@base.apk@classes.vdex
  #     ls -l /data/dalvik-cache/profiles/org.chromium.chrome
  #         (these profiles all appear to be 0 bytes)
  #     # Option 2:
  #     ls -l /data/app/org.chromium.chrome-1/oat/arm/base.odex
  #
  # For Android N+:
  #   dumpsys package contains:
  #     dataDir=/data/user/0/org.chromium.chrome
  #     codePath=/data/app/org.chromium.chrome-UuCZ71IE-i5sZgHAkU49_w==
  #     resourcePath=/data/app/org.chromium.chrome-UuCZ71IE-i5sZgHAkU49_w==
  #     legacyNativeLibraryDir=/data/app/org.chromium.chrome-GUID/lib
  #     Instruction Set: arm
  #       path: /data/app/org.chromium.chrome-UuCZ71IE-i5sZgHAkU49_w==/base.apk
  #       status: /data/.../oat/arm/base.odex[status=kOatUpToDate, compilation_f
  #       ilter=quicken]
  #     Instruction Set: arm64
  #       path: /data/app/org.chromium.chrome-UuCZ71IE-i5sZgHAkU49_w==/base.apk
  #       status: /data/.../oat/arm64/base.odex[status=..., compilation_filter=q
  #       uicken]
  #   To measure odex:
  #     ls -l /data/app/.../oat/arm/base.odex
  #     ls -l /data/app/.../oat/arm/base.vdex (optional)
  #   To measure the correct odex size:
  #     cmd package compile -m speed org.chromium.chrome  # For webview
  #     cmd package compile -m speed-profile org.chromium.chrome  # For others
  def disk_usage_helper(d):
    package_output = '\n'.join(d.RunShellCommand(
        ['dumpsys', 'package', package_name], check_return=True))
    # Prints a message but does not return error when apk is not installed.
    if 'Unable to find package:' in package_output:
      return None
    # Ignore system apks.
    idx = package_output.find('Hidden system packages:')
    if idx != -1:
      package_output = package_output[:idx]

    try:
      data_dir = re.search(r'dataDir=(.*)', package_output).group(1)
      code_path = re.search(r'codePath=(.*)', package_output).group(1)
      lib_path = re.search(r'(?:legacyN|n)ativeLibrary(?:Dir|Path)=(.*)',
                           package_output).group(1)
    except AttributeError:
      raise Exception('Error parsing dumpsys output: ' + package_output)
    compilation_filters = set()
    # Match "compilation_filter=value", where a line break can occur at any spot
    # (refer to examples above).
    awful_wrapping = r'\s*'.join('compilation_filter=')
    for m in re.finditer(awful_wrapping + r'([\s\S]+?)[\],]', package_output):
      compilation_filters.add(re.sub(r'\s+', '', m.group(1)))
    compilation_filter = ','.join(sorted(compilation_filters))

    data_dir_sizes = _DuHelper(d, '%s/{*,.*}' % data_dir, run_as=package_name)
    # Measure code_cache separately since it can be large.
    code_cache_sizes = {}
    code_cache_dir = next(
        (k for k in data_dir_sizes if k.endswith('/code_cache')), None)
    if code_cache_dir:
      data_dir_sizes.pop(code_cache_dir)
      code_cache_sizes = _DuHelper(d, '%s/{*,.*}' % code_cache_dir,
                                   run_as=package_name)

    apk_path_spec = code_path
    if not apk_path_spec.endswith('.apk'):
      apk_path_spec += '/*.apk'
    apk_sizes = _DuHelper(d, apk_path_spec)
    if lib_path.endswith('/lib'):
      # Shows architecture subdirectory.
      lib_sizes = _DuHelper(d, '%s/{*,.*}' % lib_path)
    else:
      lib_sizes = _DuHelper(d, lib_path)

    # Look at all possible locations for odex files.
    odex_paths = []
    for apk_path in apk_sizes:
      mangled_apk_path = apk_path[1:].replace('/', '@')
      apk_basename = posixpath.basename(apk_path)[:-4]
      for ext in ('dex', 'odex', 'vdex', 'art'):
        # Easier to check all architectures than to determine active ones.
        for arch in ('arm', 'arm64', 'x86', 'x86_64', 'mips', 'mips64'):
          odex_paths.append(
              '%s/oat/%s/%s.%s' % (code_path, arch, apk_basename, ext))
          # No app could possibly have more than 6 dex files.
          for suffix in ('', '2', '3', '4', '5'):
            odex_paths.append('/data/dalvik-cache/%s/%s@classes%s.%s' % (
                arch, mangled_apk_path, suffix, ext))
            # This path does not have |arch|, so don't repeat it for every arch.
            if arch == 'arm':
              odex_paths.append('/data/dalvik-cache/%s@classes%s.dex' % (
                  mangled_apk_path, suffix))

    odex_sizes = _DuHelper(d, ' '.join(pipes.quote(p) for p in odex_paths))

    return (data_dir_sizes, code_cache_sizes, apk_sizes, lib_sizes, odex_sizes,
            compilation_filter)

  def print_sizes(desc, sizes):
    print '%s: %dkb' % (desc, sum(sizes.itervalues()))
    if verbose:
      for path, size in sorted(sizes.iteritems()):
        print '    %s: %skb' % (path, size)

  parallel_devices = device_utils.DeviceUtils.parallel(devices)
  all_results = parallel_devices.pMap(disk_usage_helper).pGet(None)
  for result in _PrintPerDeviceOutput(devices, all_results):
    if not result:
      print 'APK is not installed.'
      continue

    (data_dir_sizes, code_cache_sizes, apk_sizes, lib_sizes, odex_sizes,
     compilation_filter) = result
    total = sum(sum(sizes.itervalues()) for sizes in result[:-1])

    print_sizes('Apk', apk_sizes)
    print_sizes('App Data (non-code cache)', data_dir_sizes)
    print_sizes('App Data (code cache)', code_cache_sizes)
    print_sizes('Native Libs', lib_sizes)
    show_warning = compilation_filter and 'speed' not in compilation_filter
    compilation_filter = compilation_filter or 'n/a'
    print_sizes('odex (compilation_filter=%s)' % compilation_filter, odex_sizes)
    if show_warning:
      logging.warning('For a more realistic odex size, run:')
      logging.warning('    %s compile-dex [speed|speed-profile]', sys.argv[0])
    print 'Total: %skb (%.1fmb)' % (total, total / 1024.0)


class _LogcatProcessor(object):
  ParsedLine = collections.namedtuple(
      'ParsedLine',
      ['date', 'invokation_time', 'pid', 'tid', 'priority', 'tag', 'message'])

  def __init__(self, device, package_name, deobfuscate=None, verbose=False):
    self._device = device
    self._package_name = package_name
    self._verbose = verbose
    self._deobfuscator = deobfuscate
    self._primary_pid = None
    self._my_pids = set()
    self._seen_pids = set()
    self._UpdateMyPids()

  def _UpdateMyPids(self):
    package_pids = _GetPackagePids(self._device, self._package_name)
    for name, pids in package_pids.iteritems():
      if ':' not in name:
        self._primary_pid = int(pids[0])
      self._my_pids.update(int(p) for p in pids)

  def _GetPidStyle(self, pid, dim=False):
    if pid == self._primary_pid:
      return colorama.Fore.WHITE
    elif pid in self._my_pids:
      # TODO(wnwen): Use one separate persistent color per process, pop LRU
      return colorama.Fore.YELLOW
    elif dim:
      return colorama.Style.DIM
    return ''

  def _GetPriorityStyle(self, priority, dim=False):
    # pylint:disable=no-self-use
    if dim:
      return ''
    style = ''
    if priority == 'E' or priority == 'F':
      style = colorama.Back.RED
    elif priority == 'W':
      style = colorama.Back.YELLOW
    elif priority == 'I':
      style = colorama.Back.GREEN
    elif priority == 'D':
      style = colorama.Back.BLUE
    return style + colorama.Fore.BLACK

  def _ParseLine(self, line):
    tokens = line.split(None, 6)
    date = tokens[0]
    invokation_time = tokens[1]
    pid = int(tokens[2])
    tid = int(tokens[3])
    priority = tokens[4]
    tag = tokens[5]
    if len(tokens) > 6:
      original_message = tokens[6]
    else:  # Empty log message
      original_message = ''
    # Example:
    #   09-19 06:35:51.113  9060  9154 W GCoreFlp: No location...
    #   09-19 06:01:26.174  9060 10617 I Auth    : [ReflectiveChannelBinder]...
    # Parsing "GCoreFlp:" vs "Auth    :", we only want tag to contain the word,
    # and we don't want to keep the colon for the message.
    if tag[-1] == ':':
      tag = tag[:-1]
    else:
      original_message = original_message[2:]
    return self.ParsedLine(
        date, invokation_time, pid, tid, priority, tag, original_message)

  def _PrintParsedLine(self, parsed_line, dim=False):
    tid_style = ''
    # Make the main thread bright.
    if not dim and parsed_line.pid == parsed_line.tid:
      tid_style = colorama.Style.BRIGHT
    pid_style = self._GetPidStyle(parsed_line.pid, dim)
    # We have to pad before adding color as that changes the width of the tag.
    pid_str = _Colorize('{:5}'.format(parsed_line.pid), pid_style)
    tid_str = _Colorize('{:5}'.format(parsed_line.tid), tid_style)
    tag = _Colorize('{:8}'.format(parsed_line.tag),
                    pid_style + ('' if dim else colorama.Style.BRIGHT))
    priority = _Colorize(parsed_line.priority,
                         self._GetPriorityStyle(parsed_line.priority))
    messages = [parsed_line.message]
    if self._deobfuscator:
      messages = self._deobfuscator.TransformLines(messages)
    for message in messages:
      message = _Colorize(message, pid_style)
      sys.stdout.write('{} {} {} {} {} {}: {}\n'.format(
          parsed_line.date, parsed_line.invokation_time, pid_str, tid_str,
          priority, tag, message))

  def ProcessLine(self, line, fast=False):
    if not line or line.startswith('------'):
      return
    log = self._ParseLine(line)
    if log.pid not in self._seen_pids:
      self._seen_pids.add(log.pid)
      if not fast:
        self._UpdateMyPids()

    owned_pid = log.pid in self._my_pids
    if fast and not owned_pid:
      return
    if owned_pid and not self._verbose and log.tag == 'dalvikvm':
      if _DALVIK_IGNORE_PATTERN.match(log.message):
        return

    if owned_pid or self._verbose or (
        log.priority == 'F' or  # Java crash dump
        log.tag == 'ActivityManager' or  # Android system
        log.tag == 'DEBUG'):  # Native crash dump
      self._PrintParsedLine(log, not owned_pid)


def _RunLogcat(device, package_name, mapping_path, verbose):
  deobfuscate = None
  if mapping_path:
    try:
      deobfuscate = deobfuscator.Deobfuscator(mapping_path)
    except OSError:
      sys.stderr.write('Error executing "bin/java_deobfuscate". '
                       'Did you forget to build it?\n')
      sys.exit(1)

  try:
    logcat_processor = _LogcatProcessor(
        device, package_name, deobfuscate, verbose)
    nonce = 'apk_wrappers.py nonce={}'.format(random.random())
    device.RunShellCommand(['log', nonce])
    fast = True
    for line in device.adb.Logcat(logcat_format='threadtime'):
      try:
        logcat_processor.ProcessLine(line, fast)
      except:
        sys.stderr.write('Failed to process line: ' + line)
        raise
      if fast and nonce in line:
        fast = False
  except KeyboardInterrupt:
    pass  # Don't show stack trace upon Ctrl-C
  finally:
    if mapping_path:
      deobfuscate.Close()


def _GetPackagePids(device, package_name):
  return dict((k, v) for k, v in device.GetPids(package_name).iteritems()
              if k == package_name or k.startswith(package_name + ':'))


def _RunPs(devices, package_name):
  parallel_devices = device_utils.DeviceUtils.parallel(devices)
  all_pids = parallel_devices.pMap(
      lambda d: _GetPackagePids(d, package_name)).pGet(None)
  for proc_map in _PrintPerDeviceOutput(devices, all_pids):
    if not proc_map:
      print 'No processes found.'
    else:
      for name, pids in sorted(proc_map.items()):
        print name, ','.join(pids)


def _RunShell(devices, package_name, cmd):
  if cmd:
    parallel_devices = device_utils.DeviceUtils.parallel(devices)
    outputs = parallel_devices.RunShellCommand(
        cmd, run_as=package_name).pGet(None)
    for output in _PrintPerDeviceOutput(devices, outputs):
      for line in output:
        print line
  else:
    adb_path = adb_wrapper.AdbWrapper.GetAdbPath()
    cmd = [adb_path, '-s', devices[0].serial, 'shell']
    # Pre-N devices do not support -t flag.
    if devices[0].build_version_sdk >= version_codes.NOUGAT:
      cmd += ['-t', 'run-as', package_name]
    else:
      print 'Upon entering the shell, run:'
      print 'run-as', package_name
      print
    os.execv(adb_path, cmd)


def _RunCompileDex(devices, package_name, compilation_filter):
  cmd = ['cmd', 'package', 'compile', '-f', '-m', compilation_filter,
         package_name]
  parallel_devices = device_utils.DeviceUtils.parallel(devices)
  outputs = parallel_devices.RunShellCommand(cmd).pGet(None)
  for output in _PrintPerDeviceOutput(devices, outputs):
    for line in output:
      print line


def _GenerateAvailableDevicesMessage(devices):
  devices_obj = device_utils.DeviceUtils.parallel(devices)
  descriptions = devices_obj.pMap(lambda d: d.build_description).pGet(None)
  msg = 'Available devices:\n'
  for d, desc in zip(devices, descriptions):
    msg += '  %s (%s)\n' % (d, desc)
  return msg


# TODO(agrieve):add "--all" in the MultipleDevicesError message and use it here.
def _GenerateMissingAllFlagMessage(devices):
  return ('More than one device available. Use --all to select all devices, ' +
          'or use --device to select a device by serial.\n\n' +
          _GenerateAvailableDevicesMessage(devices))


def _DisplayArgs(devices, device_args_file):
  def flags_helper(d):
    changer = flag_changer.FlagChanger(d, device_args_file)
    return changer.GetCurrentFlags()

  parallel_devices = device_utils.DeviceUtils.parallel(devices)
  outputs = parallel_devices.pMap(flags_helper).pGet(None)
  print 'Existing flags per-device (via /data/local/tmp/%s):' % device_args_file
  for flags in _PrintPerDeviceOutput(devices, outputs, single_line=True):
    quoted_flags = ' '.join(pipes.quote(f) for f in flags)
    print quoted_flags or 'No flags set.'


def _DeviceCachePath(device, output_directory):
  file_name = 'device_cache_%s.json' % device.serial
  return os.path.join(output_directory, file_name)


def _LoadDeviceCaches(devices, output_directory):
  if not output_directory:
    return
  for d in devices:
    cache_path = _DeviceCachePath(d, output_directory)
    if os.path.exists(cache_path):
      logging.debug('Using device cache: %s', cache_path)
      with open(cache_path) as f:
        d.LoadCacheData(f.read())
      # Delete the cached file so that any exceptions cause it to be cleared.
      os.unlink(cache_path)
    else:
      logging.debug('No cache present for device: %s', d)


def _SaveDeviceCaches(devices, output_directory):
  if not output_directory:
    return
  for d in devices:
    cache_path = _DeviceCachePath(d, output_directory)
    with open(cache_path, 'w') as f:
      f.write(d.DumpCacheData())
      logging.info('Wrote device cache: %s', cache_path)


class _Command(object):
  name = None
  description = None
  needs_package_name = False
  needs_output_directory = False
  needs_apk_path = False
  supports_incremental = False
  accepts_command_line_flags = False
  accepts_args = False
  all_devices_by_default = False
  calls_exec = False
  supports_multiple_devices = True

  def __init__(self, from_wrapper_script):
    self._parser = None
    self._from_wrapper_script = from_wrapper_script
    self.args = None
    self.apk_helper = None
    self.install_dict = None
    self.devices = None
    # Do not support incremental install outside the context of wrapper scripts.
    if not from_wrapper_script:
      self.supports_incremental = False

  def _RegisterExtraArgs(self, subp):
    pass

  def RegisterArgs(self, parser):
    subp = parser.add_parser(self.name, help=self.description)
    self._parser = subp
    subp.set_defaults(command=self)
    subp.add_argument('--all',
                      action='store_true',
                      default=self.all_devices_by_default,
                      help='Operate on all connected devices.',)
    subp.add_argument('-d',
                      '--device',
                      action='append',
                      default=[],
                      dest='devices',
                      help='Target device for script to work on. Enter '
                           'multiple times for multiple devices.')
    subp.add_argument('-v',
                      '--verbose',
                      action='count',
                      default=0,
                      dest='verbose_count',
                      help='Verbose level (multiple times for more)')
    group = subp.add_argument_group('%s arguments' % self.name)

    if self.needs_package_name:
      # Always gleaned from apk when using wrapper scripts.
      group.add_argument('--package-name',
          help=argparse.SUPPRESS if self._from_wrapper_script else (
              "App's package name."))

    if self.needs_apk_path or self.needs_package_name:
      # Adding this argument to the subparser would override the set_defaults()
      # value set by on the parent parser (even if None).
      if not self._from_wrapper_script:
        group.add_argument('--apk-path',
                           required=self.needs_apk_path,
                           help='Path to .apk')

    if self.supports_incremental:
      group.add_argument('--incremental',
                          action='store_true',
                          default=False,
                          help='Always install an incremental apk.')
      group.add_argument('--non-incremental',
                          action='store_true',
                          default=False,
                          help='Always install a non-incremental apk.')

    # accepts_command_line_flags and accepts_args are mutually exclusive.
    # argparse will throw if they are both set.
    if self.accepts_command_line_flags:
      group.add_argument(
          '--args', help='Command-line flags. Use = to assign args.')

    if self.accepts_args:
      group.add_argument(
          '--args', help='Extra arguments. Use = to assign args')

    if not self._from_wrapper_script and self.accepts_command_line_flags:
      # Provided by wrapper scripts.
      group.add_argument(
          '--command-line-flags-file',
          help='Name of the command-line flags file')

    self._RegisterExtraArgs(group)

  def ProcessArgs(self, args):
    devices = device_utils.DeviceUtils.HealthyDevices(
        device_arg=args.devices,
        enable_device_files_cache=bool(args.output_directory),
        default_retries=0)
    self.args = args
    self.devices = devices
    # TODO(agrieve): Device cache should not depend on output directory.
    #     Maybe put int /tmp?
    _LoadDeviceCaches(devices, args.output_directory)
    # Ensure these keys always exist. They are set by wrapper scripts, but not
    # always added when not using wrapper scripts.
    args.__dict__.setdefault('apk_path', None)
    args.__dict__.setdefault('incremental_json', None)

    try:
      if len(devices) > 1:
        if not self.supports_multiple_devices:
          self._parser.error(device_errors.MultipleDevicesError(devices))
        if not args.all and not args.devices:
          self._parser.error(_GenerateMissingAllFlagMessage(devices))

      if self.supports_incremental:
        if args.incremental and args.non_incremental:
          self._parser.error('Must use only one of --incremental and '
                             '--non-incremental')
        elif args.non_incremental:
          if not args.apk_path:
            self._parser.error('Apk has not been built.')
          args.incremental_json = None
        elif args.incremental:
          if not args.incremental_json:
            self._parser.error('Incremental apk has not been built.')
          args.apk_path = None

        if args.apk_path and args.incremental_json:
          self._parser.error('Both incremental and non-incremental apks exist. '
                             'Select using --incremental or --non-incremental')

      if self.needs_apk_path or args.apk_path or args.incremental_json:
        if args.incremental_json:
          with open(args.incremental_json) as f:
            install_dict = json.load(f)
          apk_path = os.path.join(args.output_directory,
                                  install_dict['apk_path'])
          if os.path.exists(apk_path):
            self.install_dict = install_dict
            self.apk_helper = apk_helper.ToHelper(
                os.path.join(args.output_directory,
                             self.install_dict['apk_path']))
        if not self.apk_helper and args.apk_path:
          self.apk_helper = apk_helper.ToHelper(args.apk_path)
        if not self.apk_helper:
          self._parser.error(
              'Neither incremental nor non-incremental apk is built.')

      if self.needs_package_name and not args.package_name:
        if self.apk_helper:
          args.package_name = self.apk_helper.GetPackageName()
        elif self._from_wrapper_script:
          self._parser.error(
              'Neither incremental nor non-incremental apk is built.')
        else:
          self._parser.error('One of --package-name or --apk-path is required.')

      # Save cache now if command will not get a chance to afterwards.
      if self.calls_exec:
        _SaveDeviceCaches(devices, args.output_directory)
    except:
      _SaveDeviceCaches(devices, args.output_directory)
      raise


class _DevicesCommand(_Command):
  name = 'devices'
  description = 'Describe attached devices.'
  all_devices_by_default = True

  def Run(self):
    print _GenerateAvailableDevicesMessage(self.devices)


class _InstallCommand(_Command):
  name = 'install'
  description = 'Installs the APK to one or more devices.'
  needs_apk_path = True
  supports_incremental = True

  def Run(self):
    _InstallApk(self.devices, self.apk_helper, self.install_dict)


class _UninstallCommand(_Command):
  name = 'uninstall'
  description = 'Removes the APK to one or more devices.'
  needs_package_name = True

  def Run(self):
    _UninstallApk(self.devices, self.install_dict, self.args.package_name)


class _LaunchCommand(_Command):
  name = 'launch'
  description = ('Sends a launch intent for the APK after first writing the '
                 'command-line flags file.')
  needs_package_name = True
  accepts_command_line_flags = True
  all_devices_by_default = True

  def _RegisterExtraArgs(self, group):
    group.add_argument('-w', '--wait-for-java-debugger', action='store_true',
                       help='Pause execution until debugger attaches. Applies '
                            'only to the main process. To have renderers wait, '
                            'use --args="--renderer-wait-for-java-debugger"')
    group.add_argument('url', nargs='?', help='A URL to launch with.')

  def Run(self):
    _LaunchUrl(self.devices, self.args.args, self.args.command_line_flags_file,
               self.args.url, self.apk_helper, self.args.package_name,
               self.args.wait_for_java_debugger)


class _StopCommand(_Command):
  name = 'stop'
  description = 'Force-stops the app.'
  needs_package_name = True
  all_devices_by_default = True

  def Run(self):
    device_utils.DeviceUtils.parallel(self.devices).ForceStop(
        self.args.package_name)


class _ClearDataCommand(_Command):
  name = 'clear-data'
  descriptions = 'Clears all app data.'
  needs_package_name = True
  all_devices_by_default = True

  def Run(self):
    device_utils.DeviceUtils.parallel(self.devices).ClearApplicationState(
        self.args.package_name)


class _ArgvCommand(_Command):
  name = 'argv'
  description = 'Display and optionally update command-line flags file.'
  needs_package_name = True
  accepts_command_line_flags = True
  all_devices_by_default = True

  def Run(self):
    _ChangeFlags(self.devices, self.args.args,
                 self.args.command_line_flags_file)


class _GdbCommand(_Command):
  name = 'gdb'
  description = 'Runs //build/android/adb_gdb with apk-specific args.'
  needs_package_name = True
  needs_output_directory = True
  accepts_args = True
  calls_exec = True
  supports_multiple_devices = False

  def Run(self):
    extra_args = shlex.split(self.args.args or '')
    _RunGdb(self.devices[0], self.args.package_name, self.args.output_directory,
            self.args.target_cpu, extra_args, bool(self.args.verbose_count))


class _LogcatCommand(_Command):
  name = 'logcat'
  description = 'Runs "adb logcat" filtering to just the current APK processes'
  needs_package_name = True
  supports_multiple_devices = False

  def Run(self):
    mapping = self.args.proguard_mapping_path
    if self.args.no_deobfuscate:
      mapping = None
    _RunLogcat(self.devices[0], self.args.package_name, mapping,
               bool(self.args.verbose_count))

  def _RegisterExtraArgs(self, group):
    if self._from_wrapper_script:
      group.add_argument('--no-deobfuscate', action='store_true',
          help='Disables ProGuard deobfuscation of logcat.')
    else:
      group.set_defaults(no_deobfuscate=False)
      group.add_argument('--proguard-mapping-path',
          help='Path to ProGuard map (enables deobfuscation)')


class _PsCommand(_Command):
  name = 'ps'
  description = 'Show PIDs of any APK processes currently running.'
  needs_package_name = True
  all_devices_by_default = True

  def Run(self):
    _RunPs(self.devices, self.args.package_name)


class _DiskUsageCommand(_Command):
  name = 'disk-usage'
  description = 'Show how much device storage is being consumed by the app.'
  needs_package_name = True
  all_devices_by_default = True

  def Run(self):
    _RunDiskUsage(self.devices, self.args.package_name,
                  bool(self.args.verbose_count))


class _MemUsageCommand(_Command):
  name = 'mem-usage'
  description = 'Show memory usage of currently running APK processes.'
  needs_package_name = True
  all_devices_by_default = True

  def Run(self):
    _RunMemUsage(self.devices, self.args.package_name)


class _ShellCommand(_Command):
  name = 'shell'
  description = ('Same as "adb shell <command>", but runs as the apk\'s uid '
                 '(via run-as). Useful for inspecting the app\'s data '
                 'directory.')
  needs_package_name = True

  @property
  def calls_exec(self):
    return not self.args.cmd

  @property
  def supports_multiple_devices(self):
    return not self.args.cmd

  def _RegisterExtraArgs(self, group):
    group.add_argument(
        'cmd', nargs=argparse.REMAINDER, help='Command to run.')

  def Run(self):
    _RunShell(self.devices, self.args.package_name, self.args.cmd)


class _CompileDexCommand(_Command):
  name = 'compile-dex'
  description = ('Applicable only for Android N+. Forces .odex files to be '
                 'compiled with the given compilation filter. To see existing '
                 'filter, use "disk-usage" command.')
  needs_package_name = True
  all_devices_by_default = True

  def _RegisterExtraArgs(self, group):
    group.add_argument(
        'compilation_filter',
        choices=['verify', 'quicken', 'space-profile', 'space',
                 'speed-profile', 'speed'],
        help='For WebView/Monochrome, use "speed". For other apks, use '
             '"speed-profile".')

  def Run(self):
    _RunCompileDex(self.devices, self.args.package_name,
                   self.args.compilation_filter)


class _RunCommand(_InstallCommand, _LaunchCommand, _LogcatCommand):
  name = 'run'
  description = 'Install, launch, and show logcat (when targeting one device).'
  all_devices_by_default = False
  supports_multiple_devices = True

  def _RegisterExtraArgs(self, group):
    _InstallCommand._RegisterExtraArgs(self, group)
    _LaunchCommand._RegisterExtraArgs(self, group)
    _LogcatCommand._RegisterExtraArgs(self, group)
    group.add_argument('--no-logcat', action='store_true',
                       help='Install and launch, but do not enter logcat.')

  def Run(self):
    logging.warning('Installing...')
    _InstallCommand.Run(self)
    logging.warning('Sending launch intent...')
    _LaunchCommand.Run(self)
    if len(self.devices) == 1 and not self.args.no_logcat:
      logging.warning('Entering logcat...')
      _LogcatCommand.Run(self)


_COMMANDS = [
    _DevicesCommand,
    _InstallCommand,
    _UninstallCommand,
    _LaunchCommand,
    _StopCommand,
    _ClearDataCommand,
    _ArgvCommand,
    _GdbCommand,
    _LogcatCommand,
    _PsCommand,
    _DiskUsageCommand,
    _MemUsageCommand,
    _ShellCommand,
    _CompileDexCommand,
    _RunCommand,
]


def _ParseArgs(parser, from_wrapper_script):
  subparsers = parser.add_subparsers()
  commands = [clazz(from_wrapper_script) for clazz in _COMMANDS]
  for command in commands:
    if from_wrapper_script or not command.needs_output_directory:
      command.RegisterArgs(subparsers)

  # Show extended help when no command is passed.
  argv = sys.argv[1:]
  if not argv:
    argv = ['--help']

  return parser.parse_args(argv)


def _RunInternal(parser, output_directory=None):
  colorama.init()
  parser.set_defaults(output_directory=output_directory)
  from_wrapper_script = bool(output_directory)
  args = _ParseArgs(parser, from_wrapper_script)
  run_tests_helper.SetLogLevel(args.verbose_count)
  args.command.ProcessArgs(args)
  args.command.Run()
  # Incremental install depends on the cache being cleared when uninstalling.
  if args.command.name != 'uninstall':
    _SaveDeviceCaches(args.command.devices, output_directory)


# TODO(agrieve): Remove =None from target_cpu on or after October 2017.
#     It exists only so that stale wrapper scripts continue to work.
def Run(output_directory, apk_path, incremental_json, command_line_flags_file,
        target_cpu, proguard_mapping_path):
  """Entry point for generated wrapper scripts."""
  constants.SetOutputDirectory(output_directory)
  devil_chromium.Initialize(output_directory=output_directory)
  parser = argparse.ArgumentParser()
  exists_or_none = lambda p: p if p and os.path.exists(p) else None
  parser.set_defaults(
      command_line_flags_file=command_line_flags_file,
      target_cpu=target_cpu,
      apk_path=exists_or_none(apk_path),
      incremental_json=exists_or_none(incremental_json),
      proguard_mapping_path=proguard_mapping_path)
  _RunInternal(parser, output_directory=output_directory)


def main():
  devil_chromium.Initialize()
  _RunInternal(argparse.ArgumentParser(), output_directory=None)


if __name__ == '__main__':
  main()
