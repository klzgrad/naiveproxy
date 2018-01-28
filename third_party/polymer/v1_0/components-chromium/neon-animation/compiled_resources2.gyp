# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'neon-animatable-behavior-extracted',
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'neon-animatable-extracted',
      'dependencies': [
        '../iron-resizable-behavior/compiled_resources2.gyp:iron-resizable-behavior-extracted',
        'neon-animatable-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'neon-animated-pages-extracted',
      'dependencies': [
        '../iron-resizable-behavior/compiled_resources2.gyp:iron-resizable-behavior-extracted',
        '../iron-selector/compiled_resources2.gyp:iron-selectable-extracted',
        'neon-animation-runner-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'neon-animation-behavior-extracted',
      'dependencies': [
        '../iron-meta/compiled_resources2.gyp:iron-meta-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'neon-animation-runner-behavior-extracted',
      'dependencies': [
        '../iron-meta/compiled_resources2.gyp:iron-meta-extracted',
        'neon-animatable-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'neon-shared-element-animatable-behavior-extracted',
      'dependencies': [
        'neon-animatable-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'neon-shared-element-animation-behavior-extracted',
      'dependencies': [
        'neon-animation-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
