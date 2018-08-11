# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'cr_toolbar_search_field',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_search_field/compiled_resources2.gyp:cr_search_field_behavior',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_toolbar_selection_overlay',
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-button/compiled_resources2.gyp:paper-button-extracted',
      ],
    },
    {
      'target_name': 'cr_toolbar',
      'dependencies': [
        '<(EXTERNS_GYP):web_animations',
        'cr_toolbar_search_field',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
