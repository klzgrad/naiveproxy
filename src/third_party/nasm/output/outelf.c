/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2022 The NASM Authors - All Rights Reserved
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
 * Common code for outelf32 and outelf64
 */

#include "compiler.h"


#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "saa.h"
#include "raa.h"
#include "stdscan.h"
#include "eval.h"
#include "outform.h"
#include "outlib.h"
#include "rbtree.h"
#include "hashtbl.h"
#include "ver.h"

#include "dwarf.h"
#include "stabs.h"
#include "outelf.h"
#include "elf.h"

#if defined(OF_ELF32) || defined(OF_ELF64) || defined(OF_ELFX32)

#define SECT_DELTA 32
static struct elf_section **sects;
static int nsects, sectlen;

#define SHSTR_DELTA 256
static char *shstrtab;
static int shstrtablen, shstrtabsize;

static struct SAA *syms;
static uint32_t nlocals, nglobs, ndebugs; /* Symbol counts */

static int32_t def_seg;

static struct RAA *bsym;

static struct SAA *symtab, *symtab_shndx;

static struct SAA *strs;
static uint32_t strslen;

static struct RAA *section_by_index;
static struct hash_table section_by_name;

static struct elf_symbol *fwds;

static char elf_module[FILENAME_MAX];
static char elf_dir[FILENAME_MAX];

extern const struct ofmt of_elf32;
extern const struct ofmt of_elf64;
extern const struct ofmt of_elfx32;

static struct ELF_SECTDATA {
    void                *data;
    int64_t             len;
    bool                is_saa;
} *elf_sects;

static int elf_nsect, nsections;
static int64_t elf_foffs;

static void elf_write(void);
static void elf_sect_write(struct elf_section *, const void *, size_t);
static void elf_sect_writeaddr(struct elf_section *, int64_t, size_t);
static void elf_section_header(int name, int type, uint64_t flags,
                               void *data, bool is_saa, uint64_t datalen,
                               int link, int info,
                               uint64_t align, uint64_t entsize);
static void elf_write_sections(void);
static size_t elf_build_symtab(void);
static int add_sectname(const char *, const char *);

/* First debugging section index */
static int sec_debug;

struct symlininfo {
    int                 offset;
    int                 section;        /* index into sects[] */
    int                 segto;          /* internal section number */
    char                *name;          /* shallow-copied pointer of section name */
};

struct linelist {
    struct linelist     *next;
    struct linelist     *last;
    struct symlininfo   info;
    char                *filename;
    int                 line;
};

struct sectlist {
    struct SAA          *psaa;
    int                 section;
    int                 line;
    int                 offset;
    int                 file;
    struct sectlist     *next;
    struct sectlist     *last;
};

/* common debug variables */
static int currentline = 1;
static int debug_immcall = 0;

/* stabs debug variables */
static struct linelist *stabslines = 0;
static int numlinestabs = 0;
static char *stabs_filename = 0;
static uint8_t *stabbuf = 0, *stabstrbuf = 0, *stabrelbuf = 0;
static int stablen, stabstrlen, stabrellen;

/* dwarf debug variables */
static struct linelist *dwarf_flist = 0, *dwarf_clist = 0, *dwarf_elist = 0;
static struct sectlist *dwarf_fsect = 0, *dwarf_csect = 0, *dwarf_esect = 0;
static int dwarf_numfiles = 0, dwarf_nsections;
static uint8_t *arangesbuf = 0, *arangesrelbuf = 0, *pubnamesbuf = 0, *infobuf = 0,  *inforelbuf = 0,
               *abbrevbuf = 0, *linebuf = 0, *linerelbuf = 0, *framebuf = 0, *locbuf = 0;
static int8_t line_base = -5, line_range = 14, opcode_base = 13;
static int arangeslen, arangesrellen, pubnameslen, infolen, inforellen,
           abbrevlen, linelen, linerellen, framelen, loclen;
static int64_t dwarf_infosym, dwarf_abbrevsym, dwarf_linesym;

static struct elf_symbol *lastsym;

/* common debugging routines */
static void debug_typevalue(int32_t);

/* stabs debugging routines */
static void stabs_linenum(const char *filename, int32_t linenumber, int32_t);
static void stabs_output(int, void *);
static void stabs_generate(void);
static void stabs_cleanup(void);

/* dwarf debugging routines */

/* This should match the order in elf_write() */
enum dwarf_sect {
    DWARF_ARANGES,
    DWARF_RELA_ARANGES,
    DWARF_PUBNAMES,
    DWARF_INFO,
    DWARF_RELA_INFO,
    DWARF_ABBREV,
    DWARF_LINE,
    DWARF_RELA_LINE,
    DWARF_FRAME,
    DWARF_LOC,
    DWARF_NSECT
};

struct dwarf_format {
    uint16_t dwarf_version;
    uint16_t sect_version[DWARF_NSECT];
    /* ... add more here to generalize further */
};
const struct dwarf_format *dwfmt;

static void dwarf32_init(void);
static void dwarfx32_init(void);
static void dwarf64_init(void);
static void dwarf_linenum(const char *filename, int32_t linenumber, int32_t);
static void dwarf_output(int, void *);
static void dwarf_generate(void);
static void dwarf_cleanup(void);
static void dwarf_findfile(const char *);
static void dwarf_findsect(const int);

struct elf_format_info {
    size_t word;                /* Word size (4 or 8) */
    size_t ehdr_size;           /* Size of the ELF header */
    size_t shdr_size;           /* Size of a section header */
    size_t sym_size;            /* Size of a symbol */
    size_t relsize;             /* Size of a reltype relocation */
    char relpfx[8];             /* Relocation section prefix */
    uint32_t reltype;           /* Relocation section type */
    uint16_t e_machine;         /* Header e_machine field */
    uint8_t ei_class;           /* ELFCLASS32 or ELFCLASS64 */
    bool elf64;                 /* 64-bit ELF */

    /* Write a symbol */
    void (*elf_sym)(const struct elf_symbol *);

    /* Build a relocation table */
    struct SAA *(*elf_build_reltab)(const struct elf_reloc *);
};
static const struct elf_format_info *efmt;

static void elf32_sym(const struct elf_symbol *sym);
static void elf64_sym(const struct elf_symbol *sym);

static struct SAA *elf32_build_reltab(const struct elf_reloc *r);
static struct SAA *elfx32_build_reltab(const struct elf_reloc *r);
static struct SAA *elf64_build_reltab(const struct elf_reloc *r);

static bool dfmt_is_stabs(void);
static bool dfmt_is_dwarf(void);

/*
 * Special NASM section numbers which are used to define ELF special
 * symbols.
 */
static int32_t elf_gotpc_sect, elf_gotoff_sect;
static int32_t elf_got_sect, elf_plt_sect;
static int32_t elf_sym_sect, elf_gottpoff_sect, elf_tlsie_sect;

uint8_t elf_osabi = 0;      /* Default OSABI = 0 (System V or Linux) */
uint8_t elf_abiver = 0;     /* Current ABI version */

/* Known sections with nonstandard defaults. -n means n*pointer size. */
struct elf_known_section {
    const char *name;   /* Name of section */
    int type;           /* Section type (SHT_) */
    uint32_t flags;     /* Section flags (SHF_) */
    int align;          /* Section alignment */
    int entsize;	/* Entry size, if applicable */
};

static const struct elf_known_section elf_known_sections[] = {
    { ".text",          SHT_PROGBITS,      SHF_ALLOC|SHF_EXECINSTR,     16,  0 },
    { ".rodata",        SHT_PROGBITS,      SHF_ALLOC,                    4,  0 },
    { ".lrodata",       SHT_PROGBITS,      SHF_ALLOC,                    4,  0 },
    { ".data",          SHT_PROGBITS,      SHF_ALLOC|SHF_WRITE,          4,  0 },
    { ".ldata",         SHT_PROGBITS,      SHF_ALLOC|SHF_WRITE,          4,  0 },
    { ".bss",           SHT_NOBITS,        SHF_ALLOC|SHF_WRITE,          4,  0 },
    { ".lbss",          SHT_NOBITS,        SHF_ALLOC|SHF_WRITE,          4,  0 },
    { ".tdata",         SHT_PROGBITS,      SHF_ALLOC|SHF_WRITE|SHF_TLS,  4,  0 },
    { ".tbss",          SHT_NOBITS,        SHF_ALLOC|SHF_WRITE|SHF_TLS,  4,  0 },
    { ".comment",       SHT_PROGBITS,      0,                            1,  0 },
    { ".preinit_array", SHT_PREINIT_ARRAY, SHF_ALLOC,                   -1, -1 },
    { ".init_array",    SHT_INIT_ARRAY,    SHF_ALLOC,                   -1, -1 },
    { ".fini_array",    SHT_FINI_ARRAY,    SHF_ALLOC,                   -1, -1 },
    { ".note",          SHT_NOTE,          0,                            4,  0 },
    { NULL /*default*/, SHT_PROGBITS,      SHF_ALLOC,                    1,  0 }
};

struct size_unit {
    char name[8];
    int bytes;
    int align;
};
static const struct size_unit size_units[] =
{
    { "byte",     1,  1 },
    { "word",     2,  2 },
    { "dword",    4,  4 },
    { "qword",    8,  8 },
    { "tword",   10,  2 },
    { "tbyte",   10,  2 },
    { "oword",   16, 16 },
    { "xword",   16, 16 },
    { "yword",   32, 32 },
    { "zword",   64, 64 },
    { "pointer", -1, -1 },
    { "",         0,  0 }
};

static inline size_t to_bytes(int val)
{
    return (val >= 0) ? (size_t)val : -val * efmt->word;
}

/* parse section attributes */
static void elf_section_attrib(char *name, char *attr, uint32_t *flags_and, uint32_t *flags_or,
                               uint64_t *alignp, uint64_t *entsize, int *type)
{
    char *opt, *val, *next;
    uint64_t align = 0;
    uint64_t xalign = 0;

    opt = nasm_skip_spaces(attr);
    if (!opt || !*opt)
        return;

    while ((opt = nasm_opt_val(opt, &val, &next))) {
        if (!nasm_stricmp(opt, "align")) {
            if (!val) {
                nasm_nonfatal("section align without value specified");
            } else {
                bool err;
                uint64_t a = readnum(val, &err);
                if (a && !is_power2(a)) {
                    nasm_error(ERR_NONFATAL,
                               "section alignment %"PRId64" is not a power of two",
                               a);
                } else if (a > align) {
                    align = a;
                }
            }
        } else if (!nasm_stricmp(opt, "alloc")) {
            *flags_and  |= SHF_ALLOC;
            *flags_or   |= SHF_ALLOC;
        } else if (!nasm_stricmp(opt, "noalloc")) {
            *flags_and  |= SHF_ALLOC;
            *flags_or   &= ~SHF_ALLOC;
        } else if (!nasm_stricmp(opt, "exec")) {
            *flags_and  |= SHF_EXECINSTR;
            *flags_or   |= SHF_EXECINSTR;
        } else if (!nasm_stricmp(opt, "noexec")) {
            *flags_and  |= SHF_EXECINSTR;
            *flags_or   &= ~SHF_EXECINSTR;
        } else if (!nasm_stricmp(opt, "write")) {
            *flags_and  |= SHF_WRITE;
            *flags_or   |= SHF_WRITE;
        } else if (!nasm_stricmp(opt, "nowrite") ||
		   !nasm_stricmp(opt, "readonly")) {
            *flags_and  |= SHF_WRITE;
            *flags_or   &= ~SHF_WRITE;
        } else if (!nasm_stricmp(opt, "tls")) {
            *flags_and  |= SHF_TLS;
            *flags_or   |= SHF_TLS;
        } else if (!nasm_stricmp(opt, "notls")) {
            *flags_and  |= SHF_TLS;
            *flags_or   &= ~SHF_TLS;
        } else if (!nasm_stricmp(opt, "merge")) {
            *flags_and  |= SHF_MERGE;
            *flags_or   |= SHF_MERGE;
        } else if (!nasm_stricmp(opt, "nomerge")) {
            *flags_and  |= SHF_MERGE;
            *flags_or   &= ~SHF_MERGE;
        } else if (!nasm_stricmp(opt, "strings")) {
            *flags_and  |= SHF_STRINGS;
            *flags_or   |= SHF_STRINGS;
        } else if (!nasm_stricmp(opt, "nostrings")) {
            *flags_and  |= SHF_STRINGS;
            *flags_or   &= ~SHF_STRINGS;
        } else if (!nasm_stricmp(opt, "progbits")) {
            *type = SHT_PROGBITS;
        } else if (!nasm_stricmp(opt, "nobits")) {
            *type = SHT_NOBITS;
        } else if (!nasm_stricmp(opt, "note")) {
            *type = SHT_NOTE;
	} else if (!nasm_stricmp(opt, "preinit_array")) {
	    *type = SHT_PREINIT_ARRAY;
	} else if (!nasm_stricmp(opt, "init_array")) {
	    *type = SHT_INIT_ARRAY;
	} else if (!nasm_stricmp(opt, "fini_array")) {
	    *type = SHT_FINI_ARRAY;
        } else {
	    uint64_t mult;
	    size_t l;
	    const char *a = strchr(opt, '*');
	    bool err;
	    const struct size_unit *su;

	    if (a) {
		l = a - opt - 1;
		mult = readnum(a+1, &err);
	    } else {
		l = strlen(opt);
		mult = 1;
	    }

	    for (su = size_units; su->bytes; su++) {
		if (!nasm_strnicmp(opt, su->name, l))
		    break;
	    }

	    if (su->bytes) {
		*entsize = to_bytes(su->bytes) * mult;
		xalign = to_bytes(su->align);
	    } else {
		/* Unknown attribute */
		nasm_warn(WARN_OTHER,
			   "unknown section attribute '%s' ignored on"
			   " declaration of section `%s'", opt, name);
	    }
         }
        opt = next;
    }

    switch (*type) {
    case SHT_PREINIT_ARRAY:
    case SHT_INIT_ARRAY:
    case SHT_FINI_ARRAY:
	if (!xalign)
	    xalign = efmt->word;
	if (!*entsize)
	    *entsize = efmt->word;
	break;
    default:
	break;
    }

    if (!align)
        align = xalign;
    if (!align)
	align = SHA_ANY;

    *alignp = align;
}

static enum directive_result
elf_directive(enum directive directive, char *value)
{
    int64_t n;
    bool err;
    char *p;

    switch (directive) {
    case D_OSABI:
        if (!pass_first())      /* XXX: Why? */
            return DIRR_OK;

        n = readnum(value, &err);
        if (err) {
            nasm_nonfatal("`osabi' directive requires a parameter");
            return DIRR_ERROR;
        }

        if (n < 0 || n > 255) {
            nasm_nonfatal("valid osabi numbers are 0 to 255");
            return DIRR_ERROR;
        }

        elf_osabi  = n;
        elf_abiver = 0;

        p = strchr(value,',');
        if (!p)
            return DIRR_OK;

        n = readnum(p + 1, &err);
        if (err || n < 0 || n > 255) {
            nasm_nonfatal("invalid ABI version number (valid: 0 to 255)");
            return DIRR_ERROR;
        }

        elf_abiver = n;
        return DIRR_OK;

    default:
        return DIRR_UNKNOWN;
    }
}

static void elf_init(void);

static void elf32_init(void)
{
    static const struct elf_format_info ef_elf32 = {
        4,
        sizeof(Elf32_Ehdr),
        sizeof(Elf32_Shdr),
        sizeof(Elf32_Sym),
        sizeof(Elf32_Rel),
        ".rel",
        SHT_REL,
        EM_386,
        ELFCLASS32,
        false,

        elf32_sym,
        elf32_build_reltab
    };
    efmt = &ef_elf32;
    elf_init();
}

static void elfx32_init(void)
{
    static const struct elf_format_info ef_elfx32 = {
        4,
        sizeof(Elf32_Ehdr),
        sizeof(Elf32_Shdr),
        sizeof(Elf32_Sym),
        sizeof(Elf32_Rela),
        ".rela",
        SHT_RELA,
        EM_X86_64,
        ELFCLASS32,
        false,

        elf32_sym,
        elfx32_build_reltab
    };
    efmt = &ef_elfx32;
    elf_init();
}

static void elf64_init(void)
{
    static const struct elf_format_info ef_elf64 = {
        8,
        sizeof(Elf64_Ehdr),
        sizeof(Elf64_Shdr),
        sizeof(Elf64_Sym),
        sizeof(Elf64_Rela),
        ".rela",
        SHT_RELA,
        EM_X86_64,
        ELFCLASS64,
        true,

        elf64_sym,
        elf64_build_reltab
    };
    efmt = &ef_elf64;
    elf_init();
}

static void elf_init(void)
{
    static const char * const reserved_sections[] = {
        ".shstrtab", ".strtab", ".symtab", ".symtab_shndx", NULL
    };
    const char * const *p;
    const char * cur_path = nasm_realpath(inname);

    strlcpy(elf_module, inname, sizeof(elf_module));
    strlcpy(elf_dir, nasm_dirname(cur_path), sizeof(elf_dir));
    sects = NULL;
    nsects = sectlen = 0;
    syms = saa_init((int32_t)sizeof(struct elf_symbol));
    nlocals = nglobs = ndebugs = 0;
    bsym = raa_init();
    strs = saa_init(1L);
    saa_wbytes(strs, "\0", 1L);
    saa_wbytes(strs, elf_module, strlen(elf_module)+1);
    strslen = 2 + strlen(elf_module);
    shstrtab = NULL;
    shstrtablen = shstrtabsize = 0;;
    add_sectname("", "");       /* SHN_UNDEF */

    fwds = NULL;

    section_by_index = raa_init();

    /*
     * Add reserved section names to the section hash, with NULL
     * as the data pointer
     */
    for (p = reserved_sections; *p; p++) {
        struct hash_insert hi;
        hash_find(&section_by_name, *p, &hi);
        hash_add(&hi, *p, NULL);
    }

    /*
     * FIXME: tlsie is Elf32 only and
     * gottpoff is Elfx32|64 only.
     */
    elf_gotpc_sect = seg_alloc();
    backend_label("..gotpc", elf_gotpc_sect + 1, 0L);
    elf_gotoff_sect = seg_alloc();
    backend_label("..gotoff", elf_gotoff_sect + 1, 0L);
    elf_got_sect = seg_alloc();
    backend_label("..got", elf_got_sect + 1, 0L);
    elf_plt_sect = seg_alloc();
    backend_label("..plt", elf_plt_sect + 1, 0L);
    elf_sym_sect = seg_alloc();
    backend_label("..sym", elf_sym_sect + 1, 0L);
    elf_gottpoff_sect = seg_alloc();
    backend_label("..gottpoff", elf_gottpoff_sect + 1, 0L);
    elf_tlsie_sect = seg_alloc();
    backend_label("..tlsie", elf_tlsie_sect + 1, 0L);

    def_seg = seg_alloc();
}

static void elf_cleanup(void)
{
    struct elf_reloc *r;
    int i;

    elf_write();
    for (i = 0; i < nsects; i++) {
        if (sects[i]->type != SHT_NOBITS)
            saa_free(sects[i]->data);
        if (sects[i]->rel)
            saa_free(sects[i]->rel);
        while (sects[i]->head) {
            r = sects[i]->head;
            sects[i]->head = sects[i]->head->next;
            nasm_free(r);
        }
    }
    hash_free(&section_by_name);
    raa_free(section_by_index);
    nasm_free(sects);
    saa_free(syms);
    raa_free(bsym);
    saa_free(strs);
    dfmt->cleanup();
}

/*
 * Add entry to the elf .shstrtab section and increment nsections.
 * Returns the section index for this new section.
 *
 * IMPORTANT: this needs to match the order the section headers are
 * emitted.
 */
static int add_sectname(const char *firsthalf, const char *secondhalf)
{
    int l1 = strlen(firsthalf);
    int l2 = strlen(secondhalf);

    while (shstrtablen + l1 + l2 + 1 > shstrtabsize)
        shstrtab = nasm_realloc(shstrtab, (shstrtabsize += SHSTR_DELTA));

    memcpy(shstrtab + shstrtablen, firsthalf, l1);
    shstrtablen += l1;
    memcpy(shstrtab + shstrtablen, secondhalf, l2+1);
    shstrtablen += l2 + 1;

    return nsections++;
}

static struct elf_section *
elf_make_section(char *name, int type, int flags, uint64_t align)
{
    struct elf_section *s;

    s = nasm_zalloc(sizeof(*s));

    if (type != SHT_NOBITS)
        s->data = saa_init(1L);
    s->tail = &s->head;
    if (!strcmp(name, ".text"))
        s->index = def_seg;
    else
        s->index = seg_alloc();

    s->name     = nasm_strdup(name);
    s->type     = type;
    s->flags    = flags;
    s->align    = align;
    s->shndx    = add_sectname("", name);

    if (nsects >= sectlen)
        sects = nasm_realloc(sects, (sectlen += SECT_DELTA) * sizeof(*sects));
    sects[nsects++] = s;

    return s;
}

static int32_t elf_section_names(char *name, int *bits)
{
    char *p;
    uint32_t flags, flags_and, flags_or;
    uint64_t align, entsize;
    void **hp;
    struct elf_section *s;
    struct hash_insert hi;
    int type;

    if (!name) {
        *bits = ofmt->maxbits;
        return def_seg;
    }

    p = nasm_skip_word(name);
    if (*p)
        *p++ = '\0';
    flags_and = flags_or = type = align = entsize = 0;

    elf_section_attrib(name, p, &flags_and, &flags_or, &align, &entsize, &type);

    hp = hash_find(&section_by_name, name, &hi);
    if (hp) {
        s = *hp;
        if (!s) {
            nasm_nonfatal("attempt to redefine reserved section name `%s'", name);
            return NO_SEG;
        }
    } else {
        const struct elf_known_section *ks = elf_known_sections;

        while (ks->name) {
            if (!strcmp(name, ks->name))
                break;
            ks++;
        }

        type = type ? type : ks->type;
	if (!align)
	    align = to_bytes(ks->align);
	if (!entsize)
	    entsize = to_bytes(ks->entsize);
        flags = (ks->flags & ~flags_and) | flags_or;

        s = elf_make_section(name, type, flags, align);
        hash_add(&hi, s->name, s);
        section_by_index = raa_write_ptr(section_by_index, s->index >> 1, s);
    }

    if ((type && s->type != type)
        || ((s->flags & flags_and) != flags_or)
        || (entsize && s->entsize && entsize != s->entsize)) {
        nasm_warn(WARN_OTHER, "incompatible section attributes ignored on"
                  " redeclaration of section `%s'", name);
    }

    if (align > s->align)
        s->align = align;

    if (entsize && !s->entsize)
        s->entsize = entsize;

    if ((flags_or & SHF_MERGE) && s->entsize == 0) {
        if (!(s->flags & SHF_STRINGS))
            nasm_nonfatal("section attribute merge specified without an entry size or `strings'");
        s->entsize = 1;
    }

    return s->index;
}

static inline bool sym_type_local(int type)
{
    return ELF32_ST_BIND(type) == STB_LOCAL;
}

static void elf_deflabel(char *name, int32_t segment, int64_t offset,
                         int is_global, char *special)
{
    int pos = strslen;
    struct elf_symbol *sym;
    const char *spcword = nasm_skip_spaces(special);
    int bind, type;             /* st_info components */
    const struct elf_section *sec = NULL;

    if (debug_level(2)) {
        nasm_debug(" elf_deflabel: %s, seg=%"PRIx32", off=%"PRIx64", is_global=%d, %s\n",
                   name, segment, offset, is_global, special);
    }

    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
        /*
         * This is a NASM special symbol. We never allow it into
         * the ELF symbol table, even if it's a valid one. If it
         * _isn't_ a valid one, we should barf immediately.
         *
         * FIXME: tlsie is Elf32 only, and gottpoff is Elfx32|64 only.
         */
        if (strcmp(name, "..gotpc") && strcmp(name, "..gotoff") &&
            strcmp(name, "..got") && strcmp(name, "..plt") &&
            strcmp(name, "..sym") && strcmp(name, "..gottpoff") &&
            strcmp(name, "..tlsie"))
            nasm_nonfatal("unrecognised special symbol `%s'", name);
        return;
    }

    if (is_global == 3) {
        struct elf_symbol **s;
        /*
         * Fix up a forward-reference symbol size from the first
         * pass.
         */
        for (s = &fwds; *s; s = &(*s)->nextfwd)
            if (!strcmp((*s)->name, name)) {
                struct tokenval tokval;
                expr *e;
                char *p = nasm_skip_spaces(nasm_skip_word(special));

                stdscan_reset();
                stdscan_set(p);
                tokval.t_type = TOKEN_INVALID;
                e = evaluate(stdscan, NULL, &tokval, NULL, 1, NULL);
                if (e) {
                    if (!is_simple(e))
                        nasm_nonfatal("cannot use relocatable"
                                      " expression as symbol size");
                    else
                        (*s)->size = reloc_value(e);
                }

                /*
                 * Remove it from the list of unresolved sizes.
                 */
                nasm_free((*s)->name);
                *s = (*s)->nextfwd;
                return;
            }
        return;                 /* it wasn't an important one */
    }

    saa_wbytes(strs, name, (int32_t)(1 + strlen(name)));
    strslen += 1 + strlen(name);

    lastsym = sym = saa_wstruct(syms);

    memset(&sym->symv, 0, sizeof(struct rbtree));

    sym->strpos = pos;
    bind = is_global ? STB_GLOBAL : STB_LOCAL;
    type = STT_NOTYPE;
    sym->other = STV_DEFAULT;
    sym->size = 0;
    if (segment == NO_SEG) {
        sym->section = XSHN_ABS;
    } else {
        sym->section = XSHN_UNDEF;
        if (segment == def_seg) {
            /* we have to be sure at least text section is there */
            int tempint;
            if (segment != elf_section_names(".text", &tempint))
                nasm_panic("strange segment conditions in ELF driver");
        }
        sec = raa_read_ptr(section_by_index, segment >> 1);
        if (sec)
            sym->section = sec->shndx;
    }

    if (is_global == 2) {
        sym->size = offset;
        sym->symv.key = 0;
        sym->section = XSHN_COMMON;
        /*
         * We have a common variable. Check the special text to see
         * if it's a valid number and power of two; if so, store it
         * as the alignment for the common variable.
         *
         * XXX: this should allow an expression.
         */
        if (spcword) {
            bool err;
            sym->symv.key = readnum(spcword, &err);
            if (err)
                nasm_nonfatal("alignment constraint `%s' is not a"
                              " valid number", special);
            else if (!is_power2(sym->symv.key))
                nasm_nonfatal("alignment constraint `%s' is not a"
                              " power of two", special);
            spcword = nasm_skip_spaces(nasm_skip_word(spcword));
        }
    } else {
        sym->symv.key = (sym->section == XSHN_UNDEF ? 0 : offset);
    }

    if (spcword && *spcword) {
        const char *wend;
        bool ok = true;

        while (ok) {
            size_t wlen;
            wend = nasm_skip_word(spcword);
            wlen = wend - spcword;

            switch (wlen) {
            case 4:
                if (!nasm_strnicmp(spcword, "data", wlen))
                    type = STT_OBJECT;
                else if (!nasm_strnicmp(spcword, "weak", wlen))
                    bind = STB_WEAK;
                else
                    ok = false;
                break;

            case 6:
                if (!nasm_strnicmp(spcword, "notype", wlen))
                    type = STT_NOTYPE;
                else if (!nasm_strnicmp(spcword, "object", wlen))
                    type = STT_OBJECT;
                else if (!nasm_strnicmp(spcword, "hidden", wlen))
                    sym->other = STV_HIDDEN;
                else if (!nasm_strnicmp(spcword, "strong", wlen))
                    bind = STB_GLOBAL;
                else
                    ok = false;
                break;

            case 7:
                if (!nasm_strnicmp(spcword, "default", wlen))
                    sym->other = STV_DEFAULT;
                else
                    ok = false;
                break;

            case 8:
                if (!nasm_strnicmp(spcword, "function", wlen))
                    type = STT_FUNC;
                else if (!nasm_stricmp(spcword, "internal"))
                    sym->other = STV_INTERNAL;
                else
                    ok = false;
                break;

            case 9:
                if (!nasm_strnicmp(spcword, "protected", wlen))
                    sym->other = STV_PROTECTED;
                else
                    ok = false;
                break;

            default:
                ok = false;
                break;
            }

            if (ok)
                spcword = nasm_skip_spaces(wend);
        }
        if (!is_global && bind != STB_LOCAL) {
            nasm_nonfatal("weak and strong only applies to global symbols");
            bind = STB_LOCAL;
        }

        if (spcword && *spcword) {
            struct tokenval tokval;
            expr *e;
            int fwd = 0;
            char *saveme = stdscan_get();

            /*
             * We have a size expression; attempt to
             * evaluate it.
             */
            stdscan_reset();
            stdscan_set((char *)spcword);
            tokval.t_type = TOKEN_INVALID;
            e = evaluate(stdscan, NULL, &tokval, &fwd, 0, NULL);
            if (fwd) {
                sym->nextfwd = fwds;
                fwds = sym;
                sym->name = nasm_strdup(name);
            } else if (e) {
                if (!is_simple(e))
                    nasm_nonfatal("cannot use relocatable"
                                  " expression as symbol size");
                else
                    sym->size = reloc_value(e);
            }
            stdscan_set(saveme);
        }
    }

    /*
     * If it is in a TLS segment, mark symbol accordingly.
     */
    if (sec && (sec->flags & SHF_TLS))
        type = STT_TLS;

    /* Note: ELF32_ST_INFO() and ELF64_ST_INFO() are identical */
    sym->type = ELF32_ST_INFO(bind, type);

    if (sym_type_local(sym->type)) {
        nlocals++;
    } else {
        /*
         * If sym->section == SHN_ABS, then the first line of the
         * else section would cause a core dump, because its a reference
         * beyond the end of the section array.
         * This behaviour is exhibited by this code:
         *     GLOBAL crash_nasm
         *     crash_nasm equ 0
         * To avoid such a crash, such requests are silently discarded.
         * This may not be the best solution.
         */
        if (sym->section == XSHN_UNDEF || sym->section == XSHN_COMMON) {
            bsym = raa_write(bsym, segment, nglobs);
        } else if (sym->section != XSHN_ABS) {
            /*
             * This is a global symbol; so we must add it to the rbtree
             * of global symbols in its section.
             *
             * In addition, we check the special text for symbol
             * type and size information.
             */
            sects[sym->section-1]->gsyms =
                rb_insert(sects[sym->section-1]->gsyms, &sym->symv);

        }
        sym->globnum = nglobs;
        nglobs++;
    }
}

static void elf_add_reloc(struct elf_section *sect, int32_t segment,
                          int64_t offset, int type)
{
    struct elf_reloc *r;

    r = *sect->tail = nasm_zalloc(sizeof(struct elf_reloc));
    sect->tail = &r->next;

    r->address = sect->len;
    r->offset = offset;

    if (segment != NO_SEG) {
        const struct elf_section *s;
        s = raa_read_ptr(section_by_index, segment >> 1);
        if (s)
            r->symbol = s->shndx + 1;
        else
            r->symbol = GLOBAL_TEMP_BASE + raa_read(bsym, segment);
    }
    r->type = type;

    sect->nrelocs++;
}

/*
 * This routine deals with ..got and ..sym relocations: the more
 * complicated kinds. In shared-library writing, some relocations
 * with respect to global symbols must refer to the precise symbol
 * rather than referring to an offset from the base of the section
 * _containing_ the symbol. Such relocations call to this routine,
 * which searches the symbol list for the symbol in question.
 *
 * R_386_GOT32 | R_X86_64_GOT32 references require the _exact_ symbol address to be
 * used; R_386_32 | R_X86_64_32 references can be at an offset from the symbol.
 * The boolean argument `exact' tells us this.
 *
 * Return value is the adjusted value of `addr', having become an
 * offset from the symbol rather than the section. Should always be
 * zero when returning from an exact call.
 *
 * Limitation: if you define two symbols at the same place,
 * confusion will occur.
 *
 * Inefficiency: we search, currently, using a linked list which
 * isn't even necessarily sorted.
 */
static int64_t elf_add_gsym_reloc(struct elf_section *sect,
                                  int32_t segment, uint64_t offset,
                                  int64_t pcrel, int type, bool exact)
{
    struct elf_reloc *r;
    struct elf_section *s;
    struct elf_symbol *sym;
    struct rbtree *srb;

    /*
     * First look up the segment/offset pair and find a global
     * symbol corresponding to it. If it's not one of our segments,
     * then it must be an external symbol, in which case we're fine
     * doing a normal elf_add_reloc after first sanity-checking
     * that the offset from the symbol is zero.
     */
    s = raa_read_ptr(section_by_index, segment >> 1);
    if (!s) {
        if (exact && offset)
            nasm_nonfatal("invalid access to an external symbol");
        else
            elf_add_reloc(sect, segment, offset - pcrel, type);
        return 0;
    }

    srb = rb_search(s->gsyms, offset);
    if (!srb || (exact && srb->key != offset)) {
        nasm_nonfatal("unable to find a suitable global symbol"
                      " for this reference");
        return 0;
    }
    sym = container_of(srb, struct elf_symbol, symv);

    r = *sect->tail = nasm_malloc(sizeof(struct elf_reloc));
    sect->tail = &r->next;

    r->next     = NULL;
    r->address  = sect->len;
    r->offset = offset - pcrel - sym->symv.key;
    r->symbol   = GLOBAL_TEMP_BASE + sym->globnum;
    r->type     = type;

    sect->nrelocs++;
    return r->offset;
}

static void elf32_out(int32_t segto, const void *data,
                      enum out_type type, uint64_t size,
                      int32_t segment, int32_t wrt)
{
    struct elf_section *s;
    int64_t addr;
    int reltype, bytes;
    static struct symlininfo sinfo;

    /*
     * handle absolute-assembly (structure definitions)
     */
    if (segto == NO_SEG) {
        if (type != OUT_RESERVE)
            nasm_nonfatal("attempt to assemble code in [ABSOLUTE] space");
        return;
    }

    s = raa_read_ptr(section_by_index, segto >> 1);
    if (!s) {
        int tempint;            /* ignored */
        if (segto != elf_section_names(".text", &tempint))
            nasm_panic("strange segment conditions in ELF driver");
        else
            s = sects[nsects - 1];
    }

    /* again some stabs debugging stuff */
    sinfo.offset = s->len;
    /* Adjust to an index of the section table. */
    sinfo.section = s->shndx - 1;
    sinfo.segto = segto;
    sinfo.name = s->name;
    dfmt->debug_output(TY_DEBUGSYMLIN, &sinfo);
    /* end of debugging stuff */

    if (s->type == SHT_NOBITS && type != OUT_RESERVE) {
        nasm_warn(WARN_OTHER, "attempt to initialize memory in"
                  " BSS section `%s': ignored", s->name);
        s->len += realsize(type, size);
        return;
    }

    switch (type) {
    case OUT_RESERVE:
        if (s->type != SHT_NOBITS) {
            nasm_warn(WARN_ZEROING, "uninitialized space declared in"
                      " non-BSS section `%s': zeroing", s->name);
            elf_sect_write(s, NULL, size);
        } else
            s->len += size;
        break;

    case OUT_RAWDATA:
        elf_sect_write(s, data, size);
        break;

    case OUT_ADDRESS:
    {
        bool err = false;
        int asize = abs((int)size);

        addr = *(int64_t *)data;
        if (segment != NO_SEG) {
            if (segment & 1) {
                nasm_nonfatal("ELF format does not support"
                              " segment base references");
            } else {
                if (wrt == NO_SEG) {
                    /*
                     * The if() is a hack to deal with compilers which
                     * don't handle switch() statements with 64-bit
                     * expressions.
                     */
                    switch (asize) {
                    case 1:
                        elf_add_reloc(s, segment, 0, R_386_8);
                        break;
                    case 2:
                        elf_add_reloc(s, segment, 0, R_386_16);
                        break;
                    case 4:
                        elf_add_reloc(s, segment, 0, R_386_32);
                        break;
                    default: /* Error issued further down */
                        err = true;
                        break;
                    }
                } else if (wrt == elf_gotpc_sect + 1) {
                    /*
                     * The user will supply GOT relative to $$. ELF
                     * will let us have GOT relative to $. So we
                     * need to fix up the data item by $-$$.
                     */
                    err = asize != 4;
                    addr += s->len;
                    elf_add_reloc(s, segment, 0, R_386_GOTPC);
                } else if (wrt == elf_gotoff_sect + 1) {
                    err = asize != 4;
                    elf_add_reloc(s, segment, 0, R_386_GOTOFF);
                } else if (wrt == elf_tlsie_sect + 1) {
                    err = asize != 4;
                    addr = elf_add_gsym_reloc(s, segment, addr, 0,
                                              R_386_TLS_IE, true);
                } else if (wrt == elf_got_sect + 1) {
                    err = asize != 4;
                    addr = elf_add_gsym_reloc(s, segment, addr, 0,
                                              R_386_GOT32, true);
                } else if (wrt == elf_sym_sect + 1) {
                    switch (asize) {
                    case 1:
                        addr = elf_add_gsym_reloc(s, segment, addr, 0,
                                                  R_386_8, false);
                        break;
                    case 2:
                        addr = elf_add_gsym_reloc(s, segment, addr, 0,
                                                  R_386_16, false);
                        break;
                    case 4:
                        addr = elf_add_gsym_reloc(s, segment, addr, 0,
                                                  R_386_32, false);
                        break;
                    default:
                        err = true;
                        break;
                    }
                } else if (wrt == elf_plt_sect + 1) {
                    nasm_nonfatal("ELF format cannot produce non-PC-"
                                  "relative PLT references");
                } else {
                    nasm_nonfatal("ELF format does not support this"
                                  " use of WRT");
                    wrt = NO_SEG; /* we can at least _try_ to continue */
                }
            }
        }

        if (err) {
            nasm_nonfatal("Unsupported %d-bit ELF relocation", asize << 3);
        }
        elf_sect_writeaddr(s, addr, asize);
        break;
    }

    case OUT_REL1ADR:
        reltype = R_386_PC8;
        bytes = 1;
        goto rel12adr;
    case OUT_REL2ADR:
        reltype = R_386_PC16;
        bytes = 2;
        goto rel12adr;

rel12adr:
        addr = *(int64_t *)data - size;
        nasm_assert(segment != segto);
        if (segment != NO_SEG && (segment & 1)) {
            nasm_nonfatal("ELF format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, 0, reltype);
            } else {
                nasm_nonfatal("Unsupported %d-bit ELF relocation", bytes << 3);
            }
        }
        elf_sect_writeaddr(s, addr, bytes);
        break;

    case OUT_REL4ADR:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_panic("intra-segment OUT_REL4ADR");
        if (segment != NO_SEG && (segment & 1)) {
            nasm_nonfatal("ELF format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, 0, R_386_PC32);
            } else if (wrt == elf_plt_sect + 1) {
                elf_add_reloc(s, segment, 0, R_386_PLT32);
            } else if (wrt == elf_gotpc_sect + 1 ||
                       wrt == elf_gotoff_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                nasm_nonfatal("ELF format cannot produce PC-"
                              "relative GOT references");
            } else {
                nasm_nonfatal("ELF format does not support this"
                              " use of WRT");
                wrt = NO_SEG;   /* we can at least _try_ to continue */
            }
        }
        elf_sect_writeaddr(s, addr, 4);
        break;

    case OUT_REL8ADR:
        nasm_nonfatal("32-bit ELF format does not support 64-bit relocations");
        addr = 0;
        elf_sect_writeaddr(s, addr, 8);
        break;

    default:
        panic();
    }
}
static void elf64_out(int32_t segto, const void *data,
                      enum out_type type, uint64_t size,
                      int32_t segment, int32_t wrt)
{
    struct elf_section *s;
    int64_t addr;
    int reltype, bytes;
    static struct symlininfo sinfo;

    /*
     * handle absolute-assembly (structure definitions)
     */
    if (segto == NO_SEG) {
        if (type != OUT_RESERVE)
            nasm_nonfatal("attempt to assemble code in [ABSOLUTE] space");
        return;
    }

    s = raa_read_ptr(section_by_index, segto >> 1);
    if (!s) {
        int tempint;            /* ignored */
        if (segto != elf_section_names(".text", &tempint))
            nasm_panic("strange segment conditions in ELF driver");
        else
            s = sects[nsects - 1];
    }

    /* again some stabs debugging stuff */
    sinfo.offset = s->len;
    /* Adjust to an index of the section table. */
    sinfo.section = s->shndx - 1;
    sinfo.segto = segto;
    sinfo.name = s->name;
    dfmt->debug_output(TY_DEBUGSYMLIN, &sinfo);
    /* end of debugging stuff */

    if (s->type == SHT_NOBITS && type != OUT_RESERVE) {
        nasm_warn(WARN_OTHER, "attempt to initialize memory in"
                  " BSS section `%s': ignored", s->name);
        s->len += realsize(type, size);
        return;
    }

    switch (type) {
    case OUT_RESERVE:
        if (s->type != SHT_NOBITS) {
            nasm_warn(WARN_ZEROING, "uninitialized space declared in"
                      " non-BSS section `%s': zeroing", s->name);
            elf_sect_write(s, NULL, size);
        } else
            s->len += size;
        break;

    case OUT_RAWDATA:
        if (segment != NO_SEG)
            nasm_panic("OUT_RAWDATA with other than NO_SEG");
        elf_sect_write(s, data, size);
        break;

    case OUT_ADDRESS:
    {
        int isize = (int)size;
        int asize = abs((int)size);

        addr = *(int64_t *)data;
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELF format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                switch (isize) {
                case 1:
                case -1:
                    elf_add_reloc(s, segment, addr, R_X86_64_8);
                    break;
                case 2:
                case -2:
                    elf_add_reloc(s, segment, addr, R_X86_64_16);
                    break;
                case 4:
                    elf_add_reloc(s, segment, addr, R_X86_64_32);
                    break;
                case -4:
                    elf_add_reloc(s, segment, addr, R_X86_64_32S);
                    break;
                case 8:
                case -8:
                    elf_add_reloc(s, segment, addr, R_X86_64_64);
                    break;
                default:
                    nasm_panic("internal error elf64-hpa-871");
                    break;
                }
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1) {
                /*
                 * The user will supply GOT relative to $$. ELF
                 * will let us have GOT relative to $. So we
                 * need to fix up the data item by $-$$.
                 */
                addr += s->len;
                elf_add_reloc(s, segment, addr, R_X86_64_GOTPC32);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1) {
                if (asize != 8) {
                    nasm_nonfatal("ELF64 requires ..gotoff "
                                  "references to be qword");
                } else {
                    elf_add_reloc(s, segment, addr, R_X86_64_GOTOFF64);
                    addr = 0;
                }
            } else if (wrt == elf_got_sect + 1) {
                switch (asize) {
                case 4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_GOT32, true);
                    addr = 0;
                    break;
                case 8:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_GOT64, true);
                    addr = 0;
                    break;
                default:
                    nasm_nonfatal("invalid ..got reference");
                    break;
                }
            } else if (wrt == elf_sym_sect + 1) {
                switch (isize) {
                case 1:
                case -1:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_8, false);
                    addr = 0;
                    break;
                case 2:
                case -2:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_16, false);
                    addr = 0;
                    break;
                case 4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_32, false);
                    addr = 0;
                    break;
                case -4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_32S, false);
                    addr = 0;
                    break;
                case 8:
                case -8:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_64, false);
                    addr = 0;
                    break;
                default:
                    nasm_panic("internal error elf64-hpa-903");
                    break;
                }
            } else if (wrt == elf_plt_sect + 1) {
                nasm_nonfatal("ELF format cannot produce non-PC-"
                              "relative PLT references");
            } else {
                nasm_nonfatal("ELF format does not support this"
                              " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, asize);
        break;
    }

    case OUT_REL1ADR:
        reltype = R_X86_64_PC8;
        bytes = 1;
        goto rel12adr;

    case OUT_REL2ADR:
        reltype = R_X86_64_PC16;
        bytes = 2;
        goto rel12adr;

rel12adr:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_panic("intra-segment OUT_REL1ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELF format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, reltype);
                addr = 0;
            } else {
                nasm_nonfatal("Unsupported %d-bit ELF relocation", bytes << 3);
            }
        }
        elf_sect_writeaddr(s, addr, bytes);
        break;

    case OUT_REL4ADR:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_panic("intra-segment OUT_REL4ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELF64 format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, R_X86_64_PC32);
                addr = 0;
            } else if (wrt == elf_plt_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_PLT32, true);
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTPCREL, true);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                nasm_nonfatal("ELF64 requires ..gotoff references to be "
                              "qword absolute");
            } else if (wrt == elf_gottpoff_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTTPOFF, true);
                addr = 0;
            } else {
                nasm_nonfatal("ELF64 format does not support this"
                              " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, 4);
        break;

    case OUT_REL8ADR:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_panic("intra-segment OUT_REL8ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELF64 format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, R_X86_64_PC64);
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTPCREL64, true);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                nasm_nonfatal("ELF64 requires ..gotoff references to be "
                              "absolute");
            } else if (wrt == elf_gottpoff_sect + 1) {
                nasm_nonfatal("ELF64 requires ..gottpoff references to be "
                              "dword");
            } else {
                nasm_nonfatal("ELF64 format does not support this"
                              " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, 8);
        break;

    default:
        panic();
    }
}

static void elfx32_out(int32_t segto, const void *data,
                       enum out_type type, uint64_t size,
                       int32_t segment, int32_t wrt)
{
    struct elf_section *s;
    int64_t addr;
    int reltype, bytes;
    static struct symlininfo sinfo;

    /*
     * handle absolute-assembly (structure definitions)
     */
    if (segto == NO_SEG) {
        if (type != OUT_RESERVE)
            nasm_nonfatal("attempt to assemble code in [ABSOLUTE] space");
        return;
    }

    s = raa_read_ptr(section_by_index, segto >> 1);
    if (!s) {
        int tempint;            /* ignored */
        if (segto != elf_section_names(".text", &tempint))
            nasm_panic("strange segment conditions in ELF driver");
        else
            s = sects[nsects - 1];
    }

    /* again some stabs debugging stuff */
    sinfo.offset = s->len;
    /* Adjust to an index of the section table. */
    sinfo.section = s->shndx - 1;
    sinfo.segto = segto;
    sinfo.name = s->name;
    dfmt->debug_output(TY_DEBUGSYMLIN, &sinfo);
    /* end of debugging stuff */

    if (s->type == SHT_NOBITS && type != OUT_RESERVE) {
        nasm_warn(WARN_OTHER, "attempt to initialize memory in"
                  " BSS section `%s': ignored", s->name);
        s->len += realsize(type, size);
        return;
    }

    switch (type) {
    case OUT_RESERVE:
        if (s->type != SHT_NOBITS) {
            nasm_warn(WARN_ZEROING, "uninitialized space declared in"
                      " non-BSS section `%s': zeroing", s->name);
            elf_sect_write(s, NULL, size);
        } else
            s->len += size;
        break;

    case OUT_RAWDATA:
        if (segment != NO_SEG)
            nasm_panic("OUT_RAWDATA with other than NO_SEG");
        elf_sect_write(s, data, size);
        break;

    case OUT_ADDRESS:
    {
        int isize = (int)size;
        int asize = abs((int)size);

        addr = *(int64_t *)data;
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELF format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                switch (isize) {
                case 1:
                case -1:
                    elf_add_reloc(s, segment, addr, R_X86_64_8);
                    break;
                case 2:
                case -2:
                    elf_add_reloc(s, segment, addr, R_X86_64_16);
                    break;
                case 4:
                    elf_add_reloc(s, segment, addr, R_X86_64_32);
                    break;
                case -4:
                    elf_add_reloc(s, segment, addr, R_X86_64_32S);
                    break;
                case 8:
                case -8:
                    elf_add_reloc(s, segment, addr, R_X86_64_64);
                    break;
                default:
                    nasm_panic("internal error elfx32-hpa-871");
                    break;
                }
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1) {
                /*
                 * The user will supply GOT relative to $$. ELF
                 * will let us have GOT relative to $. So we
                 * need to fix up the data item by $-$$.
                 */
                addr += s->len;
                elf_add_reloc(s, segment, addr, R_X86_64_GOTPC32);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1) {
                nasm_nonfatal("ELFX32 doesn't support "
                              "R_X86_64_GOTOFF64");
            } else if (wrt == elf_got_sect + 1) {
                switch (asize) {
                case 4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_GOT32, true);
                    addr = 0;
                    break;
                default:
                    nasm_nonfatal("invalid ..got reference");
                    break;
                }
            } else if (wrt == elf_sym_sect + 1) {
                switch (isize) {
                case 1:
                case -1:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_8, false);
                    addr = 0;
                    break;
                case 2:
                case -2:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_16, false);
                    addr = 0;
                    break;
                case 4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_32, false);
                    addr = 0;
                    break;
                case -4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_32S, false);
                    addr = 0;
                    break;
                case 8:
                case -8:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_64, false);
                    addr = 0;
                    break;
                default:
                    nasm_panic("internal error elfx32-hpa-903");
                    break;
                }
            } else if (wrt == elf_plt_sect + 1) {
                nasm_nonfatal("ELF format cannot produce non-PC-"
                              "relative PLT references");
            } else {
                nasm_nonfatal("ELF format does not support this"
                              " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, asize);
        break;
    }

    case OUT_REL1ADR:
        reltype = R_X86_64_PC8;
        bytes = 1;
        goto rel12adr;

    case OUT_REL2ADR:
        reltype = R_X86_64_PC16;
        bytes = 2;
        goto rel12adr;

rel12adr:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_panic("intra-segment OUT_REL1ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELF format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, reltype);
                addr = 0;
            } else {
                nasm_nonfatal("unsupported %d-bit ELF relocation", bytes << 3);
            }
        }
        elf_sect_writeaddr(s, addr, bytes);
        break;

    case OUT_REL4ADR:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_panic("intra-segment OUT_REL4ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment & 1) {
            nasm_nonfatal("ELFX32 format does not support"
                          " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, R_X86_64_PC32);
                addr = 0;
            } else if (wrt == elf_plt_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_PLT32, true);
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTPCREL, true);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                nasm_nonfatal("invalid ..gotoff reference");
            } else if (wrt == elf_gottpoff_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTTPOFF, true);
                addr = 0;
            } else {
                nasm_nonfatal("ELFX32 format does not support this use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, 4);
        break;

    case OUT_REL8ADR:
        nasm_nonfatal("32-bit ELF format does not support 64-bit relocations");
        addr = 0;
        elf_sect_writeaddr(s, addr, 8);
        break;

    default:
        panic();
    }
}

/*
 * Section index/count with a specified overflow value (usually SHN_INDEX,
 * but 0 for e_shnum.
 */
static inline uint16_t elf_shndx(int section, uint16_t overflow)
{
    return cpu_to_le16(section < (int)SHN_LORESERVE ? section : overflow);
}

struct ehdr_common {
    uint8_t	e_ident[EI_NIDENT];
    uint16_t    e_type;
    uint16_t    e_machine;
    uint32_t	e_version;
};

union ehdr {
    Elf32_Ehdr ehdr32;
    Elf64_Ehdr ehdr64;
    struct ehdr_common com;
};

static void elf_write(void)
{
    int align;
    char *p;
    int i;
    size_t symtablocal;
    int sec_shstrtab, sec_symtab, sec_strtab;
    union ehdr ehdr;

    /*
     * Add any sections we don't already have:
     * rel/rela sections for the user sections, debug sections, and
     * the ELF special sections.
     */

    sec_debug = nsections;
    if (dfmt_is_stabs()) {
        /* in case the debug information is wanted, just add these three sections... */
        add_sectname("", ".stab");
        add_sectname("", ".stabstr");
        add_sectname(efmt->relpfx, ".stab");
    } else if (dfmt_is_dwarf()) {
        /* the dwarf debug standard specifies the following ten sections,
           not all of which are currently implemented,
           although all of them are defined. */
        add_sectname("", ".debug_aranges");
        add_sectname(efmt->relpfx, ".debug_aranges");
        add_sectname("", ".debug_pubnames");
        add_sectname("", ".debug_info");
        add_sectname(efmt->relpfx, ".debug_info");
        add_sectname("", ".debug_abbrev");
        add_sectname("", ".debug_line");
        add_sectname(efmt->relpfx, ".debug_line");
        add_sectname("", ".debug_frame");
        add_sectname("", ".debug_loc");
    }

    sec_shstrtab = add_sectname("", ".shstrtab");
    sec_symtab   = add_sectname("", ".symtab");
    sec_strtab   = add_sectname("", ".strtab");

    /*
     * Build the symbol table and relocation tables.
     */
    symtablocal = elf_build_symtab();

    /* Do we need an .symtab_shndx section? */
    if (symtab_shndx)
        add_sectname("", ".symtab_shndx");

    for (i = 0; i < nsects; i++) {
        if (sects[i]->head) {
            add_sectname(efmt->relpfx, sects[i]->name);
            sects[i]->rel = efmt->elf_build_reltab(sects[i]->head);
        }
    }

    /*
     * Output the ELF header.
     */
    nasm_zero(ehdr);

    /* These fields are in the same place for 32 and 64 bits */
    memcpy(&ehdr.com.e_ident[EI_MAG0], ELFMAG, SELFMAG);
    ehdr.com.e_ident[EI_CLASS]      = efmt->ei_class;
    ehdr.com.e_ident[EI_DATA]       = ELFDATA2LSB;
    ehdr.com.e_ident[EI_VERSION]    = EV_CURRENT;
    ehdr.com.e_ident[EI_OSABI]      = elf_osabi;
    ehdr.com.e_ident[EI_ABIVERSION] = elf_abiver;
    ehdr.com.e_type                 = cpu_to_le16(ET_REL);
    ehdr.com.e_machine              = cpu_to_le16(efmt->e_machine);
    ehdr.com.e_version              = cpu_to_le16(EV_CURRENT);

    if (!efmt->elf64) {
        ehdr.ehdr32.e_shoff         = cpu_to_le32(sizeof ehdr);
        ehdr.ehdr32.e_ehsize        = cpu_to_le16(sizeof(Elf32_Ehdr));
        ehdr.ehdr32.e_shentsize     = cpu_to_le16(sizeof(Elf32_Shdr));
        ehdr.ehdr32.e_shnum         = elf_shndx(nsections, 0);
        ehdr.ehdr32.e_shstrndx      = elf_shndx(sec_shstrtab, SHN_XINDEX);
    } else {
        ehdr.ehdr64.e_shoff         = cpu_to_le64(sizeof ehdr);
        ehdr.ehdr64.e_ehsize        = cpu_to_le16(sizeof(Elf64_Ehdr));
        ehdr.ehdr64.e_shentsize     = cpu_to_le16(sizeof(Elf64_Shdr));
        ehdr.ehdr64.e_shnum         = elf_shndx(nsections, 0);
        ehdr.ehdr64.e_shstrndx      = elf_shndx(sec_shstrtab, SHN_XINDEX);
    }

    nasm_write(&ehdr, sizeof(ehdr), ofile);
    elf_foffs = sizeof ehdr + efmt->shdr_size * nsections;

    /*
     * Now output the section header table.
     */
    align = ALIGN(elf_foffs, SEC_FILEALIGN) - elf_foffs;
    elf_foffs += align;
    elf_nsect = 0;
    elf_sects = nasm_malloc(sizeof(*elf_sects) * nsections);

    /* SHN_UNDEF */
    elf_section_header(0, SHT_NULL, 0, NULL, false,
                       nsections > (int)SHN_LORESERVE ? nsections : 0,
                       sec_shstrtab >= (int)SHN_LORESERVE ? sec_shstrtab : 0,
                       0, 0, 0);
    p = shstrtab + 1;

    /* The normal sections */
    for (i = 0; i < nsects; i++) {
        elf_section_header(p - shstrtab, sects[i]->type, sects[i]->flags,
                           sects[i]->data, true,
                           sects[i]->len, 0, 0,
                           sects[i]->align, sects[i]->entsize);
        p += strlen(p) + 1;
    }

    /* The debugging sections */
    if (dfmt_is_stabs()) {
        /* for debugging information, create the last three sections
           which are the .stab , .stabstr and .rel.stab sections respectively */

        /* this function call creates the stab sections in memory */
        stabs_generate();

        if (stabbuf && stabstrbuf && stabrelbuf) {
            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, stabbuf, false,
                               stablen, sec_stabstr, 0, 4, 12);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_STRTAB, 0, stabstrbuf, false,
                               stabstrlen, 0, 0, 4, 0);
            p += strlen(p) + 1;

            /* link -> symtable  info -> section to refer to */
            elf_section_header(p - shstrtab, efmt->reltype, 0,
                               stabrelbuf, false, stabrellen,
                               sec_symtab, sec_stab,
                               efmt->word, efmt->relsize);
            p += strlen(p) + 1;
        }
    } else if (dfmt_is_dwarf()) {
        /* for dwarf debugging information, create the ten dwarf sections */

        /* this function call creates the dwarf sections in memory */
	if (dwarf_fsect)
            dwarf_generate();

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, arangesbuf, false,
                           arangeslen, 0, 0, 1, 0);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, efmt->reltype, 0, arangesrelbuf, false,
			   arangesrellen, sec_symtab,
                           sec_debug_aranges,
                           efmt->word, efmt->relsize);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, pubnamesbuf,
                           false, pubnameslen, 0, 0, 1, 0);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, infobuf, false,
                           infolen, 0, 0, 1, 0);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, efmt->reltype, 0, inforelbuf, false,
                           inforellen, sec_symtab,
                           sec_debug_info,
                           efmt->word, efmt->relsize);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, abbrevbuf, false,
                           abbrevlen, 0, 0, 1, 0);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, linebuf, false,
                           linelen, 0, 0, 1, 0);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, efmt->reltype, 0, linerelbuf, false,
                           linerellen, sec_symtab,
                           sec_debug_line,
                           efmt->word, efmt->relsize);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, framebuf, false,
                           framelen, 0, 0, 8, 0);
        p += strlen(p) + 1;

        elf_section_header(p - shstrtab, SHT_PROGBITS, 0, locbuf, false,
                           loclen, 0, 0, 1, 0);
        p += strlen(p) + 1;
    }

    /* .shstrtab */
    elf_section_header(p - shstrtab, SHT_STRTAB, 0, shstrtab, false,
                       shstrtablen, 0, 0, 1, 0);
    p += strlen(p) + 1;

    /* .symtab */
    elf_section_header(p - shstrtab, SHT_SYMTAB, 0, symtab, true,
                       symtab->datalen, sec_strtab, symtablocal,
                       efmt->word, efmt->sym_size);
    p += strlen(p) + 1;

    /* .strtab */
    elf_section_header(p - shstrtab, SHT_STRTAB, 0, strs, true,
                       strslen, 0, 0, 1, 0);
    p += strlen(p) + 1
;
    /* .symtab_shndx */
    if (symtab_shndx) {
        elf_section_header(p - shstrtab, SHT_SYMTAB_SHNDX, 0,
                           symtab_shndx, true, symtab_shndx->datalen,
                           sec_symtab, 0, 1, 0);
        p += strlen(p) + 1;
    }

    /* The relocation sections */
    for (i = 0; i < nsects; i++) {
        if (sects[i]->rel) {
            elf_section_header(p - shstrtab, efmt->reltype, 0,
                               sects[i]->rel, true, sects[i]->rel->datalen,
                               sec_symtab, sects[i]->shndx,
                               efmt->word, efmt->relsize);
            p += strlen(p) + 1;
        }
    }
    fwritezero(align, ofile);

    /*
     * Now output the sections.
     */
    elf_write_sections();

    nasm_free(elf_sects);
    saa_free(symtab);
    if (symtab_shndx)
        saa_free(symtab_shndx);
}

static size_t nsyms;

static void elf_sym(const struct elf_symbol *sym)
{
    int shndx = sym->section;

    /*
     * Careful here. This relies on sym->section being signed; for
     * special section indices this value needs to be cast to
     * (int16_t) so that it sign-extends, however, here SHN_LORESERVE
     * is used as an unsigned constant.
     */
    if (shndx >= (int)SHN_LORESERVE) {
        if (unlikely(!symtab_shndx)) {
            /* Create symtab_shndx and fill previous entries with zero */
            symtab_shndx = saa_init(1);
            saa_wbytes(symtab_shndx, NULL, nsyms << 2);
        }
    } else {
        shndx = 0;              /* Section index table always write zero */
    }

    if (symtab_shndx)
        saa_write32(symtab_shndx, shndx);

    efmt->elf_sym(sym);
    nsyms++;
}

static void elf32_sym(const struct elf_symbol *sym)
{
    Elf32_Sym sym32;

    sym32.st_name     = cpu_to_le32(sym->strpos);
    sym32.st_value    = cpu_to_le32(sym->symv.key);
    sym32.st_size     = cpu_to_le32(sym->size);
    sym32.st_info     = sym->type;
    sym32.st_other    = sym->other;
    sym32.st_shndx    = elf_shndx(sym->section, SHN_XINDEX);
    saa_wbytes(symtab, &sym32, sizeof sym32);
}

static void elf64_sym(const struct elf_symbol *sym)
{
    Elf64_Sym sym64;

    sym64.st_name     = cpu_to_le32(sym->strpos);
    sym64.st_value    = cpu_to_le64(sym->symv.key);
    sym64.st_size     = cpu_to_le64(sym->size);
    sym64.st_info     = sym->type;
    sym64.st_other    = sym->other;
    sym64.st_shndx    = elf_shndx(sym->section, SHN_XINDEX);
    saa_wbytes(symtab, &sym64, sizeof sym64);
}

static size_t elf_build_symtab(void)
{
    struct elf_symbol *sym, xsym;
    size_t nlocal;
    int i;

    symtab       = saa_init(1);
    symtab_shndx = NULL;

    /*
     * Zero symbol first as required by spec.
     */
    nasm_zero(xsym);
    elf_sym(&xsym);

    /*
     * Next, an entry for the file name.
     */
    nasm_zero(xsym);
    xsym.strpos  = 1;
    xsym.type    = ELF32_ST_INFO(STB_LOCAL, STT_FILE);
    xsym.section = XSHN_ABS;
    elf_sym(&xsym);

    /*
     * Now some standard symbols defining the segments, for relocation
     * purposes.
     */
    nasm_zero(xsym);
    for (i = 1; i <= nsects; i++) {
        xsym.type    = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        xsym.section = i;
        elf_sym(&xsym);
    }

    /*
     * dwarf needs symbols for debug sections
     * which are relocation targets.
     */
    if (dfmt_is_dwarf()) {
        dwarf_infosym   = nsyms;
        xsym.section    = sec_debug_info;
        elf_sym(&xsym);

        dwarf_abbrevsym = nsyms;
        xsym.section    = sec_debug_abbrev;
        elf_sym(&xsym);

        dwarf_linesym   = nsyms;
        xsym.section    = sec_debug_line;
        elf_sym(&xsym);
    }

    /*
     * Now the other local symbols.
     */
    saa_rewind(syms);
    while ((sym = saa_rstruct(syms))) {
        if (!sym_type_local(sym->type))
            continue;

        elf_sym(sym);
    }

    nlocal = nsyms;

    /*
     * Now the global symbols.
     */
    saa_rewind(syms);
    while ((sym = saa_rstruct(syms))) {
        if (sym_type_local(sym->type))
            continue;

        elf_sym(sym);
    }

    return nlocal;
}

static struct SAA *elf32_build_reltab(const struct elf_reloc *r)
{
    struct SAA *s;
    int32_t global_offset;
    Elf32_Rel rel32;

    if (!r)
        return NULL;

    s = saa_init(1L);

    /*
     * How to onvert from a global placeholder to a real symbol index;
     * the +2 refers to the two special entries, the null entry and
     * the filename entry.
     */
    global_offset = -GLOBAL_TEMP_BASE + nsects + nlocals + ndebugs + 2;

    while (r) {
        int32_t sym = r->symbol;

        if (sym >= GLOBAL_TEMP_BASE)
            sym += global_offset;

        rel32.r_offset    = cpu_to_le32(r->address);
        rel32.r_info      = cpu_to_le32(ELF32_R_INFO(sym, r->type));
        saa_wbytes(s, &rel32, sizeof rel32);

        r = r->next;
    }

    return s;
}

static struct SAA *elfx32_build_reltab(const struct elf_reloc *r)
{
    struct SAA *s;
    int32_t global_offset;
    Elf32_Rela rela32;

    if (!r)
        return NULL;

    s = saa_init(1L);

    /*
     * How to onvert from a global placeholder to a real symbol index;
     * the +2 refers to the two special entries, the null entry and
     * the filename entry.
     */
    global_offset = -GLOBAL_TEMP_BASE + nsects + nlocals + ndebugs + 2;

    while (r) {
        int32_t sym = r->symbol;

        if (sym >= GLOBAL_TEMP_BASE)
            sym += global_offset;

        rela32.r_offset   = cpu_to_le32(r->address);
        rela32.r_info     = cpu_to_le32(ELF32_R_INFO(sym, r->type));
        rela32.r_addend   = cpu_to_le32(r->offset);
        saa_wbytes(s, &rela32, sizeof rela32);

        r = r->next;
    }

    return s;
}

static struct SAA *elf64_build_reltab(const struct elf_reloc *r)
{
    struct SAA *s;
    int32_t global_offset;
    Elf64_Rela rela64;

    if (!r)
        return NULL;

    s = saa_init(1L);

    /*
     * How to onvert from a global placeholder to a real symbol index;
     * the +2 refers to the two special entries, the null entry and
     * the filename entry.
     */
    global_offset = -GLOBAL_TEMP_BASE + nsects + nlocals + ndebugs + 2;

    while (r) {
        int32_t sym = r->symbol;

        if (sym >= GLOBAL_TEMP_BASE)
            sym += global_offset;

        rela64.r_offset   = cpu_to_le64(r->address);
        rela64.r_info     = cpu_to_le64(ELF64_R_INFO(sym, r->type));
        rela64.r_addend   = cpu_to_le64(r->offset);
        saa_wbytes(s, &rela64, sizeof rela64);

        r = r->next;
    }

    return s;
}

static void elf_section_header(int name, int type, uint64_t flags,
                               void *data, bool is_saa, uint64_t datalen,
                               int link, int info,
                               uint64_t align, uint64_t entsize)
{
    elf_sects[elf_nsect].data = data;
    elf_sects[elf_nsect].len = datalen;
    elf_sects[elf_nsect].is_saa = is_saa;
    elf_nsect++;

    if (!efmt->elf64) {
        Elf32_Shdr  shdr;

        shdr.sh_name         = cpu_to_le32(name);
        shdr.sh_type         = cpu_to_le32(type);
        shdr.sh_flags        = cpu_to_le32(flags);
        shdr.sh_addr         = 0;
        shdr.sh_offset       = cpu_to_le32(type == SHT_NULL ? 0 : elf_foffs);
        shdr.sh_size         = cpu_to_le32(datalen);
        if (data)
            elf_foffs += ALIGN(datalen, SEC_FILEALIGN);
        shdr.sh_link         = cpu_to_le32(link);
        shdr.sh_info         = cpu_to_le32(info);
        shdr.sh_addralign    = cpu_to_le32(align);
        shdr.sh_entsize      = cpu_to_le32(entsize);

        nasm_write(&shdr, sizeof shdr, ofile);
    } else {
        Elf64_Shdr  shdr;

        shdr.sh_name         = cpu_to_le32(name);
        shdr.sh_type         = cpu_to_le32(type);
        shdr.sh_flags        = cpu_to_le64(flags);
        shdr.sh_addr         = 0;
        shdr.sh_offset       = cpu_to_le64(type == SHT_NULL ? 0 : elf_foffs);
        shdr.sh_size         = cpu_to_le64(datalen);
        if (data)
            elf_foffs += ALIGN(datalen, SEC_FILEALIGN);
        shdr.sh_link        = cpu_to_le32(link);
        shdr.sh_info        = cpu_to_le32(info);
        shdr.sh_addralign   = cpu_to_le64(align);
        shdr.sh_entsize     = cpu_to_le64(entsize);

        nasm_write(&shdr, sizeof shdr, ofile);
    }
}

static void elf_write_sections(void)
{
    int i;
    for (i = 0; i < elf_nsect; i++)
        if (elf_sects[i].data) {
            int32_t len = elf_sects[i].len;
            int32_t reallen = ALIGN(len, SEC_FILEALIGN);
            int32_t align = reallen - len;
            if (elf_sects[i].is_saa)
                saa_fpwrite(elf_sects[i].data, ofile);
            else
                nasm_write(elf_sects[i].data, len, ofile);
            fwritezero(align, ofile);
        }
}

static void elf_sect_write(struct elf_section *sect, const void *data, size_t len)
{
    saa_wbytes(sect->data, data, len);
    sect->len += len;
}

static void elf_sect_writeaddr(struct elf_section *sect, int64_t data, size_t len)
{
    saa_writeaddr(sect->data, data, len);
    sect->len += len;
}

static void elf_sectalign(int32_t seg, unsigned int value)
{
    struct elf_section *s;

    s = raa_read_ptr(section_by_index, seg >> 1);
    if (!s || !is_power2(value))
        return;

    if (value > s->align)
        s->align = value;
}

extern macros_t elf_stdmac[];

/* Claim "elf" as a pragma namespace, for the future */
static const struct pragma_facility elf_pragma_list[] =
{
    { "elf", NULL },
    { NULL, NULL }          /* Implements the canonical output name */
};


static const struct dfmt elf32_df_dwarf = {
    "ELF32 (i386) dwarf (newer)",
    "dwarf",
    dwarf32_init,
    dwarf_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    debug_typevalue,
    dwarf_output,
    dwarf_cleanup,
    NULL                        /* pragma list */
};

static const struct dfmt elf32_df_stabs = {
    "ELF32 (i386) stabs (older)",
    "stabs",
    null_debug_init,
    stabs_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    debug_typevalue,
    stabs_output,
    stabs_cleanup,
    NULL                        /* pragma list */
};

static const struct dfmt * const elf32_debugs_arr[3] =
  { &elf32_df_dwarf, &elf32_df_stabs, NULL };

const struct ofmt of_elf32 = {
    "ELF32 (i386) (Linux, most Unix variants)",
    "elf32",
    ".o",
    0,
    32,
    elf32_debugs_arr,
    &elf32_df_dwarf,
    elf_stdmac,
    elf32_init,
    null_reset,
    nasm_do_legacy_output,
    elf32_out,
    elf_deflabel,
    elf_section_names,
    NULL,
    elf_sectalign,
    null_segbase,
    elf_directive,
    elf_cleanup,
    elf_pragma_list,
};

static const struct dfmt elf64_df_dwarf = {
    "ELF64 (x86-64) dwarf (newer)",
    "dwarf",
    dwarf64_init,
    dwarf_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    debug_typevalue,
    dwarf_output,
    dwarf_cleanup,
    NULL                        /* pragma list */
};

static const struct dfmt elf64_df_stabs = {
    "ELF64 (x86-64) stabs (older)",
    "stabs",
    null_debug_init,
    stabs_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    debug_typevalue,
    stabs_output,
    stabs_cleanup,
    NULL                        /* pragma list */
};

static const struct dfmt * const elf64_debugs_arr[3] =
  { &elf64_df_dwarf, &elf64_df_stabs, NULL };

const struct ofmt of_elf64 = {
    "ELF64 (x86-64) (Linux, most Unix variants)",
    "elf64",
    ".o",
    0,
    64,
    elf64_debugs_arr,
    &elf64_df_dwarf,
    elf_stdmac,
    elf64_init,
    null_reset,
    nasm_do_legacy_output,
    elf64_out,
    elf_deflabel,
    elf_section_names,
    NULL,
    elf_sectalign,
    null_segbase,
    elf_directive,
    elf_cleanup,
    elf_pragma_list,
};

static const struct dfmt elfx32_df_dwarf = {
    "ELFx32 (x86-64) dwarf (newer)",
    "dwarf",
    dwarfx32_init,
    dwarf_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    debug_typevalue,
    dwarf_output,
    dwarf_cleanup,
    NULL                        /* pragma list */
};

static const struct dfmt elfx32_df_stabs = {
    "ELFx32 (x86-64) stabs (older)",
    "stabs",
    null_debug_init,
    stabs_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    debug_typevalue,
    stabs_output,
    stabs_cleanup,
    elf_pragma_list,
};

static const struct dfmt * const elfx32_debugs_arr[3] =
  { &elfx32_df_dwarf, &elfx32_df_stabs, NULL };

const struct ofmt of_elfx32 = {
    "ELFx32 (ELF32 for x86-64) (Linux)",
    "elfx32",
    ".o",
    0,
    64,
    elfx32_debugs_arr,
    &elfx32_df_dwarf,
    elf_stdmac,
    elfx32_init,
    null_reset,
    nasm_do_legacy_output,
    elfx32_out,
    elf_deflabel,
    elf_section_names,
    NULL,
    elf_sectalign,
    null_segbase,
    elf_directive,
    elf_cleanup,
    NULL                        /* pragma list */
};

static bool is_elf64(void)
{
	return ofmt == &of_elf64;
}

static bool is_elf32(void)
{
	return ofmt == &of_elf32;
}

static bool is_elfx32(void)
{
	return ofmt == &of_elfx32;
}

static bool dfmt_is_stabs(void)
{
	return dfmt == &elf32_df_stabs ||
               dfmt == &elfx32_df_stabs ||
               dfmt == &elf64_df_stabs;
}

static bool dfmt_is_dwarf(void)
{
	return dfmt == &elf32_df_dwarf ||
               dfmt == &elfx32_df_dwarf ||
               dfmt == &elf64_df_dwarf;
}

/* common debugging routines */
static void debug_typevalue(int32_t type)
{
    int32_t stype, ssize;
    switch (TYM_TYPE(type)) {
        case TY_LABEL:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
        case TY_BYTE:
            ssize = 1;
            stype = STT_OBJECT;
            break;
        case TY_WORD:
            ssize = 2;
            stype = STT_OBJECT;
            break;
        case TY_DWORD:
            ssize = 4;
            stype = STT_OBJECT;
            break;
        case TY_FLOAT:
            ssize = 4;
            stype = STT_OBJECT;
            break;
        case TY_QWORD:
            ssize = 8;
            stype = STT_OBJECT;
            break;
        case TY_TBYTE:
            ssize = 10;
            stype = STT_OBJECT;
            break;
        case TY_OWORD:
            ssize = 16;
            stype = STT_OBJECT;
            break;
        case TY_YWORD:
            ssize = 32;
            stype = STT_OBJECT;
            break;
        case TY_ZWORD:
            ssize = 64;
            stype = STT_OBJECT;
            break;
        case TY_COMMON:
            ssize = 0;
            stype = STT_COMMON;
            break;
        case TY_SEG:
            ssize = 0;
            stype = STT_SECTION;
            break;
        case TY_EXTERN:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
        case TY_EQU:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
        default:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
    }
    /* Set type and size info on most recently seen symbol if we haven't set it already.
       But avoid setting size info on object (data) symbols in absolute sections (which
       is primarily structs); some environments get confused with non-zero-extent absolute
       object symbols and end up showing them in backtraces for NULL fn pointer calls. */
    if (stype == STT_OBJECT && lastsym && !lastsym->type && lastsym->section != XSHN_ABS) {
        lastsym->size = ssize;
        lastsym->type = stype;
    }
}

/* stabs debugging routines */

static void stabs_linenum(const char *filename, int32_t linenumber, int32_t segto)
{
    (void)segto;
    if (!stabs_filename) {
        stabs_filename = nasm_malloc(strlen(filename) + 1);
        strcpy(stabs_filename, filename);
    } else {
        if (strcmp(stabs_filename, filename)) {
            /* yep, a memory leak...this program is one-shot anyway, so who cares...
               in fact, this leak comes in quite handy to maintain a list of files
               encountered so far in the symbol lines... */

            /* why not nasm_free(stabs_filename); we're done with the old one */

            stabs_filename = nasm_malloc(strlen(filename) + 1);
            strcpy(stabs_filename, filename);
        }
    }
    debug_immcall = 1;
    currentline = linenumber;
}

static void stabs_output(int type, void *param)
{
    struct symlininfo *s;
    struct linelist *el;
    if (type == TY_DEBUGSYMLIN) {
        if (debug_immcall) {
            s = (struct symlininfo *)param;
            if (!(sects[s->section]->flags & SHF_EXECINSTR))
                return; /* line info is only collected for executable sections */
            numlinestabs++;
            el = nasm_malloc(sizeof(struct linelist));
            el->info.offset = s->offset;
            el->info.section = s->section;
            el->info.name = s->name;
            el->line = currentline;
            el->filename = stabs_filename;
            el->next = 0;
            if (stabslines) {
                stabslines->last->next = el;
                stabslines->last = el;
            } else {
                stabslines = el;
                stabslines->last = el;
            }
        }
    }
    debug_immcall = 0;
}

/* for creating the .stab , .stabstr and .rel.stab sections in memory */

static void stabs_generate(void)
{
    int i, numfiles, strsize, numstabs = 0, currfile, mainfileindex;
    uint8_t *sbuf, *ssbuf, *rbuf, *sptr, *rptr;
    char **allfiles;
    int *fileidx;

    struct linelist *ptr;

    ptr = stabslines;

    allfiles = nasm_zalloc(numlinestabs * sizeof(char *));
    numfiles = 0;
    while (ptr) {
        if (numfiles == 0) {
            allfiles[0] = ptr->filename;
            numfiles++;
        } else {
            for (i = 0; i < numfiles; i++) {
                if (!strcmp(allfiles[i], ptr->filename))
                    break;
            }
            if (i >= numfiles) {
                allfiles[i] = ptr->filename;
                numfiles++;
            }
        }
        ptr = ptr->next;
    }
    strsize = 1;
    fileidx = nasm_malloc(numfiles * sizeof(int));
    for (i = 0; i < numfiles; i++) {
        fileidx[i] = strsize;
        strsize += strlen(allfiles[i]) + 1;
    }
    currfile = mainfileindex = 0;
    for (i = 0; i < numfiles; i++) {
        if (!strcmp(allfiles[i], elf_module)) {
            currfile = mainfileindex = i;
            break;
        }
    }

    /*
     * worst case size of the stab buffer would be:
     * the sourcefiles changes each line, which would mean 1 SOL, 1 SYMLIN per line
     * plus one "ending" entry
     */
    sbuf = nasm_malloc((numlinestabs * 2 + 4) *
                                    sizeof(struct stabentry));
    ssbuf = nasm_malloc(strsize);
    rbuf = nasm_malloc(numlinestabs * (is_elf64() ? 16 : 8) * (2 + 3));
    rptr = rbuf;

    for (i = 0; i < numfiles; i++)
        strcpy((char *)ssbuf + fileidx[i], allfiles[i]);
    ssbuf[0] = 0;

    stabstrlen = strsize;       /* set global variable for length of stab strings */

    sptr = sbuf;
    ptr = stabslines;
    numstabs = 0;

    if (ptr) {
        /*
         * this is the first stab, its strx points to the filename of the
         * source-file, the n_desc field should be set to the number
         * of remaining stabs
         */
        WRITE_STAB(sptr, fileidx[0], 0, 0, 0, stabstrlen);

        /* this is the stab for the main source file */
        WRITE_STAB(sptr, fileidx[mainfileindex], N_SO, 0, 0, 0);

        /* relocation table entry */

        /*
         * Since the symbol table has two entries before
         * the section symbols, the index in the info.section
         * member must be adjusted by adding 2
         */

        if (is_elf32()) {
            WRITELONG(rptr, (sptr - sbuf) - 4);
            WRITELONG(rptr, ((ptr->info.section + 2) << 8) | R_386_32);
        } else if (is_elfx32()) {
            WRITELONG(rptr, (sptr - sbuf) - 4);
            WRITELONG(rptr, ((ptr->info.section + 2) << 8) | R_X86_64_32);
            WRITELONG(rptr, 0);
        } else {
            nasm_assert(is_elf64());
            WRITEDLONG(rptr, (int64_t)(sptr - sbuf) - 4);
            WRITELONG(rptr, R_X86_64_32);
            WRITELONG(rptr, ptr->info.section + 2);
            WRITEDLONG(rptr, 0);
        }
        numstabs++;
    }

    if (is_elf32()) {
        while (ptr) {
            if (strcmp(allfiles[currfile], ptr->filename)) {
                /* oops file has changed... */
                for (i = 0; i < numfiles; i++)
                    if (!strcmp(allfiles[i], ptr->filename))
                        break;
                currfile = i;
                WRITE_STAB(sptr, fileidx[currfile], N_SOL, 0, 0,
                           ptr->info.offset);
                numstabs++;

                /* relocation table entry */
                WRITELONG(rptr, (sptr - sbuf) - 4);
                WRITELONG(rptr, ((ptr->info.section + 2) << 8) | R_386_32);
            }

            WRITE_STAB(sptr, 0, N_SLINE, 0, ptr->line, ptr->info.offset);
            numstabs++;

            /* relocation table entry */
            WRITELONG(rptr, (sptr - sbuf) - 4);
            WRITELONG(rptr, ((ptr->info.section + 2) << 8) | R_386_32);

            ptr = ptr->next;
        }
    } else if (is_elfx32()) {
        while (ptr) {
            if (strcmp(allfiles[currfile], ptr->filename)) {
                /* oops file has changed... */
                for (i = 0; i < numfiles; i++)
                    if (!strcmp(allfiles[i], ptr->filename))
                        break;
                currfile = i;
                WRITE_STAB(sptr, fileidx[currfile], N_SOL, 0, 0,
                           ptr->info.offset);
                numstabs++;

                /* relocation table entry */
                WRITELONG(rptr, (sptr - sbuf) - 4);
                WRITELONG(rptr, ((ptr->info.section + 2) << 8) | R_X86_64_32);
                WRITELONG(rptr, ptr->info.offset);
            }

            WRITE_STAB(sptr, 0, N_SLINE, 0, ptr->line, ptr->info.offset);
            numstabs++;

            /* relocation table entry */
            WRITELONG(rptr, (sptr - sbuf) - 4);
            WRITELONG(rptr, ((ptr->info.section + 2) << 8) | R_X86_64_32);
            WRITELONG(rptr, ptr->info.offset);

            ptr = ptr->next;
        }
    } else {
        nasm_assert(is_elf64());
        while (ptr) {
            if (strcmp(allfiles[currfile], ptr->filename)) {
                /* oops file has changed... */
                for (i = 0; i < numfiles; i++)
                    if (!strcmp(allfiles[i], ptr->filename))
                        break;
                currfile = i;
                WRITE_STAB(sptr, fileidx[currfile], N_SOL, 0, 0,
                           ptr->info.offset);
                numstabs++;

                /* relocation table entry */
                WRITEDLONG(rptr, (int64_t)(sptr - sbuf) - 4);
                WRITELONG(rptr, R_X86_64_32);
                WRITELONG(rptr, ptr->info.section + 2);
                WRITEDLONG(rptr, ptr->info.offset);
            }

            WRITE_STAB(sptr, 0, N_SLINE, 0, ptr->line, ptr->info.offset);
            numstabs++;

            /* relocation table entry */
            WRITEDLONG(rptr, (int64_t)(sptr - sbuf) - 4);
            WRITELONG(rptr, R_X86_64_32);
            WRITELONG(rptr, ptr->info.section + 2);
            WRITEDLONG(rptr, ptr->info.offset);

            ptr = ptr->next;
        }
    }

    /* this is an "ending" token */
    WRITE_STAB(sptr, 0, N_SO, 0, 0, 0);
    numstabs++;

    ((struct stabentry *)sbuf)->n_desc = numstabs;

    nasm_free(allfiles);
    nasm_free(fileidx);

    stablen = (sptr - sbuf);
    stabrellen = (rptr - rbuf);
    stabrelbuf = rbuf;
    stabbuf = sbuf;
    stabstrbuf = ssbuf;
}

static void stabs_cleanup(void)
{
    struct linelist *ptr, *del;
    if (!stabslines)
        return;

    ptr = stabslines;
    while (ptr) {
        del = ptr;
        ptr = ptr->next;
        nasm_free(del);
    }

    nasm_free(stabbuf);
    nasm_free(stabrelbuf);
    nasm_free(stabstrbuf);
}

/* dwarf routines */

static void dwarf_init_common(const struct dwarf_format *fmt)
{
    dwfmt = fmt;
    ndebugs = 3; /* 3 debug symbols */
}

static void dwarf32_init(void)
{
    static const struct dwarf_format dwfmt32 = {
        2,                          /* DWARF 2 */
        /* section version numbers: */
        { 2,                        /* .debug_aranges */
          0,                        /* .rela.debug_aranges */
          2,                        /* .debug_pubnames */
          2,                        /* .debug_info */
          0,                        /* .rela.debug_info */
          0,                        /* .debug_abbrev */
          2,                        /* .debug_line */
          0,                        /* .rela.debug_line */
          1,                        /* .debug_frame */
          0 }                       /* .debug_loc */
    };
    dwarf_init_common(&dwfmt32);
}

static void dwarfx32_init(void)
{
    static const struct dwarf_format dwfmtx32 = {
        3,                          /* DWARF 3 */
        /* section version numbers: */
        { 2,                        /* .debug_aranges */
          0,                        /* .rela.debug_aranges */
          2,                        /* .debug_pubnames */
          3,                        /* .debug_info */
          0,                        /* .rela.debug_info */
          0,                        /* .debug_abbrev */
          3,                        /* .debug_line */
          0,                        /* .rela.debug_line */
          3,                        /* .debug_frame */
          0 }                       /* .debug_loc */
    };
    dwarf_init_common(&dwfmtx32);
}

static void dwarf64_init(void)
{
    static const struct dwarf_format dwfmt64 = {
        3,                          /* DWARF 3 */
        /* section version numbers: */
        { 2,                        /* .debug_aranges */
          0,                        /* .rela.debug_aranges */
          2,                        /* .debug_pubnames */
          3,                        /* .debug_info */
          0,                        /* .rela.debug_info */
          0,                        /* .debug_abbrev */
          3,                        /* .debug_line */
          0,                        /* .rela.debug_line */
          3,                        /* .debug_frame */
          0 }                       /* .debug_loc */
    };
    dwarf_init_common(&dwfmt64);
}

static void dwarf_linenum(const char *filename, int32_t linenumber,
                            int32_t segto)
{
    (void)segto;
    dwarf_findfile(filename);
    debug_immcall = 1;
    currentline = linenumber;
}

/* called from elf_out with type == TY_DEBUGSYMLIN */
static void dwarf_output(int type, void *param)
{
    int ln, aa, inx, maxln, soc;
    struct symlininfo *s;
    struct SAA *plinep;

    (void)type;

    s = (struct symlininfo *)param;

    /* line number info is only gathered for executable sections */
    if (!(sects[s->section]->flags & SHF_EXECINSTR))
        return;

    /* Check if section index has changed */
    if (!(dwarf_csect && (dwarf_csect->section) == (s->section)))
        dwarf_findsect(s->section);

    /* do nothing unless line or file has changed */
    if (!debug_immcall)
        return;

    ln = currentline - dwarf_csect->line;
    aa = s->offset - dwarf_csect->offset;
    inx = dwarf_clist->line;
    plinep = dwarf_csect->psaa;
    /* check for file change */
    if (!(inx == dwarf_csect->file)) {
        saa_write8(plinep,DW_LNS_set_file);
        saa_write8(plinep,inx);
        dwarf_csect->file = inx;
    }
    /* check for line change */
    if (ln) {
        /* test if in range of special op code */
        maxln = line_base + line_range;
        soc = (ln - line_base) + (line_range * aa) + opcode_base;
        if (ln >= line_base && ln < maxln && soc < 256) {
            saa_write8(plinep,soc);
        } else {
            saa_write8(plinep,DW_LNS_advance_line);
            saa_wleb128s(plinep,ln);
            if (aa) {
                saa_write8(plinep,DW_LNS_advance_pc);
                saa_wleb128u(plinep,aa);
            }
            saa_write8(plinep,DW_LNS_copy);
        }
        dwarf_csect->line = currentline;
        dwarf_csect->offset = s->offset;
    }

    /* show change handled */
    debug_immcall = 0;
}


static void dwarf_generate(void)
{
    uint8_t *pbuf;
    int indx;
    struct linelist *ftentry;
    struct SAA *paranges, *ppubnames, *pinfo, *pabbrev, *plines, *plinep;
    struct SAA *parangesrel, *plinesrel, *pinforel;
    struct sectlist *psect;
    size_t saalen, linepoff, totlen, highaddr;

    if (is_elf32()) {
        /* write epilogues for each line program range */
        /* and build aranges section */
        paranges = saa_init(1L);
        parangesrel = saa_init(1L);
        saa_write16(paranges, dwfmt->sect_version[DWARF_ARANGES]);
        saa_write32(parangesrel, paranges->datalen+4);
        saa_write32(parangesrel, (dwarf_infosym << 8) +  R_386_32); /* reloc to info */
        saa_write32(paranges,0);    /* offset into info */
        saa_write8(paranges,4);     /* pointer size */
        saa_write8(paranges,0);     /* not segmented */
        saa_write32(paranges,0);    /* padding */
        /* iterate though sectlist entries */
        psect = dwarf_fsect;
        totlen = 0;
        highaddr = 0;
        for (indx = 0; indx < dwarf_nsections; indx++) {
            plinep = psect->psaa;
            /* Line Number Program Epilogue */
            saa_write8(plinep,2);           /* std op 2 */
            saa_write8(plinep,(sects[psect->section]->len)-psect->offset);
            saa_write8(plinep,DW_LNS_extended_op);
            saa_write8(plinep,1);           /* operand length */
            saa_write8(plinep,DW_LNE_end_sequence);
            totlen += plinep->datalen;
            /* range table relocation entry */
            saa_write32(parangesrel, paranges->datalen + 4);
            saa_write32(parangesrel, ((uint32_t) (psect->section + 2) << 8) +  R_386_32);
            /* range table entry */
            saa_write32(paranges,0x0000);   /* range start */
            saa_write32(paranges,sects[psect->section]->len); /* range length */
            highaddr += sects[psect->section]->len;
            /* done with this entry */
            psect = psect->next;
        }
        saa_write32(paranges,0);    /* null address */
        saa_write32(paranges,0);    /* null length */
        saalen = paranges->datalen;
        arangeslen = saalen + 4;
        arangesbuf = pbuf = nasm_malloc(arangeslen);
        WRITELONG(pbuf,saalen);     /* initial length */
        saa_rnbytes(paranges, pbuf, saalen);
        saa_free(paranges);
    } else if (is_elfx32()) {
        /* write epilogues for each line program range */
        /* and build aranges section */
        paranges = saa_init(1L);
        parangesrel = saa_init(1L);
        saa_write16(paranges, dwfmt->sect_version[DWARF_ARANGES]);
        saa_write32(parangesrel, paranges->datalen+4);
        saa_write32(parangesrel, (dwarf_infosym << 8) +  R_X86_64_32); /* reloc to info */
        saa_write32(parangesrel, 0);
        saa_write32(paranges,0);            /* offset into info */
        saa_write8(paranges,4);             /* pointer size */
        saa_write8(paranges,0);             /* not segmented */
        saa_write32(paranges,0);            /* padding */
        /* iterate though sectlist entries */
        psect = dwarf_fsect;
        totlen = 0;
        highaddr = 0;
        for (indx = 0; indx < dwarf_nsections; indx++)  {
            plinep = psect->psaa;
            /* Line Number Program Epilogue */
            saa_write8(plinep,2);			/* std op 2 */
            saa_write8(plinep,(sects[psect->section]->len)-psect->offset);
            saa_write8(plinep,DW_LNS_extended_op);
            saa_write8(plinep,1);			/* operand length */
            saa_write8(plinep,DW_LNE_end_sequence);
            totlen += plinep->datalen;
            /* range table relocation entry */
            saa_write32(parangesrel, paranges->datalen + 4);
            saa_write32(parangesrel, ((uint32_t) (psect->section + 2) << 8) +  R_X86_64_32);
            saa_write32(parangesrel, (uint32_t) 0);
            /* range table entry */
            saa_write32(paranges,0x0000);		/* range start */
            saa_write32(paranges,sects[psect->section]->len);	/* range length */
            highaddr += sects[psect->section]->len;
            /* done with this entry */
            psect = psect->next;
        }
        saa_write32(paranges,0);		/* null address */
        saa_write32(paranges,0);		/* null length */
        saalen = paranges->datalen;
        arangeslen = saalen + 4;
        arangesbuf = pbuf = nasm_malloc(arangeslen);
        WRITELONG(pbuf,saalen);			/* initial length */
        saa_rnbytes(paranges, pbuf, saalen);
        saa_free(paranges);
    } else {
        nasm_assert(is_elf64());
        /* write epilogues for each line program range */
        /* and build aranges section */
        paranges = saa_init(1L);
        parangesrel = saa_init(1L);
        saa_write16(paranges, dwfmt->sect_version[DWARF_ARANGES]);
        saa_write64(parangesrel, paranges->datalen+4);
        saa_write64(parangesrel, (dwarf_infosym << 32) +  R_X86_64_32); /* reloc to info */
        saa_write64(parangesrel, 0);
        saa_write32(paranges,0);            /* offset into info */
        saa_write8(paranges,8);             /* pointer size */
        saa_write8(paranges,0);             /* not segmented */
        saa_write32(paranges,0);            /* padding */
        /* iterate though sectlist entries */
        psect = dwarf_fsect;
        totlen = 0;
        highaddr = 0;
        for (indx = 0; indx < dwarf_nsections; indx++) {
            plinep = psect->psaa;
            /* Line Number Program Epilogue */
            saa_write8(plinep,2);			/* std op 2 */
            saa_write8(plinep,(sects[psect->section]->len)-psect->offset);
            saa_write8(plinep,DW_LNS_extended_op);
            saa_write8(plinep,1);			/* operand length */
            saa_write8(plinep,DW_LNE_end_sequence);
            totlen += plinep->datalen;
            /* range table relocation entry */
            saa_write64(parangesrel, paranges->datalen + 4);
            saa_write64(parangesrel, ((uint64_t) (psect->section + 2) << 32) +  R_X86_64_64);
            saa_write64(parangesrel, (uint64_t) 0);
            /* range table entry */
            saa_write64(paranges,0x0000);		/* range start */
            saa_write64(paranges,sects[psect->section]->len);	/* range length */
            highaddr += sects[psect->section]->len;
            /* done with this entry */
            psect = psect->next;
        }
        saa_write64(paranges,0);		/* null address */
        saa_write64(paranges,0);		/* null length */
        saalen = paranges->datalen;
        arangeslen = saalen + 4;
        arangesbuf = pbuf = nasm_malloc(arangeslen);
        WRITELONG(pbuf,saalen);			/* initial length */
        saa_rnbytes(paranges, pbuf, saalen);
        saa_free(paranges);
    }

    /* build rela.aranges section */
    arangesrellen = saalen = parangesrel->datalen;
    arangesrelbuf = pbuf = nasm_malloc(arangesrellen);
    saa_rnbytes(parangesrel, pbuf, saalen);
    saa_free(parangesrel);

    /* build pubnames section */
    if (0) {
        ppubnames = saa_init(1L);
        saa_write16(ppubnames,dwfmt->sect_version[DWARF_PUBNAMES]);
        saa_write32(ppubnames,0);   /* offset into info */
        saa_write32(ppubnames,0);   /* space used in info */
        saa_write32(ppubnames,0);   /* end of list */
        saalen = ppubnames->datalen;
        pubnameslen = saalen + 4;
        pubnamesbuf = pbuf = nasm_malloc(pubnameslen);
        WRITELONG(pbuf,saalen);     /* initial length */
        saa_rnbytes(ppubnames, pbuf, saalen);
        saa_free(ppubnames);
    } else {
        /* Don't write a section without actual information */
        pubnameslen = 0;
    }

    if (is_elf32()) {
        /* build info section */
        pinfo = saa_init(1L);
        pinforel = saa_init(1L);
        saa_write16(pinfo, dwfmt->sect_version[DWARF_INFO]);
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, (dwarf_abbrevsym << 8) +  R_386_32); /* reloc to abbrev */
        saa_write32(pinfo,0);       /* offset into abbrev */
        saa_write8(pinfo,4);        /* pointer size */
        saa_write8(pinfo,1);        /* abbrviation number LEB128u */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, ((dwarf_fsect->section + 2) << 8) +  R_386_32);
        saa_write32(pinfo,0);       /* DW_AT_low_pc */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, ((dwarf_fsect->section + 2) << 8) +  R_386_32);
        saa_write32(pinfo,highaddr);    /* DW_AT_high_pc */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, (dwarf_linesym << 8) +  R_386_32); /* reloc to line */
        saa_write32(pinfo,0);       /* DW_AT_stmt_list */
        saa_wbytes(pinfo, elf_module, strlen(elf_module)+1); /* DW_AT_name */
        saa_wbytes(pinfo, elf_dir, strlen(elf_dir)+1); /* DW_AT_comp_dir */
        saa_wbytes(pinfo, nasm_signature(), nasm_signature_len()+1);
        saa_write16(pinfo,DW_LANG_Mips_Assembler);
        saa_write8(pinfo,2);        /* abbrviation number LEB128u */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, ((dwarf_fsect->section + 2) << 8) +  R_386_32);
        saa_write32(pinfo,0);       /* DW_AT_low_pc */
        saa_write32(pinfo,0);       /* DW_AT_frame_base */
        saa_write8(pinfo,0);        /* end of entries */
        saalen = pinfo->datalen;
        infolen = saalen + 4;
        infobuf = pbuf = nasm_malloc(infolen);
        WRITELONG(pbuf,saalen);     /* initial length */
        saa_rnbytes(pinfo, pbuf, saalen);
        saa_free(pinfo);
    } else if (is_elfx32()) {
        /* build info section */
        pinfo = saa_init(1L);
        pinforel = saa_init(1L);
        saa_write16(pinfo, dwfmt->sect_version[DWARF_INFO]);
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, (dwarf_abbrevsym << 8) + R_X86_64_32); /* reloc to abbrev */
        saa_write32(pinforel, 0);
        saa_write32(pinfo,0);			/* offset into abbrev */
        saa_write8(pinfo,4);			/* pointer size */
        saa_write8(pinfo,1);			/* abbrviation number LEB128u */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, ((dwarf_fsect->section + 2) << 8) + R_X86_64_32);
        saa_write32(pinforel, 0);
        saa_write32(pinfo,0);			/* DW_AT_low_pc */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, ((dwarf_fsect->section + 2) << 8) + R_X86_64_32);
        saa_write32(pinforel, highaddr);
        saa_write32(pinfo,0);			/* DW_AT_high_pc */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, (dwarf_linesym << 8) + R_X86_64_32); /* reloc to line */
        saa_write32(pinforel, 0);
        saa_write32(pinfo,0);			/* DW_AT_stmt_list */
        saa_wbytes(pinfo, elf_module, strlen(elf_module)+1); /* DW_AT_name */
        saa_wbytes(pinfo, elf_dir, strlen(elf_dir)+1); /* DW_AT_comp_dir */
        saa_wbytes(pinfo, nasm_signature(), nasm_signature_len()+1);
        saa_write16(pinfo,DW_LANG_Mips_Assembler);
        saa_write8(pinfo,2);			/* abbrviation number LEB128u */
        saa_write32(pinforel, pinfo->datalen + 4);
        saa_write32(pinforel, ((dwarf_fsect->section + 2) << 8) +  R_X86_64_32);
        saa_write32(pinforel, 0);
        saa_write32(pinfo,0);			/* DW_AT_low_pc */
        saa_write32(pinfo,0);			/* DW_AT_frame_base */
        saa_write8(pinfo,0);			/* end of entries */
        saalen = pinfo->datalen;
        infolen = saalen + 4;
        infobuf = pbuf = nasm_malloc(infolen);
        WRITELONG(pbuf,saalen);     /* initial length */
        saa_rnbytes(pinfo, pbuf, saalen);
        saa_free(pinfo);
    } else {
        nasm_assert(is_elf64());
        /* build info section */
        pinfo = saa_init(1L);
        pinforel = saa_init(1L);
        saa_write16(pinfo, dwfmt->sect_version[DWARF_INFO]);
        saa_write64(pinforel, pinfo->datalen + 4);
        saa_write64(pinforel, (dwarf_abbrevsym << 32) +  R_X86_64_32); /* reloc to abbrev */
        saa_write64(pinforel, 0);
        saa_write32(pinfo,0);			/* offset into abbrev */
        saa_write8(pinfo,8);			/* pointer size */
        saa_write8(pinfo,1);			/* abbrviation number LEB128u */
        saa_write64(pinforel, pinfo->datalen + 4);
        saa_write64(pinforel, ((uint64_t)(dwarf_fsect->section + 2) << 32) +  R_X86_64_64);
        saa_write64(pinforel, 0);
        saa_write64(pinfo,0);			/* DW_AT_low_pc */
        saa_write64(pinforel, pinfo->datalen + 4);
        saa_write64(pinforel, ((uint64_t)(dwarf_fsect->section + 2) << 32) +  R_X86_64_64);
        saa_write64(pinforel, highaddr);
        saa_write64(pinfo,0);			/* DW_AT_high_pc */
        saa_write64(pinforel, pinfo->datalen + 4);
        saa_write64(pinforel, (dwarf_linesym << 32) +  R_X86_64_32); /* reloc to line */
        saa_write64(pinforel, 0);
        saa_write32(pinfo,0);			/* DW_AT_stmt_list */
        saa_wbytes(pinfo, elf_module, strlen(elf_module)+1); /* DW_AT_name */
        saa_wbytes(pinfo, elf_dir, strlen(elf_dir)+1); /* DW_AT_comp_dir */
        saa_wbytes(pinfo, nasm_signature(), nasm_signature_len()+1);
        saa_write16(pinfo,DW_LANG_Mips_Assembler);
        saa_write8(pinfo,2);			/* abbrviation number LEB128u */
        saa_write64(pinforel, pinfo->datalen + 4);
        saa_write64(pinforel, ((uint64_t)(dwarf_fsect->section + 2) << 32) +  R_X86_64_64);
        saa_write64(pinforel, 0);
        saa_write64(pinfo,0);			/* DW_AT_low_pc */
        saa_write64(pinfo,0);			/* DW_AT_frame_base */
        saa_write8(pinfo,0);			/* end of entries */
        saalen = pinfo->datalen;
        infolen = saalen + 4;
        infobuf = pbuf = nasm_malloc(infolen);
        WRITELONG(pbuf,saalen);     /* initial length */
        saa_rnbytes(pinfo, pbuf, saalen);
        saa_free(pinfo);
    }

    /* build rela.info section */
    inforellen = saalen = pinforel->datalen;
    inforelbuf = pbuf = nasm_malloc(inforellen);
    saa_rnbytes(pinforel, pbuf, saalen);
    saa_free(pinforel);

    /* build abbrev section */
    pabbrev = saa_init(1L);
    saa_write8(pabbrev,1);      /* entry number LEB128u */
    saa_write8(pabbrev,DW_TAG_compile_unit);    /* tag LEB128u */
    saa_write8(pabbrev,1);      /* has children */
    /* the following attributes and forms are all LEB128u values */
    saa_write8(pabbrev,DW_AT_low_pc);
    saa_write8(pabbrev,DW_FORM_addr);
    saa_write8(pabbrev,DW_AT_high_pc);
    saa_write8(pabbrev,DW_FORM_addr);
    saa_write8(pabbrev,DW_AT_stmt_list);
    saa_write8(pabbrev,DW_FORM_data4);
    saa_write8(pabbrev,DW_AT_name);
    saa_write8(pabbrev,DW_FORM_string);
    saa_write8(pabbrev,DW_AT_comp_dir);
    saa_write8(pabbrev,DW_FORM_string);
    saa_write8(pabbrev,DW_AT_producer);
    saa_write8(pabbrev,DW_FORM_string);
    saa_write8(pabbrev,DW_AT_language);
    saa_write8(pabbrev,DW_FORM_data2);
    saa_write16(pabbrev,0);     /* end of entry */
    /* LEB128u usage same as above */
    saa_write8(pabbrev,2);      /* entry number */
    saa_write8(pabbrev,DW_TAG_subprogram);
    saa_write8(pabbrev,0);      /* no children */
    saa_write8(pabbrev,DW_AT_low_pc);
    saa_write8(pabbrev,DW_FORM_addr);
    saa_write8(pabbrev,DW_AT_frame_base);
    saa_write8(pabbrev,DW_FORM_data4);
    saa_write16(pabbrev,0);     /* end of entry */
    /* Terminal zero entry */
    saa_write8(pabbrev,0);
    abbrevlen = saalen = pabbrev->datalen;
    abbrevbuf = pbuf = nasm_malloc(saalen);
    saa_rnbytes(pabbrev, pbuf, saalen);
    saa_free(pabbrev);

    /* build line section */
    /* prolog */
    plines = saa_init(1L);
    saa_write8(plines,1);           /* Minimum Instruction Length */
    saa_write8(plines,1);           /* Initial value of 'is_stmt' */
    saa_write8(plines,line_base);   /* Line Base */
    saa_write8(plines,line_range);  /* Line Range */
    saa_write8(plines,opcode_base); /* Opcode Base */
    /* standard opcode lengths (# of LEB128u operands) */
    saa_write8(plines,0);           /* Std opcode 1 length */
    saa_write8(plines,1);           /* Std opcode 2 length */
    saa_write8(plines,1);           /* Std opcode 3 length */
    saa_write8(plines,1);           /* Std opcode 4 length */
    saa_write8(plines,1);           /* Std opcode 5 length */
    saa_write8(plines,0);           /* Std opcode 6 length */
    saa_write8(plines,0);           /* Std opcode 7 length */
    saa_write8(plines,0);           /* Std opcode 8 length */
    saa_write8(plines,1);           /* Std opcode 9 length */
    saa_write8(plines,0);           /* Std opcode 10 length */
    saa_write8(plines,0);           /* Std opcode 11 length */
    saa_write8(plines,1);           /* Std opcode 12 length */
    /* Directory Table */
    saa_write8(plines,0);           /* End of table */
    /* File Name Table */
    ftentry = dwarf_flist;
    for (indx = 0; indx < dwarf_numfiles; indx++) {
        saa_wbytes(plines, ftentry->filename, (int32_t)(strlen(ftentry->filename) + 1));
        saa_write8(plines,0);       /* directory  LEB128u */
        saa_write8(plines,0);       /* time LEB128u */
        saa_write8(plines,0);       /* size LEB128u */
        ftentry = ftentry->next;
    }
    saa_write8(plines,0);           /* End of table */
    linepoff = plines->datalen;
    linelen = linepoff + totlen + 10;
    linebuf = pbuf = nasm_malloc(linelen);
    WRITELONG(pbuf,linelen-4);      /* initial length */
    WRITESHORT(pbuf,dwfmt->sect_version[DWARF_LINE]);
    WRITELONG(pbuf,linepoff);       /* offset to line number program */
    /* write line header */
    saalen = linepoff;
    saa_rnbytes(plines, pbuf, saalen);   /* read a given no. of bytes */
    pbuf += linepoff;
    saa_free(plines);
    /* concatenate line program ranges */
    linepoff += 13;
    plinesrel = saa_init(1L);
    psect = dwarf_fsect;
    if (is_elf32()) {
        for (indx = 0; indx < dwarf_nsections; indx++) {
            saa_write32(plinesrel, linepoff);
            saa_write32(plinesrel, ((uint32_t) (psect->section + 2) << 8) +  R_386_32);
            plinep = psect->psaa;
            saalen = plinep->datalen;
            saa_rnbytes(plinep, pbuf, saalen);
            pbuf += saalen;
            linepoff += saalen;
            saa_free(plinep);
            /* done with this entry */
            psect = psect->next;
        }
    } else if (is_elfx32()) {
        for (indx = 0; indx < dwarf_nsections; indx++) {
            saa_write32(plinesrel, linepoff);
            saa_write32(plinesrel, ((psect->section + 2) << 8) + R_X86_64_32);
            saa_write32(plinesrel, 0);
            plinep = psect->psaa;
            saalen = plinep->datalen;
            saa_rnbytes(plinep, pbuf, saalen);
            pbuf += saalen;
            linepoff += saalen;
            saa_free(plinep);
            /* done with this entry */
            psect = psect->next;
        }
    } else {
        nasm_assert(is_elf64());
        for (indx = 0; indx < dwarf_nsections; indx++) {
            saa_write64(plinesrel, linepoff);
            saa_write64(plinesrel, ((uint64_t) (psect->section + 2) << 32) +  R_X86_64_64);
            saa_write64(plinesrel, (uint64_t) 0);
            plinep = psect->psaa;
            saalen = plinep->datalen;
            saa_rnbytes(plinep, pbuf, saalen);
            pbuf += saalen;
            linepoff += saalen;
            saa_free(plinep);
            /* done with this entry */
            psect = psect->next;
        }
    }

    /* build rela.lines section */
    linerellen =saalen = plinesrel->datalen;
    linerelbuf = pbuf = nasm_malloc(linerellen);
    saa_rnbytes(plinesrel, pbuf, saalen);
    saa_free(plinesrel);

    /* build .debug_frame section */
    if (0) {
        framelen = 4;
        framebuf = pbuf = nasm_malloc(framelen);
        WRITELONG(pbuf,framelen-4); /* initial length */
    } else {
        /* Leave .debug_frame empty if not used! */
        framelen = 0;
    }

    /* build .debug_loc section */
    if (0) {
        loclen = 16;
        locbuf = pbuf = nasm_malloc(loclen);
        if (is_elf32() || is_elfx32()) {
            WRITELONG(pbuf,0);  /* null  beginning offset */
            WRITELONG(pbuf,0);  /* null  ending offset */
        } else {
            nasm_assert(is_elf64());
            WRITEDLONG(pbuf,0);  /* null  beginning offset */
            WRITEDLONG(pbuf,0);  /* null  ending offset */
        }
    } else {
        /* Leave .debug_frame empty if not used! */
        loclen = 0;
    }
}

static void dwarf_cleanup(void)
{
    nasm_free(arangesbuf);
    nasm_free(arangesrelbuf);
    nasm_free(pubnamesbuf);
    nasm_free(infobuf);
    nasm_free(inforelbuf);
    nasm_free(abbrevbuf);
    nasm_free(linebuf);
    nasm_free(linerelbuf);
    nasm_free(framebuf);
    nasm_free(locbuf);
}

static void dwarf_findfile(const char * fname)
{
    int finx;
    struct linelist *match;

    /* return if fname is current file name */
    if (dwarf_clist && !(strcmp(fname, dwarf_clist->filename)))
        return;

    /* search for match */
    match = 0;
    if (dwarf_flist) {
        match = dwarf_flist;
        for (finx = 0; finx < dwarf_numfiles; finx++) {
            if (!(strcmp(fname, match->filename))) {
                dwarf_clist = match;
                return;
            }
            match = match->next;
        }
    }

    /* add file name to end of list */
    dwarf_clist = nasm_malloc(sizeof(struct linelist));
    dwarf_numfiles++;
    dwarf_clist->line = dwarf_numfiles;
    dwarf_clist->filename = nasm_malloc(strlen(fname) + 1);
    strcpy(dwarf_clist->filename,fname);
    dwarf_clist->next = 0;
    if (!dwarf_flist) {     /* if first entry */
        dwarf_flist = dwarf_elist = dwarf_clist;
        dwarf_clist->last = 0;
    } else {                /* chain to previous entry */
        dwarf_elist->next = dwarf_clist;
        dwarf_elist = dwarf_clist;
    }
}

static void dwarf_findsect(const int index)
{
    int sinx;
    struct sectlist *match;
    struct SAA *plinep;

    /* return if index is current section index */
    if (dwarf_csect && (dwarf_csect->section == index))
        return;

    /* search for match */
    match = 0;
    if (dwarf_fsect) {
        match = dwarf_fsect;
        for (sinx = 0; sinx < dwarf_nsections; sinx++) {
            if (match->section == index) {
                dwarf_csect = match;
                return;
            }
            match = match->next;
        }
    }

    /* add entry to end of list */
    dwarf_csect = nasm_malloc(sizeof(struct sectlist));
    dwarf_nsections++;
    dwarf_csect->psaa = plinep = saa_init(1L);
    dwarf_csect->line = 1;
    dwarf_csect->offset = 0;
    dwarf_csect->file = 1;
    dwarf_csect->section = index;
    dwarf_csect->next = 0;
    /* set relocatable address at start of line program */
    saa_write8(plinep,DW_LNS_extended_op);
    saa_write8(plinep,is_elf64() ? 9 : 5);   /* operand length */
    saa_write8(plinep,DW_LNE_set_address);
    if (is_elf64())
        saa_write64(plinep,0);  /* Start Address */
    else
        saa_write32(plinep,0);  /* Start Address */

    if (!dwarf_fsect) { /* if first entry */
        dwarf_fsect = dwarf_esect = dwarf_csect;
        dwarf_csect->last = 0;
    } else {            /* chain to previous entry */
        dwarf_esect->next = dwarf_csect;
        dwarf_esect = dwarf_csect;
    }
}

#endif /* defined(OF_ELF32) || defined(OF_ELF64) || defined(OF_ELFX32) */
