# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'actions',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_extensions',
        'util',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'api_listener',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_extensions',
        'actions',
        'debouncer',
        'store',
        'util',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'app',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:splitter',
        '<(EXTERNS_GYP):chrome_extensions',
        'api_listener',
        'dnd_manager',
        'mouse_focus_behavior',
        'router',
        'store',
        'store_client',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'command_manager',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_action_menu/compiled_resources2.gyp:cr_action_menu',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_lazy_render/compiled_resources2.gyp:cr_lazy_render',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(DEPTH)/ui/webui/resources/js/cr/ui/compiled_resources2.gyp:command',
        '<(EXTERNS_GYP):bookmark_manager_private',
        'api_listener',
        'dialog_focus_manager',
        'edit_dialog',
        'store_client',
        'toast_manager'
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'constants',
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'debouncer',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'dialog_focus_manager',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'dnd_chip',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:icon',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'dnd_manager',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):bookmark_manager_private',
        '<(EXTERNS_GYP):metrics_private',
        'api_listener',
        'debouncer',
        'dnd_chip',
        'folder_node',
        'store',
        'types',
        'util',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'edit_dialog',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/paper-input/compiled_resources2.gyp:paper-input-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):chrome_extensions',
        'api_listener',
        'dialog_focus_manager',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'folder_node',
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        '<(EXTERNS_GYP):chrome_extensions',
        'actions',
        'command_manager',
        'store_client',
      ],
    },
    {
      'target_name': 'item',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:icon',
        '<(EXTERNS_GYP):chrome_extensions',
        'actions',
        'command_manager',
        'store_client',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'list',
      'dependencies': [
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-list/compiled_resources2.gyp:iron-list-extracted',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:load_time_data',
        'actions',
        'command_manager',
        'item',
        'store_client',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'mouse_focus_behavior',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'reducers',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'actions',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'router',
      'dependencies': [
        'actions',
        'store_client',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'store',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_extensions',
        'reducers',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'store_client',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        'store',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'toast_manager',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/iron-a11y-announcer/compiled_resources2.gyp:iron-a11y-announcer-extracted',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'toolbar',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_action_menu/compiled_resources2.gyp:cr_action_menu',
        '<(DEPTH)/ui/webui/resources/cr_elements/cr_toolbar/compiled_resources2.gyp:cr_toolbar',
        '<(EXTERNS_GYP):bookmark_manager_private',
        '<(EXTERNS_GYP):chrome_extensions',
        'command_manager',
        'edit_dialog',
        'store_client',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'types',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
        'constants',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    },
    {
      'target_name': 'util',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(EXTERNS_GYP):chrome_extensions',
        'types',
      ],
      'includes': ['../../../../third_party/closure_compiler/compile_js2.gypi']
    }
  ]
}
