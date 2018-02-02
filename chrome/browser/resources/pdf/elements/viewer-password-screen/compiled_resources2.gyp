# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'viewer-password-screen',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-input/compiled_resources2.gyp:paper-input-extracted'
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
