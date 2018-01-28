# Copyright (c) 2014 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
    'variables':
    {
        # chromeos=1 is used in some build configurations to disable GL
        # and GLX code because it typically wouldn't build for Chrome OS.
        # It does not mean "enable Chrome OS code."
        'chromeos': 0,

        # Chrome OS chroot builds need a special pkg-config, so make it possible to change.
        'pkg-config%': 'pkg-config',

        # Use a nested variable trick to get use_x11 evaluated more
        # eagerly than other conditional variables.
        'variables':
        {
            'variables':
            {
                'use_ozone%': 0,
            },
            'conditions':
            [
                ['OS=="linux" and use_ozone==0',
                {
                    'use_x11': 1,
                },
                {
                    'use_x11': 0,
                }],
            ],

            # Copy conditionally-set variables out one scope.
            'use_ozone%': '<(use_ozone)',
        },

        # Copy conditionally-set variables out one scope.
        'use_x11%': '<(use_x11)',
    },
}
