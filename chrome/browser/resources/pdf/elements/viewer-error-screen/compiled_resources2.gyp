# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'viewer-error-screen',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_dialog/compiled_resources2.gyp:cr_dialog',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
