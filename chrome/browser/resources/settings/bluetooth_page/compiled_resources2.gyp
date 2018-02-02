# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'bluetooth_page',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '../prefs/compiled_resources2.gyp:prefs_behavior',
        '../settings_page/compiled_resources2.gyp:settings_animated_pages',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):bluetooth',
        '<(EXTERNS_GYP):bluetooth_private',
        '<(INTERFACES_GYP):bluetooth_interface',
        '<(INTERFACES_GYP):bluetooth_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'bluetooth_subpage',
      'dependencies': [
        '../compiled_resources2.gyp:route',
        '<(DEPTH)/ui/webui/resources/cr_elements/compiled_resources2.gyp:cr_scrollable_behavior',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):bluetooth',
        '<(EXTERNS_GYP):bluetooth_private',
        '<(INTERFACES_GYP):bluetooth_interface',
        '<(INTERFACES_GYP):bluetooth_private_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'bluetooth_device_list_item',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:i18n_behavior',
        '<(EXTERNS_GYP):bluetooth',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_action_menu/compiled_resources2.gyp:cr_action_menu',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
