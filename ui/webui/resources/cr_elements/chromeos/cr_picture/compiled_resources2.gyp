# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'dependencies': [ 'cr_png_behavior' ],
      'target_name': 'cr_camera',
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-selector/compiled_resources2.gyp:iron-selector-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'cr_png_behavior',
        'cr_picture_types',
      ],
      'target_name': 'cr_picture_list',
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'dependencies': [
        'cr_camera',
        'cr_png_behavior',
        'cr_picture_types',
      ],
      'target_name': 'cr_picture_pane',
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_picture_types',
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_png_behavior',
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
