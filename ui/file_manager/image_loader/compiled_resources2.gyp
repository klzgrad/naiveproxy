# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'background',
      'dependencies': [
        'image_loader',
      ],
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'cache',
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'image_loader',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):file_manager_private',
        'cache',
        'piex_loader',
        'request',
        'scheduler',
      ],
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'image_loader_util',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:assert',
        '<(EXTERNS_GYP):file_manager_private',
        'piex_loader',
      ],
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'image_loader_client',
      'dependencies': [
        '../file_manager/common/js/compiled_resources2.gyp:lru_cache',
        '<(EXTERNS_GYP):chrome_extensions',
        '<(EXTERNS_GYP):metrics_private',
      ],
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'piex_loader',
      'dependencies': [
        '../file_manager/foreground/js/metadata/compiled_resources2.gyp:image_orientation',
        '<(EXTERNS_GYP):file_manager_private',
      ],
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'request',
      'dependencies': [
        '../file_manager/common/js/compiled_resources2.gyp:file_type',
        '../file_manager/common/js/compiled_resources2.gyp:metrics',
        '../file_manager/common/js/compiled_resources2.gyp:metrics_events',
        'cache',
        'image_loader_util',
        'piex_loader',
      ],
      'includes': ['../compile_js2.gypi'],
    },
    {
      'target_name': 'scheduler',
      'dependencies': [
        'request',
      ],
      'includes': ['../compile_js2.gypi'],
    },
  ],
}
