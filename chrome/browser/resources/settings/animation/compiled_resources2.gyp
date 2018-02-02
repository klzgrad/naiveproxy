# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'animation',
      'dependencies': [
        '<(DEPTH)/ui/webui/resources/js/compiled_resources2.gyp:cr',
        '<(DEPTH)/ui/webui/resources/js/cr/compiled_resources2.gyp:event_target',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'fade_animations',
      'dependencies': [
        'animation',
        '<(DEPTH)/third_party/polymer/v1_0/components-chromium/neon-animation/compiled_resources2.gyp:neon-animation-behavior-extracted',
        '<(EXTERNS_GYP):web_animations',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
  ],
}
