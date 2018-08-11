# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'chromeos_resources',
      'type': 'none',
      'dependencies': [
        'bluetooth_pairing_dialog/compiled_resources2.gyp:*',
        'internet_config_dialog/compiled_resources2.gyp:*',
        'internet_detail_dialog/compiled_resources2.gyp:*',
      ],
    },
  ]
}
