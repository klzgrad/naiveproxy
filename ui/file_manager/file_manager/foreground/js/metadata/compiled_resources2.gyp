# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'byte_reader',
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'content_metadata_provider',
      'dependencies': [
        '../../../common/js/compiled_resources2.gyp:file_type',
        '../../../common/js/compiled_resources2.gyp:util',
        '<(EXTERNS_GYP):chrome_extensions',
        'metadata_provider',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'exif_constants',
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'exif_parser',
      'dependencies': [
        '../../../../externs/compiled_resources2.gyp:exif_entry',
        'exif_constants',
        'image_parsers',
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'external_metadata_provider',
      'dependencies': [
        '<(EXTERNS_GYP):file_manager_private',
        'metadata_provider',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'file_system_metadata_provider',
      'dependencies': [
        'metadata_provider',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'function_parallel',
      'dependencies': [
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'function_sequence',
      'dependencies': [
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'id3_parser',
      'dependencies': [
        'byte_reader',
        'function_parallel',
        'function_sequence',
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_orientation',
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'image_parsers',
      'dependencies': [
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_cache_item',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'metadata_item',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
     'target_name': 'metadata_cache_set',
      'dependencies': [
        '../../../common/js/compiled_resources2.gyp:lru_cache',
        '../../../common/js/compiled_resources2.gyp:util',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        'metadata_cache_item',
        'metadata_item',
        'metadata_request',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_dispatcher',
      'dependencies': [
        '../../../../externs/compiled_resources2.gyp:metadata_worker_window',
        '../../../../externs/compiled_resources2.gyp:platform',
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_item',
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_model',
      'dependencies': [
        '../../../common/js/compiled_resources2.gyp:util',
        'file_system_metadata_provider',
        'external_metadata_provider',
        'content_metadata_provider',
        'metadata_item',
        'metadata_cache_set',
        'metadata_provider',
        'multi_metadata_provider',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_parser',
      'dependencies': [
        '../../../../externs/compiled_resources2.gyp:metadata_worker_window',
        'byte_reader',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_provider',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        'metadata_item',
        'metadata_request',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'metadata_request',
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'mpeg_parser',
      'dependencies': [
        'metadata_parser',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'multi_metadata_provider',
      'dependencies': [
        'metadata_provider',
        'file_system_metadata_provider',
        'external_metadata_provider',
        'content_metadata_provider',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
    {
      'target_name': 'thumbnail_model',
      'dependencies': [
        'metadata_model',
      ],
      'includes': ['../../../../compile_js2.gypi'],
    },
  ],
}
