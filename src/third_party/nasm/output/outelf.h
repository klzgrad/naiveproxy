/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2009 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * Internal definitions common to outelf32 and outelf64
 */
#ifndef OUTPUT_OUTELF_H
#define OUTPUT_OUTELF_H

#include "elf.h"
#include "rbtree.h"
#include "saa.h"

/* symbol binding */
#define SYM_GLOBAL      ELF32_ST_MKBIND(STB_GLOBAL)
#define SYM_LOCAL       ELF32_ST_MKBIND(STB_LOCAL)

#define GLOBAL_TEMP_BASE  0x40000000 /* bigger than any sane symbol index */

/* alignment of sections in file */
#define SEC_FILEALIGN 16

/* this stuff is needed for the dwarf/stabs debugging format */
#define TY_DEBUGSYMLIN 0x40     /* internal call to debug_out */

/* Known sections with nonstandard defaults */
struct elf_known_section {
    const char *name;   /* Name of section */
    int type;           /* Section type (SHT_) */
    uint32_t flags;     /* Section flags (SHF_) */
    uint32_t align;     /* Section alignment */
};
extern const struct elf_known_section elf_known_sections[];

/*
 * Special ELF sections (after the real sections but before debugging ones)
 */
#define sec_shstrtab            (nsects + 1)
#define sec_symtab              (nsects + 2)
#define sec_strtab              (nsects + 3)
#define sec_numspecial          3

/*
 * Debugging ELF sections (last in the file)
 */

/* stabs */
#define sec_stab                (nsections-3)
#define sec_stabstr             (nsections-2)
#define sec_rel_stab            (nsections-1)

/* stabs symbol table format */
struct stabentry {
    uint32_t    n_strx;
    uint8_t     n_type;
    uint8_t     n_other;
    uint16_t    n_desc;
    uint32_t    n_value;
};

/* dwarf */
#define sec_debug_aranges       (nsections-10)
#define sec_rela_debug_aranges  (nsections-9)
#define sec_debug_pubnames      (nsections-8)
#define sec_debug_info          (nsections-7)
#define sec_rela_debug_info     (nsections-6)
#define sec_debug_abbrev        (nsections-5)
#define sec_debug_line          (nsections-4)
#define sec_rela_debug_line     (nsections-3)
#define sec_debug_frame         (nsections-2)
#define sec_debug_loc           (nsections-1)

extern uint8_t elf_osabi;
extern uint8_t elf_abiver;

#define WRITE_STAB(p,n_strx,n_type,n_other,n_desc,n_value)  \
    do {                                                    \
        WRITELONG(p, n_strx);                               \
        WRITECHAR(p, n_type);                               \
        WRITECHAR(p, n_other);                              \
        WRITESHORT(p, n_desc);                              \
        WRITELONG(p, n_value);                              \
    } while (0)

struct elf_reloc {
    struct elf_reloc    *next;
    int64_t             address;        /* relative to _start_ of section */
    int64_t             symbol;         /* symbol index */
    int64_t             offset;         /* symbol addend */
    int                 type;           /* type of relocation */
};

struct elf_symbol {
    struct rbtree       symv;           /* symbol value and symbol rbtree */
    int32_t             strpos;         /* string table position of name */
    int32_t             section;        /* section ID of the symbol */
    int                 type;           /* symbol type */
    int                 other;          /* symbol visibility */
    int32_t             size;           /* size of symbol */
    int32_t             globnum;        /* symbol table offset if global */
    struct elf_symbol   *nextfwd;       /* list of unresolved-size symbols */
    char                *name;          /* used temporarily if in above list */
};

struct elf_section {
    struct SAA          *data;
    uint64_t            len;
    uint64_t            size;
    uint64_t            nrelocs;
    int32_t             index;
    int                 type;           /* SHT_PROGBITS or SHT_NOBITS */
    uint64_t            align;          /* alignment: power of two */
    uint64_t            flags;          /* section flags */
    char                *name;
    struct SAA          *rel;
    uint64_t             rellen;
    struct elf_reloc    *head;
    struct elf_reloc    **tail;
    struct rbtree       *gsyms;         /* global symbols in section */
};

#endif /* OUTPUT_OUTELF_H */
