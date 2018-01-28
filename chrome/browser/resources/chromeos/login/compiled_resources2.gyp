# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'offline_ad_login',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'oobe_change_picture',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/cr_picture/compiled_resources2.gyp:cr_picture_list',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/cr_picture/compiled_resources2.gyp:cr_picture_pane',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/cr_picture/compiled_resources2.gyp:cr_picture_types',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
