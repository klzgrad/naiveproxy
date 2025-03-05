# Copyright 2013 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import difflib
import hashlib
import itertools
import json
import os
import sys
import zipfile

from util import build_utils
import action_helpers  # build_utils adds //build to sys.path.
import print_python_deps

# When set and a difference is detected, a diff of what changed is printed.
PRINT_EXPLANATIONS = int(os.environ.get('PRINT_BUILD_EXPLANATIONS', 0))

# An escape hatch that causes all targets to be rebuilt.
_FORCE_REBUILD = int(os.environ.get('FORCE_REBUILD', 0))


def CallAndWriteDepfileIfStale(on_stale_md5,
                               options,
                               record_path=None,
                               input_paths=None,
                               input_strings=None,
                               output_paths=None,
                               force=False,
                               pass_changes=False,
                               track_subpaths_allowlist=None,
                               depfile_deps=None):
  """Wraps CallAndRecordIfStale() and writes a depfile if applicable.

  Depfiles are automatically added to output_paths when present in the |options|
  argument. They are then created after |on_stale_md5| is called.

  By default, only python dependencies are added to the depfile. If there are
  other input paths that are not captured by GN deps, then they should be listed
  in depfile_deps. It's important to write paths to the depfile that are already
  captured by GN deps since GN args can cause GN deps to change, and such
  changes are not immediately reflected in depfiles (http://crbug.com/589311).
  """
  if not output_paths:
    raise Exception('At least one output_path must be specified.')
  input_paths = list(input_paths or [])
  input_strings = list(input_strings or [])
  output_paths = list(output_paths or [])

  input_paths += print_python_deps.ComputePythonDependencies()

  CallAndRecordIfStale(
      on_stale_md5,
      record_path=record_path,
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=output_paths,
      force=force,
      pass_changes=pass_changes,
      track_subpaths_allowlist=track_subpaths_allowlist)

  # Write depfile even when inputs have not changed to ensure build correctness
  # on bots that build with & without patch, and the patch changes the depfile
  # location.
  if hasattr(options, 'depfile') and options.depfile:
    action_helpers.write_depfile(options.depfile, output_paths[0], depfile_deps)


def CallAndRecordIfStale(function,
                         record_path=None,
                         input_paths=None,
                         input_strings=None,
                         output_paths=None,
                         force=False,
                         pass_changes=False,
                         track_subpaths_allowlist=None):
  """Calls function if outputs are stale.

  Outputs are considered stale if:
  - any output_paths are missing, or
  - the contents of any file within input_paths has changed, or
  - the contents of input_strings has changed.

  To debug which files are out-of-date, set the environment variable:
      PRINT_MD5_DIFFS=1

  Args:
    function: The function to call.
    record_path: Path to record metadata.
      Defaults to output_paths[0] + '.md5.stamp'
    input_paths: List of paths to calcualte an md5 sum on.
    input_strings: List of strings to record verbatim.
    output_paths: List of output paths.
    force: Whether to treat outputs as missing regardless of whether they
      actually are.
    pass_changes: Whether to pass a Changes instance to |function|.
    track_subpaths_allowlist: Relevant only when pass_changes=True. List of .zip
      files from |input_paths| to make subpath information available for.
  """
  assert record_path or output_paths
  input_paths = input_paths or []
  input_strings = input_strings or []
  output_paths = output_paths or []
  record_path = record_path or output_paths[0] + '.md5.stamp'

  assert record_path.endswith('.stamp'), (
      'record paths must end in \'.stamp\' so that they are easy to find '
      'and delete')

  new_metadata = _Metadata(track_entries=pass_changes or PRINT_EXPLANATIONS)
  new_metadata.AddStrings(input_strings)

  zip_allowlist = set(track_subpaths_allowlist or [])
  for path in input_paths:
    # It's faster to md5 an entire zip file than it is to just locate & hash
    # its central directory (which is what this used to do).
    if path in zip_allowlist:
      entries = _ExtractZipEntries(path)
      new_metadata.AddZipFile(path, entries)
    else:
      new_metadata.AddFile(path, _ComputeTagForPath(path))

  old_metadata = None
  force = force or _FORCE_REBUILD
  missing_outputs = [x for x in output_paths if force or not os.path.exists(x)]
  too_new = []
  # When outputs are missing, don't bother gathering change information.
  if not missing_outputs and os.path.exists(record_path):
    record_mtime = os.path.getmtime(record_path)
    # Outputs newer than the change information must have been modified outside
    # of the build, and should be considered stale.
    too_new = [x for x in output_paths if os.path.getmtime(x) > record_mtime]
    if not too_new:
      with open(record_path, 'r') as jsonfile:
        try:
          old_metadata = _Metadata.FromFile(jsonfile)
        except:  # pylint: disable=bare-except
          pass  # Not yet using new file format.

  changes = Changes(old_metadata, new_metadata, force, missing_outputs, too_new)
  if not changes.HasChanges():
    return

  if PRINT_EXPLANATIONS:
    print('=' * 80)
    print('Target is stale: %s' % record_path)
    print(changes.DescribeDifference())
    print('=' * 80)

  args = (changes,) if pass_changes else ()
  function(*args)

  with open(record_path, 'w') as f:
    new_metadata.ToFile(f)


class Changes:
  """Provides and API for querying what changed between runs."""

  def __init__(self, old_metadata, new_metadata, force, missing_outputs,
               too_new):
    self.old_metadata = old_metadata
    self.new_metadata = new_metadata
    self.force = force
    self.missing_outputs = missing_outputs
    self.too_new = too_new

  def _GetOldTag(self, path, subpath=None):
    return self.old_metadata and self.old_metadata.GetTag(path, subpath)

  def HasChanges(self):
    """Returns whether any changes exist."""
    return (self.HasStringChanges()
            or self.old_metadata.FilesMd5() != self.new_metadata.FilesMd5())

  def HasStringChanges(self):
    """Returns whether string metadata changed."""
    return (self.force or not self.old_metadata
            or self.old_metadata.StringsMd5() != self.new_metadata.StringsMd5())

  def AddedOrModifiedOnly(self):
    """Returns whether the only changes were from added or modified (sub)files.

    No missing outputs, no removed paths/subpaths.
    """
    if self.HasStringChanges():
      return False
    if any(self.IterRemovedPaths()):
      return False
    for path in self.IterModifiedPaths():
      if any(self.IterRemovedSubpaths(path)):
        return False
    return True

  def IterAllPaths(self):
    """Generator for paths."""
    return self.new_metadata.IterPaths();

  def IterAllSubpaths(self, path):
    """Generator for subpaths."""
    return self.new_metadata.IterSubpaths(path);

  def IterAddedPaths(self):
    """Generator for paths that were added."""
    for path in self.new_metadata.IterPaths():
      if self._GetOldTag(path) is None:
        yield path

  def IterAddedSubpaths(self, path):
    """Generator for paths that were added within the given zip file."""
    for subpath in self.new_metadata.IterSubpaths(path):
      if self._GetOldTag(path, subpath) is None:
        yield subpath

  def IterRemovedPaths(self):
    """Generator for paths that were removed."""
    if self.old_metadata:
      for path in self.old_metadata.IterPaths():
        if self.new_metadata.GetTag(path) is None:
          yield path

  def IterRemovedSubpaths(self, path):
    """Generator for paths that were removed within the given zip file."""
    if self.old_metadata:
      for subpath in self.old_metadata.IterSubpaths(path):
        if self.new_metadata.GetTag(path, subpath) is None:
          yield subpath

  def IterModifiedPaths(self):
    """Generator for paths whose contents have changed."""
    for path in self.new_metadata.IterPaths():
      old_tag = self._GetOldTag(path)
      new_tag = self.new_metadata.GetTag(path)
      if old_tag is not None and old_tag != new_tag:
        yield path

  def IterModifiedSubpaths(self, path):
    """Generator for paths within a zip file whose contents have changed."""
    for subpath in self.new_metadata.IterSubpaths(path):
      old_tag = self._GetOldTag(path, subpath)
      new_tag = self.new_metadata.GetTag(path, subpath)
      if old_tag is not None and old_tag != new_tag:
        yield subpath

  def IterChangedPaths(self):
    """Generator for all changed paths (added/removed/modified)."""
    return itertools.chain(self.IterRemovedPaths(),
                           self.IterModifiedPaths(),
                           self.IterAddedPaths())

  def IterChangedSubpaths(self, path):
    """Generator for paths within a zip that were added/removed/modified."""
    return itertools.chain(self.IterRemovedSubpaths(path),
                           self.IterModifiedSubpaths(path),
                           self.IterAddedSubpaths(path))

  def DescribeDifference(self):
    """Returns a human-readable description of what changed."""
    if self.force:
      return 'force=True'
    if self.missing_outputs:
      return 'Outputs do not exist:\n  ' + '\n  '.join(self.missing_outputs)
    if self.too_new:
      return 'Outputs newer than stamp file:\n  ' + '\n  '.join(self.too_new)
    if self.old_metadata is None:
      return 'Previous stamp file not found.'

    if self.old_metadata.StringsMd5() != self.new_metadata.StringsMd5():
      ndiff = difflib.ndiff(self.old_metadata.GetStrings(),
                            self.new_metadata.GetStrings())
      changed = [s for s in ndiff if not s.startswith(' ')]
      return 'Input strings changed:\n  ' + '\n  '.join(changed)

    if self.old_metadata.FilesMd5() == self.new_metadata.FilesMd5():
      return "There's no difference."

    lines = []
    lines.extend('Added: ' + p for p in self.IterAddedPaths())
    lines.extend('Removed: ' + p for p in self.IterRemovedPaths())
    for path in self.IterModifiedPaths():
      lines.append('Modified: ' + path)
      lines.extend('  -> Subpath added: ' + p
                   for p in self.IterAddedSubpaths(path))
      lines.extend('  -> Subpath removed: ' + p
                   for p in self.IterRemovedSubpaths(path))
      lines.extend('  -> Subpath modified: ' + p
                   for p in self.IterModifiedSubpaths(path))
    if lines:
      return 'Input files changed:\n  ' + '\n  '.join(lines)
    return 'I have no idea what changed (there is a bug).'


class _Metadata:
  """Data model for tracking change metadata.

  Args:
    track_entries: Enables per-file change tracking. Slower, but required for
        Changes functionality.
  """
  # Schema:
  # {
  #   "files-md5": "VALUE",
  #   "strings-md5": "VALUE",
  #   "input-files": [
  #     {
  #       "path": "path.jar",
  #       "tag": "{MD5 of entries}",
  #       "entries": [
  #         { "path": "org/chromium/base/Foo.class", "tag": "{CRC32}" }, ...
  #       ]
  #     }, {
  #       "path": "path.txt",
  #       "tag": "{MD5}",
  #     }
  #   ],
  #   "input-strings": ["a", "b", ...],
  # }
  def __init__(self, track_entries=False):
    self._track_entries = track_entries
    self._files_md5 = None
    self._strings_md5 = None
    self._files = []
    self._strings = []
    # Map of (path, subpath) -> entry. Created upon first call to _GetEntry().
    self._file_map = None

  @classmethod
  def FromFile(cls, fileobj):
    """Returns a _Metadata initialized from a file object."""
    ret = cls()
    obj = json.load(fileobj)
    ret._files_md5 = obj['files-md5']
    ret._strings_md5 = obj['strings-md5']
    ret._files = obj.get('input-files', [])
    ret._strings = obj.get('input-strings', [])
    return ret

  def ToFile(self, fileobj):
    """Serializes metadata to the given file object."""
    obj = {
        'files-md5': self.FilesMd5(),
        'strings-md5': self.StringsMd5(),
    }
    if self._track_entries:
      obj['input-files'] = sorted(self._files, key=lambda e: e['path'])
      obj['input-strings'] = self._strings

    json.dump(obj, fileobj, indent=2)

  def _AssertNotQueried(self):
    assert self._files_md5 is None
    assert self._strings_md5 is None
    assert self._file_map is None

  def AddStrings(self, values):
    self._AssertNotQueried()
    self._strings.extend(str(v) for v in values)

  def AddFile(self, path, tag):
    """Adds metadata for a non-zip file.

    Args:
      path: Path to the file.
      tag: A short string representative of the file contents.
    """
    self._AssertNotQueried()
    self._files.append({
        'path': path,
        'tag': tag,
    })

  def AddZipFile(self, path, entries):
    """Adds metadata for a zip file.

    Args:
      path: Path to the file.
      entries: List of (subpath, tag) tuples for entries within the zip.
    """
    self._AssertNotQueried()
    tag = _ComputeInlineMd5(itertools.chain((e[0] for e in entries),
                                            (e[1] for e in entries)))
    self._files.append({
        'path': path,
        'tag': tag,
        'entries': [{"path": e[0], "tag": e[1]} for e in entries],
    })

  def GetStrings(self):
    """Returns the list of input strings."""
    return self._strings

  def FilesMd5(self):
    """Lazily computes and returns the aggregate md5 of input files."""
    if self._files_md5 is None:
      # Omit paths from md5 since temporary files have random names.
      self._files_md5 = _ComputeInlineMd5(
          self.GetTag(p) for p in sorted(self.IterPaths()))
    return self._files_md5

  def StringsMd5(self):
    """Lazily computes and returns the aggregate md5 of input strings."""
    if self._strings_md5 is None:
      self._strings_md5 = _ComputeInlineMd5(self._strings)
    return self._strings_md5

  def _GetEntry(self, path, subpath=None):
    """Returns the JSON entry for the given path / subpath."""
    if self._file_map is None:
      self._file_map = {}
      for entry in self._files:
        self._file_map[(entry['path'], None)] = entry
        for subentry in entry.get('entries', ()):
          self._file_map[(entry['path'], subentry['path'])] = subentry
    return self._file_map.get((path, subpath))

  def GetTag(self, path, subpath=None):
    """Returns the tag for the given path / subpath."""
    ret = self._GetEntry(path, subpath)
    return ret and ret['tag']

  def IterPaths(self):
    """Returns a generator for all top-level paths."""
    return (e['path'] for e in self._files)

  def IterSubpaths(self, path):
    """Returns a generator for all subpaths in the given zip.

    If the given path is not a zip file or doesn't exist, returns an empty
    iterable.
    """
    outer_entry = self._GetEntry(path)
    if not outer_entry:
      return ()
    subentries = outer_entry.get('entries', [])
    return (entry['path'] for entry in subentries)


def _ComputeTagForPath(path):
  stat = os.stat(path)
  if stat.st_size > 1 * 1024 * 1024:
    # Fallback to mtime for large files so that md5_check does not take too long
    # to run.
    return stat.st_mtime
  md5 = hashlib.md5()
  with open(path, 'rb') as f:
    md5.update(f.read())
  return md5.hexdigest()


def _ComputeInlineMd5(iterable):
  """Computes the md5 of the concatenated parameters."""
  md5 = hashlib.md5()
  for item in iterable:
    md5.update(str(item).encode('ascii'))
  return md5.hexdigest()


def _ExtractZipEntries(path):
  """Returns a list of (path, CRC32) of all files within |path|."""
  entries = []
  with zipfile.ZipFile(path) as zip_file:
    for zip_info in zip_file.infolist():
      # Skip directories and empty files.
      if zip_info.CRC:
        entries.append(
            (zip_info.filename, zip_info.CRC + zip_info.compress_type))
  return entries
