#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''SCons integration for GRIT.
'''

# NOTE: DO NOT IMPORT ANY GRIT STUFF HERE - we import lazily so that
# grit and its dependencies aren't imported until actually needed.

import os
import types

def _IsDebugEnabled():
  return 'GRIT_DEBUG' in os.environ and os.environ['GRIT_DEBUG'] == '1'

def _SourceToFile(source):
  '''Return the path to the source file, given the 'source' argument as provided
  by SCons to the _Builder or _Emitter functions.
  '''
  # Get the filename of the source.  The 'source' parameter can be a string,
  # a "node", or a list of strings or nodes.
  if isinstance(source, types.ListType):
    source = str(source[0])
  else:
    source = str(source)
  return source


def _ParseRcFlags(flags):
  """Gets a mapping of defines.

  Args:
    flags: env['RCFLAGS']; the input defines.

  Returns:
    A tuple of (defines, res_file):
      defines: A mapping of {name: val}
      res_file: None, or the specified res file for static file dependencies.
  """
  from grit import util

  defines = {}
  res_file = None
  # Get the CPP defines from the environment.
  res_flag = '--res_file='
  for flag in flags:
    if flag.startswith(res_flag):
      res_file = flag[len(res_flag):]
      continue
    if flag.startswith('/D'):
      flag = flag[2:]
    name, val = util.ParseDefine(flag)
    # Only apply to first instance of a given define
    if name not in defines:
      defines[name] = val
  return (defines, res_file)


def _Builder(target, source, env):
  print _SourceToFile(source)

  from grit import grit_runner
  from grit.tool import build
  options = grit_runner.Options()
  # This sets options to default values
  options.ReadOptions([])
  options.input = _SourceToFile(source)

  # TODO(joi) Check if we can get the 'verbose' option from the environment.

  builder = build.RcBuilder(defines=_ParseRcFlags(env['RCFLAGS'])[0])

  # To ensure that our output files match what we promised SCons, we
  # use the list of targets provided by SCons and update the file paths in
  # our .grd input file with the targets.
  builder.scons_targets = [str(t) for t in target]
  builder.Run(options, [])
  return None  # success


def _GetOutputFiles(grd, base_dir):
  """Processes outputs listed in the grd into rc_headers and rc_alls.

  Note that anything that's not an rc_header is classified as an rc_all.

  Args:
    grd: An open GRD reader.

  Returns:
    A tuple of (rc_headers, rc_alls, lang_folders):
      rc_headers: Outputs marked as rc_header.
      rc_alls: All other outputs.
      lang_folders: The output language folders.
  """
  rc_headers = []
  rc_alls = []
  lang_folders = {}

  # Explicit output files.
  for output in grd.GetOutputFiles():
    path = os.path.join(base_dir, output.GetFilename())
    if (output.GetType() == 'rc_header'):
      rc_headers.append(path)
    else:
      rc_alls.append(path)
    if _IsDebugEnabled():
      print 'GRIT: Added target %s' % path
    if output.attrs['lang'] != '':
      lang_folders[output.attrs['lang']] = os.path.dirname(path)

  return (rc_headers, rc_alls, lang_folders)


def _ProcessNodes(grd, base_dir, lang_folders):
  """Processes the GRD nodes to figure out file dependencies.

  Args:
    grd: An open GRD reader.
    base_dir: The base directory for filenames.
    lang_folders: THe output language folders.

  Returns:
    A tuple of (structure_outputs, translated_files, static_files):
      structure_outputs: Structures marked as sconsdep.
      translated_files: Files that are structures or skeletons, and get
        translated by GRIT.
      static_files: Files that are includes, and are used directly by res files.
  """
  structure_outputs = []
  translated_files = []
  static_files = []

  # Go through nodes, figuring out resources.  Also output certain resources
  # as build targets, based on the sconsdep flag.
  for node in grd.ActiveDescendants():
    with node:
      file = node.ToRealPath(node.GetInputPath())
      if node.name == 'structure':
        translated_files.append(os.path.abspath(file))
        # TODO(joi) Should remove the "if sconsdep is true" thing as it is a
        # hack - see grit/node/structure.py
        if node.HasFileForLanguage() and node.attrs['sconsdep'] == 'true':
          for lang in lang_folders:
            path = node.FileForLanguage(lang, lang_folders[lang],
                                        create_file=False,
                                        return_if_not_generated=False)
            if path:
              structure_outputs.append(path)
              if _IsDebugEnabled():
                print 'GRIT: Added target %s' % path
      elif (node.name == 'skeleton' or (node.name == 'file' and node.parent and
                                        node.parent.name == 'translations')):
        translated_files.append(os.path.abspath(file))
      elif node.name == 'include':
        # If it's added by file name and the file isn't easy to find, don't make
        # it a dependency.  This could add some build flakiness, but it doesn't
        # work otherwise.
        if node.attrs['filenameonly'] != 'true' or os.path.exists(file):
          static_files.append(os.path.abspath(file))
        # If it's output from mk, look in the output directory.
        elif node.attrs['mkoutput'] == 'true':
          static_files.append(os.path.join(base_dir, os.path.basename(file)))

  return (structure_outputs, translated_files, static_files)


def _SetDependencies(env, base_dir, res_file, rc_alls, translated_files,
                     static_files):
  """Sets dependencies in the environment.

  Args:
    env: The SCons environment.
    base_dir: The base directory for filenames.
    res_file: The res_file specified in the RC flags.
    rc_alls: All non-rc_header outputs.
    translated_files: Files that are structures or skeletons, and get
      translated by GRIT.
    static_files: Files that are includes, and are used directly by res files.
  """
  if res_file:
    env.Depends(os.path.join(base_dir, res_file), static_files)
  else:
    # Make a best effort dependency setup when no res file is specified.
    translated_files.extend(static_files)

  for rc_all in rc_alls:
    env.Depends(rc_all, translated_files)


def _Emitter(target, source, env):
  """Modifies the list of targets to include all outputs.

  Note that this also sets up the dependencies, even though it's an emitter
  rather than a scanner.  This is so that the resource header file doesn't show
  as having dependencies.

  Args:
    target: The list of targets to emit for.
    source: The source or list of sources for the target.
    env: The SCons environment.

  Returns:
    A tuple of (targets, sources).
  """
  from grit import grd_reader
  from grit import util

  (defines, res_file) = _ParseRcFlags(env['RCFLAGS'])

  grd = grd_reader.Parse(_SourceToFile(source), debug=_IsDebugEnabled())
  # TODO(jperkins): This is a hack to get an output context set for the reader.
  # This should really be smarter about the language.
  grd.SetOutputLanguage('en')
  grd.SetDefines(defines)

  base_dir = util.dirname(str(target[0]))
  (rc_headers, rc_alls, lang_folders) = _GetOutputFiles(grd, base_dir)
  (structure_outputs, translated_files, static_files) = _ProcessNodes(grd,
      base_dir, lang_folders)

  rc_alls.extend(structure_outputs)
  _SetDependencies(env, base_dir, res_file, rc_alls, translated_files,
                   static_files)

  targets = rc_headers
  targets.extend(rc_alls)

  # Return target and source lists.
  return (targets, source)


# Function name is mandated by newer versions of SCons.
def generate(env):
  # Importing this module should be possible whenever this function is invoked
  # since it should only be invoked by SCons.
  import SCons.Builder
  import SCons.Action

  # The varlist parameter tells SCons that GRIT needs to be invoked again
  # if RCFLAGS has changed since last compilation.
  build_action = SCons.Action.FunctionAction(_Builder, varlist=['RCFLAGS'])
  emit_action = SCons.Action.FunctionAction(_Emitter, varlist=['RCFLAGS'])

  builder = SCons.Builder.Builder(action=build_action, emitter=emit_action,
                                  src_suffix='.grd')

  # Add our builder and scanner to the environment.
  env.Append(BUILDERS = {'GRIT': builder})


# Function name is mandated by newer versions of SCons.
def exists(env):
  return 1
