# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'cr_radio_button',
      'dependencies': [
        'cr_radio_button_behavior'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'cr_radio_button_behavior',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-behaviors/compiled_resources2.gyp:paper-ripple-behavior-extracted',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
