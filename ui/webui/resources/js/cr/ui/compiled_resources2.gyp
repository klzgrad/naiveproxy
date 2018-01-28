# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'alert_overlay',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'array_data_model',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:event_target',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'autocomplete_list',
      'dependencies': [
        'list',
        'list_single_selection_model',
        'position_util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'command',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:ui',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'context_menu_button',
      'dependencies': [
        'menu_button',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'context_menu_handler',
      'dependencies': [
         '../../compiled_resources2.gyp:cr',
         '../compiled_resources2.gyp:event_target',
         '../compiled_resources2.gyp:ui',
         'menu',
         'menu_button',
         'position_util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'dialogs',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'drag_wrapper',
      'dependencies': [
        '../../compiled_resources2.gyp:assert',
        '../../compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'focus_grid',
      'dependencies': [
        '../../compiled_resources2.gyp:assert',
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:event_tracker',
        'focus_row',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'focus_manager',
      'dependencies': ['../../compiled_resources2.gyp:cr'],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'focus_outline_manager',
      'dependencies': ['../../compiled_resources2.gyp:cr'],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'focus_row',
      'dependencies': [
        '../../compiled_resources2.gyp:assert',
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:event_tracker',
        '../../compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'focus_without_ink',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:ui',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'grid',
      'dependencies': [
        'list',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'list',
      'dependencies': [
        'array_data_model',
        'list_item',
        'list_selection_controller',
        'list_selection_model',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'list_item',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'list_selection_controller',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        'list_selection_model',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'list_selection_model',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:event_target',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'list_single_selection_model',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:event_target',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'menu_button',
      'dependencies': [
        '../../compiled_resources2.gyp:assert',
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:event_tracker',
        '../compiled_resources2.gyp:ui',
        'menu',
        'menu_item',
        'position_util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'menu_item',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:load_time_data',
        '../compiled_resources2.gyp:ui',
        'command',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'menu',
      'dependencies': [
        '../../compiled_resources2.gyp:assert',
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:ui',
        'menu_item',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'node_utils',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'overlay',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:util',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'position_util',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'splitter',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../compiled_resources2.gyp:ui',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'table',
      'dependencies': [
        'list',
        'list_single_selection_model',
        'table/compiled_resources2.gyp:table_column_model',
        'table/compiled_resources2.gyp:table_header',
        'table/compiled_resources2.gyp:table_list',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'tree',
      'dependencies': [
        '../../compiled_resources2.gyp:cr',
        '../../compiled_resources2.gyp:util',
        '../compiled_resources2.gyp:ui',
      ],
      'includes': ['../../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
