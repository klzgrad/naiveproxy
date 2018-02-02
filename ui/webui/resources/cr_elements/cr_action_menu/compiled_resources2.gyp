# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'cr_action_menu',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
