#!/usr/bin/env python
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import subprocess
import sys
import timeit


IS_WIN = sys.platform.startswith('win')


def RemoveDir(d):
  if os.path.isdir(d):
    shutil.rmtree(d)


def Trial(gn_path_to_use, save_out_dir=None):
  bin_path = os.path.join('out', 'gntrial')
  if not os.path.isdir(bin_path):
    os.makedirs(bin_path)
  gn_to_run = os.path.join(bin_path, 'gn' + ('.exe' if IS_WIN else ''))
  shutil.copy2(gn_path_to_use, gn_to_run)
  comp_dir = os.path.join('out', 'COMP')
  subprocess.check_call([gn_to_run, 'gen', comp_dir, '-q', '--check'])
  if save_out_dir:
    RemoveDir(save_out_dir)
    shutil.move(comp_dir, save_out_dir)


def main():
  if len(sys.argv) < 3 or len(sys.argv) > 4:
    print 'Usage: full_test.py /chrome/tree/at/762a25542878 rel_gn_path [clean]'
    return 1

  if len(sys.argv) == 4:
    RemoveDir('out')

  subprocess.check_call([sys.executable, os.path.join('build', 'gen.py')])
  subprocess.check_call(['ninja', '-C', 'out'])
  subprocess.check_call([os.path.join('out', 'gn_unittests')])
  orig_dir = os.getcwd()

  in_chrome_tree_gn = sys.argv[2]
  our_gn = os.path.join(orig_dir, 'out', 'gn' + ('.exe' if IS_WIN else ''))

  os.chdir(sys.argv[1])

  # Check in-tree vs. ours. Uses:
  # - Chromium tree at 762a25542878 in argv[1] (this can be off by a bit, but
  #   is roughly when GN was moved out of the Chrome tree, so matches in case GN
  #   semantics/ordering change after that.)
  # - relative path to argv[1] built gn binary in argv[2]

  # First, do a comparison to make sure the output between the two gn binaries
  # actually matches.
  print 'Confirming output matches...'
  dir_a = os.path.join('out', 'a')
  dir_b = os.path.join('out', 'b')
  Trial(in_chrome_tree_gn, dir_a)
  Trial(our_gn, dir_b)
  subprocess.check_call(['diff', '-r', dir_a, dir_b])

  # Then, some time trials.
  TRIALS = 5
  print 'Comparing performance... (takes a while)'
  time_a = timeit.timeit('Trial("%s")' % in_chrome_tree_gn, number=TRIALS,
                         setup='from __main__ import Trial')
  time_b = timeit.timeit('Trial("%s")' % our_gn, number=TRIALS,
                         setup='from __main__ import Trial')
  print 'In-tree gn avg: %.3fs' % (time_a / TRIALS)
  print 'Our gn avg: %.3fs' % (time_b / TRIALS)

  return 0


if __name__ == '__main__':
    sys.exit(main())
