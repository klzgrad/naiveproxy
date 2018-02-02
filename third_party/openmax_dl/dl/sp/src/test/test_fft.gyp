#  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
#
#  Use of this source code is governed by a BSD-style license
#  that can be found in the LICENSE file in the root of the source
#  tree. An additional intellectual property rights grant can be found
#  in the file PATENTS.  All contributing project authors may
#  be found in the AUTHORS file in the root of the source tree.

{
  'variables' : {
    # Override this value to build with small float FFT tables
    'big_float_fft%' : 1,
  },
  'target_defaults': {
    'include_dirs': [
      '../../../../',
    ],
    'dependencies' : [
      '../../../dl.gyp:openmax_dl',
      'test_utilities',
    ],
    'conditions': [
      ['big_float_fft == 1', {
        'defines': [
          'BIG_FFT_TABLE',
        ],
      }],
    ],
  },
  'conditions': [
    ['target_arch == "arm"', {
      # Test programs supported on ARM
      'targets': [
        {
          # Test complex fixed-point 16-bit FFT
          'target_name': 'test_fft16',
          'type': 'executable',
          'sources': [
            'test_fft16.c',
          ],
        },
        {
          # Test complex fixed-point 32-bit FFT
          'target_name': 'test_fft32',
          'type': 'executable',
          'sources': [
            'test_fft32.c',
          ],
        },
        {
          # Test real 32-bit fixed-point FFT
          'target_name': 'test_rfft32',
          'type': 'executable',
          'sources': [
            'test_rfft32.c',
          ],
        },
        {
          # Test real 16-bit fixed-point FFT implemented with S32 routines.
          'target_name': 'test_rfft16_s32',
          'type': 'executable',
          'sources': [
            'test_rfft16_s32.c',
          ],
        },
        {
          # Test real 16-bit fixed-point FFT implemented with S16 routines.
          'target_name': 'test_rfft16_s16',
          'type': 'executable',
          'sources': [
            'test_rfft16_s16.c',
          ],
        },
        {
          # Test complex floating-point FFT
          'target_name': 'test_float_fft',
          'type': 'executable',
          'sources': [
            'test_float_fft.c',
            'support/float_fft_neon.c',
          ],
        },
        # Non-NEON test programs
        {
          # Test complex floating-point FFT, non-NEON
          'target_name': 'test_float_fft_armv7',
          'type': 'executable',
          'defines': [
            'ARM_VFP_TEST'
          ],
          'sources': [
            'test_float_fft.c',
            'support/float_fft_armv7.c',
          ],
        },
        {
          # Test real floating-point FFT, non-NEON
          'target_name': 'test_float_rfft_armv7',
          'type': 'executable',
          'sources': [
            'test_float_rfft.c',
            'support/float_rfft_armv7.c',
            'support/float_rfft_thresholds.h',
          ],
        },
        {
          # Test real floating-point FFT, detecting NEON support
          'target_name': 'test_float_rfft_detect',
          'type': 'executable',
          'sources': [
            'test_float_rfft.c',
            'support/float_rfft_detect.c',
            'support/float_rfft_thresholds.h',
          ],
        },
        {
          # Simple timing test of FFTs, non-NEON
          'target_name': 'test_fft_time_armv7',
          'type': 'executable',
          'defines': [
            # Timing test for non-NEON is only supported for float FFTs.
            'ARM_VFP_TEST',
            'FLOAT_ONLY',
          ],
          'sources': [
            'test_fft_time.c',
          ],
        },
      ],
    }],
    ['target_arch == "arm64"', {
      'targets': [
        {
          # Test complex floating-point FFT
          'target_name': 'test_float_fft',
          'type': 'executable',
          'sources': [
            'test_float_fft.c',
            'support/float_fft_neon.c',
          ],
        },
      ],
    }],
  ],
  'targets': [
    # Targets that should be supported by all architectures
    {
      # Test utilities
      'target_name': 'test_utilities',
      'type' : 'static_library',
      'dependencies!' : [
        'test_utilities'
      ],
      'link_settings': {
        'libraries': [
          '-lm',
        ],
      },
      'sources' : [
        'aligned_ptr.c',
        'compare.c',
        'gensig.c',
        'test_util.c',
        'test_util_fft.c',
      ],
    },
    {
      # Test real floating-point FFT
      'target_name': 'test_float_rfft',
      'type': 'executable',
      'sources': [
        'test_float_rfft.c',
        'support/float_rfft_thresholds.h',
      ],
      'conditions': [
        ['target_arch == "arm" or target_arch == "arm64"', {
          'sources': [
            'support/float_rfft_neon.c',
          ],
        }],
        ['target_arch == "ia32" or target_arch == "x64"', {
          'sources': [
            'support/float_rfft_x86.c',
          ],
        }],
        ['target_arch == "mipsel"', {
          'sources': [
            'support/float_rfft_mips.c',
          ],
        }],
      ],
    },
    {
      # Simple timing test of FFTs
      'target_name': 'test_fft_time',
      'type': 'executable',
      'sources': [
        'test_fft_time.c',
      ],
      'conditions': [
        ['target_arch == "ia32" or target_arch == "x64" or target_arch == "arm64" or target_arch == "mipsel"', {
          'defines': [
            # Timing test only for float FFTs on x86 and arm64 and MIPSEL.
            'FLOAT_ONLY',
          ],
        }],
      ],
    },
    {
      # Build all test programs.
      'target_name': 'All',
      'type': 'none',
      'conditions' : [
        ['target_arch == "arm"', {
          'conditions' : [
            ['arm_neon==1 or OS=="android"', {
              # NEON tests.
              'dependencies': [
                'test_fft16',
                'test_fft32',
                'test_float_fft',
                'test_float_rfft',
                'test_rfft16_s32',
                'test_rfft16_s16',
                'test_rfft32',
              ],
            }],
            ['arm_neon==0 or OS=="android"', {
              # Non-NEON tests.
              'dependencies': [
                'test_fft_time_armv7',
                'test_float_fft_armv7',
                'test_float_rfft_armv7',
              ],
            }],
            ['OS=="android"', {
              # Tests with detection.
              'dependencies': [
                'test_float_rfft_detect',
              ],
            }],
          ],
        }],
        ['target_arch == "arm64"', {
          # Supported test programs for ARM64.
          'dependencies': [
            'test_float_fft',
           ],
        }],
      ],
      'dependencies' : [
        # All architectures must support at least the float rfft test.
        'test_float_rfft',
        'test_fft_time',
      ],
    },
  ],
}
