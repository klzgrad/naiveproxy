# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The `macos_sdk` module provides safe functions to access a semi-hermetic
XCode installation.

Available only to Google-run bots."""

from contextlib import contextmanager

from recipe_engine import recipe_api


class MacOSSDKApi(recipe_api.RecipeApi):
  """API for using OS X SDK distributed via CIPD."""

  def __init__(self, sdk_properties, *args, **kwargs):
    super(MacOSSDKApi, self).__init__(*args, **kwargs)

    self._sdk_version = sdk_properties['sdk_version'].lower()
    self._tool_package = sdk_properties['tool_package']
    self._tool_version = sdk_properties['tool_version']

  @contextmanager
  def __call__(self):
    """Sets up the XCode SDK environment.

    This call is a no-op on non-Mac platforms.

    This will deploy the helper tool and the XCode.app bundle at
    `[START_DIR]/cache/macos_sdk`.

    To avoid machines rebuilding these on every run, set up a named cache in
    your cr-buildbucket.cfg file like:

        caches: {
          # Cache for mac_toolchain tool and XCode.app
          name: "macos_sdk"
          path: "macos_sdk"
        }

    If you have builders which e.g. use a non-current SDK, you can give them
    a uniqely named cache:

        caches: {
          # Cache for N-1 version mac_toolchain tool and XCode.app
          name: "macos_sdk_old"
          path: "macos_sdk"
        }

    Usage:
      with api.macos_sdk():
        # sdk with mac build bits

    Raises:
        StepFailure or InfraFailure.
    """
    if not self.m.platform.is_mac:
      yield
      return

    try:
      with self.m.context(infra_steps=True):
        sdk_dir = self._ensure_sdk()
        self.m.step('select XCode',
                    ['sudo', 'xcode-select', '--switch', sdk_dir])
      yield
    finally:
      with self.m.context(infra_steps=True):
        self.m.step('reset XCode', ['sudo', 'xcode-select', '--reset'])

  def _ensure_sdk(self):
    """Ensures the mac_toolchain tool and MacOS SDK packages are installed.

    Returns Path to the installed sdk app bundle."""
    cache_dir = self.m.path['cache'].join('macos_sdk')
    pkgs = self.m.cipd.EnsureFile()
    pkgs.add_package(self._tool_package, self._tool_version)
    self.m.cipd.ensure(cache_dir, pkgs)

    sdk_dir = cache_dir.join('XCode.app')
    self.m.step('install xcode', [
        cache_dir.join('mac_toolchain'),
        'install',
        '-kind',
        'mac',
        '-xcode-version',
        self._sdk_version,
        '-output-dir',
        sdk_dir,
    ])
    return sdk_dir
