# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'paper-fab-extracted',
      'dependencies': [
        '../iron-icon/compiled_resources2.gyp:iron-icon-extracted',
        '../paper-behaviors/compiled_resources2.gyp:paper-button-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
