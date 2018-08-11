# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'select_to_speak',
      'dependencies': [
        '../chromevox/cvox2/background/constants',
        '../chromevox/cvox2/background/automation_util',
        'externs',
        'rect_utils',
        'paragraph_utils',
        'word_utils',
        'node_utils',
        '<(EXTERNS_GYP):accessibility_private',
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):clipboard',
        '<(EXTERNS_GYP):command_line_private',
        '<(EXTERNS_GYP):metrics_private',
       ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'select_to_speak_options',
      'dependencies': [
        'externs',
        '<(EXTERNS_GYP):accessibility_private',
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):metrics_private',
       ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'externs',
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'node_utils',
      'dependencies': [
        'externs',
        'rect_utils',
        'paragraph_utils',
        '<(EXTERNS_GYP):automation',
       ],
       'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'word_utils',
      'dependencies': [
        'externs',
        'paragraph_utils',
        '<(EXTERNS_GYP):automation',
       ],
       'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'paragraph_utils',
      'dependencies': [
        'externs',
        '<(EXTERNS_GYP):accessibility_private',
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
       ],
       'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'rect_utils',
       'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': '../chromevox/cvox2/background/automation_util',
      'dependencies': [
        '../chromevox/cvox2/background/automation_predicate',
        '../chromevox/cvox2/background/tree_walker',
        '../chromevox/cvox2/background/constants',
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes':  ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': '../chromevox/cvox2/background/tree_walker',
      'dependencies': [
        '../chromevox/cvox2/background/automation_predicate',
        '../chromevox/cvox2/background/constants',
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes':  ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': '../chromevox/cvox2/background/automation_predicate',
      'dependencies': [
        '../chromevox/cvox2/background/constants',
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes':  ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': '../chromevox/cvox2/background/constants',
      'includes':  ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'closure_shim',
      'includes':  ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
