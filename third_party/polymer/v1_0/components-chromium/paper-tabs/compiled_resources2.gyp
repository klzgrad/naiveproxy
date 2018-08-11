# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'paper-tab-extracted',
      'dependencies': [
        '../iron-behaviors/compiled_resources2.gyp:iron-button-state-extracted',
        '../iron-behaviors/compiled_resources2.gyp:iron-control-state-extracted',
        '../paper-behaviors/compiled_resources2.gyp:paper-ripple-behavior-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paper-tabs-extracted',
      'dependencies': [
        '../iron-icon/compiled_resources2.gyp:iron-icon-extracted',
        '../iron-menu-behavior/compiled_resources2.gyp:iron-menubar-behavior-extracted',
        '../iron-resizable-behavior/compiled_resources2.gyp:iron-resizable-behavior-extracted',
        '../paper-icon-button/compiled_resources2.gyp:paper-icon-button-extracted',
        'paper-tab-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
