# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'settings_ui',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/chromeos/network/compiled_resources2.gyp:cr_onc_types',
        '<(DEPTH)/ui/webui/resources/cr_elements/compiled_resources2.gyp:cr_container_shadow_behavior',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_drawer/compiled_resources2.gyp:cr_drawer',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_toolbar/compiled_resources2.gyp:cr_toolbar',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_toolbar/compiled_resources2.gyp:cr_toolbar_search_field',
        '<(DEPTH)/ui/webui/resources/cr_elements/policy/compiled_resources2.gyp:cr_policy_indicator_behavior',
        '../compiled_resources2.gyp:global_scroll_target_behavior',
        '../prefs/compiled_resources2.gyp:prefs',
        '../settings_main/compiled_resources2.gyp:settings_main',
        '../compiled_resources2.gyp:page_visibility'
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
