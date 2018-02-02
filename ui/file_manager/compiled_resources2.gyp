# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'file_manager_resources',
      'type': 'none',
      'dependencies': [
        'audio_player/elements/compiled_resources2.gyp:*',
        'audio_player/js/compiled_resources2.gyp:*',
        'file_manager/background/js/compiled_resources2.gyp:*',
        'file_manager/common/js/compiled_resources2.gyp:*',
        'file_manager/foreground/elements/compiled_resources2.gyp:*',
        'file_manager/foreground/js/compiled_resources2.gyp:*',
        'file_manager/foreground/js/metadata/compiled_resources2.gyp:*',
        'file_manager/foreground/js/ui/compiled_resources2.gyp:*',
        'gallery/js/compiled_resources2.gyp:*',
        'gallery/js/image_editor/compiled_resources2.gyp:*',
        'image_loader/compiled_resources2.gyp:*',
        'video_player/js/cast/compiled_resources2.gyp:*',
        'video_player/js/compiled_resources2.gyp:*',
      ],
    },
  ],
}
