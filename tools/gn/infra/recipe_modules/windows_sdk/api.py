# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from contextlib import contextmanager

from recipe_engine import recipe_api


class WindowsSDKApi(recipe_api.RecipeApi):
  """API for using Windows SDK distributed via CIPD."""

  def __init__(self, sdk_properties, *args, **kwargs):
    super(WindowsSDKApi, self).__init__(*args, **kwargs)

    self._sdk_package = sdk_properties['sdk_package']
    self._sdk_version = sdk_properties['sdk_version']

  @contextmanager
  def __call__(self):
    """Setups the Windows SDK environment.

    This call is a no-op on non-Windows platforms.

    Raises:
        StepFailure or InfraFailure.
    """
    if not self.m.platform.is_win:
      yield
      return

    try:
      with self.m.context(infra_steps=True):
        sdk_dir = self._ensure_sdk()
      with self.m.context(**self._sdk_env(sdk_dir)):
        yield
    finally:
      # cl.exe automatically starts background mspdbsrv.exe daemon which
      # needs to be manually stopped so Swarming can tidy up after itself.
      self.m.step('taskkill mspdbsrv',
                  ['taskkill.exe', '/f', '/t', '/im', 'mspdbsrv.exe'])

  def _ensure_sdk(self):
    """Ensures the Windows SDK CIPD package is installed.

    Returns the directory where the SDK package has been installed.

    Args:
      path (path): Path to a directory.
      version (str): CIPD instance ID, tag or ref.
    """
    sdk_dir = self.m.path['cache'].join('windows_sdk')
    pkgs = self.m.cipd.EnsureFile()
    pkgs.add_package(self._sdk_package, self._sdk_version)
    self.m.cipd.ensure(sdk_dir, pkgs)
    return sdk_dir

  def _sdk_env(self, sdk_dir):
    """Constructs the environment for the SDK.

    Returns environment and environment prefixes.

    Args:
      sdk_dir (path): Path to a directory containing the SDK.
    """
    env = {}
    env_prefixes = {}

    # Load .../win_sdk/bin/SetEnv.${arch}.json to extract the required
    # environment. It contains a dict that looks like this:
    # {
    #   "env": {
    #     "VAR": [["..", "..", "x"], ["..", "..", "y"]],
    #     ...
    #   }
    # }
    # All these environment variables need to be added to the environment
    # for the compiler and linker to work.
    filename = 'SetEnv.%s.json' % {32: 'x86', 64: 'x64'}[self.m.platform.bits]
    step_result = self.m.json.read(
        'read %s' % filename,
        sdk_dir.join('win_sdk', 'bin', filename),
        step_test_data=lambda: self.m.json.test_api.output({
            'env': {
                'PATH': [['..', '..', 'win_sdk', 'bin', 'x64']],
                'VSINSTALLDIR': [['..', '..\\']],},}))
    data = step_result.json.output.get('env')
    for key in data:
      # recipes' Path() does not like .., ., \, or /, so this is cumbersome.
      # What we want to do is:
      #   [sdk_bin_dir.join(*e) for e in env[k]]
      # Instead do that badly, and rely (but verify) on the fact that the paths
      # are all specified relative to the root, but specified relative to
      # win_sdk/bin (i.e. everything starts with "../../".)
      results = []
      for value in data[key]:
        assert value[0] == '..' and (value[1] == '..' or value[1] == '..\\')
        results.append('%s' % sdk_dir.join(*value[2:]))

      # PATH is special-cased because we don't want to overwrite other things
      # like C:\Windows\System32. Others are replacements because prepending
      # doesn't necessarily makes sense, like VSINSTALLDIR.
      if key.lower() == 'path':
        env_prefixes[key] = results
      else:
        env[key] = ';'.join(results)

    return {'env': env, 'env_prefixes': env_prefixes}
