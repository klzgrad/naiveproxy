/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2020 The NASM Authors - All Rights Reserved */

#include "compiler.h"
#include "nasmlib.h"

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

#if defined(HAVE_GETRLIMIT) && defined(RLIMIT_STACK)

size_t nasm_get_stack_size_limit(void)
{
    struct rlimit rl;

    if (getrlimit(RLIMIT_STACK, &rl))
        return SIZE_MAX;

# ifdef RLIM_SAVED_MAX
    if (rl.rlim_cur == RLIM_SAVED_MAX)
        rl.rlim_cur = rl.rlim_max;
# endif

    if (
# ifdef RLIM_INFINITY
        rl.rlim_cur >= RLIM_INFINITY ||
# endif
# ifdef RLIM_SAVED_CUR
        rl.rlim_cur == RLIM_SAVED_CUR ||
# endif
# ifdef RLIM_SAVED_MAX
        rl.rlim_cur == RLIM_SAVED_MAX ||
# endif
        (size_t)rl.rlim_cur != rl.rlim_cur)
        return SIZE_MAX;

    return rl.rlim_cur;
}

#else

size_t nasm_get_stack_size_limit(void)
{
    return SIZE_MAX;
}

#endif
