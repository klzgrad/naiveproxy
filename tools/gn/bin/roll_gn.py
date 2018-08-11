#!/usr/bin/env python
# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""An auto-roller for GN binaries into Chromium.

This script is used to update the GN binaries that a Chromium
checkout uses. In order to update the binaries, one must follow
four steps in order:

1. Trigger try jobs to build a new GN binary at tip-of-tree and upload
   the newly-built binaries into the right Google CloudStorage bucket.
2. Wait for the try jobs to complete.
3. Update the buildtools repo with the .sha1 hashes of the newly built
   binaries.
4. Update Chromium's DEPS file to the new version of the buildtools repo.

The script has four commands that correspond to the four steps above:
'build', 'wait', 'roll_buildtools', and 'roll_deps'.

The script has a fifth command, 'roll', that runs the four in order.

If given no arguments, the script will run the 'roll' command.

It can only be run on linux in a clean Chromium checkout; it should
error out in most cases if something bad happens, but the error checking
isn't yet foolproof.

"""

from __future__ import print_function

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib2


depot_tools_path = os.path.abspath(os.path.normpath(os.path.join(
    os.path.dirname(__file__), '..', '..', '..', 'third_party', 'depot_tools')))
if not depot_tools_path in sys.path:
  sys.path.insert(0, depot_tools_path)


CHROMIUM_REPO = 'https://chromium.googlesource.com/chromium/src.git'

COMMITISH_DIGITS = 10

UNKNOWN, PENDING, STARTED, SUCCESS = (
  'unknown', 'pending', 'started', 'success')

class BuildResult(object):
  def __init__(self):
    self.masterName = '-'
    self.builderName = '-'
    self.buildNumber = '-'
    self.state = UNKNOWN
    self.sha1 = '-'
    self.url = '-'


class GNRoller(object):
  def __init__(self):
    self.chromium_src_dir = None
    self.buildtools_dir = None
    self.old_gn_commitish = None
    self.new_gn_commitish = None
    self.old_gn_version   = None
    self.new_gn_version = None
    self.reviewer = 'dpranke@chromium.org'
    if os.getenv('USER') == 'dpranke':
      self.reviewer = 'brettw@chromium.org'

  def Roll(self):
    parser = argparse.ArgumentParser()
    parser.usage = __doc__
    parser.add_argument('command', nargs='?', default='roll',
                        help='build|roll|roll_buildtools|roll_deps|wait'
                             ' (%(default)s is the default)')

    args = parser.parse_args()
    command = args.command
    ret = self.SetUp()
    if not ret and command in ('roll', 'build'):
      ret = self.TriggerBuild()
    if not ret and command in ('roll', 'wait'):
      ret = self.WaitForBuildToFinish()
    if not ret and command in ('roll', 'roll_buildtools'):
      ret = self.RollBuildtools()
    if not ret and command in ('roll', 'roll_deps'):
      ret = self.RollDEPS()

    return ret

  def SetUp(self):
    if sys.platform not in ('darwin', 'linux2'):
      print('roll_gn is only tested and working on Linux and Mac for now.')
      return 1

    ret, out, _ = self.Call('git config --get remote.origin.url')
    origin = out.strip()
    if ret or origin != CHROMIUM_REPO:
      print('Not in a Chromium repo? git config --get remote.origin.url '
            'returned %d: %s' % (ret, origin))
      return 1

    ret, _, _ = self.Call('git diff -q')
    if ret:
      print("Checkout is dirty, exiting")
      return 1

    _, out, _ = self.Call('git rev-parse --show-toplevel', cwd=os.getcwd())
    self.chromium_src_dir = out.strip()
    self.buildtools_dir = os.path.join(self.chromium_src_dir, 'buildtools')

    self.new_gn_commitish, self.new_gn_version = self.GetNewVersions()

    _, out, _ = self.Call('gn --version')
    self.old_gn_version = out.strip()

    _, out, _ = self.Call('git crrev-parse %s' % self.old_gn_version)
    self.old_gn_commitish = out.strip()
    return 0

  def GetNewVersions(self):
    _, out, _ = self.Call('git log -1 --grep Cr-Commit-Position')
    commit_msg = out.splitlines()
    first_line = commit_msg[0]
    new_gn_commitish = first_line.split()[1]

    last_line = commit_msg[-1]
    new_gn_version = re.sub('.*master@{#(\d+)}', '\\1', last_line)

    return new_gn_commitish, new_gn_version

  def TriggerBuild(self):
    ret, _, _ = self.Call('git new-branch build_gn_%s' % self.new_gn_version)
    if ret:
      print('Failed to create a new branch for build_gn_%s' %
            self.new_gn_version)
      return 1

    self.MakeDummyDepsChange()

    ret, out, err = self.Call('git commit -a -m "Build gn at %s"' %
                         self.new_gn_version)
    if ret:
      print('git commit failed: %s' % out + err)
      return 1

    print('Uploading CL to build GN at {#%s} - %s' %
          (self.new_gn_version, self.new_gn_commitish))
    ret, out, err = self.Call('git cl upload -f')
    if ret:
      print('git-cl upload failed: %s' % out + err)
      return 1

    print('Starting try jobs')
    self.Call('git-cl try -m tryserver.chromium.linux '
              '-b linux_chromium_gn_upload -r %s' % self.new_gn_commitish)
    self.Call('git-cl try -m tryserver.chromium.mac '
              '-b mac_chromium_gn_upload -r %s' % self.new_gn_commitish)
    self.Call('git-cl try -m tryserver.chromium.win '
              '-b win_chromium_gn_upload -r %s' % self.new_gn_commitish)

    return 0

  def MakeDummyDepsChange(self):
    with open('DEPS') as fp:
      deps_content = fp.read()
      new_deps = deps_content.replace("'buildtools_revision':",
                                      "'buildtools_revision':  ")

    with open('DEPS', 'w') as fp:
      fp.write(new_deps)

  def WaitForBuildToFinish(self):
    ret = self.CheckoutBuildBranch()
    if ret:
      return ret

    print('Checking build')
    results = self.CheckBuild()
    while (any(r.state in (PENDING, STARTED) for r in results.values())):
      print()
      print('Sleeping for 30 seconds')
      time.sleep(30)
      print('Checking build')
      results = self.CheckBuild()

    ret = 0 if all(r.state == SUCCESS for r in results.values()) else 1
    if ret:
      print('Build failed.')
    else:
      print('Builds ready.')

    # Close the build CL and move off of the build branch back to whatever
    # we were on before.
    self.Call('git-cl set-close')
    self.MoveToLastHead()

    return ret

  def CheckoutBuildBranch(self):
    ret, out, err = self.Call('git checkout build_gn_%s' % self.new_gn_version)
    if ret:
      print('Failed to check out build_gn_%s' % self.new_gn_version)
      if out:
        print(out)
      if err:
        print(err, file=sys.stderr)
    return ret

  def CheckBuild(self):
    _, out, _ = self.Call('git-cl try-results')

    builders = {
        'linux_chromium_gn_upload': 'linux64',
        'mac_chromium_gn_upload': 'mac',
        'win_chromium_gn_upload': 'win'
    }

    results = {}
    for platform in ('linux64', 'mac', 'win'):
      results[platform] = BuildResult()

    state = PENDING
    for line in out.splitlines():
      fields = line.strip().split()
      if fields[0] == 'Started:':
        state = STARTED
      if fields[0] == 'Successes:':
        state = SUCCESS
      elif fields[0] == 'Total':
        pass
      elif fields[0] in builders:
        builder = fields[0]
        platform = builders[builder]
        result = results[platform]
        result.masterName = ('tryserver.chromium.%s' %
                                platform.replace('linux64', 'linux'))
        result.builderName = builder
        result.url = fields[1]
        if result.url.startswith('id'):
          result.state = PENDING
        else:
          result.state = state
          result.buildNumber = int(result.url[result.url.rfind('/')+1:])

    for result in results.values():
      if result.state == SUCCESS:
        url = 'https://luci-milo.appspot.com/prpc/milo.BuildInfo/Get'
        data = json.dumps({"buildbot": {
            'masterName': result.masterName,
            'builderName': result.builderName,
            'buildNumber': result.buildNumber,
            }})
        headers = {
            'content-type': 'application/json',
            'accept': 'application/json',
        }

        req = urllib2.Request(url, data, headers)
        resp = urllib2.urlopen(req)
        data = resp.read()
        resp.close()

        # The first line of the response is garbage; skip it.
        js = json.loads(data.splitlines()[1])

        sha1_step_name = 'gn sha1'
        for step in js['step']['substep']:
          if step['step']['name'] == sha1_step_name:
            sha1 = step['step']['text'][-1]

        result.sha1 = sha1

    for platform, r in results.items():
      print(platform)
      print('  sha1:  %s' % r.sha1)
      print('  state: %s' % r.state)
      print('  build: %s' % r.buildNumber)
      print('  url:   %s' % r.url)
      print()

    return results

  def RollBuildtools(self):
    ret = self.CheckoutBuildBranch()
    if ret:
      return ret

    results = self.CheckBuild()
    if (len(results) < 3 or
        not all(r.state == SUCCESS for r in results.values()) or
        not all(r.sha1 != '-' for r in results.values())):
      print("Roll isn't done or didn't succeed, exiting:")
      return 1

    desc = self.GetBuildtoolsDesc()

    self.Call('git new-branch roll_buildtools_gn_%s' % self.new_gn_version,
              cwd=self.buildtools_dir)

    for platform in results:
      fname = 'gn.exe.sha1' if platform == 'win' else 'gn.sha1'
      path = os.path.join(self.buildtools_dir, platform, fname)
      with open(path, 'w') as fp:
        fp.write('%s\n' % results[platform].sha1)

    desc_file = tempfile.NamedTemporaryFile(delete=False)
    try:
      desc_file.write(desc)
      desc_file.close()
      self.Call('git commit -a -F %s' % desc_file.name,
                cwd=self.buildtools_dir)
      self.Call('git-cl upload -f --send-mail',
                cwd=self.buildtools_dir)
    finally:
      os.remove(desc_file.name)

    ret, out, err = self.Call('git cl land', cwd=self.buildtools_dir)
    if ret:
        print("buildtools git cl land failed: %d" % ret)
        if out:
            print(out)
        if err:
            print(err)
        return ret

    # Fetch the revision we just committed so that RollDEPS will find it.
    self.Call('git fetch', cwd=self.buildtools_dir)

    # Reset buildtools to the new commit so that we're not still on the
    # merged branch.
    self.Call('git checkout origin/master', cwd=self.buildtools_dir)

    _, out, _ = self.Call('git rev-parse origin/master',
                          cwd=self.buildtools_dir)
    new_buildtools_commitish = out.strip()
    print('Ready to roll buildtools to %s in DEPS' % new_buildtools_commitish)

    return 0

  def RollDEPS(self):
    ret, _, _ = self.Call('git new-branch roll_gn_%s' % self.new_gn_version)
    if ret:
      print('Failed to create a new branch for roll_gn_%s' %
            self.new_gn_version)
      return 1

    _, out, _ = self.Call('git rev-parse origin/master',
                          cwd=self.buildtools_dir)
    new_buildtools_commitish = out.strip()

    new_deps_lines = []
    old_buildtools_commitish = ''
    with open(os.path.join(self.chromium_src_dir, 'DEPS')) as fp:
      for l in fp.readlines():
        m = re.match(".*'buildtools_revision':.*'(.+)',", l)
        if m:
          old_buildtools_commitish = m.group(1)
          new_deps_lines.append("  'buildtools_revision': '%s',\n" %
                                new_buildtools_commitish)
        else:
          new_deps_lines.append(l)

    if not old_buildtools_commitish:
      print('Could not update DEPS properly, exiting')
      return 1

    with open('DEPS', 'w') as fp:
      fp.write(''.join(new_deps_lines))

    desc = self.GetDEPSRollDesc(old_buildtools_commitish,
                                new_buildtools_commitish)
    desc_file = tempfile.NamedTemporaryFile(delete=False)
    try:
      desc_file.write(desc)
      desc_file.close()
      self.Call('git commit -a -F %s' % desc_file.name)
      self.Call('git-cl upload -f --send-mail --use-commit-queue')
    finally:
      os.remove(desc_file.name)

    # Move off of the roll branch onto whatever we were on before.
    # Do not explicitly close the roll CL issue, however; the CQ
    # will close it when the roll lands, assuming it does so.
    self.MoveToLastHead()

    return 0

  def MoveToLastHead(self):
    # When this is called, there will be a commit + a checkout as
    # the two most recent entries in the reflog, assuming nothing as
    # modified the repo while this script has been running.
    _, out, _ = self.Call('git reflog -2')
    m = re.search('moving from ([^\s]+)', out)
    last_head = m.group(1)
    self.Call('git checkout %s' % last_head)

  def GetBuildtoolsDesc(self):
    gn_changes = self.GetGNChanges()
    return (
      'Roll gn %s..%s (r%s:r%s)\n'
      '\n'
      '%s'
      '\n'
      'TBR=%s\n' % (
        self.old_gn_commitish[:COMMITISH_DIGITS],
        self.new_gn_commitish[:COMMITISH_DIGITS],
        self.old_gn_version,
        self.new_gn_version,
        gn_changes,
        self.reviewer,
      ))

  def GetDEPSRollDesc(self, old_buildtools_commitish, new_buildtools_commitish):
    gn_changes = self.GetGNChanges()

    return (
      'Roll buildtools %s..%s\n'
      '\n'
      '  In order to roll GN %s..%s (r%s:r%s) and pick up\n'
      '  the following changes:\n'
      '\n'
      '%s'
      '\n'
      'TBR=%s\n' % (
        old_buildtools_commitish[:COMMITISH_DIGITS],
        new_buildtools_commitish[:COMMITISH_DIGITS],
        self.old_gn_commitish[:COMMITISH_DIGITS],
        self.new_gn_commitish[:COMMITISH_DIGITS],
        self.old_gn_version,
        self.new_gn_version,
        gn_changes,
        self.reviewer,
      ))

  def GetGNChanges(self):
    _, out, _ = self.Call(
        "git log --pretty='  %h %s' " +
        "%s..%s tools/gn" % (self.old_gn_commitish, self.new_gn_commitish))
    return out

  def Call(self, cmd, cwd=None):
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True,
                            cwd=(cwd or self.chromium_src_dir))
    out, err = proc.communicate()
    return proc.returncode, out or '', err or ''


if __name__ == '__main__':
  roller = GNRoller()
  sys.exit(roller.Roll())
