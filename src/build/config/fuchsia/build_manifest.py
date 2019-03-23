# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Creates a archive manifest used for Fuchsia package generation."""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile


def ReadDynamicLibDeps(paths):
  """Returns a list of NEEDED libraries read from a binary's ELF header."""

  LIBRARY_RE = re.compile(r'.*\(NEEDED\)\s+Shared library: \[(?P<lib>.*)\]')
  elfinfo = subprocess.check_output(['readelf', '-d'] + paths,
                                    stderr=open(os.devnull, 'w'))
  libs = []
  for line in elfinfo.split('\n'):
    match = LIBRARY_RE.match(line.rstrip())
    if match:
      lib = match.group('lib')

      # libc.so is an alias for ld.so.1 .
      if lib == 'libc.so':
        lib = 'ld.so.1'

      # Skip libzircon.so, as it is supplied by the OS loader.
      if lib != 'libzircon.so':
        libs.append(lib)

  return libs


def ComputeTransitiveLibDeps(executable_path, available_libs):
  """Returns a set representing the library dependencies of |executable_path|,
  the dependencies of its dependencies, and so on.

  A list of candidate library filesystem paths is passed using |available_libs|
  to help with resolving full paths from the short ELF header filenames."""

  # Stack of binaries (libraries, executables) awaiting traversal.
  to_visit = [executable_path]

  # The computed set of visited transitive dependencies.
  deps = set()

  while to_visit:
    deps = deps.union(to_visit)

    # Resolve the full paths for all of |cur_path|'s NEEDED libraries.
    dep_paths = {available_libs[dep]
                 for dep in ReadDynamicLibDeps(list(to_visit))}

    # Add newly discovered dependencies to the pending traversal stack.
    to_visit = dep_paths.difference(deps)

  return deps


def EnumerateDirectoryFiles(path):
  """Returns a flattened list of all files contained under |path|."""

  output = set()
  for dirname, _, files in os.walk(path):
    output = output.union({os.path.join(dirname, f) for f in files})
  return output


def MakePackagePath(file_path, roots):
  """Computes a path for |file_path| that is relative to one of the directory
  paths in |roots|.

  file_path: The absolute file path to relativize.
  roots: A list of absolute directory paths which may serve as a relative root
         for |file_path|. At least one path must contain |file_path|.
         Overlapping roots are permitted; the deepest matching root will be
         chosen.

  Examples:

  >>> MakePackagePath('/foo/bar.txt', ['/foo/'])
  'bar.txt'

  >>> MakePackagePath('/foo/dir/bar.txt', ['/foo/'])
  'dir/bar.txt'

  >>> MakePackagePath('/foo/out/Debug/bar.exe', ['/foo/', '/foo/out/Debug/'])
  'bar.exe'
  """

  # Prevents greedily matching against a shallow path when a deeper, better
  # matching path exists.
  roots.sort(key=len, reverse=True)

  for next_root in roots:
    if not next_root.endswith(os.sep):
      next_root += os.sep

    if file_path.startswith(next_root):
      relative_path = file_path[len(next_root):]

      # Move all dynamic libraries (ending in .so or .so.<number>) to lib/.
      if re.search('.*\.so(\.\d+)?$', file_path):
        relative_path = 'lib/' + os.path.basename(relative_path)

      return relative_path

  raise Exception('Error: no matching root paths found for \'%s\'.' % file_path)


def _GetStrippedPath(bin_path):
  """Finds the stripped version of the binary |bin_path| in the build
  output directory."""

  # Skip the resolution step for binaries that don't have stripped counterparts,
  # like system libraries or other libraries built outside the Chromium build.
  if not '.unstripped' in bin_path:
    return bin_path

  return os.path.normpath(os.path.join(bin_path,
                                       os.path.pardir,
                                       os.path.pardir,
                                       os.path.basename(bin_path)))


def _IsBinary(path):
  """Checks if the file at |path| is an ELF executable by inspecting its FourCC
  header."""

  with open(path, 'rb') as f:
    file_tag = f.read(4)
  return file_tag == '\x7fELF'


def BuildManifest(args):
  with open(args.output_path, 'w') as manifest, \
       open(args.depfile_path, 'w') as depfile:
    # Process the runtime deps file for file paths, recursively walking
    # directories as needed. File paths are stored in absolute form,
    # so that MakePackagePath() may relativize to either the source root or
    # output directory.
    # runtime_deps may contain duplicate paths, so use a set for
    # de-duplication.
    expanded_files = set()
    for next_path in open(args.runtime_deps_file, 'r'):
      next_path = next_path.strip()
      if os.path.isdir(next_path):
        for root, _, files in os.walk(next_path):
          for current_file in files:
            if current_file.startswith('.'):
              continue
            expanded_files.add(os.path.abspath(
                os.path.join(root, current_file)))
      else:
        expanded_files.add(os.path.abspath(next_path))

    # Get set of dist libraries available for dynamic linking.
    dist_libs = set()
    for next_dir in args.dynlib_path:
      dist_libs = dist_libs.union(EnumerateDirectoryFiles(next_dir))

    # Compute the set of dynamic libraries used by the application or its
    # transitive dependencies (dist libs and components), and merge the result
    # with |expanded_files| so that they are included in the manifest.
    #
    # TODO(crbug.com/861931): Make sure that deps of the files in data_deps
    # (binaries and libraries) are included as well.
    expanded_files = expanded_files.union(
       ComputeTransitiveLibDeps(
           args.app_filename,
           {os.path.basename(f): f for f in expanded_files.union(dist_libs)}))

    # Format and write out the manifest contents.
    gen_dir = os.path.join(args.out_dir, "gen")
    app_found = False
    excluded_files_set = set(args.exclude_file)
    for current_file in expanded_files:
      if _IsBinary(current_file):
        current_file = _GetStrippedPath(current_file)

      absolute_file_path = os.path.join(args.out_dir, current_file)
      in_package_path = MakePackagePath(absolute_file_path,
                                        [gen_dir, args.root_dir, args.out_dir])
      if in_package_path == args.app_filename:
        app_found = True

      if in_package_path in excluded_files_set:
        excluded_files_set.remove(in_package_path)
        continue

      # The source path is relativized so that it can be used on multiple
      # environments with differing parent directory structures,
      # e.g. builder bots and swarming clients.
      manifest.write('%s=%s\n' % (in_package_path,
                                  os.path.relpath(current_file, args.out_dir)))

    if len(excluded_files_set) > 0:
      raise Exception('Some files were excluded with --exclude-file, but '
                      'not found in the deps list: %s' %
                          ', '.join(excluded_files_set));

    if not app_found:
      raise Exception('Could not locate executable inside runtime_deps.')

    # Write meta/package manifest file.
    with open(os.path.join(os.path.dirname(args.output_path), 'package'), 'w') \
        as package_json:
      json.dump({'version': '0', 'name': args.app_name}, package_json)
      manifest.write('meta/package=%s\n' %
                   os.path.relpath(package_json.name, args.out_dir))

    # Write component manifest file.
    cmx_file_path = os.path.join(os.path.dirname(args.output_path),
                                 args.app_name + '.cmx')
    with open(cmx_file_path, 'w') as component_manifest_file:
      component_manifest = {
          'program': { 'binary': args.app_filename },
          'sandbox': json.load(open(args.sandbox_policy_path, 'r')),
      }
      json.dump(component_manifest, component_manifest_file)

      manifest.write('meta/%s=%s\n' %
                     (os.path.basename(component_manifest_file.name),
                      os.path.relpath(cmx_file_path, args.out_dir)))

    depfile.write(
        "%s: %s" % (os.path.relpath(args.output_path, args.out_dir),
                    " ".join([os.path.relpath(f, args.out_dir)
                              for f in expanded_files])))
  return 0

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--root-dir', required=True, help='Build root directory')
  parser.add_argument('--out-dir', required=True, help='Build output directory')
  parser.add_argument('--app-name', required=True, help='Package name')
  parser.add_argument('--app-filename', required=True,
      help='Path to the main application binary relative to the output dir.')
  parser.add_argument('--sandbox-policy-path', required=True,
      help='Path to the sandbox policy file relative to the output dir.')
  parser.add_argument('--runtime-deps-file', required=True,
      help='File with the list of runtime dependencies.')
  parser.add_argument('--depfile-path', required=True,
      help='Path to write GN deps file.')
  parser.add_argument('--exclude-file', action='append', default=[],
      help='Package-relative file path to exclude from the package.')
  parser.add_argument('--dynlib-path', action='append', default=[],
      help='Paths for the dynamic libraries relative to the output dir.')
  parser.add_argument('--output-path', required=True, help='Output file path.')

  args = parser.parse_args()

  return BuildManifest(args)

if __name__ == '__main__':
  sys.exit(main())
