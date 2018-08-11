# Copyright (c) 2017 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
    'targets':
    [
        {
            'target_name': 'gyp_deprecation',
            'type': 'none',
            'actions':
            [
                {
                    'action_name': 'GYP deprecation warning',
                    'message': ' * * * WARNING: GYP IS DEPRECATED * * *',
                    'inputs': [],
                    'outputs':
                    [
                        # this file is never created and should not exist so the action always runs
                        'this_file_never_exists',
                    ],
                    'action':
                    [
                        'python', '-c', 'print "See https://chromium.googlesource.com/angle/angle/+/master/doc/DevSetup.md for new build instructions."',
                    ],
                },
            ],
        },
    ],
}
