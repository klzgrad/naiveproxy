# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'network_ui',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_network_icon_externs',
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:util',
        '<(EXTERNS_GYP):networking_private',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
