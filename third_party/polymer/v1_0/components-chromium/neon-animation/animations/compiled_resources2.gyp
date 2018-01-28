# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'cascaded-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'fade-in-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'fade-out-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'hero-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-shared-element-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'opaque-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'reverse-ripple-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-shared-element-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'ripple-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-shared-element-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'scale-down-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'scale-up-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-down-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-from-bottom-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-from-left-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-from-right-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-from-top-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-left-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-right-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'slide-up-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'transform-animation-extracted',
      'dependencies': [
        '../compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
