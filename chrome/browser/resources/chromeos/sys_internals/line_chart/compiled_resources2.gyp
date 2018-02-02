# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'line_chart',
      'dependencies': [
        'sub_chart',
        'data_series',
        'scrollbar',
        'menu',
        'constants',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'sub_chart',
      'dependencies': [
        'unit_label',
        'data_series',
        'constants',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'scrollbar',
      'dependencies': [
        'constants',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'unit_label',
      'dependencies': [
        'constants',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'menu',
      'dependencies': [
        'constants',
        'data_series',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'data_series',
      'dependencies': [
        'constants',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'constants',
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
