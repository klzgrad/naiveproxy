# Copyright (c) 2010 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
    'includes': [ 'common_defines.gypi', ],
    'variables':
    {
        'angle_path': '<(DEPTH)',
        'angle_build_winrt%': '0',
        'angle_build_winrt_app_type_revision%': '10.0',
        'angle_build_winrt_target_platform_ver%' : '',
        # angle_code is set to 1 for the core ANGLE targets defined in src/build_angle.gyp.
        # angle_code is set to 0 for test code, sample code, and third party code.
        # When angle_code is 1, we build with additional warning flags on Mac and Linux.
        'angle_code%': 0,
        'release_symbols%': 'true',
        'gcc_or_clang_warnings':
        [
            '-Wall',
            '-Wchar-subscripts',
            '-Werror',
            '-Wextra',
            '-Wformat=2',
            '-Winit-self',
            '-Wno-format-nonliteral',
            '-Wno-unknown-pragmas',
            '-Wno-unused-function',
            '-Wno-unused-parameter',
            '-Wpacked',
            '-Wpointer-arith',
            '-Wundef',
            '-Wwrite-strings',
        ],
        'gcc_or_clang_warnings_cc':
        [
            '-Wnon-virtual-dtor',
        ],

        # TODO: Pull chromium's clang dep.
        'clang%': 0,

        'clang_only_warnings':
        [
            '-Wshorten-64-to-32',
        ],
    },
    'target_defaults':
    {
        'default_configuration': 'Debug',
        'variables':
        {
            'warn_as_error%': 1,
        },
        'target_conditions':
        [
            ['warn_as_error == 1',
            {
                'msvs_settings':
                {
                    'VCCLCompilerTool':
                    {
                        'WarnAsError': 'true',
                    },
                    'VCLinkerTool':
                    {
                        'TreatLinkerWarningAsErrors': 'true',
                    },
                },
            }],
        ],
        'conditions':
        [
            ['angle_build_winrt==1',
            {
                'msvs_enable_winrt' : '1',
                'msvs_application_type_revision' : '<(angle_build_winrt_app_type_revision)',
                'msvs_target_platform_version' : '<(angle_build_winrt_target_platform_ver)',
            }],
        ],
        'configurations':
        {
            'Common_Base':
            {
                'abstract': 1,
                # Require the version of the Windows 10 SDK installed on the local machine.
                'msvs_windows_sdk_version': 'v10.0',
                'msvs_configuration_attributes':
                {
                    'OutputDirectory': '$(SolutionDir)$(ConfigurationName)_$(Platform)',
                    'IntermediateDirectory': '$(OutDir)\\obj\\$(ProjectName)',
                    'CharacterSet': '0',    # ASCII
                },
                'msvs_settings':
                {
                    'VCCLCompilerTool':
                    {
                        # Control Flow Guard is a security feature in Windows
                        # 8.1 and higher designed to prevent exploitation of
                        # indirect calls in executables.
                        # Control Flow Guard is enabled using the /d2guard4
                        # compiler setting in combination with the /guard:cf
                        # linker setting.
                        'AdditionalOptions': ['/MP', '/d2guard4'],
                        'BufferSecurityCheck': 'true',
                        'DebugInformationFormat': '3',
                        'ExceptionHandling': '0',
                        'EnableFunctionLevelLinking': 'true',
                        'MinimalRebuild': 'false',
                        'WarningLevel': '4',
                        'conditions':
                        [
                            ['angle_build_winrt==1',
                            {
                                # Use '/Wv:18' to avoid WRL warnings in VS2015 Update 3
                                # Use /Gw and /Zc:threadSafeInit to avoid
                                # LTCG-related crashes with VS2015 Update 3
                                'AdditionalOptions': ['/Wv:18', '/Gw', '/Zc:threadSafeInit-'],
                            }],
                        ],
                    },
                    'VCLinkerTool':
                    {
                        # Control Flow Guard is a security feature in Windows
                        # 8.1 and higher designed to prevent exploitation of
                        # indirect calls in executables.
                        # Control Flow Guard is enabled using the /d2guard4
                        # compiler setting in combination with the /guard:cf
                        # linker setting.
                        'AdditionalOptions': ['/guard:cf'],
                        'FixedBaseAddress': '1',
                        'ImportLibrary': '$(OutDir)\\lib\\$(TargetName).lib',
                        'MapFileName': '$(OutDir)\\$(TargetName).map',
                        # Most of the executables we'll ever create are tests
                        # and utilities with console output.
                        'SubSystem': '1',    # /SUBSYSTEM:CONSOLE
                    },
                    'VCResourceCompilerTool':
                    {
                        'Culture': '1033',
                    },
                },
                'xcode_settings':
                {
                    'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
                },
            },    # Common_Base

            'Debug_Base':
            {
                'abstract': 1,
                'defines':
                [
                    '_DEBUG'
                ],
                'msvs_settings':
                {
                    'VCCLCompilerTool':
                    {
                        'Optimization': '0',    # /Od
                        'BasicRuntimeChecks': '3',
                        'RuntimeTypeInfo': 'true',
                        'conditions':
                        [
                            ['angle_build_winrt==1',
                            {
                                # Use the dynamic C runtime to match
                                # Windows Application Store requirements

                                # The C runtime for Windows Store applications
                                # is a framework package that is managed by
                                # the Windows deployment model and can be
                                # shared by multiple packages.

                                'RuntimeLibrary': '3', # /MDd (debug dll)
                            },
                            {
                                # Use the static C runtime to
                                # match chromium and make sure
                                # we don't depend on the dynamic
                                # runtime's shared heaps
                                'RuntimeLibrary': '1', # /MTd (debug static)
                            }],
                        ],
                    },
                    'VCLinkerTool':
                    {
                        'GenerateDebugInformation': 'true',
                        'LinkIncremental': '2',
                        'conditions':
                        [
                            ['angle_build_winrt==1',
                            {
                                'AdditionalDependencies':
                                [
                                    'dxgi.lib',
                                ],
                                'EnableCOMDATFolding': '1', # disable
                                'OptimizeReferences': '1', # disable
                            }],
                        ],
                    },
                },
                'xcode_settings':
                {
                    'COPY_PHASE_STRIP': 'NO',
                    'GCC_OPTIMIZATION_LEVEL': '0',
                },
            },    # Debug_Base

            'Release_Base':
            {
                'abstract': 1,
                'defines':
                [
                    'NDEBUG'
                ],
                'msvs_settings':
                {
                    'VCCLCompilerTool':
                    {
                        'RuntimeTypeInfo': 'false',

                        'conditions':
                        [
                            ['angle_build_winrt==1',
                            {
                                # Use Chromium's settings for 'Official' builds
                                # to optimize WinRT release builds
                                'Optimization': '1', # /O1, minimize size
                                'FavorSizeOrSpeed': '2', # /Os
                                'WholeProgramOptimization': 'true',

                                # Use the dynamic C runtime to match
                                # Windows Application Store requirements

                                # The C runtime for Windows Store applications
                                # is a framework package that is managed by
                                # the Windows deployment model and can be
                                # shared by multiple packages.
                                'RuntimeLibrary': '2', # /MD (nondebug dll)
                            },
                            {
                                'Optimization': '2', # /O2, maximize speed

                                # Use the static C runtime to
                                # match chromium and make sure
                                # we don't depend on the dynamic
                                'RuntimeLibrary': '0', # /MT (nondebug static)
                            }],
                        ],
                    },
                    'VCLinkerTool':
                    {
                        'GenerateDebugInformation': '<(release_symbols)',
                        'LinkIncremental': '1',

                        'conditions':
                        [
                            ['angle_build_winrt==1',
                            {
                                # Use Chromium's settings for 'Official' builds
                                # to optimize WinRT release builds
                                'LinkTimeCodeGeneration': '1',
                                'AdditionalOptions': ['/cgthreads:8'],
                            }],
                        ],
                    },
                },
            },    # Release_Base

            'x86_Base':
            {
                'abstract': 1,
                'msvs_configuration_platform': 'Win32',
                'msvs_settings':
                {
                    'VCLinkerTool':
                    {
                        'TargetMachine': '1', # x86
                    },
                    'VCLibrarianTool':
                    {
                        'TargetMachine': '1', # x86
                    },
                },
                'defines': [ 'ANGLE_IS_32_BIT_CPU' ],
            }, # x86_Base

            'x64_Base':
            {
                'abstract': 1,
                'msvs_configuration_platform': 'x64',
                'msvs_settings':
                {
                    'VCLinkerTool':
                    {
                        'TargetMachine': '17', # x86 - 64
                    },
                    'VCLibrarianTool':
                    {
                        'TargetMachine': '17', # x86 - 64
                    },
                },
                'defines': [ 'ANGLE_IS_64_BIT_CPU' ],
            },    # x64_Base

            # Concrete configurations
            'Debug':
            {
                'inherit_from': ['Common_Base', 'x86_Base', 'Debug_Base'],
            },
            'Release':
            {
                'inherit_from': ['Common_Base', 'x86_Base', 'Release_Base'],
            },
            'conditions':
            [
                ['OS == "win"',
                {
                    'Debug_x64':
                    {
                        'inherit_from': ['Common_Base', 'x64_Base', 'Debug_Base'],
                    },
                    'Release_x64':
                    {
                        'inherit_from': ['Common_Base', 'x64_Base', 'Release_Base'],
                    },
                }],
                ['angle_build_winrt==1',
                {
                    'arm_Base':
                    {
                        'abstract': 1,
                        'msvs_configuration_platform': 'ARM',
                        'msvs_settings':
                        {
                            'VCLinkerTool':
                            {
                                'TargetMachine': '3', # ARM
                            },
                            'VCLibrarianTool':
                            {
                                'TargetMachine': '3', # ARM
                            },
                        },
                    }, # arm_Base
                    'Debug_ARM':
                    {
                        'inherit_from': ['Common_Base', 'arm_Base', 'Debug_Base'],
                    },
                    'Release_ARM':
                    {
                        'inherit_from': ['Common_Base', 'arm_Base', 'Release_Base'],
                    },
                }],
            ],
        },    # configurations
    },    # target_defaults
    'conditions':
    [
        ['OS == "win"',
        {
            'target_defaults':
            {
                'msvs_cygwin_dirs': ['../third_party/cygwin'],
            },
        },
        { # OS != win
            'target_defaults':
            {
                'cflags':
                [
                    '-fPIC',
                ],
                'cflags_cc':
                [
                    '-std=c++14',
                ],
            },
        }],
        ['OS != "win" and OS != "mac"',
        {
            'target_defaults':
            {
                'cflags':
                [
                    '-pthread',
                ],
                'cflags_cc':
                [
                    '-fno-exceptions',
                ],
                'ldflags':
                [
                    '-pthread',
                ],
                'configurations':
                {
                    'Debug':
                    {
                        'variables':
                        {
                            'debug_optimize%': '0',
                        },
                        'defines':
                        [
                            '_DEBUG',
                        ],
                        'cflags':
                        [
                            '-O>(debug_optimize)',
                            '-g',
                        ],
                    }
                },
            },
        }],
        ['angle_code==1',
        {
            'target_defaults':
            {
                'conditions':
                [
                    ['OS == "mac"',
                    {
                        'xcode_settings':
                        {
                            'WARNING_CFLAGS': ['<@(gcc_or_clang_warnings)']
                        },
                    }],
                    ['OS != "win" and OS != "mac"',
                    {
                        'cflags_c':
                        [
                            '<@(gcc_or_clang_warnings)',
                            '-std=c99',
                        ],
                        'cflags_cc':
                        [
                            '<@(gcc_or_clang_warnings_cc)',
                        ],
                        'defines':
                        [
                            'SYSCONFDIR="/etc"',
                            'FALLBACK_CONFIG_DIRS="/etc/xdg"',
                            'FALLBACK_DATA_DIRS="/usr/local/share:/usr/share"',
                        ],
                    }],
                    ['clang==1',
                    {
                        'cflags': ['<@(clang_only_warnings)']
                    }],
                ]
            }
        }],
    ],
}
