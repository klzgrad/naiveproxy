# Copyright 2015 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configures devil for use in chromium."""

import os
import sys

from pylib import constants
from pylib.constants import host_paths

if host_paths.DEVIL_PATH not in sys.path:
  sys.path.insert(1, host_paths.DEVIL_PATH)

from devil import devil_env
from devil.android.ndk import abis

_BUILD_DIR = os.path.join(constants.DIR_SOURCE_ROOT, 'build')
if _BUILD_DIR not in sys.path:
  sys.path.insert(1, _BUILD_DIR)

import gn_helpers

_DEVIL_CONFIG = os.path.abspath(
    os.path.join(os.path.dirname(__file__), 'devil_chromium.json'))

_DEVIL_BUILD_PRODUCT_DEPS = {
  'chromium_commands': [
    {
      'platform': 'linux2',
      'arch': 'x86_64',
      'path_components': ['lib.java', 'chromium_commands.dex.jar'],
    }
  ],
  'forwarder_device': [
    {
      'platform': 'android',
      'arch': abis.ARM,
      'path_components': ['forwarder_dist'],
    },
    {
      'platform': 'android',
      'arch': abis.ARM_64,
      'path_components': ['forwarder_dist'],
    },
    {
      'platform': 'android',
      'arch': 'mips',
      'path_components': ['forwarder_dist'],
    },
    {
      'platform': 'android',
      'arch': 'mips64',
      'path_components': ['forwarder_dist'],
    },
    {
      'platform': 'android',
      'arch': abis.X86,
      'path_components': ['forwarder_dist'],
    },
    {
      'platform': 'android',
      'arch': abis.X86_64,
      'path_components': ['forwarder_dist'],
    },
  ],
  'forwarder_host': [
    {
      'platform': 'linux2',
      'arch': 'x86_64',
      'path_components': ['host_forwarder'],
    },
  ],
  'md5sum_device': [
    {
      'platform': 'android',
      'arch': abis.ARM,
      'path_components': ['md5sum_dist'],
    },
    {
      'platform': 'android',
      'arch': abis.ARM_64,
      'path_components': ['md5sum_dist'],
    },
    {
      'platform': 'android',
      'arch': 'mips',
      'path_components': ['md5sum_dist'],
    },
    {
      'platform': 'android',
      'arch': 'mips64',
      'path_components': ['md5sum_dist'],
    },
    {
      'platform': 'android',
      'arch': abis.X86,
      'path_components': ['md5sum_dist'],
    },
    {
      'platform': 'android',
      'arch': abis.X86_64,
      'path_components': ['md5sum_dist'],
    },
  ],
  'md5sum_host': [
    {
      'platform': 'linux2',
      'arch': 'x86_64',
      'path_components': ['md5sum_bin_host'],
    },
  ],
}


def _UseLocalBuildProducts(output_directory, devil_dynamic_config):
  output_directory = os.path.abspath(output_directory)
  devil_dynamic_config['dependencies'] = {
      dep_name: {
          'file_info': {
              '%s_%s' % (dep_config['platform'], dep_config['arch']): {
                  'local_paths': [
                      os.path.join(output_directory,
                                   *dep_config['path_components']),
                  ],
              }
              for dep_config in dep_configs
          }
      }
      for dep_name, dep_configs in _DEVIL_BUILD_PRODUCT_DEPS.items()
  }


def _BuildWithChromium():
  """Returns value of gclient's |build_with_chromium|."""
  gni_path = os.path.join(_BUILD_DIR, 'config', 'gclient_args.gni')
  if not os.path.exists(gni_path):
    return False
  with open(gni_path) as f:
    data = f.read()
  args = gn_helpers.FromGNArgs(data)
  return args.get('build_with_chromium', False)


def Initialize(output_directory=None,
               custom_deps=None,
               adb_path=None,
               use_local_devil_tools=False):
  """Initializes devil with chromium's binaries and third-party libraries.

  This includes:
    - Libraries:
      - the android SDK ("android_sdk")
    - Build products:
      - host & device forwarder binaries
          ("forwarder_device" and "forwarder_host")
      - host & device md5sum binaries ("md5sum_device" and "md5sum_host")

  Args:
    output_directory: An optional path to the output directory. If not set,
      no built dependencies are configured.
    custom_deps: An optional dictionary specifying custom dependencies.
      This should be of the form:

        {
          'dependency_name': {
            'platform': 'path',
            ...
          },
          ...
        }
    adb_path: An optional path to use for the adb binary. If not set, this uses
      the adb binary provided by the Android SDK.
    use_local_devil_tools: Use locally built versions of md5sum,
      forwarder_dist, etc.
  """
  build_with_chromium = _BuildWithChromium()

  devil_dynamic_config = {
    'config_type': 'BaseConfig',
    'dependencies': {},
  }
  if use_local_devil_tools:
    # Non-chromium users of chromium's //build directory fetch build products
    # from google storage rather than use locally built copies. Chromium uses
    # locally-built copies so that changes to the tools can be easily tested.
    _UseLocalBuildProducts(output_directory, devil_dynamic_config)

  if custom_deps:
    devil_dynamic_config['dependencies'].update(custom_deps)
  if adb_path:
    devil_dynamic_config['dependencies'].update({
      'adb': {
        'file_info': {
          devil_env.GetPlatform(): {
            'local_paths': [adb_path]
          }
        }
      }
    })

  config_files = [_DEVIL_CONFIG] if build_with_chromium else None
  devil_env.config.Initialize(configs=[devil_dynamic_config],
                              config_files=config_files)
