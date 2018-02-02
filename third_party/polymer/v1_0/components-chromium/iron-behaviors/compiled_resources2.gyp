# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'iron-button-state-extracted',
      'dependencies': [
        '../iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        'iron-control-state-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'iron-control-state-extracted',
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
