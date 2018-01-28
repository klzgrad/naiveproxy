# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'paper-slider-extracted',
      'dependencies': [
        '../iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        '../iron-form-element-behavior/compiled_resources2.gyp:iron-form-element-behavior-extracted',
        '../iron-range-behavior/compiled_resources2.gyp:iron-range-behavior-extracted',
        '../paper-behaviors/compiled_resources2.gyp:paper-inky-focus-behavior-extracted',
        '../paper-input/compiled_resources2.gyp:paper-input-extracted',
        '../paper-progress/compiled_resources2.gyp:paper-progress-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
