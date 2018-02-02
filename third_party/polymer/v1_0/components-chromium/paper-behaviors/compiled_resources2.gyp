# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'paper-button-behavior-extracted',
      'dependencies': [
        '../iron-behaviors/compiled_resources2.gyp:iron-button-state-extracted',
        'paper-ripple-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-checked-element-behavior-extracted',
      'dependencies': [
        '../iron-checked-element-behavior/compiled_resources2.gyp:iron-checked-element-behavior-extracted',
        'paper-inky-focus-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-inky-focus-behavior-extracted',
      'dependencies': [
        '../iron-behaviors/compiled_resources2.gyp:iron-button-state-extracted',
        'paper-ripple-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-ripple-behavior-extracted',
      'dependencies': [
        '../paper-ripple/compiled_resources2.gyp:paper-ripple-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
