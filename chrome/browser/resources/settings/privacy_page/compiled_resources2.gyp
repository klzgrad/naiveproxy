# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'privacy_page_browser_proxy',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:lifetime_browser_proxy',
        '<(EXTERNS_GYP):chrome_send',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'privacy_page',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:web_ui_listener_behavior',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:focus_without_ink',
        '../compiled_resources2.gyp:route',
        '../controls/compiled_resources2.gyp:settings_toggle_button',
        '../settings_page/compiled_resources2.gyp:settings_animated_pages',
        '../compiled_resources2.gyp:page_visibility',
        '../site_settings/compiled_resources2.gyp:constants',
        '../site_settings/compiled_resources2.gyp:site_data_details_subpage',
        'privacy_page_browser_proxy',
        '<(EXTERNS_GYP):settings_private',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
