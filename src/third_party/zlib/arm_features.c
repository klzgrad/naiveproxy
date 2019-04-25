/* arm_features.c -- ARM processor features detection.
 *
 * Copyright 2018 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#include "arm_features.h"
#include "zutil.h"

int ZLIB_INTERNAL arm_cpu_enable_crc32 = 0;
int ZLIB_INTERNAL arm_cpu_enable_pmull = 0;

#if !defined(_MSC_VER)

#include <pthread.h>
#include <stdint.h>

#if defined(ARMV8_OS_ANDROID)
#include <cpu-features.h>
#elif defined(ARMV8_OS_LINUX)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#else
#error arm_features.c ARM feature detection in not defined for your platform
#endif

static pthread_once_t cpu_check_inited_once = PTHREAD_ONCE_INIT;

static void _arm_check_features(void);

void ZLIB_INTERNAL arm_check_features(void)
{
    pthread_once(&cpu_check_inited_once, _arm_check_features);
}

/*
 * See http://bit.ly/2CcoEsr for run-time detection of ARM features and also
 * crbug.com/931275 for android_getCpuFeatures() use in the Android sandbox.
 */
static void _arm_check_features(void)
{
#if defined(ARMV8_OS_ANDROID) && defined(__aarch64__)
    uint64_t features = android_getCpuFeatures();
    arm_cpu_enable_crc32 = !!(features & ANDROID_CPU_ARM64_FEATURE_CRC32);
    arm_cpu_enable_pmull = !!(features & ANDROID_CPU_ARM64_FEATURE_PMULL);
#elif defined(ARMV8_OS_ANDROID) /* aarch32 */
    uint64_t features = android_getCpuFeatures();
    arm_cpu_enable_crc32 = !!(features & ANDROID_CPU_ARM_FEATURE_CRC32);
    arm_cpu_enable_pmull = !!(features & ANDROID_CPU_ARM_FEATURE_PMULL);
#elif defined(ARMV8_OS_LINUX) && defined(__aarch64__)
    unsigned long features = getauxval(AT_HWCAP);
    arm_cpu_enable_crc32 = !!(features & HWCAP_CRC32);
    arm_cpu_enable_pmull = !!(features & HWCAP_PMULL);
#elif defined(ARMV8_OS_LINUX) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
    /* Query HWCAP2 for ARMV8-A SoCs running in aarch32 mode */
    unsigned long features = getauxval(AT_HWCAP2);
    arm_cpu_enable_crc32 = !!(features & HWCAP2_CRC32);
    arm_cpu_enable_pmull = !!(features & HWCAP2_PMULL);
#endif
    /* TODO(crbug.com/810125): add ARMV8_OS_ZIRCON support for fucshia */
}

#else /* _MSC_VER */

#include <windows.h>

static INIT_ONCE cpu_check_inited_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK _arm_check_features(PINIT_ONCE once,
                                         PVOID param,
                                         PVOID *context);

void ZLIB_INTERNAL arm_check_features(void)
{
    InitOnceExecuteOnce(&cpu_check_inited_once, _arm_check_features,
                        NULL, NULL);
}

static BOOL CALLBACK _arm_check_features(PINIT_ONCE once,
                                         PVOID param,
                                         PVOID *context)
{
    if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
        arm_cpu_enable_crc32 = 1;

    if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE))
        arm_cpu_enable_pmull = 1;

    return TRUE;
}

#endif /* _MSC_VER */
