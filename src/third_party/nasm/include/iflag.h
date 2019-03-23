#ifndef NASM_IFLAG_H
#define NASM_IFLAG_H

#include "compiler.h"
#include "ilog2.h"

#include <string.h>

#include "iflaggen.h"

#define IF_GENBIT(bit)          (UINT32_C(1) << (bit))

static inline bool iflag_test(const iflag_t *f, unsigned int bit)
{
    return !!(f->field[bit >> 5] & IF_GENBIT(bit & 31));
}

static inline void iflag_set(iflag_t *f, unsigned int bit)
{
    f->field[bit >> 5] |= IF_GENBIT(bit & 31);
}

static inline void iflag_clear(iflag_t *f, unsigned int bit)
{
    f->field[bit >> 5] &= ~IF_GENBIT(bit & 31);
}

static inline void iflag_clear_all(iflag_t *f)
{
     memset(f, 0, sizeof(*f));
}

static inline void iflag_set_all(iflag_t *f)
{
     memset(f, ~0, sizeof(*f));
}

#define iflag_for_each_field(v) for ((v) = 0; (v) < IF_FIELD_COUNT; (v)++)

static inline int iflag_cmp(const iflag_t *a, const iflag_t *b)
{
    int i;

    /* This is intentionally a reverse loop! */
    for (i = IF_FIELD_COUNT-1; i >= 0; i--) {
        if (a->field[i] == b->field[i])
            continue;

        return (int)(a->field[i] - b->field[i]);
    }

    return 0;
}

#define IF_GEN_HELPER(name, op)                                         \
    static inline iflag_t iflag_##name(const iflag_t *a, const iflag_t *b) \
    {                                                                   \
        unsigned int i;                                                 \
        iflag_t res;                                                    \
                                                                        \
        iflag_for_each_field(i)                                         \
            res.field[i] = a->field[i] op b->field[i];                  \
                                                                        \
        return res;                                                     \
    }

IF_GEN_HELPER(xor, ^)

/* Some helpers which are to work with predefined masks */
#define IF_SMASK        \
    (IF_GENBIT(IF_SB)  |\
     IF_GENBIT(IF_SW)  |\
     IF_GENBIT(IF_SD)  |\
     IF_GENBIT(IF_SQ)  |\
     IF_GENBIT(IF_SO)  |\
     IF_GENBIT(IF_SY)  |\
     IF_GENBIT(IF_SZ)  |\
     IF_GENBIT(IF_SIZE))
#define IF_ARMASK       \
    (IF_GENBIT(IF_AR0) |\
     IF_GENBIT(IF_AR1) |\
     IF_GENBIT(IF_AR2) |\
     IF_GENBIT(IF_AR3) |\
     IF_GENBIT(IF_AR4))

#define _itemp_smask(idx)      (insns_flags[(idx)].field[0] & IF_SMASK)
#define _itemp_armask(idx)     (insns_flags[(idx)].field[0] & IF_ARMASK)
#define _itemp_arg(idx)        ((_itemp_armask(idx) >> IF_AR0) - 1)

#define itemp_smask(itemp)      _itemp_smask((itemp)->iflag_idx)
#define itemp_arg(itemp)        _itemp_arg((itemp)->iflag_idx)
#define itemp_armask(itemp)     _itemp_armask((itemp)->iflag_idx)

/*
 * IF_8086 is the first CPU level flag and IF_PLEVEL the last
 */
#if IF_8086 & 31
#error "IF_8086 must be on a uint32_t boundary"
#endif
#define IF_PLEVEL               IF_IA64
#define IF_CPU_FIELD	       (IF_8086 >> 5)
#define IF_CPU_LEVEL_MASK      ((IF_GENBIT(IF_PLEVEL & 31) << 1) - 1)

/*
 * IF_PRIV is the firstr instruction filtering flag
 */
#if IF_PRIV & 31
#error "IF_PRIV must be on a uint32_t boundary"
#endif
#define IF_FEATURE_FIELD	(IF_PRIV >> 5)

static inline int iflag_cmp_cpu(const iflag_t *a, const iflag_t *b)
{
    return (int)(a->field[IF_CPU_FIELD] - b->field[IF_CPU_FIELD]);
}

static inline uint32_t _iflag_cpu_level(const iflag_t *a)
{
    return a->field[IF_CPU_FIELD] & IF_CPU_LEVEL_MASK;
}

static inline int iflag_cmp_cpu_level(const iflag_t *a, const iflag_t *b)
{
    uint32_t aa = _iflag_cpu_level(a);
    uint32_t bb = _iflag_cpu_level(b);

    return (int)(aa - bb);
}

/* Returns true if the CPU level is at least a certain value */
static inline bool iflag_cpu_level_ok(const iflag_t *a, unsigned int bit)
{
    return _iflag_cpu_level(a) >= IF_GENBIT(bit & 31);
}

static inline void iflag_set_all_features(iflag_t *a)
{
    size_t i;

    for (i = IF_FEATURE_FIELD; i < IF_CPU_FIELD; i++)
        a->field[i] = ~UINT32_C(0);
}

static inline void iflag_set_cpu(iflag_t *a, unsigned int cpu)
{
    a->field[0] = 0;     /* Not applicable to the CPU type */
    iflag_set_all_features(a);    /* All feature masking bits set for now */
    a->field[IF_CPU_FIELD] &= ~IF_CPU_LEVEL_MASK;
    iflag_set(a, cpu);
}

static inline void iflag_set_default_cpu(iflag_t *a)
{
    iflag_set_cpu(a, IF_PLEVEL);
}

static inline iflag_t _iflag_pfmask(const iflag_t *a)
{
    iflag_t r;

    iflag_clear_all(&r);

    if (iflag_test(a, IF_CYRIX))
        iflag_set(&r, IF_CYRIX);
    if (iflag_test(a, IF_AMD))
        iflag_set(&r, IF_AMD);

    return r;
}

#define iflag_pfmask(itemp)     _iflag_pfmask(&insns_flags[(itemp)->iflag_idx])

#endif /* NASM_IFLAG_H */
