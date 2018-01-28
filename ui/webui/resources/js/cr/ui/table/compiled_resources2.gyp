# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'table_column',
      'dependencies': [
        '../../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:event_target',
      ],
      'includes': ['../../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'table_column_model',
      'dependencies': [
        '../../../compiled_resources2.gyp:cr',
        'table_column',
      ],
      'includes': ['../../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'table_header',
      'dependencies': [
        '../../../compiled_resources2.gyp:cr',
        'table_splitter',
      ],
      'includes': ['../../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'table_list',
      'dependencies': [
        '../../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:list',
        'table_column_model',
      ],
      'includes': ['../../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'table_splitter',
      'dependencies': [
        '../../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:splitter',
        'table_column_model',
      ],
      'includes': ['../../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
