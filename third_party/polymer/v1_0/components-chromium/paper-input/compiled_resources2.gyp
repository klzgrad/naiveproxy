# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'paper-input-addon-behavior-extracted',
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-input-behavior-extracted',
      'dependencies': [
        '../iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        '../iron-behaviors/compiled_resources2.gyp:iron-control-state-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-input-char-counter-extracted',
      'dependencies': [
        'paper-input-addon-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-input-container-extracted',
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-input-error-extracted',
      'dependencies': [
        'paper-input-addon-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-input-extracted',
      'dependencies': [
        '../iron-form-element-behavior/compiled_resources2.gyp:iron-form-element-behavior-extracted',
        '../iron-input/compiled_resources2.gyp:iron-input-extracted',
        'paper-input-behavior-extracted',
        'paper-input-char-counter-extracted',
        'paper-input-container-extracted',
        'paper-input-error-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-textarea-extracted',
      'dependencies': [
        '../iron-autogrow-textarea/compiled_resources2.gyp:iron-autogrow-textarea-extracted',
        '../iron-form-element-behavior/compiled_resources2.gyp:iron-form-element-behavior-extracted',
        'paper-input-behavior-extracted',
        'paper-input-char-counter-extracted',
        'paper-input-container-extracted',
        'paper-input-error-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
