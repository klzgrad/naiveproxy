# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'commands',
      'dependencies': [
        'filter',
        'image_editor_prompt',
        'image_util',
        'image_view',
        'viewport',
        '../../../file_manager/foreground/elements/compiled_resources2.gyp:files_toast',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'exif_encoder',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:exif_entry',
        '../../../file_manager/foreground/js/metadata/compiled_resources2.gyp:exif_constants',
        '../../../file_manager/foreground/js/metadata/compiled_resources2.gyp:metadata_item',
        'image_encoder',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'filter',
      'dependencies': [
        'image_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_adjust',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'commands',
        'image_editor',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_buffer',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_editor',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        'image_buffer',
        'image_editor_mode',
        'image_editor_toolbar',
        'image_resize',
        'image_util',
        'image_view',
        'viewport',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_editor_mode',
      'dependencies': [
        'commands',
        'image_buffer',
        'image_editor_toolbar',
        'image_view',
        'viewport',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_editor_prompt',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_editor_toolbar',
      'dependencies': [
        '../../../externs/compiled_resources2.gyp:paper_elements',
        '../../../file_manager/common/js/compiled_resources2.gyp:util',
        '../../../file_manager/foreground/elements/compiled_resources2.gyp:files_tooltip',
        '../compiled_resources2.gyp:gallery_util',
        'image_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_encoder',
      'dependencies': [
        '../../../file_manager/foreground/js/metadata/compiled_resources2.gyp:metadata_item',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'image_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_loader',
      'dependencies': [
        '../../../file_manager/common/js/compiled_resources2.gyp:file_type',
        '../../../file_manager/common/js/compiled_resources2.gyp:metrics_base',
        '../../../file_manager/common/js/compiled_resources2.gyp:util',
        '../../../file_manager/foreground/js/metadata/compiled_resources2.gyp:metadata_model',
        '../../../image_loader/compiled_resources2.gyp:image_loader_client',
        '../compiled_resources2.gyp:gallery_item',
        'image_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_resize',
      'dependencies': [
        '../../../file_manager/foreground/js/ui/compiled_resources2.gyp:files_alert_dialog',
        'image_editor_mode',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_transform',
      'dependencies': [
        'commands',
        'image_buffer',
        'image_editor_mode',
        'image_editor_toolbar',
        'image_util',
        'viewport',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_util',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_view',
      'dependencies': [
        '../../../file_manager/common/js/compiled_resources2.gyp:metrics',
        '../../../file_manager/foreground/js/compiled_resources2.gyp:thumbnail_loader',
        '../compiled_resources2.gyp:gallery_item',
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'image_buffer',
        'image_loader',
        'image_util',
        'viewport',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'test_util',
      'includes': ['../../../compile_js2.gypi'],
    },
    {
      'target_name': 'viewport',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        'image_util',
      ],
      'includes': ['../../../compile_js2.gypi'],
    },
  ],
}
