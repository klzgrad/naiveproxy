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
import re
import shutil
import signal
import subprocess
import sys


DIR_SOURCE_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
SDK_ROOT = os.path.join(DIR_SOURCE_ROOT, 'third_party', 'fuchsia-sdk')
SYMBOLIZATION_TIMEOUT_SECS = 10

# The guest will get 192.168.3.9 from DHCP, while the host will be
# accessible as 192.168.3.2 .
GUEST_NET = '192.168.3.0/24'
GUEST_IP_ADDRESS = '192.168.3.9'
HOST_IP_ADDRESS = '192.168.3.2'


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


def _StripBinary(dry_run, bin_path):
  """Creates a stripped copy of the executable at |bin_path| and returns the
  path to the stripped copy."""
  strip_path = bin_path + '.bootfs_stripped'
  _RunAndCheck(dry_run, ['/usr/bin/strip', bin_path, '-o', strip_path])
  if not dry_run and not os.path.exists(strip_path):
    raise Exception('strip did not create output file')
  return strip_path


def _StripBinaries(dry_run, file_mapping, target_cpu):
  """Updates the supplied manifest |file_mapping|, by stripping any executables
  and updating their entries to point to the stripped location. Returns a
  mapping from target executables to their un-stripped paths, for use during
  symbolization."""
  symbols_mapping = {}
  for target, source in file_mapping.iteritems():
    with open(source, 'rb') as f:
      file_tag = f.read(4)
    if file_tag == '\x7fELF':
      symbols_mapping[target] = source
      # TODO(wez): Strip ARM64 binaries as well. See crbug.com/773444.
      if target_cpu == 'x64':
        file_mapping[target] = _StripBinary(dry_run, source)
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


class BootfsData(object):
  """Results from BuildBootfs().

  bootfs: Local path to .bootfs image file.
  symbols_mapping: A dict mapping executables to their unstripped originals.
  target_cpu: GN's target_cpu setting for the image.
  """
  def __init__(self, bootfs_name, symbols_mapping, target_cpu):
    self.bootfs = bootfs_name
    self.symbols_mapping = symbols_mapping
    self.target_cpu = target_cpu


def BuildBootfs(output_directory, runtime_deps, bin_name, child_args, dry_run,
                summary_output, power_off, target_cpu):
  # |runtime_deps| already contains (target, source) pairs for the runtime deps,
  # so we can initialize |file_mapping| from it directly.
  file_mapping = dict(runtime_deps)

  # Generate a script that runs the binaries and shuts down QEMU (if used).
  autorun_file = open(bin_name + '.bootfs_autorun', 'w')
  autorun_file.write('#!/boot/bin/sh\n')

  if _IsRunningOnBot():
    # TODO(scottmg): Passed through for https://crbug.com/755282.
    autorun_file.write('export CHROME_HEADLESS=1\n')

  if summary_output:
    # Unfortunately, devmgr races with this autorun script. This delays long
    # enough so that the block device is discovered before we try to mount it.
    autorun_file.write('msleep 2000\n')
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

  if power_off:
    autorun_file.write('echo Sleeping and shutting down...\n')
    # A delay is required to give qemu a chance to flush stdout before it
    # terminates.
    autorun_file.write('msleep 3000\n')
    autorun_file.write('dm poweroff\n')

  autorun_file.flush()
  os.chmod(autorun_file.name, 0750)
  _DumpFile(dry_run, autorun_file.name, 'autorun')

  # Add the autorun file, logger file, and target binary to |file_mapping|.
  file_mapping['autorun'] = autorun_file.name
  file_mapping[os.path.basename(bin_name)] = bin_name

  # Find the full list of files to add to the bootfs.
  file_mapping = _ExpandDirectories(
      file_mapping,
      lambda x: _MakeTargetImageName(DIR_SOURCE_ROOT, output_directory, x))

  # Strip any binaries in the file list, and generate a manifest mapping.
  symbols_mapping = _StripBinaries(dry_run, file_mapping, target_cpu)

  # Write the target, source mappings to a file suitable for bootfs.
  manifest_file = open(bin_name + '.bootfs_manifest', 'w')
  _WriteManifest(manifest_file, file_mapping)
  manifest_file.flush()
  _DumpFile(dry_run, manifest_file.name, 'manifest')

  # Run mkbootfs with the manifest to copy the necessary files into the bootfs.
  mkbootfs_path = os.path.join(SDK_ROOT, 'tools', 'mkbootfs')
  bootfs_name = bin_name + '.bootfs'
  if _RunAndCheck(
      dry_run,
      [mkbootfs_path, '-o', bootfs_name,
       # TODO(wez): Parameterize this on the |target_cpu| from GN.
       '--target=boot', os.path.join(
           _TargetCpuToSdkBinPath(target_cpu), 'bootdata.bin'),
       '--target=system', manifest_file.name]) != 0:
    return None

  return BootfsData(bootfs_name, symbols_mapping, target_cpu)


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

      if '??' in filtered_line:
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


def _FindDebugBinary(entry, file_mapping):
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
    debug_binary = _FindDebugBinary(entry, file_mapping)
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


def RunFuchsia(bootfs_data, use_device, dry_run, test_launcher_summary_output):
  # TODO(wez): Parameterize this on the |target_cpu| from GN.
  kernel_path = os.path.join(_TargetCpuToSdkBinPath(bootfs_data.target_cpu),
                             'zircon.bin')

  if use_device:
    # TODO(fuchsia): This doesn't capture stdout as there's no way to do so
    # currently. See https://crbug.com/749242.
    bootserver_path = os.path.join(SDK_ROOT, 'tools', 'bootserver')
    bootserver_command = [bootserver_path, '-1', kernel_path,
                          bootfs_data.bootfs]
    return _RunAndCheck(dry_run, bootserver_command)

  qemu_path = os.path.join(
      SDK_ROOT, 'qemu', 'bin',
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
      '-device', 'e1000,netdev=net0,mac=52:54:00:63:5e:7b',

      # Use stdio for the guest OS only; don't attach the QEMU interactive
      # monitor.
      '-serial', 'stdio',
      '-monitor', 'none',

      # TERM=dumb tells the guest OS to not emit ANSI commands that trigger
      # noisy ANSI spew from the user's terminal emulator.
      '-append', 'TERM=dumb kernel.halt_on_panic=true',
    ]

  # Configure the machine & CPU to emulate, based on the target architecture.
  if bootfs_data.target_cpu == 'arm64':
    qemu_command.extend([
        '-machine','virt',
        '-cpu', 'cortex-a53',
    ])
  else:
    qemu_command.extend([
        '-enable-kvm',
        '-machine', 'q35',
        '-cpu', 'host,migratable=no',
    ])

  if test_launcher_summary_output:
    # Make and mount a 100M minfs formatted image that is used to copy the
    # results json to, for extraction from the target.
    img_filename = test_launcher_summary_output + '.minfs'
    _RunAndCheck(dry_run, ['truncate', '-s100M', img_filename,])
    _RunAndCheck(dry_run, [os.path.join(SDK_ROOT, 'tools', 'minfs'),
                           img_filename, 'mkfs'])
    qemu_command.extend(['-drive', 'file=' + img_filename + ',format=raw'])


  if dry_run:
    print 'Run:', ' '.join(qemu_command)
    return 0

  # Set up backtrace-parsing regexps.
  qemu_prefix = re.compile(r'^.*> ')
  backtrace_prefix = re.compile(r'bt#(?P<frame_id>\d+): ')

  # Back-trace line matcher/parser assumes that 'pc' is always present, and
  # expects that 'sp' and ('binary','pc_offset') may also be provided.
  backtrace_entry = re.compile(
      r'pc 0(?:x[0-9a-f]+)? ' +
      r'(?:sp 0x[0-9a-f]+ )?' +
      r'(?:\((?P<binary>\S+),(?P<pc_offset>0x[0-9a-f]+)\))?$')

  # We pass a separate stdin stream to qemu. Sharing stdin across processes
  # leads to flakiness due to the OS prematurely killing the stream and the
  # Python script panicking and aborting.
  # The precise root cause is still nebulous, but this fix works.
  # See crbug.com/741194.
  qemu_popen = subprocess.Popen(
      qemu_command, stdout=subprocess.PIPE, stdin=open(os.devnull))

  # A buffer of backtrace entries awaiting symbolization, stored as dicts:
  # raw: The original back-trace line that followed the prefix.
  # frame_id: backtrace frame number (starting at 0).
  # binary: path to executable code corresponding to the current frame.
  # pc_offset: memory offset within the executable.
  backtrace_entries = []

  success = False
  while True:
    line = qemu_popen.stdout.readline().strip()
    if not line:
      break
    if 'SUCCESS: all tests passed.' in line:
      success = True

    # If the line is not from QEMU then don't try to process it.
    matched = qemu_prefix.match(line)
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
                                             bootfs_data.symbols_mapping):
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

  qemu_popen.wait()

  if test_launcher_summary_output:
    _GetResultsFromImg(dry_run, test_launcher_summary_output)

  return 0 if success else 1
