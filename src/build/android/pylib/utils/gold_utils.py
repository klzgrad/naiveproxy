# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Utilities for interacting with the Skia Gold image diffing service."""

import json
import logging
import os
import shutil
import tempfile

from devil.utils import cmd_helper
from pylib.base.output_manager import Datatype
from pylib.constants import host_paths
from pylib.utils import local_utils
from pylib.utils import repo_utils

DEFAULT_INSTANCE = 'chrome'

GOLDCTL_BINARY = os.path.join(host_paths.DIR_SOURCE_ROOT, 'tools',
                              'skia_goldctl', 'linux', 'goldctl')


class SkiaGoldSession(object):
  class StatusCodes(object):
    """Status codes for RunComparison."""
    SUCCESS = 0
    AUTH_FAILURE = 1
    COMPARISON_FAILURE_REMOTE = 2
    COMPARISON_FAILURE_LOCAL = 3
    LOCAL_DIFF_FAILURE = 4
    NO_OUTPUT_MANAGER = 5

  class ComparisonResults(object):
    """Struct-like object for storing results of an image comparison."""

    def __init__(self):
      self.triage_link = None
      self.triage_link_omission_reason = None
      self.local_diff_given_image = None
      self.local_diff_closest_image = None
      self.local_diff_diff_image = None

  def __init__(self, working_dir, gold_properties, instance=DEFAULT_INSTANCE):
    """A class to handle all aspects of an image comparison via Skia Gold.

    Args:
      working_dir: The directory to store config files, etc. Sharing the same
          working directory between multiple SkiaGoldSessions allows re-use of
          authentication and downloaded baselines.
      gold_properties: A SkiaGoldProperties instance for the current test run.
      instance: The name of the Skia Gold instance to interact with.
    """
    self._working_dir = working_dir
    self._gold_properties = gold_properties
    self._instance = instance
    self._triage_link_file = tempfile.NamedTemporaryFile(
        suffix='.txt', dir=working_dir, delete=False).name
    # A map of image name (string) to ComparisonResults for that image.
    self._comparison_results = {}

  def RunComparison(self,
                    name,
                    keys_file,
                    png_file,
                    output_manager,
                    use_luci=True):
    """Helper method to run all steps to compare a produced image.

    Handles authentication, comparison, and, if necessary, local diffing.

    Args:
      name: The name of the image being compared.
      keys_file: A path to a JSON file containing various comparison config
          data such as corpus and debug information like the hardware/software
          configuration the image was produced on.
      png_file: A path to a PNG file containing the image to be compared.
      output_manager: The output manager used to save local diff images if
          necessary. Can be None, but will fail if it ends up needing to be used
          and is not set.
      use_luci: If true, authentication will use the service account provided
          by the LUCI context. If false, will attempt to use whatever is set up
          in gsutil, which is only supported for local runs.

    Returns:
      A tuple (status, error). |status| is a value from
      SkiaGoldSession.StatusCodes signifying the result of the comparison.
      |error| is an error message describing the status if not successful.
    """
    auth_rc, auth_stdout = self.Authenticate(use_luci=use_luci)
    if auth_rc:
      return self.StatusCodes.AUTH_FAILURE, auth_stdout

    with open(keys_file) as f:
      corpus = json.load(f).get('source_type', self._instance)

    compare_rc, compare_stdout = self.Compare(
        name=name, keys_file=keys_file, png_file=png_file, corpus=corpus)
    if not compare_rc:
      return self.StatusCodes.SUCCESS, None

    logging.error('Gold comparison failed: %s', compare_stdout)
    if not self._gold_properties.local_pixel_tests:
      return self.StatusCodes.COMPARISON_FAILURE_REMOTE, compare_stdout

    if not output_manager:
      return (self.StatusCodes.NO_OUTPUT_MANAGER,
              'No output manager for local diff images')
    diff_rc, diff_stdout = self.Diff(
        name=name,
        png_file=png_file,
        corpus=corpus,
        output_manager=output_manager)
    if diff_rc:
      return self.StatusCodes.LOCAL_DIFF_FAILURE, diff_stdout
    return self.StatusCodes.COMPARISON_FAILURE_LOCAL, compare_stdout

  def Authenticate(self, use_luci=True):
    """Authenticates with Skia Gold for this session.

    Args:
      use_luci: If true, authentication will use the service account provided
          by the LUCI context. If false, will attempt to use whatever is set up
          in gsutil, which is only supported for local runs.

    Returns:
      A tuple (return_code, output). |return_code| is the return code of the
      authentication process. |output| is the stdout + stderr of the
      authentication process.
    """
    if self._gold_properties.bypass_skia_gold_functionality:
      logging.warning('Not actually authenticating with Gold due to '
                      '--bypass-skia-gold-functionality being present.')
      return 0, ''

    auth_cmd = [GOLDCTL_BINARY, 'auth', '--work-dir', self._working_dir]
    if use_luci:
      auth_cmd.append('--luci')
    elif not self._gold_properties.local_pixel_tests:
      raise RuntimeError(
          'Cannot authenticate to Skia Gold with use_luci=False unless running '
          'local pixel tests')

    rc, stdout, _ = cmd_helper.GetCmdStatusOutputAndError(
        auth_cmd, merge_stderr=True)
    return rc, stdout

  def Compare(self, name, keys_file, png_file, corpus):
    """Compares the given image to images known to Gold.

    Triage links can later be retrieved using GetTriageLink().

    Args:
      name: The name of the image being compared.
      keys_file: A path to a JSON file containing various comparison config
          data such as corpus and debug information like the hardware/software
          configuration the image was produced on.
      png_file: A path to a PNG file containing the image to be compared.
      corpus: The corpus that the image belongs to.

    Returns:
      A tuple (return_code, output). |return_code| is the return code of the
      comparison process. |output| is the stdout + stderr of the comparison
      process.
    """
    if self._gold_properties.bypass_skia_gold_functionality:
      logging.warning('Not actually comparing with Gold due to '
                      '--bypass-skia-gold-functionality being present.')
      return 0, ''

    compare_cmd = [
        GOLDCTL_BINARY,
        'imgtest',
        'add',
        '--passfail',
        '--test-name',
        name,
        '--instance',
        self._instance,
        '--corpus',
        corpus,
        '--keys-file',
        keys_file,
        '--png-file',
        png_file,
        '--work-dir',
        self._working_dir,
        '--failure-file',
        self._triage_link_file,
        '--commit',
        self._gold_properties.git_revision,
    ]
    if self._gold_properties.local_pixel_tests:
      compare_cmd.append('--dryrun')
    if self._gold_properties.IsTryjobRun():
      compare_cmd.extend([
          '--issue',
          str(self._gold_properties.issue),
          '--patchset',
          str(self._gold_properties.patchset),
          '--jobid',
          str(self._gold_properties.job_id),
          '--crs',
          str(self._gold_properties.code_review_system),
          '--cis',
          str(self._gold_properties.continuous_integration_system),
      ])

    rc, stdout, _ = cmd_helper.GetCmdStatusOutputAndError(
        compare_cmd, merge_stderr=True)

    self._comparison_results[name] = self.ComparisonResults()
    if rc == 0:
      self._comparison_results[name].triage_link_omission_reason = (
          'Comparison succeeded, no triage link')
    elif self._gold_properties.IsTryjobRun():
      # TODO(skbug.com/9879): Remove the explicit corpus when Gold's UI is
      # updated to show results from all corpora for tryjobs.
      cl_triage_link = ('https://{instance}-gold.skia.org/search?'
                        'issue={issue}&'
                        'new_clstore=true&'
                        'query=source_type%3D{corpus}')
      cl_triage_link = cl_triage_link.format(
          instance=self._instance,
          issue=self._gold_properties.issue,
          corpus=corpus)
      self._comparison_results[name].triage_link = cl_triage_link
    else:
      try:
        with open(self._triage_link_file) as tlf:
          triage_link = tlf.read().strip()
        self._comparison_results[name].triage_link = triage_link
      except IOError:
        self._comparison_results[name].triage_link_omission_reason = (
            'Failed to read triage link from file')
    return rc, stdout

  def Diff(self, name, png_file, corpus, output_manager):
    """Performs a local image diff against the closest known positive in Gold.

    This is used for running tests on a workstation, where uploading data to
    Gold for ingestion is not allowed, and thus the web UI is not available.

    Image links can later be retrieved using Get*ImageLink().

    Args:
      name: The name of the image being compared.
      png_file: The path to a PNG file containing the image to be diffed.
      corpus: The corpus that the image belongs to.
      output_manager: The output manager used to save local diff images.

    Returns:
      A tuple (return_code, output). |return_code| is the return code of the
      diff process. |output| is the stdout + stderr of the diff process.
    """
    # Instead of returning that everything is okay and putting in dummy links,
    # just fail since this should only be called when running locally and
    # --bypass-skia-gold-functionality is only meant for use on the bots.
    if self._gold_properties.bypass_skia_gold_functionality:
      raise RuntimeError(
          '--bypass-skia-gold-functionality is not supported when running '
          'tests locally.')

    # Output managers only support archived files, not directories, so we have
    # to use a temporary directory and later move the data into the archived
    # files.
    output_dir = tempfile.mkdtemp(dir=self._working_dir)
    diff_cmd = [
        GOLDCTL_BINARY,
        'diff',
        '--corpus',
        corpus,
        '--instance',
        self._instance,
        '--input',
        png_file,
        '--test',
        name,
        '--work-dir',
        self._working_dir,
        '--out-dir',
        output_dir,
    ]
    rc, stdout, _ = cmd_helper.GetCmdStatusOutputAndError(
        diff_cmd, merge_stderr=True)
    given_path = closest_path = diff_path = None
    # The directory should contain "input-<hash>.png", "closest-<hash>.png",
    # and "diff.png".
    for f in os.listdir(output_dir):
      filepath = os.path.join(output_dir, f)
      if f.startswith('input-'):
        given_path = filepath
      elif f.startswith('closest-'):
        closest_path = filepath
      elif f == 'diff.png':
        diff_path = filepath
    results = self._comparison_results.setdefault(name,
                                                  self.ComparisonResults())
    if given_path:
      with output_manager.ArchivedTempfile('given_%s.png' % name,
                                           'gold_local_diffs',
                                           Datatype.PNG) as given_file:
        shutil.move(given_path, given_file.name)
      results.local_diff_given_image = given_file.Link()
    if closest_path:
      with output_manager.ArchivedTempfile('closest_%s.png' % name,
                                           'gold_local_diffs',
                                           Datatype.PNG) as closest_file:
        shutil.move(closest_path, closest_file.name)
      results.local_diff_closest_image = closest_file.Link()
    if diff_path:
      with output_manager.ArchivedTempfile(
          'diff_%s.png' % name, 'gold_local_diffs', Datatype.PNG) as diff_file:
        shutil.move(diff_path, diff_file.name)
      results.local_diff_diff_image = diff_file.Link()
    return rc, stdout

  def GetTriageLink(self, name):
    """Gets the triage link for the given image.

    Args:
      name: The name of the image to retrieve the triage link for.

    Returns:
      A string containing the triage link if it is available, or None if it is
      not available for some reason. The reason can be retrieved using
      GetTriageLinkOmissionReason.
    """
    return self._comparison_results.get(name,
                                        self.ComparisonResults()).triage_link

  def GetTriageLinkOmissionReason(self, name):
    """Gets the reason why a triage link is not available for an image.

    Args:
      name: The name of the image whose triage link does not exist.

    Returns:
      A string containing the reason why a triage link is not available.
    """
    if name not in self._comparison_results:
      return 'No image comparison performed for %s' % name
    results = self._comparison_results[name]
    # This method should not be called if there is a valid triage link.
    assert results.triage_link is None
    if results.triage_link_omission_reason:
      return results.triage_link_omission_reason
    if results.local_diff_given_image:
      return 'Gold only used to do a local image diff'
    raise RuntimeError(
        'Somehow have a ComparisonResults instance for %s that should not '
        'exist' % name)

  def GetGivenImageLink(self, name):
    """Gets the link to the given image used for local diffing.

    Args:
      name: The name of the image that was diffed.

    Returns:
      A string containing the link to where the image is saved, or None if it
      does not exist. Since local diffing should only be done when running
      locally, this *should* be a file:// URL, but there is no guarantee of
      that.
    """
    assert name in self._comparison_results
    return self._comparison_results[name].local_diff_given_image

  def GetClosestImageLink(self, name):
    """Gets the link to the closest known image used for local diffing.

    Args:
      name: The name of the image that was diffed.

    Returns:
      A string containing the link to where the image is saved, or None if it
      does not exist. Since local diffing should only be done when running
      locally, this *should* be a file:// URL, but there is no guarantee of
      that.
    """
    assert name in self._comparison_results
    return self._comparison_results[name].local_diff_closest_image

  def GetDiffImageLink(self, name):
    """Gets the link to the diff between the given and closest images.

    Args:
      name: The name of the image that was diffed.

    Returns:
      A string containing the link to where the image is saved, or None if it
      does not exist. Since local diffing should only be done when running
      locally, this *should* be a file:// URL, but there is no guarantee of
      that.
    """
    assert name in self._comparison_results
    return self._comparison_results[name].local_diff_diff_image


class SkiaGoldProperties(object):
  def __init__(self, args):
    """Class to validate and store properties related to Skia Gold.

    Args:
      args: The parsed arguments from an argparse.ArgumentParser.
    """
    self._git_revision = None
    self._issue = None
    self._patchset = None
    self._job_id = None
    self._local_pixel_tests = None
    self._no_luci_auth = None
    self._bypass_skia_gold_functionality = None

    # Could in theory be configurable, but hard-coded for now since there's
    # no plan to support anything else.
    self._code_review_system = 'gerrit'
    self._continuous_integration_system = 'buildbucket'

    self._InitializeProperties(args)

  def IsTryjobRun(self):
    return self.issue is not None

  @property
  def continuous_integration_system(self):
    return self._continuous_integration_system

  @property
  def code_review_system(self):
    return self._code_review_system

  @property
  def git_revision(self):
    return self._GetGitRevision()

  @property
  def issue(self):
    return self._issue

  @property
  def job_id(self):
    return self._job_id

  @property
  def local_pixel_tests(self):
    return self._IsLocalRun()

  @property
  def no_luci_auth(self):
    return self._no_luci_auth

  @property
  def patchset(self):
    return self._patchset

  @property
  def bypass_skia_gold_functionality(self):
    return self._bypass_skia_gold_functionality

  def _GetGitRevision(self):
    if not self._git_revision:
      # Automated tests should always pass the revision, so assume we're on
      # a workstation and try to get the local origin/master HEAD.
      if not self._IsLocalRun():
        raise RuntimeError(
            '--git-revision was not passed when running on a bot')
      revision = repo_utils.GetGitOriginMasterHeadSHA1(
          host_paths.DIR_SOURCE_ROOT)
      if not revision or len(revision) != 40:
        raise RuntimeError(
            '--git-revision not passed and unable to determine from git')
      self._git_revision = revision
    return self._git_revision

  def _IsLocalRun(self):
    if self._local_pixel_tests is None:
      self._local_pixel_tests = not local_utils.IsOnSwarming()
      if self._local_pixel_tests:
        logging.warning(
            'Automatically determined that test is running on a workstation')
      else:
        logging.warning(
            'Automatically determined that test is running on a bot')
    return self._local_pixel_tests

  def _InitializeProperties(self, args):
    if hasattr(args, 'local_pixel_tests'):
      # If not set, will be automatically determined later if needed.
      self._local_pixel_tests = args.local_pixel_tests

    if hasattr(args, 'no_luci_auth'):
      self._no_luci_auth = args.no_luci_auth

    if hasattr(args, 'bypass_skia_gold_functionality'):
      self._bypass_skia_gold_functionality = args.bypass_skia_gold_functionality

    # Will be automatically determined later if needed.
    if not hasattr(args, 'git_revision') or not args.git_revision:
      return
    self._git_revision = args.git_revision

    # Only expected on tryjob runs.
    if not hasattr(args, 'gerrit_issue') or not args.gerrit_issue:
      return
    self._issue = args.gerrit_issue
    if not hasattr(args, 'gerrit_patchset') or not args.gerrit_patchset:
      raise RuntimeError(
          '--gerrit-issue passed, but --gerrit-patchset not passed.')
    self._patchset = args.gerrit_patchset
    if not hasattr(args, 'buildbucket_id') or not args.buildbucket_id:
      raise RuntimeError(
          '--gerrit-issue passed, but --buildbucket-id not passed.')
    self._job_id = args.buildbucket_id
