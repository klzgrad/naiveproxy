# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'settings_menu',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-ripple/compiled_resources2.gyp:paper-ripple-extracted',
        '../compiled_resources2.gyp:route',
        '../compiled_resources2.gyp:page_visibility',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
