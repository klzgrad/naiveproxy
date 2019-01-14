#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''The 'grit build' tool along with integration for this tool with the
SCons build system.
'''

import codecs
import filecmp
import getopt
import os
import shutil
import sys

from grit import grd_reader
from grit import shortcuts
from grit import util
from grit.format import minifier
from grit.node import include
from grit.node import message
from grit.node import structure
from grit.tool import interface


# It would be cleaner to have each module register itself, but that would
# require importing all of them on every run of GRIT.
'''Map from <output> node types to modules under grit.format.'''
_format_modules = {
  'android': 'android_xml',
  'c_format': 'c_format',
  'chrome_messages_json': 'chrome_messages_json',
  'data_package': 'data_pack',
  'gzipped_resource_file_map_source': 'resource_map',
  'gzipped_resource_map_header': 'resource_map',
  'js_map_format': 'js_map_format',
  'policy_templates': 'policy_templates_json',
  'rc_all': 'rc',
  'rc_header': 'rc_header',
  'rc_nontranslateable': 'rc',
  'rc_translateable': 'rc',
  'resource_file_map_source': 'resource_map',
  'resource_map_header': 'resource_map',
  'resource_map_source': 'resource_map',
}

def GetFormatter(type):
  modulename = 'grit.format.' + _format_modules[type]
  __import__(modulename)
  module = sys.modules[modulename]
  try:
    return module.Format
  except AttributeError:
    return module.GetFormatter(type)


class RcBuilder(interface.Tool):
  '''A tool that builds RC files and resource header files for compilation.

Usage:  grit build [-o OUTPUTDIR] [-D NAME[=VAL]]*

All output options for this tool are specified in the input file (see
'grit help' for details on how to specify the input file - it is a global
option).

Options:

  -a FILE           Assert that the given file is an output. There can be
                    multiple "-a" flags listed for multiple outputs. If a "-a"
                    or "--assert-file-list" argument is present, then the list
                    of asserted files must match the output files or the tool
                    will fail. The use-case is for the build system to maintain
                    separate lists of output files and to catch errors if the
                    build system's list and the grit list are out-of-sync.

  --assert-file-list  Provide a file listing multiple asserted output files.
                    There is one file name per line. This acts like specifying
                    each file with "-a" on the command line, but without the
                    possibility of running into OS line-length limits for very
                    long lists.

  -o OUTPUTDIR      Specify what directory output paths are relative to.
                    Defaults to the current directory.

  -p FILE           Specify a file containing a pre-determined mapping from
                    resource names to resource ids which will be used to assign
                    resource ids to those resources. Resources not found in this
                    file will be assigned ids normally. The motivation is to run
                    your app's startup and have it dump the resources it loads,
                    and then pass these via this flag. This will pack startup
                    resources together, thus reducing paging while all other
                    resources are unperturbed. The file should have the format:
                      RESOURCE_ONE_NAME 123
                      RESOURCE_TWO_NAME 124

  -D NAME[=VAL]     Specify a C-preprocessor-like define NAME with optional
                    value VAL (defaults to 1) which will be used to control
                    conditional inclusion of resources.

  -E NAME=VALUE     Set environment variable NAME to VALUE (within grit).

  -f FIRSTIDSFILE   Path to a python file that specifies the first id of
                    value to use for resources.  A non-empty value here will
                    override the value specified in the <grit> node's
                    first_ids_file.

  -w WHITELISTFILE  Path to a file containing the string names of the
                    resources to include.  Anything not listed is dropped.

  -t PLATFORM       Specifies the platform the build is targeting; defaults
                    to the value of sys.platform. The value provided via this
                    flag should match what sys.platform would report for your
                    target platform; see grit.node.base.EvaluateCondition.

  --whitelist-support
                    Generate code to support extracting a resource whitelist
                    from executables.

  --write-only-new flag
                    If flag is non-0, write output files to a temporary file
                    first, and copy it to the real output only if the new file
                    is different from the old file.  This allows some build
                    systems to realize that dependent build steps might be
                    unnecessary, at the cost of comparing the output data at
                    grit time.

  --depend-on-stamp
                    If specified along with --depfile and --depdir, the depfile
                    generated will depend on a stampfile instead of the first
                    output in the input .grd file.

  --js-minifier     A command to run the Javascript minifier. If not set then
                    Javascript won't be minified. The command should read the
                    original Javascript from standard input, and output the
                    minified Javascript to standard output. A non-zero exit
                    status will be taken as indicating failure.

Conditional inclusion of resources only affects the output of files which
control which resources get linked into a binary, e.g. it affects .rc files
meant for compilation but it does not affect resource header files (that define
IDs).  This helps ensure that values of IDs stay the same, that all messages
are exported to translation interchange files (e.g. XMB files), etc.
'''

  def ShortDescription(self):
    return 'A tool that builds RC files for compilation.'

  def Run(self, opts, args):
    os.environ['cwd'] = os.getcwd()
    self.output_directory = '.'
    first_ids_file = None
    predetermined_ids_file = None
    whitelist_filenames = []
    assert_output_files = []
    target_platform = None
    depfile = None
    depdir = None
    whitelist_support = False
    write_only_new = False
    depend_on_stamp = False
    js_minifier = None
    replace_ellipsis = True
    (own_opts, args) = getopt.getopt(args, 'a:p:o:D:E:f:w:t:',
        ('depdir=','depfile=','assert-file-list=',
         'output-all-resource-defines',
         'no-output-all-resource-defines',
         'no-replace-ellipsis',
         'depend-on-stamp',
         'js-minifier=',
         'write-only-new=',
         'whitelist-support'))
    for (key, val) in own_opts:
      if key == '-a':
        assert_output_files.append(val)
      elif key == '--assert-file-list':
        with open(val) as f:
          assert_output_files += f.read().splitlines()
      elif key == '-o':
        self.output_directory = val
      elif key == '-D':
        name, val = util.ParseDefine(val)
        self.defines[name] = val
      elif key == '-E':
        (env_name, env_value) = val.split('=', 1)
        os.environ[env_name] = env_value
      elif key == '-f':
        # TODO(joi@chromium.org): Remove this override once change
        # lands in WebKit.grd to specify the first_ids_file in the
        # .grd itself.
        first_ids_file = val
      elif key == '-w':
        whitelist_filenames.append(val)
      elif key == '--no-replace-ellipsis':
        replace_ellipsis = False
      elif key == '-p':
        predetermined_ids_file = val
      elif key == '-t':
        target_platform = val
      elif key == '--depdir':
        depdir = val
      elif key == '--depfile':
        depfile = val
      elif key == '--write-only-new':
        write_only_new = val != '0'
      elif key == '--depend-on-stamp':
        depend_on_stamp = True
      elif key == '--js-minifier':
        js_minifier = val
      elif key == '--whitelist-support':
        whitelist_support = True

    if len(args):
      print 'This tool takes no tool-specific arguments.'
      return 2
    self.SetOptions(opts)
    if self.scons_targets:
      self.VerboseOut('Using SCons targets to identify files to output.\n')
    else:
      self.VerboseOut('Output directory: %s (absolute path: %s)\n' %
                      (self.output_directory,
                       os.path.abspath(self.output_directory)))

    if whitelist_filenames:
      self.whitelist_names = set()
      for whitelist_filename in whitelist_filenames:
        self.VerboseOut('Using whitelist: %s\n' % whitelist_filename);
        whitelist_contents = util.ReadFile(whitelist_filename, util.RAW_TEXT)
        self.whitelist_names.update(whitelist_contents.strip().split('\n'))

    if js_minifier:
      minifier.SetJsMinifier(js_minifier)

    self.write_only_new = write_only_new

    self.res = grd_reader.Parse(opts.input,
                                debug=opts.extra_verbose,
                                first_ids_file=first_ids_file,
                                predetermined_ids_file=predetermined_ids_file,
                                defines=self.defines,
                                target_platform=target_platform)

    # Set an output context so that conditionals can use defines during the
    # gathering stage; we use a dummy language here since we are not outputting
    # a specific language.
    self.res.SetOutputLanguage('en')
    self.res.SetWhitelistSupportEnabled(whitelist_support)
    self.res.RunGatherers()

    # Replace ... with the single-character version. http://crbug.com/621772
    if replace_ellipsis:
      for node in self.res:
        if isinstance(node, message.MessageNode):
          node.SetReplaceEllipsis(True)

    self.Process()

    if assert_output_files:
      if not self.CheckAssertedOutputFiles(assert_output_files):
        return 2

    if depfile and depdir:
      self.GenerateDepfile(depfile, depdir, first_ids_file, depend_on_stamp)

    return 0

  def __init__(self, defines=None):
    # Default file-creation function is codecs.open().  Only done to allow
    # overriding by unit test.
    self.fo_create = codecs.open

    # key/value pairs of C-preprocessor like defines that are used for
    # conditional output of resources
    self.defines = defines or {}

    # self.res is a fully-populated resource tree if Run()
    # has been called, otherwise None.
    self.res = None

    # Set to a list of filenames for the output nodes that are relative
    # to the current working directory.  They are in the same order as the
    # output nodes in the file.
    self.scons_targets = None

    # The set of names that are whitelisted to actually be included in the
    # output.
    self.whitelist_names = None

    # Whether to compare outputs to their old contents before writing.
    self.write_only_new = False

  @staticmethod
  def AddWhitelistTags(start_node, whitelist_names):
    # Walk the tree of nodes added attributes for the nodes that shouldn't
    # be written into the target files (skip markers).
    for node in start_node:
      # Same trick data_pack.py uses to see what nodes actually result in
      # real items.
      if (isinstance(node, include.IncludeNode) or
          isinstance(node, message.MessageNode) or
          isinstance(node, structure.StructureNode)):
        text_ids = node.GetTextualIds()
        # Mark the item to be skipped if it wasn't in the whitelist.
        if text_ids and text_ids[0] not in whitelist_names:
          node.SetWhitelistMarkedAsSkip(True)

  @staticmethod
  def ProcessNode(node, output_node, outfile):
    '''Processes a node in-order, calling its formatter before and after
    recursing to its children.

    Args:
      node: grit.node.base.Node subclass
      output_node: grit.node.io.OutputNode
      outfile: open filehandle
    '''
    base_dir = util.dirname(output_node.GetOutputFilename())

    formatter = GetFormatter(output_node.GetType())
    formatted = formatter(node, output_node.GetLanguage(), output_dir=base_dir)
    outfile.writelines(formatted)
    if output_node.GetType() == 'data_package':
      with open(output_node.GetOutputFilename() + '.info', 'w') as infofile:
        if node.info:
          # We terminate with a newline so that when these files are
          # concatenated later we consistently terminate with a newline so
          # consumers can account for terminating newlines.
          infofile.writelines(['\n'.join(node.info), '\n'])

  @staticmethod
  def _EncodingForOutputType(output_type):
    # Microsoft's RC compiler can only deal with single-byte or double-byte
    # files (no UTF-8), so we make all RC files UTF-16 to support all
    # character sets.
    if output_type in ('rc_header', 'resource_map_header',
                       'resource_map_source', 'resource_file_map_source',
                       'gzipped_resource_map_header',
                       'gzipped_resource_file_map_source'):
      return 'cp1252'
    if output_type in ('android', 'c_format', 'js_map_format', 'plist',
                       'plist_strings', 'doc', 'json', 'android_policy',
                       'chrome_messages_json'):
      return 'utf_8'
    # TODO(gfeher) modify here to set utf-8 encoding for admx/adml
    return 'utf_16'

  def Process(self):
    # Update filenames with those provided by SCons if we're being invoked
    # from SCons.  The list of SCons targets also includes all <structure>
    # node outputs, but it starts with our output files, in the order they
    # occur in the .grd
    if self.scons_targets:
      assert len(self.scons_targets) >= len(self.res.GetOutputFiles())
      outfiles = self.res.GetOutputFiles()
      for ix in range(len(outfiles)):
        outfiles[ix].output_filename = os.path.abspath(
          self.scons_targets[ix])
    else:
      for output in self.res.GetOutputFiles():
        output.output_filename = os.path.abspath(os.path.join(
          self.output_directory, output.GetOutputFilename()))

    # If there are whitelisted names, tag the tree once up front, this way
    # while looping through the actual output, it is just an attribute check.
    if self.whitelist_names:
      self.AddWhitelistTags(self.res, self.whitelist_names)

    for output in self.res.GetOutputFiles():
      self.VerboseOut('Creating %s...' % output.GetOutputFilename())

      # Set the context, for conditional inclusion of resources
      self.res.SetOutputLanguage(output.GetLanguage())
      self.res.SetOutputContext(output.GetContext())
      self.res.SetFallbackToDefaultLayout(output.GetFallbackToDefaultLayout())
      self.res.SetDefines(self.defines)

      # Assign IDs only once to ensure that all outputs use the same IDs.
      if self.res.GetIdMap() is None:
        self.res.InitializeIds()

      # Make the output directory if it doesn't exist.
      self.MakeDirectoriesTo(output.GetOutputFilename())

      # Write the results to a temporary file and only overwrite the original
      # if the file changed.  This avoids unnecessary rebuilds.
      outfile = self.fo_create(output.GetOutputFilename() + '.tmp', 'wb')

      if output.GetType() != 'data_package':
        encoding = self._EncodingForOutputType(output.GetType())
        outfile = util.WrapOutputStream(outfile, encoding)

      # Iterate in-order through entire resource tree, calling formatters on
      # the entry into a node and on exit out of it.
      with outfile:
        self.ProcessNode(self.res, output, outfile)

      # Now copy from the temp file back to the real output, but on Windows,
      # only if the real output doesn't exist or the contents of the file
      # changed.  This prevents identical headers from being written and .cc
      # files from recompiling (which is painful on Windows).
      if not os.path.exists(output.GetOutputFilename()):
        os.rename(output.GetOutputFilename() + '.tmp',
                  output.GetOutputFilename())
      else:
        # CHROMIUM SPECIFIC CHANGE.
        # This clashes with gyp + vstudio, which expect the output timestamp
        # to change on a rebuild, even if nothing has changed, so only do
        # it when opted in.
        if not self.write_only_new:
          write_file = True
        else:
          files_match = filecmp.cmp(output.GetOutputFilename(),
              output.GetOutputFilename() + '.tmp')
          write_file = not files_match
        if write_file:
          shutil.copy2(output.GetOutputFilename() + '.tmp',
                       output.GetOutputFilename())
        os.remove(output.GetOutputFilename() + '.tmp')

      self.VerboseOut(' done.\n')

    # Print warnings if there are any duplicate shortcuts.
    warnings = shortcuts.GenerateDuplicateShortcutsWarnings(
        self.res.UberClique(), self.res.GetTcProject())
    if warnings:
      print '\n'.join(warnings)

    # Print out any fallback warnings, and missing translation errors, and
    # exit with an error code if there are missing translations in a non-pseudo
    # and non-official build.
    warnings = (self.res.UberClique().MissingTranslationsReport().
        encode('ascii', 'replace'))
    if warnings:
      self.VerboseOut(warnings)
    if self.res.UberClique().HasMissingTranslations():
      print self.res.UberClique().missing_translations_
      sys.exit(-1)


  def CheckAssertedOutputFiles(self, assert_output_files):
    '''Checks that the asserted output files are specified in the given list.

    Returns true if the asserted files are present. If they are not, returns
    False and prints the failure.
    '''
    # Compare the absolute path names, sorted.
    asserted = sorted([os.path.abspath(i) for i in assert_output_files])
    actual = sorted([
        os.path.abspath(os.path.join(self.output_directory,
                                     i.GetOutputFilename()))
        for i in self.res.GetOutputFiles()])

    if asserted != actual:
      missing = list(set(actual) - set(asserted))
      extra = list(set(asserted) - set(actual))
      error = '''Asserted file list does not match.

Expected output files:
%s
Actual output files:
%s
Missing output files:
%s
Extra output files:
%s
'''
      print error % ('\n'.join(asserted), '\n'.join(actual), '\n'.join(missing),
          '\n'.join(extra))
      return False
    return True


  def GenerateDepfile(self, depfile, depdir, first_ids_file, depend_on_stamp):
    '''Generate a depfile that contains the imlicit dependencies of the input
    grd. The depfile will be in the same format as a makefile, and will contain
    references to files relative to |depdir|. It will be put in |depfile|.

    For example, supposing we have three files in a directory src/

    src/
      blah.grd    <- depends on input{1,2}.xtb
      input1.xtb
      input2.xtb

    and we run

      grit -i blah.grd -o ../out/gen --depdir ../out --depfile ../out/gen/blah.rd.d

    from the directory src/ we will generate a depfile ../out/gen/blah.grd.d
    that has the contents

      gen/blah.h: ../src/input1.xtb ../src/input2.xtb

    Where "gen/blah.h" is the first output (Ninja expects the .d file to list
    the first output in cases where there is more than one). If the flag
    --depend-on-stamp is specified, "gen/blah.rd.d.stamp" will be used that is
    'touched' whenever a new depfile is generated.

    Note that all paths in the depfile are relative to ../out, the depdir.
    '''
    depfile = os.path.abspath(depfile)
    depdir = os.path.abspath(depdir)
    infiles = self.res.GetInputFiles()

    # We want to trigger a rebuild if the first ids change.
    if first_ids_file is not None:
      infiles.append(first_ids_file)

    if (depend_on_stamp):
      output_file = depfile + ".stamp"
      # Touch the stamp file before generating the depfile.
      with open(output_file, 'a'):
        os.utime(output_file, None)
    else:
      # Get the first output file relative to the depdir.
      outputs = self.res.GetOutputFiles()
      output_file = os.path.join(self.output_directory,
                                 outputs[0].GetOutputFilename())

    output_file = os.path.relpath(output_file, depdir)
    # The path prefix to prepend to dependencies in the depfile.
    prefix = os.path.relpath(os.getcwd(), depdir)
    deps_text = ' '.join([os.path.join(prefix, i) for i in infiles])

    depfile_contents = output_file + ': ' + deps_text
    self.MakeDirectoriesTo(depfile)
    outfile = self.fo_create(depfile, 'w', encoding='utf-8')
    outfile.writelines(depfile_contents)

  @staticmethod
  def MakeDirectoriesTo(file):
    '''Creates directories necessary to contain |file|.'''
    dir = os.path.split(file)[0]
    if not os.path.exists(dir):
      os.makedirs(dir)
