#!/usr/bin/env vpython
# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for gold_utils."""

#pylint: disable=protected-access

import collections
import json
import os
import unittest

from pylib.constants import host_paths
from pylib.utils import gold_utils
from py_utils import tempfile_ext

with host_paths.SysPath(host_paths.PYMOCK_PATH):
  import mock  # pylint: disable=import-error

_SkiaGoldArgs = collections.namedtuple('_SkiaGoldArgs', [
    'local_pixel_tests',
    'no_luci_auth',
    'git_revision',
    'gerrit_issue',
    'gerrit_patchset',
    'buildbucket_id',
    'bypass_skia_gold_functionality',
])


def createSkiaGoldArgs(local_pixel_tests=None,
                       no_luci_auth=None,
                       git_revision=None,
                       gerrit_issue=None,
                       gerrit_patchset=None,
                       buildbucket_id=None,
                       bypass_skia_gold_functionality=None):
  return _SkiaGoldArgs(local_pixel_tests, no_luci_auth, git_revision,
                       gerrit_issue, gerrit_patchset, buildbucket_id,
                       bypass_skia_gold_functionality)


def assertArgWith(test, arg_list, arg, value):
  i = arg_list.index(arg)
  test.assertEqual(arg_list[i + 1], value)


class SkiaGoldSessionRunComparisonTest(unittest.TestCase):
  """Tests the functionality of SkiaGoldSession.RunComparison."""

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_comparisonSuccess(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (0, None)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({}, f)
      session = gold_utils.SkiaGoldSession(working_dir, None)
      status, _ = session.RunComparison(None, keys_file, None, None)
      self.assertEqual(status, gold_utils.SkiaGoldSession.StatusCodes.SUCCESS)
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 0)

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_authFailure(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (1, 'Auth failed')
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, None)
      status, error = session.RunComparison(None, None, None, None)
      self.assertEqual(status,
                       gold_utils.SkiaGoldSession.StatusCodes.AUTH_FAILURE)
      self.assertEqual(error, 'Auth failed')
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 0)
      self.assertEqual(diff_mock.call_count, 0)

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_compareFailureRemote(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (1, 'Compare failed')
    args = createSkiaGoldArgs(local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({}, f)
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      status, error = session.RunComparison(None, keys_file, None, None)
      self.assertEqual(
          status,
          gold_utils.SkiaGoldSession.StatusCodes.COMPARISON_FAILURE_REMOTE)
      self.assertEqual(error, 'Compare failed')
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 0)

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_compareFailureLocal(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (1, 'Compare failed')
    diff_mock.return_value = (0, None)
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({}, f)
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      status, error = session.RunComparison(None, keys_file, None, working_dir)
      self.assertEqual(
          status,
          gold_utils.SkiaGoldSession.StatusCodes.COMPARISON_FAILURE_LOCAL)
      self.assertEqual(error, 'Compare failed')
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 1)

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_diffFailure(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (1, 'Compare failed')
    diff_mock.return_value = (1, 'Diff failed')
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({}, f)
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      status, error = session.RunComparison(None, keys_file, None, working_dir)
      self.assertEqual(
          status, gold_utils.SkiaGoldSession.StatusCodes.LOCAL_DIFF_FAILURE)
      self.assertEqual(error, 'Diff failed')
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 1)

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_noOutputDirLocal(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (1, 'Compare failed')
    diff_mock.return_value = (0, None)
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({}, f)
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      status, error = session.RunComparison(None, keys_file, None, None)
      self.assertEqual(status,
                       gold_utils.SkiaGoldSession.StatusCodes.NO_OUTPUT_MANAGER)
      self.assertEqual(error, 'No output manager for local diff images')
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 0)

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_corpusDefault(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (0, None)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({}, f)
      session = gold_utils.SkiaGoldSession(working_dir, None, 'SomeCorpus')
      status, _ = session.RunComparison(None, keys_file, None, None)
      self.assertEqual(status, gold_utils.SkiaGoldSession.StatusCodes.SUCCESS)
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 0)
      compare_mock.assertCalledWith(
          name=None, keys_file=keys_file, png_file=None, corpus='SomeCorpus')

  @mock.patch.object(gold_utils.SkiaGoldSession, 'Diff')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Compare')
  @mock.patch.object(gold_utils.SkiaGoldSession, 'Authenticate')
  def test_corpusFromJson(self, auth_mock, compare_mock, diff_mock):
    auth_mock.return_value = (0, None)
    compare_mock.return_value = (0, None)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      keys_file = os.path.join(working_dir, 'keys.json')
      with open(os.path.join(working_dir, 'keys.json'), 'w') as f:
        json.dump({'source_type': 'foobar'}, f)
      session = gold_utils.SkiaGoldSession(working_dir, None, 'SomeCorpus')
      status, _ = session.RunComparison(None, keys_file, None, None)
      self.assertEqual(status, gold_utils.SkiaGoldSession.StatusCodes.SUCCESS)
      self.assertEqual(auth_mock.call_count, 1)
      self.assertEqual(compare_mock.call_count, 1)
      self.assertEqual(diff_mock.call_count, 0)
      compare_mock.assertCalledWith(
          name=None, keys_file=keys_file, png_file=None, corpus='foobar')


class SkiaGoldSessionAuthenticateTest(unittest.TestCase):
  """Tests the functionality of SkiaGoldSession.Authenticate."""

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandOutputReturned(self, cmd_mock):
    cmd_mock.return_value = (1, 'Something bad :(', None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, stdout = session.Authenticate()
    self.assertEqual(cmd_mock.call_count, 1)
    self.assertEqual(rc, 1)
    self.assertEqual(stdout, 'Something bad :(')

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_bypassSkiaGoldFunctionality(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(
        git_revision='a', bypass_skia_gold_functionality=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, _ = session.Authenticate()
    self.assertEqual(rc, 0)
    cmd_mock.assert_not_called()

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandWithUseLuciTrue(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Authenticate(use_luci=True)
    self.assertIn('--luci', cmd_mock.call_args[0][0])

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandWithUseLuciFalse(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a', local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Authenticate(use_luci=False)
    self.assertNotIn('--luci', cmd_mock.call_args[0][0])

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandWithUseLuciFalseNotLocal(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a', local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      with self.assertRaises(RuntimeError):
        session.Authenticate(use_luci=False)

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandCommonArgs(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Authenticate()
    call_args = cmd_mock.call_args[0][0]
    self.assertIn('auth', call_args)
    assertArgWith(self, call_args, '--work-dir', working_dir)


class SkiaGoldSessionCompareTest(unittest.TestCase):
  """Tests the functionality of SkiaGoldSession.Compare."""

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandOutputReturned(self, cmd_mock):
    cmd_mock.return_value = (1, 'Something bad :(', None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, stdout = session.Compare(None, None, None, None)
    self.assertEqual(cmd_mock.call_count, 1)
    self.assertEqual(rc, 1)
    self.assertEqual(stdout, 'Something bad :(')

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_bypassSkiaGoldFunctionality(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(
        git_revision='a', bypass_skia_gold_functionality=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, _ = session.Compare(None, None, None, None)
    self.assertEqual(rc, 0)
    cmd_mock.assert_not_called()

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandWithLocalPixelTestsTrue(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a', local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Compare(None, None, None, None)
    self.assertIn('--dryrun', cmd_mock.call_args[0][0])

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandWithLocalPixelTestsFalse(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a', local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Compare(None, None, None, None)
    self.assertNotIn('--dryrun', cmd_mock.call_args[0][0])

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandTryjobArgs(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(
        git_revision='a', gerrit_issue=1, gerrit_patchset=2, buildbucket_id=3)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Compare(None, None, None, None)
    call_args = cmd_mock.call_args[0][0]
    assertArgWith(self, call_args, '--issue', '1')
    assertArgWith(self, call_args, '--patchset', '2')
    assertArgWith(self, call_args, '--jobid', '3')
    assertArgWith(self, call_args, '--crs', 'gerrit')
    assertArgWith(self, call_args, '--cis', 'buildbucket')

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandTryjobArgsMissing(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      session.Compare(None, None, None, None)
    call_args = cmd_mock.call_args[0][0]
    self.assertNotIn('--issue', call_args)
    self.assertNotIn('--patchset', call_args)
    self.assertNotIn('--jobid', call_args)
    self.assertNotIn('--crs', call_args)
    self.assertNotIn('--cis', call_args)

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandCommonArgs(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(
          working_dir, sgp, instance='instance')
      session.Compare('name', 'keys_file', 'png_file', 'corpus')
    call_args = cmd_mock.call_args[0][0]
    self.assertIn('imgtest', call_args)
    self.assertIn('add', call_args)
    self.assertIn('--passfail', call_args)
    assertArgWith(self, call_args, '--test-name', 'name')
    assertArgWith(self, call_args, '--instance', 'instance')
    assertArgWith(self, call_args, '--corpus', 'corpus')
    assertArgWith(self, call_args, '--keys-file', 'keys_file')
    assertArgWith(self, call_args, '--png-file', 'png_file')
    assertArgWith(self, call_args, '--work-dir', working_dir)
    assertArgWith(self, call_args, '--failure-file', session._triage_link_file)
    assertArgWith(self, call_args, '--commit', 'a')

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_noLinkOnSuccess(self, cmd_mock):
    cmd_mock.return_value = (0, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, _ = session.Compare('name', 'keys_file', 'png_file', None)
    self.assertEqual(rc, 0)
    self.assertEqual(session._comparison_results['name'].triage_link, None)
    self.assertNotEqual(
        session._comparison_results['name'].triage_link_omission_reason, None)

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_clLinkOnTrybot(self, cmd_mock):
    cmd_mock.return_value = (1, None, None)
    args = createSkiaGoldArgs(
        git_revision='a', gerrit_issue=1, gerrit_patchset=2, buildbucket_id=3)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, _ = session.Compare('name', 'keys_file', 'png_file', None)
    self.assertEqual(rc, 1)
    self.assertNotEqual(session._comparison_results['name'].triage_link, None)
    self.assertIn('issue=1', session._comparison_results['name'].triage_link)
    self.assertEqual(
        session._comparison_results['name'].triage_link_omission_reason, None)

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_individualLinkOnCi(self, cmd_mock):
    cmd_mock.return_value = (1, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      m = mock.mock_open(read_data='foobar')
      with mock.patch('__builtin__.open', m, create=True):
        rc, _ = session.Compare('name', 'keys_file', 'png_file', None)
    self.assertEqual(rc, 1)
    self.assertNotEqual(session._comparison_results['name'].triage_link, None)
    self.assertEqual(session._comparison_results['name'].triage_link, 'foobar')
    self.assertEqual(
        session._comparison_results['name'].triage_link_omission_reason, None)

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_validOmissionOnIoError(self, cmd_mock):
    cmd_mock.return_value = (1, None, None)
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      m = mock.mock_open()
      m.side_effect = IOError('No read today')
      with mock.patch('__builtin__.open', m, create=True):
        rc, _ = session.Compare('name', 'keys_file', 'png_file', None)
    self.assertEqual(rc, 1)
    self.assertEqual(session._comparison_results['name'].triage_link, None)
    self.assertNotEqual(
        session._comparison_results['name'].triage_link_omission_reason, None)
    self.assertIn(
        'Failed to read',
        session._comparison_results['name'].triage_link_omission_reason)


class SkiaGoldSessionDiffTest(unittest.TestCase):
  """Tests the functionality of SkiaGoldSession.Diff."""

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandOutputReturned(self, cmd_mock):
    cmd_mock.return_value = (1, 'Something bad :(', None)
    args = createSkiaGoldArgs(git_revision='a', local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      rc, stdout = session.Diff(None, None, None, None)
    self.assertEqual(cmd_mock.call_count, 1)
    self.assertEqual(rc, 1)
    self.assertEqual(stdout, 'Something bad :(')

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_bypassSkiaGoldFunctionality(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(
        git_revision='a', bypass_skia_gold_functionality=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(working_dir, sgp)
      with self.assertRaises(RuntimeError):
        session.Diff(None, None, None, None)

  @mock.patch('devil.utils.cmd_helper.GetCmdStatusOutputAndError')
  def test_commandCommonArgs(self, cmd_mock):
    cmd_mock.return_value = (None, None, None)
    args = createSkiaGoldArgs(git_revision='a', local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    with tempfile_ext.NamedTemporaryDirectory() as working_dir:
      session = gold_utils.SkiaGoldSession(
          working_dir, sgp, instance='instance')
      session.Diff('name', 'png_file', 'corpus', None)
    call_args = cmd_mock.call_args[0][0]
    self.assertIn('diff', call_args)
    assertArgWith(self, call_args, '--corpus', 'corpus')
    assertArgWith(self, call_args, '--instance', 'instance')
    assertArgWith(self, call_args, '--input', 'png_file')
    assertArgWith(self, call_args, '--test', 'name')
    assertArgWith(self, call_args, '--work-dir', working_dir)
    i = call_args.index('--out-dir')
    # The output directory should be a subdirectory of the working directory.
    self.assertIn(working_dir, call_args[i + 1])


class SkiaGoldSessionTriageLinkOmissionTest(unittest.TestCase):
  """Tests the functionality of SkiaGoldSession.GetTriageLinkOmissionReason."""

  # Avoid having to bother with the working directory.
  class FakeGoldSession(gold_utils.SkiaGoldSession):
    def __init__(self):  # pylint: disable=super-init-not-called
      self._comparison_results = {
          'foo': gold_utils.SkiaGoldSession.ComparisonResults(),
      }

  def test_noComparison(self):
    session = self.FakeGoldSession()
    session._comparison_results = {}
    reason = session.GetTriageLinkOmissionReason('foo')
    self.assertEqual(reason, 'No image comparison performed for foo')

  def test_validReason(self):
    session = self.FakeGoldSession()
    session._comparison_results['foo'].triage_link_omission_reason = 'bar'
    reason = session.GetTriageLinkOmissionReason('foo')
    self.assertEqual(reason, 'bar')

  def test_onlyLocal(self):
    session = self.FakeGoldSession()
    session._comparison_results['foo'].local_diff_given_image = 'bar'
    reason = session.GetTriageLinkOmissionReason('foo')
    self.assertEqual(reason, 'Gold only used to do a local image diff')

  def test_onlyWithoutTriageLink(self):
    session = self.FakeGoldSession()
    session._comparison_results['foo'].triage_link = 'bar'
    with self.assertRaises(AssertionError):
      session.GetTriageLinkOmissionReason('foo')

  def test_resultsShouldNotExist(self):
    session = self.FakeGoldSession()
    with self.assertRaises(RuntimeError):
      session.GetTriageLinkOmissionReason('foo')


class SkiaGoldPropertiesInitializationTest(unittest.TestCase):
  """Tests that SkiaGoldProperties initializes (or doesn't) when expected."""

  def verifySkiaGoldProperties(self, instance, expected):
    self.assertEqual(instance._local_pixel_tests,
                     expected.get('local_pixel_tests'))
    self.assertEqual(instance._no_luci_auth, expected.get('no_luci_auth'))
    self.assertEqual(instance._git_revision, expected.get('git_revision'))
    self.assertEqual(instance._issue, expected.get('gerrit_issue'))
    self.assertEqual(instance._patchset, expected.get('gerrit_patchset'))
    self.assertEqual(instance._job_id, expected.get('buildbucket_id'))
    self.assertEqual(instance._bypass_skia_gold_functionality,
                     expected.get('bypass_skia_gold_functionality'))

  def test_initializeSkiaGoldAttributes_unsetLocal(self):
    args = createSkiaGoldArgs()
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {})

  def test_initializeSkiaGoldAttributes_explicitLocal(self):
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {'local_pixel_tests': True})

  def test_initializeSkiaGoldAttributes_explicitNonLocal(self):
    args = createSkiaGoldArgs(local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {'local_pixel_tests': False})

  def test_initializeSkiaGoldAttributes_explicitNoLuciAuth(self):
    args = createSkiaGoldArgs(no_luci_auth=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {'no_luci_auth': True})

  def test_initializeSkiaGoldAttributes_bypassExplicitTrue(self):
    args = createSkiaGoldArgs(bypass_skia_gold_functionality=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {'bypass_skia_gold_functionality': True})

  def test_initializeSkiaGoldAttributes_explicitGitRevision(self):
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {'git_revision': 'a'})

  def test_initializeSkiaGoldAttributes_tryjobArgsIgnoredWithoutRevision(self):
    args = createSkiaGoldArgs(
        gerrit_issue=1, gerrit_patchset=2, buildbucket_id=3)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(sgp, {})

  def test_initializeSkiaGoldAttributes_tryjobArgs(self):
    args = createSkiaGoldArgs(
        git_revision='a', gerrit_issue=1, gerrit_patchset=2, buildbucket_id=3)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.verifySkiaGoldProperties(
        sgp, {
            'git_revision': 'a',
            'gerrit_issue': 1,
            'gerrit_patchset': 2,
            'buildbucket_id': 3
        })

  def test_initializeSkiaGoldAttributes_tryjobMissingPatchset(self):
    args = createSkiaGoldArgs(
        git_revision='a', gerrit_issue=1, buildbucket_id=3)
    with self.assertRaises(RuntimeError):
      gold_utils.SkiaGoldProperties(args)

  def test_initializeSkiaGoldAttributes_tryjobMissingBuildbucket(self):
    args = createSkiaGoldArgs(
        git_revision='a', gerrit_issue=1, gerrit_patchset=2)
    with self.assertRaises(RuntimeError):
      gold_utils.SkiaGoldProperties(args)


class SkiaGoldPropertiesCalculationTest(unittest.TestCase):
  """Tests that SkiaGoldProperties properly calculates certain properties."""

  def testLocalPixelTests_determineTrue(self):
    args = createSkiaGoldArgs()
    sgp = gold_utils.SkiaGoldProperties(args)
    with mock.patch.dict(os.environ, {}, clear=True):
      self.assertTrue(sgp.local_pixel_tests)

  def testLocalPixelTests_determineFalse(self):
    args = createSkiaGoldArgs()
    sgp = gold_utils.SkiaGoldProperties(args)
    with mock.patch.dict(os.environ, {'SWARMING_SERVER': ''}, clear=True):
      self.assertFalse(sgp.local_pixel_tests)

  def testIsTryjobRun_noIssue(self):
    args = createSkiaGoldArgs()
    sgp = gold_utils.SkiaGoldProperties(args)
    self.assertFalse(sgp.IsTryjobRun())

  def testIsTryjobRun_issue(self):
    args = createSkiaGoldArgs(
        git_revision='a', gerrit_issue=1, gerrit_patchset=2, buildbucket_id=3)
    sgp = gold_utils.SkiaGoldProperties(args)
    self.assertTrue(sgp.IsTryjobRun())

  def testGetGitRevision_revisionSet(self):
    args = createSkiaGoldArgs(git_revision='a')
    sgp = gold_utils.SkiaGoldProperties(args)
    self.assertEqual(sgp.git_revision, 'a')

  def testGetGitRevision_findValidRevision(self):
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with mock.patch(
        'pylib.utils.repo_utils.GetGitOriginMasterHeadSHA1') as patched_head:
      expected = 'a' * 40
      patched_head.return_value = expected
      self.assertEqual(sgp.git_revision, expected)
      # Should be cached.
      self.assertEqual(sgp._git_revision, expected)

  def testGetGitRevision_noExplicitOnBot(self):
    args = createSkiaGoldArgs(local_pixel_tests=False)
    sgp = gold_utils.SkiaGoldProperties(args)
    with self.assertRaises(RuntimeError):
      _ = sgp.git_revision

  def testGetGitRevision_findEmptyRevision(self):
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with mock.patch(
        'pylib.utils.repo_utils.GetGitOriginMasterHeadSHA1') as patched_head:
      patched_head.return_value = ''
      with self.assertRaises(RuntimeError):
        _ = sgp.git_revision

  def testGetGitRevision_findMalformedRevision(self):
    args = createSkiaGoldArgs(local_pixel_tests=True)
    sgp = gold_utils.SkiaGoldProperties(args)
    with mock.patch(
        'pylib.utils.repo_utils.GetGitOriginMasterHeadSHA1') as patched_head:
      patched_head.return_value = 'a' * 39
      with self.assertRaises(RuntimeError):
        _ = sgp.git_revision


if __name__ == '__main__':
  unittest.main(verbosity=2)
