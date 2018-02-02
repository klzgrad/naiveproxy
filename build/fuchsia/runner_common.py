#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Packages a user.bootfs for a Fuchsia boot image, pulling in the runtime
dependencies of a  binary, and then uses either QEMU from the Fuchsia SDK
to run, or starts the bootserver to allow running on a hardware device."""

import argparse
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import time
import uuid


DIR_SOURCE_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
SDK_ROOT = os.path.join(DIR_SOURCE_ROOT, 'third_party', 'fuchsia-sdk')
QEMU_ROOT = os.path.join(DIR_SOURCE_ROOT, 'third_party',
                         'qemu-' + platform.machine())

# The guest will get 192.168.3.9 from DHCP, while the host will be
# accessible as 192.168.3.2 .
GUEST_NET = '192.168.3.0/24'
GUEST_IP_ADDRESS = '192.168.3.9'
HOST_IP_ADDRESS = '192.168.3.2'
GUEST_MAC_ADDRESS = '52:54:00:63:5e:7b'

# A string used to uniquely identify this invocation of Fuchsia.
INSTANCE_ID = str(uuid.uuid1())

# Signals to the host that the the remote binary has finished executing.
# The UUID reduces the likelihood of the remote end generating the signal
# by coincidence.
ALL_DONE_MESSAGE = '*** RUN FINISHED: %s' % INSTANCE_ID


def _RunAndCheck(dry_run, args):
  if dry_run:
    print 'Run:', ' '.join(args)
    return 0

  try:
    subprocess.check_call(args)
    return 0
  except subprocess.CalledProcessError as e:
    return e.returncode
  finally:
    sys.stdout.flush()
    sys.stderr.flush()


def _IsRunningOnBot():
  return int(os.environ.get('CHROME_HEADLESS', 0)) != 0


def _DumpFile(dry_run, name, description):
  """Prints out the contents of |name| if |dry_run|."""
  if not dry_run:
    return
  print
  print 'Contents of %s (for %s)' % (name, description)
  print '-' * 80
  with open(name) as f:
    sys.stdout.write(f.read())
  print '-' * 80


def _MakeTargetImageName(common_prefix, output_directory, location):
  """Generates the relative path name to be used in the file system image.
  common_prefix: a prefix of both output_directory and location that
                 be removed.
  output_directory: an optional prefix on location that will also be removed.
  location: the file path to relativize.

  .so files will be stored into the lib subdirectory to be able to be found by
  default by the loader.

  Examples:

  >>> _MakeTargetImageName(common_prefix='/work/cr/src',
  ...                      output_directory='/work/cr/src/out/fuch',
  ...                      location='/work/cr/src/base/test/data/xyz.json')
  'base/test/data/xyz.json'

  >>> _MakeTargetImageName(common_prefix='/work/cr/src',
  ...                      output_directory='/work/cr/src/out/fuch',
  ...                      location='/work/cr/src/out/fuch/icudtl.dat')
  'icudtl.dat'

  >>> _MakeTargetImageName(common_prefix='/work/cr/src',
  ...                      output_directory='/work/cr/src/out/fuch',
  ...                      location='/work/cr/src/out/fuch/libbase.so')
  'lib/libbase.so'
  """
  if not common_prefix.endswith(os.sep):
    common_prefix += os.sep
  assert output_directory.startswith(common_prefix)
  output_dir_no_common_prefix = output_directory[len(common_prefix):]
  assert location.startswith(common_prefix)
  loc = location[len(common_prefix):]
  if loc.startswith(output_dir_no_common_prefix):
    loc = loc[len(output_dir_no_common_prefix)+1:]
  # TODO(fuchsia): The requirements for finding/loading .so are in flux, so this
  # ought to be reconsidered at some point. See https://crbug.com/732897.
  if location.endswith('.so'):
    loc = 'lib/' + loc
  return loc


def _ExpandDirectories(file_mapping, mapper):
  """Walks directories listed in |file_mapping| and adds their contents to
  |file_mapping|, using |mapper| to determine the target filename.
  """
  expanded = {}
  for target, source in file_mapping.items():
    if os.path.isdir(source):
      files = [os.path.join(dir_path, filename)
               for dir_path, dir_names, file_names in os.walk(source)
               for filename in file_names]
      for f in files:
        expanded[mapper(f)] = f
    elif os.path.exists(source):
      expanded[target] = source
    else:
      raise Exception('%s does not exist' % source)
  return expanded


def _GetSymbolsMapping(dry_run, file_mapping, output_directory):
  """For each stripped executable or dynamic library in |file_mapping|, looks
  for an unstripped version in [exe|lib].unstripped under |output_directory|.
  Returns a map from target filenames to un-stripped binary, if available, or
  to the run-time binary otherwise."""
  symbols_mapping = {}
  for target, source in file_mapping.iteritems():
    with open(source, 'rb') as f:
      file_tag = f.read(4)
    if file_tag != '\x7fELF':
      continue

    # TODO(wez): Rather than bake-in assumptions about the naming of unstripped
    # binaries, once we have ELF Build-Id values in the stack printout we should
    # just scan the two directories to populate an Id->path mapping.
    binary_name = os.path.basename(source)
    exe_unstripped_path = os.path.join(
        output_directory, 'exe.unstripped', binary_name)
    lib_unstripped_path = os.path.join(
        output_directory, 'lib.unstripped', binary_name)
    if os.path.exists(exe_unstripped_path):
      symbols_mapping[target] = exe_unstripped_path
    elif os.path.exists(lib_unstripped_path):
      symbols_mapping[target] = lib_unstripped_path
    else:
      symbols_mapping[target] = source

    if dry_run:
      print 'Symbols:', binary_name, '->', symbols_mapping[target]

  return symbols_mapping


def _WriteManifest(manifest_file, file_mapping):
  """Writes |file_mapping| to the given |manifest_file| (a file object) in a
  form suitable for consumption by mkbootfs."""
  for target, source in file_mapping.viewitems():
    manifest_file.write('%s=%s\n' % (target, source))


def ReadRuntimeDeps(deps_path, output_directory):
  result = []
  for f in open(deps_path):
    abs_path = os.path.abspath(os.path.join(output_directory, f.strip()));
    target_path = \
        _MakeTargetImageName(DIR_SOURCE_ROOT, output_directory, abs_path)
    result.append((target_path, abs_path))
  return result


def _TargetCpuToArch(target_cpu):
  """Returns the Fuchsia SDK architecture name for the |target_cpu|."""
  if target_cpu == 'arm64':
    return 'aarch64'
  elif target_cpu == 'x64':
    return 'x86_64'
  raise Exception('Unknown target_cpu:' + target_cpu)


def _TargetCpuToSdkBinPath(target_cpu):
  """Returns the path to the kernel & bootfs .bin files for |target_cpu|."""
  return os.path.join(SDK_ROOT, 'target', _TargetCpuToArch(target_cpu))


def AddCommonCommandLineArguments(parser):
  """Adds command line arguments used by all the helper scripts to an
  argparse.ArgumentParser object."""
  parser.add_argument('--exe-name',
                      type=os.path.realpath,
                      help='Name of the the binary executable.')
  parser.add_argument('--output-directory',
                      type=os.path.realpath,
                      help=('Path to the directory in which build files are'
                            ' located (must include build type).'))
  parser.add_argument('--runtime-deps-path',
                      type=os.path.realpath,
                      help='Runtime data dependency file from GN.')
  parser.add_argument('--target-cpu',
                      help='GN target_cpu setting for the build.')


def AddRunnerCommandLineArguments(parser):
  """Adds command line arguments used by the runner scripts to an
  argparse.ArgumentParser object. Includes all the arguments added by
  AddCommonCommandLineArguments()."""
  AddCommonCommandLineArguments(parser)
  parser.add_argument('--bootdata', type=os.path.realpath,
                      help='Path to a bootdata to use instead of the default '
                           'one from the SDK')
  parser.add_argument('--device', '-d', action='store_true', default=False,
                      help='Run on hardware device instead of QEMU.')
  parser.add_argument('--dry-run', '-n', action='store_true', default=False,
                      help='Just print commands, don\'t execute them.')
  parser.add_argument('--kernel', type=os.path.realpath,
                      help='Path to a kernel to use instead of the default '
                           'one from the SDK')
  parser.add_argument('--wait-for-network', action='store_true', default=False,
                      help='Wait for network connectivity before executing '
                           'the test binary.')


class ImageCreationData(object):
  """Grabbag of data needed to build bootfs or archive of binary's dependencies.

  output_directory: Path to the directory in which the build files are located.
  exe_name: The name of the binary executable.
  runtime_deps: A list of file paths on which the given binary depends. This is
                generated by GN, and that file can be read by ReadRuntimeDeps().
  target_cpu: 'arm64' or 'x64'.
  dry_run: Print the commands that would be run, but don't execute them.
  child_args: Arguments to pass to the child process when run on the target by
              the autorun script.
  use_device: Run on device if true, otherwise on QEMU. Also affects timeouts.
  bootdata: Path to a custom bootdata to use, rather than the default one from
            the SDK.
  summary_output: Use --test-launcher-summary-output when running to extra
                  test results to this file.
  shutdown_machine: Reboot or shutdown the machine on completion when using
                    autorun.
  wait_for_network: Block at startup until a successful ping to google.com
                    before running the target binary.
  use_autorun: Create and set up an autorun script that runs the target binary.
  """
  def __init__(self, output_directory, exe_name, runtime_deps, target_cpu,
               dry_run=False, child_args=[], use_device=False, bootdata=None,
               summary_output=None, shutdown_machine=False,
               wait_for_network=False, use_autorun=False):
    self.output_directory = output_directory
    self.exe_name = exe_name
    self.runtime_deps = runtime_deps
    self.target_cpu = target_cpu
    self.dry_run = dry_run
    self.child_args = child_args
    self.use_device = use_device
    self.bootdata = bootdata
    self.summary_output = summary_output
    self.shutdown_machine = shutdown_machine
    self.wait_for_network = wait_for_network
    self.use_autorun = use_autorun


class BootfsData(object):
  """Results from BuildBootfs().

  bootfs: Local path to .bootfs image file.
  symbols_mapping: A dict mapping executables to their unstripped originals.
  target_cpu: GN's target_cpu setting for the image.
  has_autorun: Whether an autorun file was written for /system/cr_autorun.
  """
  def __init__(self, bootfs_name, symbols_mapping, target_cpu, has_autorun):
    self.bootfs = bootfs_name
    self.symbols_mapping = symbols_mapping
    self.target_cpu = target_cpu
    self.has_autorun = has_autorun


def WriteAutorun(bin_name, child_args, summary_output, shutdown_machine,
                 wait_for_network, dry_run, use_device, file_mapping):
  # Generate a script that runs the binaries and shuts down QEMU (if used).
  autorun_file = open(bin_name + '.bootfs_autorun', 'w')
  autorun_file.write('#!/boot/bin/sh\n')

  if _IsRunningOnBot():
    # TODO(scottmg): Passed through for https://crbug.com/755282.
    autorun_file.write('export CHROME_HEADLESS=1\n')

  if wait_for_network:
    # Quietly block until `ping google.com` succeeds.
    autorun_file.write("""echo "Waiting for network connectivity..."
                       until ping -c 1 google.com >/dev/null 2>/dev/null
                       do
                       :
                       done
                       """)

  if summary_output:
    # Unfortunately, devmgr races with this autorun script. This delays long
    # enough so that the block device is discovered before we try to mount it.
    # See https://crbug.com/789473.
    autorun_file.write('msleep 5000\n')
    autorun_file.write('mkdir /volume/results\n')
    autorun_file.write('mount /dev/class/block/000 /volume/results\n')
    child_args.append('--test-launcher-summary-output='
                      '/volume/results/output.json')

  autorun_file.write('echo Executing ' + os.path.basename(bin_name) + ' ' +
                     ' '.join(child_args) + '\n')

  # Due to Fuchsia's object name length limit being small, we cd into /system
  # and set PATH to "." to reduce the length of the main executable path.
  autorun_file.write('cd /system\n')
  autorun_file.write('PATH=. ' + os.path.basename(bin_name))
  for arg in child_args:
    autorun_file.write(' "%s"' % arg);
  autorun_file.write('\n')
  autorun_file.write('echo \"%s\"\n' % ALL_DONE_MESSAGE)

  if shutdown_machine:
    autorun_file.write('echo Sleeping and shutting down...\n')

    # A delay is required to give the guest OS or remote device a chance to
    # flush its output before it terminates.
    if use_device:
      autorun_file.write('msleep 8000\n')
      autorun_file.write('dm reboot\n')
    else:
      autorun_file.write('msleep 3000\n')
      autorun_file.write('dm poweroff\n')


  autorun_file.flush()
  os.chmod(autorun_file.name, 0750)
  _DumpFile(dry_run, autorun_file.name, 'cr_autorun')

  # Add the autorun file, logger file, and target binary to |file_mapping|.
  file_mapping['cr_autorun'] = autorun_file.name
  file_mapping[os.path.basename(bin_name)] = bin_name

def _ConfigureSSH(output_dir):
  """Gets the public/private keypair to use for connecting to Fuchsia's SSH
  services. Generates a new keypair if one doesn't already exist.

  output_dir: The build directory which will contain the generated keys.
  Returns: a tuple (private_key_path, public_key_path)."""

  if not os.path.exists(output_dir):
    os.makedirs(output_dir)

  host_key_path = output_dir + '/ssh_key'
  host_pubkey_path = host_key_path + '.pub'
  id_key_path = output_dir + '/id_ed25519'
  id_pubkey_path = id_key_path + '.pub'

  if not os.path.isfile(host_key_path):
    subprocess.check_call(['ssh-keygen', '-t', 'ed25519', '-h', '-f',
                           host_key_path, '-P', '', '-N', ''],
                          stdout=open(os.devnull))
  if not os.path.isfile(id_key_path):
    subprocess.check_call(['ssh-keygen', '-t', 'ed25519', '-f', id_key_path,
                           '-P', '', '-N', ''], stdout=open(os.devnull))

  print 'SSH private key location: ' + id_key_path
  return [
      ('data/ssh/ssh_host_ed25519_key', host_key_path),
      ('data/ssh/ssh_host_ed25519_key.pub', host_pubkey_path),
      ('data/ssh/authorized_keys', id_pubkey_path)
  ]


def _BuildBootfsManifest(image_creation_data):
  icd = image_creation_data

  icd.runtime_deps.extend(_ConfigureSSH(icd.output_directory + '/gen'))

  # |runtime_deps| already contains (target, source) pairs for the runtime deps,
  # so we can initialize |file_mapping| from it directly.
  file_mapping = dict(icd.runtime_deps)

  if icd.use_autorun:
      WriteAutorun(icd.exe_name, icd.child_args, icd.summary_output,
                   icd.shutdown_machine, icd.wait_for_network, icd.dry_run,
                   icd.use_device, file_mapping)

  # Find the full list of files to add to the bootfs.
  file_mapping = _ExpandDirectories(
      file_mapping,
      lambda x: _MakeTargetImageName(DIR_SOURCE_ROOT, icd.output_directory, x))

  # Determine the locations of unstripped versions of each binary, if any.
  symbols_mapping = _GetSymbolsMapping(
      icd.dry_run, file_mapping, icd.output_directory)

  return file_mapping, symbols_mapping


def BuildBootfs(image_creation_data):
  file_mapping, symbols_mapping = _BuildBootfsManifest(image_creation_data)

  # Write the target, source mappings to a file suitable for bootfs.
  manifest_file = open(image_creation_data.exe_name + '.bootfs_manifest', 'w')
  _WriteManifest(manifest_file, file_mapping)
  manifest_file.flush()
  _DumpFile(image_creation_data.dry_run, manifest_file.name, 'manifest')

  # Run mkbootfs with the manifest to copy the necessary files into the bootfs.
  mkbootfs_path = os.path.join(SDK_ROOT, 'tools', 'mkbootfs')
  bootfs_name = image_creation_data.exe_name + '.bootfs'
  bootdata = image_creation_data.bootdata
  if not bootdata:
    bootdata = os.path.join(
        _TargetCpuToSdkBinPath(image_creation_data.target_cpu), 'bootdata.bin')
  args = [mkbootfs_path, '-o', bootfs_name,
          '--target=boot', bootdata,
          '--target=system', manifest_file.name]
  if _RunAndCheck(image_creation_data.dry_run, args) != 0:
    return None

  return BootfsData(bootfs_name, symbols_mapping,
                    image_creation_data.target_cpu,
                    image_creation_data.use_autorun)


def BuildArchive(image_creation_data, output_name):
  """Creates an archive (.tar.gz) of the given binary and its dependencies,
  storing them into output_name."""
  file_mapping, symbols_mapping = _BuildBootfsManifest(image_creation_data)

  print 'Archiving to', output_name
  tar = tarfile.open(output_name, 'w:gz')
  for archive_name, source_name in file_mapping.iteritems():
    tar.add(source_name, '/system/' + archive_name, recursive=False)


def _SymbolizeEntries(entries):
  filename_re = re.compile(r'at ([-._a-zA-Z0-9/+]+):(\d+)')

  # Use addr2line to symbolize all the |pc_offset|s in |entries| in one go.
  # Entries with no |debug_binary| are also processed here, so that we get
  # consistent output in that case, with the cannot-symbolize case.
  addr2line_output = None
  if entries[0].has_key('debug_binary'):
    addr2line_args = (['addr2line', '-Cipf', '-p',
                      '--exe=' + entries[0]['debug_binary']] +
                      map(lambda entry: entry['pc_offset'], entries))
    addr2line_output = subprocess.check_output(addr2line_args).splitlines()
    assert addr2line_output

  # Collate a set of |(frame_id, result)| pairs from the output lines.
  results = {}
  for entry in entries:
    raw, frame_id = entry['raw'], entry['frame_id']
    prefix = '#%s: ' % frame_id

    if not addr2line_output:
      # Either there was no addr2line output, or too little of it.
      filtered_line = raw
    else:
      output_line = addr2line_output.pop(0)

      # Relativize path to DIR_SOURCE_ROOT if we see a filename.
      def RelativizePath(m):
        relpath = os.path.relpath(os.path.normpath(m.group(1)), DIR_SOURCE_ROOT)
        return 'at ' + relpath + ':' + m.group(2)
      filtered_line = filename_re.sub(RelativizePath, output_line)

      if '??' in filtered_line.split():
        # If symbolization fails just output the raw backtrace.
        filtered_line = raw
      else:
        # Release builds may inline things, resulting in "(inlined by)" lines.
        inlined_by_prefix = " (inlined by)"
        while (addr2line_output and
               addr2line_output[0].startswith(inlined_by_prefix)):
          inlined_by_line = '\n' + (' ' * len(prefix)) + addr2line_output.pop(0)
          filtered_line += filename_re.sub(RelativizePath, inlined_by_line)

    results[entry['frame_id']] = prefix + filtered_line

  return results


def _LookupDebugBinary(entry, file_mapping):
  """Looks up the binary listed in |entry| in the |file_mapping|, and returns
  the corresponding host-side binary's filename, or None."""
  binary = entry['binary']
  if not binary:
    return None

  app_prefix = 'app:'
  if binary.startswith(app_prefix):
    binary = binary[len(app_prefix):]

  # We change directory into /system/ before running the target executable, so
  # all paths are relative to "/system/", and will typically start with "./".
  # Some crashes still uses the full filesystem path, so cope with that as well.
  system_prefix = '/system/'
  cwd_prefix = './'
  if binary.startswith(cwd_prefix):
    binary = binary[len(cwd_prefix):]
  elif binary.startswith(system_prefix):
    binary = binary[len(system_prefix):]
  # Allow any other paths to pass-through; sometimes neither prefix is present.

  if binary in file_mapping:
    return file_mapping[binary]

  # |binary| may be truncated by the crashlogger, so if there is a unique
  # match for the truncated name in |file_mapping|, use that instead.
  matches = filter(lambda x: x.startswith(binary), file_mapping.keys())
  if len(matches) == 1:
    return file_mapping[matches[0]]

  return None


def _SymbolizeBacktrace(backtrace, file_mapping):
  # Group |backtrace| entries according to the associated binary, and locate
  # the path to the debug symbols for that binary, if any.
  batches = {}
  for entry in backtrace:
    debug_binary = _LookupDebugBinary(entry, file_mapping)
    if debug_binary:
      entry['debug_binary'] = debug_binary
    batches.setdefault(debug_binary, []).append(entry)

  # Run _SymbolizeEntries on each batch and collate the results.
  symbolized = {}
  for batch in batches.itervalues():
    symbolized.update(_SymbolizeEntries(batch))

  # Map each backtrace to its symbolized form, by frame-id, and return the list.
  return map(lambda entry: symbolized[entry['frame_id']], backtrace)


def _GetResultsFromImg(dry_run, test_launcher_summary_output):
  """Extract the results .json out of the .minfs image."""
  if os.path.exists(test_launcher_summary_output):
    os.unlink(test_launcher_summary_output)
  img_filename = test_launcher_summary_output + '.minfs'
  _RunAndCheck(dry_run, [os.path.join(SDK_ROOT, 'tools', 'minfs'), img_filename,
                         'cp', '::/output.json', test_launcher_summary_output])


def _HandleOutputFromProcess(process, symbols_mapping):
  # Set up backtrace-parsing regexps.
  fuch_prefix = re.compile(r'^.*> ')
  backtrace_prefix = re.compile(r'bt#(?P<frame_id>\d+): ')

  # Back-trace line matcher/parser assumes that 'pc' is always present, and
  # expects that 'sp' and ('binary','pc_offset') may also be provided.
  backtrace_entry = re.compile(
      r'pc 0(?:x[0-9a-f]+)? ' +
      r'(?:sp 0x[0-9a-f]+ )?' +
      r'(?:\((?P<binary>\S+),(?P<pc_offset>0x[0-9a-f]+)\))?$')

  # A buffer of backtrace entries awaiting symbolization, stored as dicts:
  # raw: The original back-trace line that followed the prefix.
  # frame_id: backtrace frame number (starting at 0).
  # binary: path to executable code corresponding to the current frame.
  # pc_offset: memory offset within the executable.
  backtrace_entries = []

  # Continue processing until we receive the ALL_DONE_MESSAGE or we read EOF,
  # whichever happens first.
  success = False
  while True:
    line = process.stdout.readline().strip()
    if not line:
      break

    if 'SUCCESS: all tests passed.' in line:
      success = True
    elif ALL_DONE_MESSAGE in line:
      break

    # If the line is not from Fuchsia then don't try to process it.
    matched = fuch_prefix.match(line)
    if not matched:
      print line
      continue
    guest_line = line[matched.end():]

    # Look for the back-trace prefix, otherwise just print the line.
    matched = backtrace_prefix.match(guest_line)
    if not matched:
      print line
      continue
    backtrace_line = guest_line[matched.end():]

    # If this was the end of a back-trace then symbolize and print it.
    frame_id = matched.group('frame_id')
    if backtrace_line == 'end':
      if backtrace_entries:
        for processed in _SymbolizeBacktrace(backtrace_entries,
                                             symbols_mapping):
          print processed
      backtrace_entries = []
      continue

    # Otherwise, parse the program-counter offset, etc into |backtrace_entries|.
    matched = backtrace_entry.match(backtrace_line)
    if matched:
      # |binary| and |pc_offset| will be None if not present.
      backtrace_entries.append(
          {'raw': backtrace_line, 'frame_id': frame_id,
           'binary': matched.group('binary'),
           'pc_offset': matched.group('pc_offset')})
    else:
      backtrace_entries.append(
          {'raw': backtrace_line, 'frame_id': frame_id,
           'binary': None, 'pc_offset': None})

  return success


def RunFuchsia(bootfs_data, use_device, kernel_path, dry_run,
               test_launcher_summary_output):
  if not kernel_path:
    # TODO(wez): Parameterize this on the |target_cpu| from GN.
    kernel_path = os.path.join(_TargetCpuToSdkBinPath(bootfs_data.target_cpu),
                               'zircon.bin')

  kernel_args = ['devmgr.epoch=%d' % time.time(),
                 'zircon.nodename=' + INSTANCE_ID]
  if bootfs_data.has_autorun:
    # See https://fuchsia.googlesource.com/zircon/+/master/docs/kernel_cmdline.md#zircon_autorun_system_command.
    kernel_args.append('zircon.autorun.system=/boot/bin/sh+/system/cr_autorun')

  if use_device:
    # Deploy the boot image to the device.
    bootserver_path = os.path.join(SDK_ROOT, 'tools', 'bootserver')
    bootserver_command = [bootserver_path, '-1', kernel_path,
                          bootfs_data.bootfs, '--'] + kernel_args
    _RunAndCheck(dry_run, bootserver_command)

    # Start listening for logging lines.
    process = subprocess.Popen(
        [os.path.join(SDK_ROOT, 'tools', 'loglistener'), INSTANCE_ID],
        stdout=subprocess.PIPE, stdin=open(os.devnull))
  else:
    qemu_path = os.path.join(
        QEMU_ROOT,'bin',
        'qemu-system-' + _TargetCpuToArch(bootfs_data.target_cpu))
    qemu_command = [qemu_path,
        '-m', '2048',
        '-nographic',
        '-kernel', kernel_path,
        '-initrd', bootfs_data.bootfs,
        '-smp', '4',

        # Configure virtual network. It is used in the tests to connect to
        # testserver running on the host.
        '-netdev', 'user,id=net0,net=%s,dhcpstart=%s,host=%s' %
            (GUEST_NET, GUEST_IP_ADDRESS, HOST_IP_ADDRESS),

        # Use stdio for the guest OS only; don't attach the QEMU interactive
        # monitor.
        '-serial', 'stdio',
        '-monitor', 'none',

        # TERM=dumb tells the guest OS to not emit ANSI commands that trigger
        # noisy ANSI spew from the user's terminal emulator.
        '-append', 'TERM=dumb ' + ' '.join(kernel_args)
      ]

    # Configure the machine & CPU to emulate, based on the target architecture.
    # Enable lightweight virtualization (KVM) if the host and guest OS run on
    # the same architecture.
    if bootfs_data.target_cpu == 'arm64':
      qemu_command.extend([
          '-machine','virt',
          '-cpu', 'cortex-a53',
          '-device', 'virtio-net-pci,netdev=net0,mac=' + GUEST_MAC_ADDRESS,
      ])
      if platform.machine() == 'aarch64':
        qemu_command.append('-enable-kvm')
    else:
      qemu_command.extend([
          '-machine', 'q35',
          '-cpu', 'host,migratable=no',
          '-device', 'e1000,netdev=net0,mac=' + GUEST_MAC_ADDRESS,
      ])
      if platform.machine() == 'x86_64':
        qemu_command.append('-enable-kvm')

    if test_launcher_summary_output:
      # Make and mount a 100M minfs formatted image that is used to copy the
      # results json to, for extraction from the target.
      img_filename = test_launcher_summary_output + '.minfs'
      _RunAndCheck(dry_run, ['truncate', '-s100M', img_filename,])
      _RunAndCheck(dry_run, [os.path.join(SDK_ROOT, 'tools', 'minfs'),
                             img_filename, 'mkfs'])
      # Specifically set an AHCI drive, otherwise the drive won't be mountable
      # on ARM64.
      qemu_command.extend(['-drive', 'file=' + img_filename +
                               ',if=none,format=raw,id=resultsdisk',
                           '-device', 'ahci,id=ahci',
                           '-device', 'ide-drive,drive=resultsdisk,bus=ahci.0'])

    if dry_run:
      print 'Run:', ' '.join(qemu_command)
      return 0

    # We pass a separate stdin stream to qemu. Sharing stdin across processes
    # leads to flakiness due to the OS prematurely killing the stream and the
    # Python script panicking and aborting.
    # The precise root cause is still nebulous, but this fix works.
    # See crbug.com/741194.
    process = subprocess.Popen(
        qemu_command, stdout=subprocess.PIPE, stdin=open(os.devnull))

  success = _HandleOutputFromProcess(process,
                                     bootfs_data.symbols_mapping)

  if not use_device:
    process.wait()

  sys.stdout.flush()

  if test_launcher_summary_output:
    _GetResultsFromImg(dry_run, test_launcher_summary_output)

  return 0 if success else 1
