# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re
import time
from util import build_utils


class _ProguardOutputFilter(object):
  """ProGuard outputs boring stuff to stdout (proguard version, jar path, etc)
  as well as interesting stuff (notes, warnings, etc). If stdout is entirely
  boring, this class suppresses the output.
  """

  IGNORE_RE = re.compile(
      r'(?:Pro.*version|Note:|Reading|Preparing|ProgramClass:|'
      '.*:.*(?:MANIFEST\.MF|\.empty))')

  def __init__(self):
    self._last_line_ignored = False
    self._ignore_next_line = False

  def __call__(self, output):
    ret = []
    for line in output.splitlines(True):
      if self._ignore_next_line:
        self._ignore_next_line = False
        continue

      if '***BINARY RUN STATS***' in line:
        self._last_line_ignored = True
        self._ignore_next_line = True
      elif not line.startswith(' '):
        self._last_line_ignored = bool(self.IGNORE_RE.match(line))
      elif 'You should check if you need to specify' in line:
        self._last_line_ignored = True

      if not self._last_line_ignored:
        ret.append(line)
    return ''.join(ret)


class ProguardCmdBuilder(object):
  def __init__(self, proguard_jar):
    assert os.path.exists(proguard_jar)
    self._proguard_jar_path = proguard_jar
    self._tested_apk_info_path = None
    self._tested_apk_info = None
    self._mapping = None
    self._libraries = None
    self._injars = None
    self._configs = None
    self._config_exclusions = None
    self._outjar = None
    self._cmd = None
    self._verbose = False
    self._disabled_optimizations = []

  def outjar(self, path):
    assert self._cmd is None
    assert self._outjar is None
    self._outjar = path

  def tested_apk_info(self, tested_apk_info_path):
    assert self._cmd is None
    assert self._tested_apk_info is None
    self._tested_apk_info_path = tested_apk_info_path

  def mapping(self, path):
    assert self._cmd is None
    assert self._mapping is None
    assert os.path.exists(path), path
    self._mapping = path

  def libraryjars(self, paths):
    assert self._cmd is None
    assert self._libraries is None
    for p in paths:
      assert os.path.exists(p), p
    self._libraries = paths

  def injars(self, paths):
    assert self._cmd is None
    assert self._injars is None
    for p in paths:
      assert os.path.exists(p), p
    self._injars = paths

  def configs(self, paths):
    assert self._cmd is None
    assert self._configs is None
    self._configs = paths
    for p in self._configs:
      assert os.path.exists(p), p

  def config_exclusions(self, paths):
    assert self._cmd is None
    assert self._config_exclusions is None
    self._config_exclusions = paths

  def verbose(self, verbose):
    assert self._cmd is None
    self._verbose = verbose

  def disable_optimizations(self, optimizations):
    assert self._cmd is None
    self._disabled_optimizations += optimizations

  def build(self):
    if self._cmd:
      return self._cmd
    assert self._injars is not None
    assert self._outjar is not None
    assert self._configs is not None
    cmd = [
      'java', '-jar', self._proguard_jar_path,
      '-forceprocessing',
    ]
    if self._tested_apk_info_path:
      tested_apk_info = build_utils.ReadJson(self._tested_apk_info_path)
      self._configs += tested_apk_info['configs']

    for path in self._config_exclusions:
      self._configs.remove(path)

    if self._mapping:
      cmd += [
        '-applymapping', self._mapping,
      ]

    if self._libraries:
      cmd += [
        '-libraryjars', ':'.join(self._libraries),
      ]

    for optimization in self._disabled_optimizations:
      cmd += [ '-optimizations', '!' + optimization ]

    cmd += [
      '-injars', ':'.join(self._injars)
    ]

    for config_file in self._configs:
      cmd += ['-include', config_file]

    # The output jar must be specified after inputs.
    cmd += [
      '-outjars', self._outjar,
      '-printseeds', self._outjar + '.seeds',
      '-printusage', self._outjar + '.usage',
      '-printmapping', self._outjar + '.mapping',
    ]

    if self._verbose:
      cmd.append('-verbose')

    self._cmd = cmd
    return self._cmd

  def GetDepfileDeps(self):
    # The list of inputs that the GN target does not directly know about.
    self.build()
    inputs = self._configs + self._injars
    if self._libraries:
      inputs += self._libraries
    if self._tested_apk_info_path:
      inputs += [self._tested_apk_info_path]
    return inputs

  def GetInputs(self):
    inputs = self.GetDepfileDeps()
    inputs += [self._proguard_jar_path]
    if self._mapping:
      inputs.append(self._mapping)
    return inputs

  def _WriteFlagsFile(self, out):
    # Quite useful for auditing proguard flags.
    for config in sorted(self._configs):
      out.write('#' * 80 + '\n')
      out.write(config + '\n')
      out.write('#' * 80 + '\n')
      with open(config) as config_file:
        contents = config_file.read().rstrip()
      # Remove numbers from generated rule comments to make file more
      # diff'able.
      contents = re.sub(r' #generated:\d+', '', contents)
      out.write(contents)
      out.write('\n\n')
    out.write('#' * 80 + '\n')
    out.write('Command-line\n')
    out.write('#' * 80 + '\n')
    out.write(' '.join(self._cmd) + '\n')

  def CheckOutput(self):
    self.build()
    # Proguard will skip writing these files if they would be empty. Create
    # empty versions of them all now so that they are updated as the build
    # expects.
    open(self._outjar + '.seeds', 'w').close()
    open(self._outjar + '.usage', 'w').close()
    open(self._outjar + '.mapping', 'w').close()

    with open(self._outjar + '.flags', 'w') as out:
      self._WriteFlagsFile(out)

    # Warning: and Error: are sent to stderr, but messages and Note: are sent
    # to stdout.
    stdout_filter = None
    stderr_filter = None
    if not self._verbose:
      stdout_filter = _ProguardOutputFilter()
      stderr_filter = _ProguardOutputFilter()
    start_time = time.time()
    build_utils.CheckOutput(self._cmd, print_stdout=True,
                            print_stderr=True,
                            stdout_filter=stdout_filter,
                            stderr_filter=stderr_filter)

    this_info = {
      'inputs': self._injars,
      'configs': self._configs,
      'mapping': self._outjar + '.mapping',
      'elapsed_time': round(time.time() - start_time),
    }

    build_utils.WriteJson(this_info, self._outjar + '.info')
