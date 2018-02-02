# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'md_pin_keyboard',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-button/compiled_resources2.gyp:paper-button-extracted',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-input/compiled_resources2.gyp:paper-input-extracted',
       ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'pin_keyboard',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-button/compiled_resources2.gyp:paper-button-extracted',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-input/compiled_resources2.gyp:paper-input-extracted',
       ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
