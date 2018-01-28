# Copyright (c) 2014 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
    'variables':
    {
        'component%': 'static_library',
        'use_libpci%': 1,
        'windows_sdk_path%': 'C:/Program Files (x86)/Windows Kits/10',

        'angle_build_winrt%': '0',

        # This works like the Ozone GBM platform in Chrome:
        # - Generic Buffer Manager (gbm) to allocate buffers
        # - EGL_EXT_image_dma_buf_import to render into those buffers via EGLImage
        # - Direct Rendering Manager + Kernel Mode Setting to scan out from those buffers
        # - EGL_PLATFORM_SURFACELESS because there are no native windows
        'use_ozone%': 0,

        'conditions':
        [
            ['OS=="linux" and use_x11==1 and chromeos==0', {
                'angle_use_glx%': 1,
            },
            {
                'angle_use_glx%': 0,
            }],
        ],

        # root of the OSX SDK for Chromium builds, the system root for standalone builds
        'SDKROOT%': "",
    },
    'defines':
    [
        'ANGLE_STANDALONE_BUILD',
    ],
    'msvs_disabled_warnings':
    [
        4100, # Unreferenced formal parameter. Not interesting.
        4127, # conditional expression is constant. Too noisy to be useful.
        4718, # Recursive call has no side effects. Fires on xtree system header.
        4251, # STL objects do not have DLL interface, needed by ShaderVars.h and angle_util
    ],
    'conditions':
    [
        ['use_ozone==1',
        {
            'defines': [ 'USE_OZONE' ],
        }],
        ['component=="shared_library"',
        {
            'defines': [ 'COMPONENT_BUILD' ],
        }],
    ],
    'msvs_settings':
    {
        'VCCLCompilerTool':
        {
            'PreprocessorDefinitions':
            [
                '_CRT_SECURE_NO_DEPRECATE',
                '_SCL_SECURE_NO_WARNINGS',
                '_HAS_EXCEPTIONS=0',
                'NOMINMAX',
            ],
        },
        'VCLinkerTool':
        {
            'conditions':
            [
                ['angle_build_winrt==0',
                {
                    'AdditionalDependencies':
                    [
                        'kernel32.lib',
                        'gdi32.lib',
                        'winspool.lib',
                        'comdlg32.lib',
                        'advapi32.lib',
                        'shell32.lib',
                        'ole32.lib',
                        'oleaut32.lib',
                        'user32.lib',
                        'uuid.lib',
                        'odbc32.lib',
                        'odbccp32.lib',
                        'delayimp.lib',
                    ],
                }],
                # winrt compilation is dynamic depending on the project
                # type. AdditionalDependencies is automatically configured
                # with the required .libs
                ['angle_build_winrt==1',
                {
                    'AdditionalDependencies':
                    [
                        '%(AdditionalDependencies)',
                        'uuid.lib',
                    ],
                }],
            ],
        },
    },

    # Windows SDK library directories for the configurations
    'configurations':
    {
        'conditions':
        [
            ['angle_build_winrt==0',
            {
                'x86_Base':
                {
                    'abstract': 1,
                    'msvs_settings':
                    {
                        'VCLinkerTool':
                        {
                            'AdditionalLibraryDirectories':
                            [
                                '<(windows_sdk_path)/Lib/winv6.3/um/x86',
                            ],
                        },
                        'VCLibrarianTool':
                        {
                            'AdditionalLibraryDirectories':
                            [
                                '<(windows_sdk_path)/Lib/winv6.3/um/x86',
                            ],
                        },
                    },
                },
                'x64_Base':
                {
                    'abstract': 1,
                    'msvs_settings':
                    {
                        'VCLinkerTool':
                        {
                            'AdditionalLibraryDirectories':
                            [
                                '<(windows_sdk_path)/Lib/winv6.3/um/x64',
                            ],
                        },
                        'VCLibrarianTool':
                        {
                            'AdditionalLibraryDirectories':
                            [
                                '<(windows_sdk_path)/Lib/winv6.3/um/x64',
                            ],
                        },
                    },
                },
            }],
        ],
    },
}
