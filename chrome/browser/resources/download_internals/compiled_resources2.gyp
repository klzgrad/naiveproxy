# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'download_internals',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        'download_internals_browser_proxy',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'download_internals_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'download_internals_visuals',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'download_internals_browser_proxy',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
