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
    'use_lto%': 0,
  },
  'target_defaults': {
    'include_dirs': [
      '../',
    ],
    'conditions' : [
      ['target_arch=="arm"', {
        'conditions' : [
          ['clang==1', {
            # TODO(hans) Enable integrated-as (crbug.com/124610).
            'cflags': [ '-fno-integrated-as' ],
            'conditions': [
              ['OS == "android"', {
                # Else /usr/bin/as gets picked up.
                'cflags': [ '-B<(android_toolchain)' ],
              }],
            ],
          }],
          ['arm_neon==1', {
            # Enable build-time NEON selection.
            'defines': ['DL_ARM_NEON',],
            'direct_dependent_settings': {
              'defines': ['DL_ARM_NEON',],
            },
          }],
          ['arm_neon==0 and arm_neon_optional==1', {
            # Enable run-time NEON selection.
            'defines': ['DL_ARM_NEON_OPTIONAL',],
            'direct_dependent_settings': {
              'defines': ['DL_ARM_NEON_OPTIONAL',],
            },
          }],
        ],
      }],
      ['target_arch=="arm64"', {
        # Enable build-time NEON selection.
        'defines': ['DL_ARM_NEON',],
        'direct_dependent_settings': {
          'defines': ['DL_ARM_NEON',],
        },
      }],
    ],
  },
  'targets': [
    {
      # GN version: //third_party/opendmax_dl/dl
      'target_name': 'openmax_dl',
      'type': 'static_library',
      'direct_dependent_settings': {
        'include_dirs': [
          '../',
        ],
      },
      'sources': [
        'api/omxtypes.h',
        'sp/api/omxSP.h',
        'sp/src/armSP_FFT_F32TwiddleTable.c',
      ],
      'conditions' : [
        ['big_float_fft==1', {
          'defines': [
            'BIG_FFT_TABLE',
          ],
        }],
        ['target_arch=="arm" or target_arch=="arm64"', {
          'sources':[
            # Common files that are used by both arm and arm64 code.
            'api/arm/armOMX.h',
            'api/arm/omxtypes_s.h',
            'sp/api/armSP.h',
            'sp/src/arm/armSP_FFT_S32TwiddleTable.c',
            'sp/src/arm/omxSP_FFTGetBufSize_C_FC32.c',
            'sp/src/arm/omxSP_FFTGetBufSize_C_SC32.c',
            'sp/src/arm/omxSP_FFTGetBufSize_R_F32.c',
            'sp/src/arm/omxSP_FFTGetBufSize_R_S32.c',
            'sp/src/arm/omxSP_FFTInit_C_FC32.c',
            'sp/src/arm/omxSP_FFTInit_R_F32.c',
          ],
        }],
        ['target_arch=="arm"', {
          'sources': [
            # Common files that are used by both the NEON and non-NEON code.
            'api/armCOMM_s.h',
            'sp/src/arm/omxSP_FFTGetBufSize_C_SC16.c',
            'sp/src/arm/omxSP_FFTGetBufSize_R_S16.c',
            'sp/src/arm/omxSP_FFTGetBufSize_R_S16S32.c',
            'sp/src/arm/omxSP_FFTInit_C_SC16.c',
            'sp/src/arm/omxSP_FFTInit_C_SC32.c',
            'sp/src/arm/omxSP_FFTInit_R_S16.c',
            'sp/src/arm/omxSP_FFTInit_R_S16S32.c',
            'sp/src/arm/omxSP_FFTInit_R_S32.c',
          ],
          'dependencies': [
            'openmax_dl_armv7',
            'openmax_dl_neon',
          ],
        }],
        ['target_arch=="arm64"', {
          'sources':[
            'api/arm/arm64COMM_s.h',

            # Complex floating-point FFT
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix2_fs_s.S',
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix2_ls_s.S',
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix2_s.S',
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix4_fs_s.S',
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix4_ls_s.S',
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix4_s.S',
            'sp/src/arm/arm64/armSP_FFT_CToC_FC32_Radix8_fs_s.S',
            'sp/src/arm/arm64/omxSP_FFTInv_CToC_FC32.c',
            'sp/src/arm/arm64/omxSP_FFTFwd_CToC_FC32.c',
            # Real floating-point FFT
            'sp/src/arm/arm64/armSP_FFTInv_CCSToR_F32_preTwiddleRadix2_s.S',
            'sp/src/arm/arm64/omxSP_FFTFwd_RToCCS_F32.c',
            'sp/src/arm/arm64/ComplexToRealFixup.S',
            'sp/src/arm/arm64/omxSP_FFTInv_CCSToR_F32.c',
          ],
        }],
        ['target_arch=="ia32" or target_arch=="x64"', {
          'conditions': [
            ['os_posix==1', {
              'cflags': [ '-msse2', ],
              'xcode_settings': {
                'OTHER_CFLAGS': [ '-msse2', ],
              },
            }],
          ],
          'sources': [
            # Real 32-bit floating-point FFT.
            'sp/api/x86SP.h',
            'sp/src/x86/omxSP_FFTFwd_RToCCS_F32_Sfs.c',
            'sp/src/x86/omxSP_FFTGetBufSize_R_F32.c',
            'sp/src/x86/omxSP_FFTInit_R_F32.c',
            'sp/src/x86/omxSP_FFTInv_CCSToR_F32_Sfs.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix2_fs.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix2_ls.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix2_ls_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix2_ms.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix4_fs.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix4_fs_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix4_ls.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix4_ls_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix4_ms.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Fwd_Radix4_ms_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix2_fs.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix2_ls.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix2_ls_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix2_ms.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix4_fs.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix4_fs_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix4_ls.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix4_ls_sse.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix4_ms.c',
            'sp/src/x86/x86SP_FFT_CToC_FC32_Inv_Radix4_ms_sse.c',
            'sp/src/x86/x86SP_FFT_F32_radix2_kernel.c',
            'sp/src/x86/x86SP_FFT_F32_radix4_kernel.c',
            'sp/src/x86/x86SP_SSE_Math.h',
          ],
        }],
        ['target_arch=="mipsel"', {
          'cflags': [
            '-std=c99',
          ],
          'sources!': [
            'sp/src/armSP_FFT_F32TwiddleTable.c',
          ],
          'sources': [
            'sp/api/mipsSP.h',
            'sp/src/mips/mips_FFTFwd_RToCCS_F32_complex.c',
            'sp/src/mips/mips_FFTFwd_RToCCS_F32_real.c',
            'sp/src/mips/mips_FFTInv_CCSToR_F32_complex.c',
            'sp/src/mips/mips_FFTInv_CCSToR_F32_real.c',
            'sp/src/mips/omxSP_FFT_F32TwiddleTable.c',
            'sp/src/mips/omxSP_FFTFwd_RToCCS_F32_Sfs.c',
            'sp/src/mips/omxSP_FFTGetBufSize_R_F32.c',
            'sp/src/mips/omxSP_FFTInit_R_F32.c',
            'sp/src/mips/omxSP_FFTInv_CCSToR_F32_Sfs.c',
          ],
        }],
      ],
    },
  ],
  'conditions': [
    ['target_arch=="arm"', {
      'targets': [
        {
          # GN version: //third_party/opendmax_dl/openmax_dl_armv7
          # Non-NEON implementation of FFT. This library is NOT
          # standalone. Applications must link with openmax_dl.
          'target_name': 'openmax_dl_armv7',
          'type': 'static_library',
          'sources': [
            # Complex floating-point FFT
            'sp/src/arm/armv7/armSP_FFT_CToC_FC32_Radix2_fs_unsafe_s.S',
            'sp/src/arm/armv7/armSP_FFT_CToC_FC32_Radix2_fs_unsafe_s.S',
            'sp/src/arm/armv7/armSP_FFT_CToC_FC32_Radix4_fs_unsafe_s.S',
            'sp/src/arm/armv7/armSP_FFT_CToC_FC32_Radix4_unsafe_s.S',
            'sp/src/arm/armv7/armSP_FFT_CToC_FC32_Radix8_fs_unsafe_s.S',
            'sp/src/arm/armv7/omxSP_FFTInv_CToC_FC32_Sfs_s.S',
            'sp/src/arm/armv7/omxSP_FFTFwd_CToC_FC32_Sfs_s.S',
            # Real floating-point FFT
            'sp/src/arm/armv7/armSP_FFTInv_CCSToR_F32_preTwiddleRadix2_unsafe_s.S',
            'sp/src/arm/armv7/omxSP_FFTFwd_RToCCS_F32_Sfs_s.S',
            'sp/src/arm/armv7/omxSP_FFTInv_CCSToR_F32_Sfs_s.S',
          ],
          'conditions': [
            ['arm_neon_optional==1', {
              # Run-time NEON detection.
              'dependencies': [
                '../../../build/android/ndk.gyp:cpu_features',
              ],
              'link_settings' : {
                'libraries': [
                  # To get the __android_log_print routine
                  '-llog',
                ],
              },
              'sources': [
                # Detection routine
                'sp/src/arm/detect.c',
              ],
            }],
          ],
        },
        {
          # GN version: //third_party/opendmax_dl/openmax_dl_neon
          # NEON implementation of FFT. This library is NOT
          # standalone. Applications must link with openmax_dl.
          'target_name': 'openmax_dl_neon',
          'type': 'static_library',
          'cflags!': [
            '-mfpu=vfpv3-d16',
          ],
          'cflags': [
            '-mfpu=neon',
          ],
          'sources': [
            # Complex 32-bit fixed-point FFT.
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix2_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix2_ls_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix2_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix4_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix4_ls_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix2_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix4_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC32_Radix8_fs_unsafe_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CToC_SC32_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTFwd_CToC_SC32_Sfs_s.S',
            # Real 32-bit fixed-point FFT
            'sp/src/arm/neon/armSP_FFTInv_CCSToR_S32_preTwiddleRadix2_unsafe_s.S',
            'sp/src/arm/neon/omxSP_FFTFwd_RToCCS_S32_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CCSToR_S32_Sfs_s.S',
            # Complex 16-bit fixed-point FFT
            'sp/src/arm/neon/armSP_FFTInv_CCSToR_S16_preTwiddleRadix2_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix2_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix2_ls_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix2_ps_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix2_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix4_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix4_ls_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix4_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_SC16_Radix8_fs_unsafe_s.S',
            'sp/src/arm/neon/omxSP_FFTFwd_CToC_SC16_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CToC_SC16_Sfs_s.S',
            # Real 16-bit fixed-point FFT
            'sp/src/arm/neon/omxSP_FFTFwd_RToCCS_S16_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CCSToR_S16_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTFwd_RToCCS_S16S32_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CCSToR_S32S16_Sfs_s.S',
            # Complex floating-point FFT
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix2_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix2_ls_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix2_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix4_fs_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix4_ls_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix2_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix4_unsafe_s.S',
            'sp/src/arm/neon/armSP_FFT_CToC_FC32_Radix8_fs_unsafe_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CToC_FC32_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTFwd_CToC_FC32_Sfs_s.S',
            # Real floating-point FFT
            'sp/src/arm/neon/armSP_FFTInv_CCSToR_F32_preTwiddleRadix2_unsafe_s.S',
            'sp/src/arm/neon/omxSP_FFTFwd_RToCCS_F32_Sfs_s.S',
            'sp/src/arm/neon/omxSP_FFTInv_CCSToR_F32_Sfs_s.S',
          ],
          'conditions': [
            # Disable GCC LTO due to NEON issues
            # crbug.com/408997
            ['clang==0 and use_lto==1', {
              'cflags!': [
                '-flto',
                '-ffat-lto-objects',
              ],
            }],
          ],
        },
      ],
    }],
  ],
}
