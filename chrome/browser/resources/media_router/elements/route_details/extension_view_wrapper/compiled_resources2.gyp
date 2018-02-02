# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'extension_view_wrapper',
      'dependencies': [
        '../../../compiled_resources2.gyp:media_router_data',
      ],
      'includes': ['../../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
