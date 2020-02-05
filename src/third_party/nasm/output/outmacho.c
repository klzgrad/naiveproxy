/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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
 * outmacho.c	output routines for the Netwide Assembler to produce
 *		NeXTstep/OpenStep/Rhapsody/Darwin/MacOS X object files
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nasm.h"
#include "nasmlib.h"
#include "ilog2.h"
#include "labels.h"
#include "error.h"
#include "saa.h"
#include "raa.h"
#include "rbtree.h"
#include "hashtbl.h"
#include "outform.h"
#include "outlib.h"
#include "ver.h"
#include "dwarf.h"
#include "macho.h"

#if defined(OF_MACHO) || defined(OF_MACHO64)

/* Mach-O in-file header structure sizes */
#define MACHO_HEADER_SIZE		28
#define MACHO_SEGCMD_SIZE		56
#define MACHO_SECTCMD_SIZE		68
#define MACHO_SYMCMD_SIZE		24
#define MACHO_NLIST_SIZE		12
#define MACHO_RELINFO_SIZE		8

#define MACHO_HEADER64_SIZE		32
#define MACHO_SEGCMD64_SIZE		72
#define MACHO_SECTCMD64_SIZE		80
#define MACHO_NLIST64_SIZE		16

/* Mach-O relocations numbers */

#define VM_PROT_DEFAULT	(VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)
#define VM_PROT_ALL	(VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)

/* Our internal relocation types */
enum reltype {
    RL_ABS,			/* Absolute relocation */
    RL_REL,			/* Relative relocation */
    RL_TLV,			/* Thread local */
    RL_BRANCH,			/* Relative direct branch */
    RL_SUB,			/* X86_64_RELOC_SUBTRACT */
    RL_GOT,			/* X86_64_RELOC_GOT */
    RL_GOTLOAD			/* X86_64_RELOC_GOT_LOAD */
};
#define RL_MAX_32	RL_TLV
#define RL_MAX_64	RL_GOTLOAD

struct macho_fmt {
    uint32_t ptrsize;		/* Pointer size in bytes */
    uint32_t mh_magic;		/* Which magic number to use */
    uint32_t cpu_type;		/* Which CPU type */
    uint32_t lc_segment;	/* Which segment load command */
    uint32_t header_size;	/* Header size */
    uint32_t segcmd_size;	/* Segment command size */
    uint32_t sectcmd_size;	/* Section command size */
    uint32_t nlist_size;	/* Nlist (symbol) size */
    enum reltype maxreltype;	/* Maximum entry in enum reltype permitted */
    uint32_t reloc_abs;		/* Absolute relocation type */
    uint32_t reloc_rel;		/* Relative relocation type */
    uint32_t reloc_tlv;		/* Thread local relocation type */
    bool forcesym;		/* Always use "external" (symbol-relative) relocations */
};

static struct macho_fmt fmt;

static void fwriteptr(uint64_t data, FILE * fp)
{
    fwriteaddr(data, fmt.ptrsize, fp);
}

struct section {
    /* nasm internal data */
    struct section *next;
    struct SAA *data;
    int32_t index;		/* Main section index */
    int32_t subsection;		/* Current subsection index */
    int32_t fileindex;
    struct reloc *relocs;
    struct rbtree *syms[2]; /* All/global symbols symbols in section */
    int align;
    bool by_name;	    /* This section was specified by full MachO name */
    char namestr[34];	    /* segment,section as a C string */

    /* data that goes into the file */
    char sectname[16];     /* what this section is called */
    char segname[16];      /* segment this section will be in */
    uint64_t addr;         /* in-memory address (subject to alignment) */
    uint64_t size;         /* in-memory and -file size  */
    uint64_t offset;	   /* in-file offset */
    uint32_t pad;          /* padding bytes before section */
    uint32_t nreloc;       /* relocation entry count */
    uint32_t flags;        /* type and attributes (masked) */
    uint32_t extreloc;     /* external relocations */
};

#define S_NASM_TYPE_MASK	 0x800004ff	/* we consider these bits "section type" */

/* fake section for absolute symbols, *not* part of the section linked list */
static struct section absolute_sect;

struct reloc {
    /* nasm internal data */
    struct reloc *next;

    /* data that goes into the file */
    int32_t addr;		/* op's offset in section */
    uint32_t snum:24,		/* contains symbol index if
				 ** ext otherwise in-file
				 ** section number */
	pcrel:1,                /* relative relocation */
	length:2,               /* 0=byte, 1=word, 2=int32_t, 3=int64_t */
	ext:1,                  /* external symbol referenced */
	type:4;                 /* reloc type */
};

struct symbol {
    /* nasm internal data */
    struct rbtree symv[2];	/* All/global symbol rbtrees; "key" contains the
				   symbol offset. */
    struct symbol *next;	/* next symbol in the list */
    char *name;			/* name of this symbol */
    int32_t initial_snum;	/* symbol number used above in reloc */
    int32_t snum;		/* true snum for reloc */

    /* data that goes into the file */
    uint32_t strx;              /* string table index */
    uint8_t type;		/* symbol type */
    uint8_t sect;		/* NO_SECT or section number */
    uint16_t desc;		/* for stab debugging, 0 for us */
};

#define DEFAULT_SECTION_ALIGNMENT 0 /* byte (i.e. no) alignment */

static struct section *sects, **sectstail, **sectstab;
static struct symbol *syms, **symstail;
static uint32_t nsyms;

/* These variables are set by macho_layout_symbols() to organize
   the symbol table and string table in order the dynamic linker
   expects.  They are then used in macho_write() to put out the
   symbols and strings in that order.

   The order of the symbol table is:
     local symbols
     defined external symbols (sorted by name)
     undefined external symbols (sorted by name)

   The order of the string table is:
     strings for external symbols
     strings for local symbols
 */
static uint32_t ilocalsym = 0;
static uint32_t iextdefsym = 0;
static uint32_t iundefsym = 0;
static uint32_t nlocalsym;
static uint32_t nextdefsym;
static uint32_t nundefsym;
static struct symbol **extdefsyms = NULL;
static struct symbol **undefsyms = NULL;

static struct RAA *extsyms;
static struct SAA *strs;
static uint32_t strslen;

/* Global file information. This should be cleaned up into either
   a structure or as function arguments.  */
static uint32_t head_ncmds = 0;
static uint32_t head_sizeofcmds = 0;
static uint32_t head_flags = 0;
static uint64_t seg_filesize = 0;
static uint64_t seg_vmsize = 0;
static uint32_t seg_nsects = 0;
static uint64_t rel_padcnt = 0;

/*
 * Functions for handling fixed-length zero-padded string
 * fields, that may or may not be null-terminated.
 */

/* Copy a string into a zero-padded fixed-length field */
#define xstrncpy(xdst, xsrc) strncpy(xdst, xsrc, sizeof(xdst))

/* Compare a fixed-length field with a string */
#define xstrncmp(xdst, xsrc) strncmp(xdst, xsrc, sizeof(xdst))

#define alignint32_t(x)							\
    ALIGN(x, sizeof(int32_t))	/* align x to int32_t boundary */

#define alignint64_t(x)							\
    ALIGN(x, sizeof(int64_t))	/* align x to int64_t boundary */

#define alignptr(x) \
    ALIGN(x, fmt.ptrsize)	/* align x to output format width */

static struct hash_table section_by_name;
static struct RAA *section_by_index;

static struct section * never_null
find_or_add_section(const char *segname, const char *sectname)
{
    struct hash_insert hi;
    void **sp;
    struct section *s;
    char sect[34];

    snprintf(sect, sizeof sect, "%-16s,%-16s", segname, sectname);

    sp = hash_find(&section_by_name, sect, &hi);
    if (sp)
	return (struct section *)(*sp);

    s = nasm_zalloc(sizeof *s);
    xstrncpy(s->segname, segname);
    xstrncpy(s->sectname, sectname);
    xstrncpy(s->namestr, sect);
    hash_add(&hi, s->namestr, s);

    s->index = s->subsection = seg_alloc();
    section_by_index = raa_write_ptr(section_by_index, s->index >> 1, s);

    return s;
}

static inline bool is_new_section(const struct section *s)
{
    return !s->data;
}

static struct section *get_section_by_name(const char *segname,
                                           const char *sectname)
{
    char sect[34];
    void **sp;

    snprintf(sect, sizeof sect, "%-16s,%-16s", segname, sectname);

    sp = hash_find(&section_by_name, sect, NULL);
    return sp ? (struct section *)(*sp) : NULL;
}

static struct section *get_section_by_index(int32_t index)
{
    if (index < 0 || index >= SEG_ABS || (index & 1))
	return NULL;

    return raa_read_ptr(section_by_index, index >> 1);
}

struct dir_list {
    struct dir_list *next;
    struct dir_list *last;
    const char *dir_name;
    uint32_t dir;
};

struct file_list {
    struct file_list *next;
    struct file_list *last;
    const char *file_name;
    uint32_t file;
    struct dir_list *dir;
};

struct dw_sect_list {
    struct SAA *psaa;
    int32_t section;
    uint32_t line;
    uint64_t offset;
    uint32_t file;
    struct dw_sect_list *next;
    struct dw_sect_list *last;
};

struct section_info {
    uint64_t size;
    int32_t secto;
};

#define DW_LN_BASE (-5)
#define DW_LN_RANGE 14
#define DW_OPCODE_BASE 13
#define DW_MAX_LN (DW_LN_BASE + DW_LN_RANGE)
#define DW_MAX_SP_OPCODE 256

static struct file_list *dw_head_file = 0, *dw_cur_file = 0, **dw_last_file_next = NULL;
static struct dir_list *dw_head_dir = 0, **dw_last_dir_next = NULL;
static struct dw_sect_list  *dw_head_sect = 0, *dw_cur_sect = 0, *dw_last_sect = 0;
static uint32_t  cur_line = 0, dw_num_files = 0, dw_num_dirs = 0, dw_num_sects = 0;
static bool  dbg_immcall = false;
static const char *module_name = NULL;

/*
 * Special section numbers which are used to define Mach-O special
 * symbols, which can be used with WRT to provide PIC relocation
 * types.
 */
static int32_t macho_tlvp_sect;
static int32_t macho_gotpcrel_sect;

static void macho_init(void)
{
    module_name = inname;
    sects = NULL;
    sectstail = &sects;

    /* Fake section for absolute symbols */
    absolute_sect.index = NO_SEG;

    syms = NULL;
    symstail = &syms;
    nsyms = 0;
    nlocalsym = 0;
    nextdefsym = 0;
    nundefsym = 0;

    extsyms = raa_init();
    strs = saa_init(1L);

    section_by_index = raa_init();
    hash_init(&section_by_name, HASH_MEDIUM);

    /* string table starts with a zero byte so index 0 is an empty string */
    saa_wbytes(strs, zero_buffer, 1);
    strslen = 1;

    /* add special symbol for TLVP */
    macho_tlvp_sect = seg_alloc() + 1;
    backend_label("..tlvp", macho_tlvp_sect, 0L);
}

static void sect_write(struct section *sect,
                       const uint8_t *data, uint32_t len)
{
    saa_wbytes(sect->data, data, len);
    sect->size += len;
}

/*
 * Find a suitable global symbol for a ..gotpcrel or ..tlvp reference
 */
static struct symbol *macho_find_sym(struct section *s, uint64_t offset,
				     bool global, bool exact)
{
    struct rbtree *srb;

    srb = rb_search(s->syms[global], offset);

    if (!srb || (exact && srb->key != offset)) {
        nasm_error(ERR_NONFATAL, "unable to find a suitable%s%s symbol"
		   " for this reference",
		   global ? " global" : "",
		   s == &absolute_sect ? " absolute " : "");
        return NULL;
    }

    return container_of(srb - global, struct symbol, symv);
}

static int64_t add_reloc(struct section *sect, int32_t section,
			 int64_t offset,
			 enum reltype reltype, int bytes)
{
    struct reloc *r;
    struct section *s;
    int32_t fi;
    int64_t adjust;

    /* Double check this is a valid relocation type for this platform */
    nasm_assert(reltype <= fmt.maxreltype);

    /* the current end of the section will be the symbol's address for
     ** now, might have to be fixed by macho_fixup_relocs() later on. make
     ** sure we don't make the symbol scattered by setting the highest
     ** bit by accident */
    r = nasm_malloc(sizeof(struct reloc));
    r->addr = sect->size & ~R_SCATTERED;
    r->ext = 1;
    adjust = 0;

    /* match byte count 1, 2, 4, 8 to length codes 0, 1, 2, 3 respectively */
    r->length = ilog2_32(bytes);

    /* set default relocation values */
    r->type = fmt.reloc_abs;
    r->pcrel = 0;
    r->snum = R_ABS;

    s = get_section_by_index(section);
    fi = s ? s->fileindex : NO_SECT;

    /* absolute relocation */
    switch (reltype) {
    case RL_ABS:
	if (section == NO_SEG) {
	    /* absolute (can this even happen?) */
	    r->ext = 0;
	} else if (fi == NO_SECT) {
	    /* external */
	    r->snum = raa_read(extsyms, section);
	} else {
	    /* local */
	    r->ext = 0;
	    r->snum = fi;
	}
	break;

    case RL_REL:
    case RL_BRANCH:
	r->type = fmt.reloc_rel;
	r->pcrel = 1;
	if (section == NO_SEG) {
	    /* may optionally be converted below by fmt.forcesym */
	    r->ext = 0;
	} else if (fi == NO_SECT) {
	    /* external */
	    sect->extreloc = 1;
	    r->snum = raa_read(extsyms, section);
	    if (reltype == RL_BRANCH)
		r->type = X86_64_RELOC_BRANCH;
	} else {
	    /* local */
	    r->ext = 0;
	    r->snum = fi;
	    if (reltype == RL_BRANCH)
		r->type = X86_64_RELOC_BRANCH;
	}
	break;

    case RL_SUB: /* obsolete */
	nasm_error(ERR_WARNING, "relcation with subtraction"
		   "becomes to be obsolete");
	r->ext = 0;
	r->type = X86_64_RELOC_SUBTRACTOR;
	break;

    case RL_GOT:
	r->type = X86_64_RELOC_GOT;
	goto needsym;

    case RL_GOTLOAD:
	r->type = X86_64_RELOC_GOT_LOAD;
	goto needsym;

    case RL_TLV:
	r->type = fmt.reloc_tlv;
	goto needsym;

    needsym:
	r->pcrel = (fmt.ptrsize == 8 ? 1 : 0);
	if (section == NO_SEG) {
	    nasm_error(ERR_NONFATAL, "Unsupported use of use of WRT");
	    goto bail;
	} else if (fi == NO_SECT) {
	    /* external */
	    r->snum = raa_read(extsyms, section);
	} else {
	    /* internal - GOTPCREL doesn't need to be in global */
	    struct symbol *sym = macho_find_sym(s, offset,
						false, /* reltype != RL_TLV */
						true);
	    if (!sym) {
		nasm_error(ERR_NONFATAL, "Symbol for WRT not found");
		goto bail;
	    }

	    adjust -= sym->symv[0].key;
	    r->snum = sym->initial_snum;
	}
	break;
    }

    /*
     * For 64-bit Mach-O, force a symbol reference if at all possible
     * Allow for r->snum == R_ABS by searching absolute_sect
     */
    if (!r->ext && fmt.forcesym) {
	struct symbol *sym = macho_find_sym(s ? s : &absolute_sect,
					    offset, false, false);
	if (sym) {
	    adjust -= sym->symv[0].key;
	    r->snum = sym->initial_snum;
	    r->ext = 1;
	}
    }

    if (r->pcrel)
	adjust += ((r->ext && fmt.ptrsize == 8) ? bytes : -(int64_t)sect->size);

    /* NeXT as puts relocs in reversed order (address-wise) into the
     ** files, so we do the same, doesn't seem to make much of a
     ** difference either way */
    r->next = sect->relocs;
    sect->relocs = r;
    if (r->ext)
	sect->extreloc = 1;
    ++sect->nreloc;

    return adjust;

 bail:
    nasm_free(r);
    return 0;
}

static void macho_output(int32_t secto, const void *data,
			 enum out_type type, uint64_t size,
                         int32_t section, int32_t wrt)
{
    struct section *s;
    int64_t addr, offset;
    uint8_t mydata[16], *p;
    bool is_bss;
    enum reltype reltype;

    s = get_section_by_index(secto);
    if (!s) {
        nasm_error(ERR_WARNING, "attempt to assemble code in"
              " section %d: defaulting to `.text'", secto);
        s = get_section_by_name("__TEXT", "__text");

        /* should never happen */
        if (!s)
            nasm_panic("text section not found");
    }

    /* debug code generation only for sections tagged with
     * instruction attribute */
    if (s->flags & S_ATTR_SOME_INSTRUCTIONS)
    {
        struct section_info sinfo;
        sinfo.size = s->size;
        sinfo.secto = secto;
        dfmt->debug_output(0, &sinfo);
    }

    is_bss = (s->flags & SECTION_TYPE) == S_ZEROFILL;

    if (is_bss && type != OUT_RESERVE) {
        nasm_error(ERR_WARNING, "attempt to initialize memory in "
              "BSS section: ignored");
        /* FIXME */
        nasm_error(ERR_WARNING, "section size may be negative"
            "with address symbols");
        s->size += realsize(type, size);
        return;
    }

    memset(mydata, 0, sizeof(mydata));

    switch (type) {
    case OUT_RESERVE:
        if (!is_bss) {
            nasm_error(ERR_WARNING, "uninitialized space declared in"
		       " %s,%s section: zeroing", s->segname, s->sectname);

            sect_write(s, NULL, size);
        } else
            s->size += size;

        break;

    case OUT_RAWDATA:
        sect_write(s, data, size);
        break;

    case OUT_ADDRESS:
    {
	int asize = abs((int)size);

        addr = *(int64_t *)data;
        if (section != NO_SEG) {
            if (section % 2) {
                nasm_error(ERR_NONFATAL, "Mach-O format does not support"
			   " section base references");
            } else if (wrt == NO_SEG) {
		if (fmt.ptrsize == 8 && asize != 8) {
		    nasm_error(ERR_NONFATAL,
			       "Mach-O 64-bit format does not support"
			       " 32-bit absolute addresses");
		} else {
		    addr += add_reloc(s, section, addr, RL_ABS, asize);
		}
	    } else if (wrt == macho_tlvp_sect && fmt.ptrsize != 8 &&
		       asize == (int) fmt.ptrsize) {
		addr += add_reloc(s, section, addr, RL_TLV, asize);
	    } else {
		nasm_error(ERR_NONFATAL, "Mach-O format does not support"
			   " this use of WRT");
	    }
	}

        p = mydata;
	WRITEADDR(p, addr, asize);
        sect_write(s, mydata, asize);
        break;
    }

    case OUT_REL1ADR:
    case OUT_REL2ADR:

        p = mydata;
	offset = *(int64_t *)data;
        addr = offset - size;

        if (section != NO_SEG && section % 2) {
            nasm_error(ERR_NONFATAL, "Mach-O format does not support"
		       " section base references");
	} else if (fmt.ptrsize == 8) {
	    nasm_error(ERR_NONFATAL, "Unsupported non-32-bit"
		       " Macho-O relocation [2]");
	} else if (wrt != NO_SEG) {
	    nasm_error(ERR_NONFATAL, "Mach-O format does not support"
		       " this use of WRT");
	    wrt = NO_SEG;	/* we can at least _try_ to continue */
	} else {
	    addr += add_reloc(s, section, addr+size, RL_REL,
                              type == OUT_REL1ADR ? 1 : 2);
	}

        WRITESHORT(p, addr);
        sect_write(s, mydata, type == OUT_REL1ADR ? 1 : 2);
        break;

    case OUT_REL4ADR:
    case OUT_REL8ADR:

        p = mydata;
	offset = *(int64_t *)data;
        addr = offset - size;
	reltype = RL_REL;

        if (section != NO_SEG && section % 2) {
            nasm_error(ERR_NONFATAL, "Mach-O format does not support"
		       " section base references");
        } else if (wrt == NO_SEG) {
	    if (fmt.ptrsize == 8 &&
		(s->flags & S_ATTR_SOME_INSTRUCTIONS)) {
		uint8_t opcode[2];

		opcode[0] = opcode[1] = 0;

		/* HACK: Retrieve instruction opcode */
		if (likely(s->data->datalen >= 2)) {
		    saa_fread(s->data, s->data->datalen-2, opcode, 2);
		} else if (s->data->datalen == 1) {
		    saa_fread(s->data, 0, opcode+1, 1);
		}

		if ((opcode[0] != 0x0f && (opcode[1] & 0xfe) == 0xe8) ||
		    (opcode[0] == 0x0f && (opcode[1] & 0xf0) == 0x80)) {
		    /* Direct call, jmp, or jcc */
		    reltype = RL_BRANCH;
		}
	    }
	} else if (wrt == macho_gotpcrel_sect) {
	    reltype = RL_GOT;

	    if ((s->flags & S_ATTR_SOME_INSTRUCTIONS) &&
		s->data->datalen >= 3) {
		uint8_t gotload[3];

		/* HACK: Retrieve instruction opcode */
		saa_fread(s->data, s->data->datalen-3, gotload, 3);
		if ((gotload[0] & 0xf8) == 0x48 &&
		    gotload[1] == 0x8b &&
		    (gotload[2] & 0307) == 0005) {
		    /* movq <reg>,[rel sym wrt ..gotpcrel] */
		    reltype = RL_GOTLOAD;
		}
	    }
	} else if (wrt == macho_tlvp_sect && fmt.ptrsize == 8) {
	    reltype = RL_TLV;
	} else {
	    nasm_error(ERR_NONFATAL, "Mach-O format does not support"
		       " this use of WRT");
	    /* continue with RL_REL */
	}

	addr += add_reloc(s, section, offset, reltype,
                          type == OUT_REL4ADR ? 4 : 8);
        WRITELONG(p, addr);
        sect_write(s, mydata, type == OUT_REL4ADR ? 4 : 8);
        break;

    default:
        nasm_error(ERR_NONFATAL, "Unrepresentable relocation in Mach-O");
        break;
    }
}

#define S_CODE  (S_REGULAR | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS)
#define NO_TYPE S_NASM_TYPE_MASK

/* Translation table from traditional Unix section names to Mach-O */
static const struct macho_known_section {
    const char      *nasmsect;
    const char      *segname;
    const char      *sectname;
    const uint32_t  flags;
} known_sections[] = {
    { ".text",          "__TEXT",   "__text",           S_CODE          },
    { ".data",          "__DATA",   "__data",           S_REGULAR       },
    { ".rodata",        "__DATA",   "__const",          S_REGULAR       },
    { ".bss",           "__DATA",   "__bss",            S_ZEROFILL      },
    { ".debug_abbrev",  "__DWARF",  "__debug_abbrev",   S_ATTR_DEBUG    },
    { ".debug_info",    "__DWARF",  "__debug_info",     S_ATTR_DEBUG    },
    { ".debug_line",    "__DWARF",  "__debug_line",     S_ATTR_DEBUG    },
    { ".debug_str",     "__DWARF",  "__debug_str",      S_ATTR_DEBUG    },
};

/* Section type or attribute directives */
static const struct macho_known_section_attr {
    const char      *name;
    uint32_t        flags;
} sect_attribs[] = {
    { "data",               S_REGULAR                               },
    { "code",               S_CODE                                  },
    { "mixed",              S_REGULAR | S_ATTR_SOME_INSTRUCTIONS    },
    { "bss",                S_ZEROFILL                              },
    { "zerofill",           S_ZEROFILL                              },
    { "no_dead_strip",      NO_TYPE | S_ATTR_NO_DEAD_STRIP          },
    { "live_support",       NO_TYPE | S_ATTR_LIVE_SUPPORT           },
    { "strip_static_syms",  NO_TYPE | S_ATTR_STRIP_STATIC_SYMS      },
    { "debug",              NO_TYPE | S_ATTR_DEBUG                  },
    { NULL, 0 }
};

static const struct macho_known_section *
lookup_known_section(const char *name, bool by_sectname)
{
    size_t i;

    if (name && name[0]) {
            for (i = 0; i < ARRAY_SIZE(known_sections); i++) {
                const char *p = by_sectname ?
                    known_sections[i].sectname :
                    known_sections[i].nasmsect;
                if (!strcmp(name, p))
                    return &known_sections[i];
            }
    }

    return NULL;
}

static int32_t macho_section(char *name, int pass, int *bits)
{
    const struct macho_known_section *known_section;
    const struct macho_known_section_attr *sa;
    char *sectionAttributes;
    struct section *s;
    const char *section, *segment;
    uint32_t flags;
    char *currentAttribute;
    char *comma;

    bool new_seg;

    (void)pass;

    /* Default to the appropriate number of bits. */
    if (!name) {
        *bits = fmt.ptrsize << 3;
        name = ".text";
        sectionAttributes = NULL;
    } else {
        sectionAttributes = name;
        name = nasm_strsep(&sectionAttributes, " \t");
    }

    section = segment = NULL;
    flags = 0;

    comma = strchr(name, ',');
    if (comma) {
	int len;

	*comma = '\0';
	segment = name;
	section = comma+1;

	len = strlen(segment);
	if (len == 0) {
	    nasm_error(ERR_NONFATAL, "empty segment name\n");
	} else if (len > 16) {
	    nasm_error(ERR_NONFATAL, "segment name %s too long\n", segment);
	}

	len = strlen(section);
	if (len == 0) {
	    nasm_error(ERR_NONFATAL, "empty section name\n");
	} else if (len > 16) {
	    nasm_error(ERR_NONFATAL, "section name %s too long\n", section);
	}

        known_section = lookup_known_section(section, true);
        if (known_section)
            flags = known_section->flags;
        else
            flags = S_REGULAR;
    } else {
        known_section = lookup_known_section(name, false);
        if (!known_section) {
            nasm_error(ERR_NONFATAL, "unknown section name %s\n", name);
            return NO_SEG;
        }

        segment = known_section->segname;
        section = known_section->sectname;
        flags = known_section->flags;
    }

    /* try to find section with that name, or create it */
    s = find_or_add_section(segment, section);
    new_seg = is_new_section(s);

    /* initialize it if it is a brand new section */
    if (new_seg) {
	*sectstail = s;
	sectstail = &s->next;

	s->data = saa_init(1L);
	s->fileindex = ++seg_nsects;
	s->align = -1;
	s->pad = -1;
	s->offset = -1;
	s->by_name = false;

	s->size = 0;
	s->nreloc = 0;
	s->flags = flags;
    }

    if (comma)
	*comma = ',';		/* Restore comma */

    s->by_name = s->by_name || comma; /* Was specified by name */

    flags = NO_TYPE;

    while (sectionAttributes &&
	   (currentAttribute = nasm_strsep(&sectionAttributes, " \t"))) {
	if (!*currentAttribute)
	    continue;

	if (!nasm_strnicmp("align=", currentAttribute, 6)) {
	    char *end;
	    int newAlignment, value;

	    value = strtoul(currentAttribute + 6, (char**)&end, 0);
	    newAlignment = alignlog2_32(value);

	    if (0 != *end) {
		nasm_error(ERR_NONFATAL,
			   "unknown or missing alignment value \"%s\" "
			   "specified for section \"%s\"",
			   currentAttribute + 6,
			   name);
	    } else if (0 > newAlignment) {
		nasm_error(ERR_NONFATAL,
			   "alignment of %d (for section \"%s\") is not "
			   "a power of two",
			   value,
			   name);
	    }

	    if (s->align < newAlignment)
		s->align = newAlignment;
	} else {
	    for (sa = sect_attribs; sa->name; sa++) {
		if (!nasm_stricmp(sa->name, currentAttribute)) {
		    if ((sa->flags & S_NASM_TYPE_MASK) != NO_TYPE) {
			flags = (flags & ~S_NASM_TYPE_MASK)
			    | (sa->flags & S_NASM_TYPE_MASK);
		    }
		    flags |= sa->flags & ~S_NASM_TYPE_MASK;
		    break;
		}
	    }

	    if (!sa->name) {
		nasm_error(ERR_NONFATAL,
			   "unknown section attribute %s for section %s",
			   currentAttribute, name);
	    }
	}
    }

    if ((flags & S_NASM_TYPE_MASK) != NO_TYPE) {
	if (!new_seg && ((s->flags ^ flags) & S_NASM_TYPE_MASK)) {
	    nasm_error(ERR_NONFATAL,
		       "inconsistent section attributes for section %s\n",
		       name);
	} else {
	    s->flags = (s->flags & ~S_NASM_TYPE_MASK) | flags;
	}
    } else {
	s->flags |= flags & ~S_NASM_TYPE_MASK;
    }

    return s->subsection;
}

static int32_t macho_herelabel(const char *name, enum label_type type,
			       int32_t section, int32_t *subsection,
			       bool *copyoffset)
{
    struct section *s;
    int32_t subsec;
    (void)name;

    if (!(head_flags & MH_SUBSECTIONS_VIA_SYMBOLS))
	return section;

    /* No subsection only for local labels */
    if (type == LBL_LOCAL)
	return section;

    s = get_section_by_index(section);
    if (!s)
	return section;

    subsec = *subsection;
    if (subsec == NO_SEG) {
	/* Allocate a new subsection index */
	subsec = *subsection = seg_alloc();
	section_by_index = raa_write_ptr(section_by_index, subsec >> 1, s);
    }

    s->subsection = subsec;
    *copyoffset = true;		/* Maintain previous offset */
    return subsec;
}

static void macho_symdef(char *name, int32_t section, int64_t offset,
                         int is_global, char *special)
{
    struct symbol *sym;
    struct section *s;
    bool special_used = false;

#if defined(DEBUG) && DEBUG>2
    nasm_error(ERR_DEBUG,
            " macho_symdef: %s, pass0=%d, passn=%"PRId64", sec=%"PRIx32", off=%"PRIx64", is_global=%d, %s\n",
	       name, pass0, passn, section, offset, is_global, special);
#endif

    if (is_global == 3) {
        if (special) {
            int n = strcspn(special, " \t");

            if (!nasm_strnicmp(special, "private_extern", n)) {
                for (sym = syms; sym != NULL; sym = sym->next) {
                    if (!strcmp(name, sym->name)) {
                        if (sym->type & N_PEXT)
                            return; /* nothing to be done */
                        else
                            break;
                    }
                }
            }
        }
        nasm_error(ERR_NONFATAL, "The Mach-O format does not "
              "(yet) support forward reference fixups.");
        return;
    }

    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
	/*
	 * This is a NASM special symbol. We never allow it into
	 * the Macho-O symbol table, even if it's a valid one. If it
	 * _isn't_ a valid one, we should barf immediately.
	 */
	if (strcmp(name, "..gotpcrel") && strcmp(name, "..tlvp"))
            nasm_error(ERR_NONFATAL, "unrecognized special symbol `%s'", name);
	return;
    }

    sym = *symstail = nasm_zalloc(sizeof(struct symbol));
    sym->next = NULL;
    symstail = &sym->next;

    sym->name = name;
    sym->strx = strslen;
    sym->type = 0;
    sym->desc = 0;
    sym->symv[0].key = offset;
    sym->symv[1].key = offset;
    sym->initial_snum = -1;

    /* external and common symbols get N_EXT */
    if (is_global != 0) {
        sym->type |= N_EXT;
    }
    if (is_global == 1) {
        /* check special to see if the global symbol shall be marked as private external: N_PEXT */
        if (special) {
            int n = strcspn(special, " \t");

            if (!nasm_strnicmp(special, "private_extern", n))
                sym->type |= N_PEXT;
            else
                nasm_error(ERR_NONFATAL, "unrecognised symbol type `%.*s'", n, special);
        }
        special_used = true;
    }

    /* track the initially allocated symbol number for use in future fix-ups */
    sym->initial_snum = nsyms;

    if (section == NO_SEG) {
        /* symbols in no section get absolute */
        sym->type |= N_ABS;
        sym->sect = NO_SECT;

	s = &absolute_sect;
    } else {
	s = get_section_by_index(section);

        sym->type |= N_SECT;

        /* get the in-file index of the section the symbol was defined in */
        sym->sect = s ? s->fileindex : NO_SECT;

        if (!s) {
            /* remember symbol number of references to external
             ** symbols, this works because every external symbol gets
             ** its own section number allocated internally by nasm and
             ** can so be used as a key */
	    extsyms = raa_write(extsyms, section, nsyms);

            switch (is_global) {
            case 1:
            case 2:
                /* there isn't actually a difference between global
                 ** and common symbols, both even have their size in
                 ** sym->symv[0].key */
                sym->type = N_EXT;
                break;

            default:
                /* give an error on unfound section if it's not an
                 ** external or common symbol (assemble_file() does a
                 ** seg_alloc() on every call for them) */
                nasm_panic("in-file index for section %d not found, is_global = %d", section, is_global);
		break;
            }
	}
    }

    if (s) {
	s->syms[0] = rb_insert(s->syms[0], &sym->symv[0]);
	if (is_global)
	    s->syms[1] = rb_insert(s->syms[1], &sym->symv[1]);
    }

    ++nsyms;

    if (special && !special_used)
        nasm_error(ERR_NONFATAL, "no special symbol features supported here");
}

static void macho_sectalign(int32_t seg, unsigned int value)
{
    struct section *s;
    int align;

    nasm_assert(!(seg & 1));

    s = get_section_by_index(seg);

    if (!s || !is_power2(value))
        return;

    align = alignlog2_32(value);
    if (s->align < align)
        s->align = align;
}

extern macros_t macho_stdmac[];

/* Comparison function for qsort symbol layout.  */
static int layout_compare (const struct symbol **s1,
			   const struct symbol **s2)
{
    return (strcmp ((*s1)->name, (*s2)->name));
}

/* The native assembler does a few things in a similar function

	* Remove temporary labels
	* Sort symbols according to local, external, undefined (by name)
	* Order the string table

   We do not remove temporary labels right now.

   numsyms is the total number of symbols we have. strtabsize is the
   number entries in the string table.  */

static void macho_layout_symbols (uint32_t *numsyms,
				  uint32_t *strtabsize)
{
    struct symbol *sym, **symp;
    uint32_t i,j;

    *numsyms = 0;
    *strtabsize = sizeof (char);

    symp = &syms;

    while ((sym = *symp)) {
	/* Undefined symbols are now external.  */
	if (sym->type == N_UNDF)
	    sym->type |= N_EXT;

	if ((sym->type & N_EXT) == 0) {
	    sym->snum = *numsyms;
	    *numsyms = *numsyms + 1;
	    nlocalsym++;
	}
	else {
	    if ((sym->type & N_TYPE) != N_UNDF) {
		nextdefsym++;
	    } else {
		nundefsym++;
	    }

	    /* If we handle debug info we'll want
	       to check for it here instead of just
	       adding the symbol to the string table.  */
	    sym->strx = *strtabsize;
	    saa_wbytes (strs, sym->name, (int32_t)(strlen(sym->name) + 1));
	    *strtabsize += strlen(sym->name) + 1;
	}
	symp = &(sym->next);
    }

    /* Next, sort the symbols.  Most of this code is a direct translation from
       the Apple cctools symbol layout. We need to keep compatibility with that.  */
    /* Set the indexes for symbol groups into the symbol table */
    ilocalsym = 0;
    iextdefsym = nlocalsym;
    iundefsym = nlocalsym + nextdefsym;

    /* allocate arrays for sorting externals by name */
    extdefsyms = nasm_malloc(nextdefsym * sizeof(struct symbol *));
    undefsyms = nasm_malloc(nundefsym * sizeof(struct symbol *));

    i = 0;
    j = 0;

    symp = &syms;

    while ((sym = *symp)) {

	if((sym->type & N_EXT) == 0) {
	    sym->strx = *strtabsize;
	    saa_wbytes (strs, sym->name, (int32_t)(strlen (sym->name) + 1));
	    *strtabsize += strlen(sym->name) + 1;
	}
	else {
	    if ((sym->type & N_TYPE) != N_UNDF) {
		extdefsyms[i++] = sym;
	    } else {
		undefsyms[j++] = sym;
	    }
	}
	symp = &(sym->next);
    }

    qsort(extdefsyms, nextdefsym, sizeof(struct symbol *),
	  (int (*)(const void *, const void *))layout_compare);
    qsort(undefsyms, nundefsym, sizeof(struct symbol *),
	  (int (*)(const void *, const void *))layout_compare);

    for(i = 0; i < nextdefsym; i++) {
	extdefsyms[i]->snum = *numsyms;
	*numsyms += 1;
    }
    for(j = 0; j < nundefsym; j++) {
	undefsyms[j]->snum = *numsyms;
	*numsyms += 1;
    }
}

/* Calculate some values we'll need for writing later.  */

static void macho_calculate_sizes (void)
{
    struct section *s;
    int fi;

    /* count sections and calculate in-memory and in-file offsets */
    for (s = sects; s != NULL; s = s->next) {
        uint64_t newaddr;

        /* recalculate segment address based on alignment and vm size */
        s->addr = seg_vmsize;

        /* we need section alignment to calculate final section address */
        if (s->align == -1)
            s->align = DEFAULT_SECTION_ALIGNMENT;

        newaddr = ALIGN(s->addr, UINT64_C(1) << s->align);
        s->addr = newaddr;

        seg_vmsize = newaddr + s->size;

        /* zerofill sections aren't actually written to the file */
        if ((s->flags & SECTION_TYPE) != S_ZEROFILL) {
	    /*
	     * LLVM/Xcode as always aligns the section data to 4
	     * bytes; there is a comment in the LLVM source code that
	     * perhaps aligning to pointer size would be better.
	     */
	    s->pad = ALIGN(seg_filesize, 4) - seg_filesize;
	    s->offset = seg_filesize + s->pad;
            seg_filesize += s->size + s->pad;

            /* filesize and vmsize needs to be aligned */
            seg_vmsize += s->pad;
	}
    }

    /* calculate size of all headers, load commands and sections to
    ** get a pointer to the start of all the raw data */
    if (seg_nsects > 0) {
        ++head_ncmds;
        head_sizeofcmds += fmt.segcmd_size  + seg_nsects * fmt.sectcmd_size;
    }

    if (nsyms > 0) {
	++head_ncmds;
	head_sizeofcmds += MACHO_SYMCMD_SIZE;
    }

    if (seg_nsects > MAX_SECT) {
	nasm_fatal("MachO output is limited to %d sections\n",
		   MAX_SECT);
    }

    /* Create a table of sections by file index to avoid linear search */
    sectstab = nasm_malloc((seg_nsects + 1) * sizeof(*sectstab));
    sectstab[NO_SECT] = &absolute_sect;
    for (s = sects, fi = 1; s != NULL; s = s->next, fi++)
	sectstab[fi] = s;
}

/* Write out the header information for the file.  */

static void macho_write_header (void)
{
    fwriteint32_t(fmt.mh_magic, ofile);	/* magic */
    fwriteint32_t(fmt.cpu_type, ofile);	/* CPU type */
    fwriteint32_t(CPU_SUBTYPE_I386_ALL, ofile);	/* CPU subtype */
    fwriteint32_t(MH_OBJECT, ofile);	/* Mach-O file type */
    fwriteint32_t(head_ncmds, ofile);	/* number of load commands */
    fwriteint32_t(head_sizeofcmds, ofile);	/* size of load commands */
    fwriteint32_t(head_flags, ofile);		/* flags, if any */
    fwritezero(fmt.header_size - 7*4, ofile);	/* reserved fields */
}

/* Write out the segment load command at offset.  */

static uint32_t macho_write_segment (uint64_t offset)
{
    uint64_t rel_base = alignptr(offset + seg_filesize);
    uint32_t s_reloff = 0;
    struct section *s;

    fwriteint32_t(fmt.lc_segment, ofile);        /* cmd == LC_SEGMENT_64 */

    /* size of load command including section load commands */
    fwriteint32_t(fmt.segcmd_size + seg_nsects * fmt.sectcmd_size,
		  ofile);

    /* in an MH_OBJECT file all sections are in one unnamed (name
    ** all zeros) segment */
    fwritezero(16, ofile);
    fwriteptr(0, ofile);		     /* in-memory offset */
    fwriteptr(seg_vmsize, ofile);	     /* in-memory size */
    fwriteptr(offset, ofile);	             /* in-file offset to data */
    fwriteptr(seg_filesize, ofile);	     /* in-file size */
    fwriteint32_t(VM_PROT_DEFAULT, ofile);   /* maximum vm protection */
    fwriteint32_t(VM_PROT_DEFAULT, ofile);   /* initial vm protection */
    fwriteint32_t(seg_nsects, ofile);        /* number of sections */
    fwriteint32_t(0, ofile);		     /* no flags */

    /* emit section headers */
    for (s = sects; s != NULL; s = s->next) {
	if (s->nreloc) {
	    nasm_assert((s->flags & SECTION_TYPE) != S_ZEROFILL);
	    s->flags |= S_ATTR_LOC_RELOC;
	    if (s->extreloc)
		s->flags |= S_ATTR_EXT_RELOC;
	} else if (!xstrncmp(s->segname, "__DATA") &&
		   !xstrncmp(s->sectname, "__const") &&
		   !s->by_name &&
		   !get_section_by_name("__TEXT", "__const")) {
	    /*
	     * The MachO equivalent to .rodata can be either
	     * __DATA,__const or __TEXT,__const; the latter only if
	     * there are no relocations.  However, when mixed it is
	     * better to specify the segments explicitly.
	     */
	    xstrncpy(s->segname, "__TEXT");
	}

        nasm_write(s->sectname, sizeof(s->sectname), ofile);
        nasm_write(s->segname, sizeof(s->segname), ofile);
        fwriteptr(s->addr, ofile);
        fwriteptr(s->size, ofile);

        /* dummy data for zerofill sections or proper values */
        if ((s->flags & SECTION_TYPE) != S_ZEROFILL) {
	    nasm_assert(s->pad != (uint32_t)-1);
	    offset += s->pad;
            fwriteint32_t(offset, ofile);
	    offset += s->size;
            /* Write out section alignment, as a power of two.
            e.g. 32-bit word alignment would be 2 (2^2 = 4).  */
            fwriteint32_t(s->align, ofile);
            /* To be compatible with cctools as we emit
            a zero reloff if we have no relocations.  */
            fwriteint32_t(s->nreloc ? rel_base + s_reloff : 0, ofile);
            fwriteint32_t(s->nreloc, ofile);

            s_reloff += s->nreloc * MACHO_RELINFO_SIZE;
        } else {
            fwriteint32_t(0, ofile);
            fwriteint32_t(s->align, ofile);
            fwriteint32_t(0, ofile);
            fwriteint32_t(0, ofile);
        }

        fwriteint32_t(s->flags, ofile);      /* flags */
        fwriteint32_t(0, ofile);	     /* reserved */
        fwriteptr(0, ofile);		     /* reserved */
    }

    rel_padcnt = rel_base - offset;
    offset = rel_base + s_reloff;

    return offset;
}

/* For a given chain of relocs r, write out the entire relocation
   chain to the object file.  */

static void macho_write_relocs (struct reloc *r)
{
    while (r) {
	uint32_t word2;

	fwriteint32_t(r->addr, ofile); /* reloc offset */

	word2 = r->snum;
	word2 |= r->pcrel << 24;
	word2 |= r->length << 25;
	word2 |= r->ext << 27;
	word2 |= r->type << 28;
	fwriteint32_t(word2, ofile); /* reloc data */
	r = r->next;
    }
}

/* Write out the section data.  */
static void macho_write_section (void)
{
    struct section *s;
    struct reloc *r;
    uint8_t *p;
    int32_t len;
    int64_t l;
    union offset {
	uint64_t val;
	uint8_t buf[8];
    } blk;

    for (s = sects; s != NULL; s = s->next) {
	if ((s->flags & SECTION_TYPE) == S_ZEROFILL)
	    continue;

	/* Like a.out Mach-O references things in the data or bss
	 * sections by addresses which are actually relative to the
	 * start of the _text_ section, in the _file_. See outaout.c
	 * for more information. */
	saa_rewind(s->data);
	for (r = s->relocs; r != NULL; r = r->next) {
	    len = (uint32_t)1 << r->length;
	    if (len > 4)	/* Can this ever be an issue?! */
		len = 8;
	    blk.val = 0;
	    saa_fread(s->data, r->addr, blk.buf, len);

	    /* get offset based on relocation type */
#ifdef WORDS_LITTLEENDIAN
	    l = blk.val;
#else
	    l  = blk.buf[0];
	    l += ((int64_t)blk.buf[1]) << 8;
	    l += ((int64_t)blk.buf[2]) << 16;
	    l += ((int64_t)blk.buf[3]) << 24;
	    l += ((int64_t)blk.buf[4]) << 32;
	    l += ((int64_t)blk.buf[5]) << 40;
	    l += ((int64_t)blk.buf[6]) << 48;
	    l += ((int64_t)blk.buf[7]) << 56;
#endif

	    /* If the relocation is internal add to the current section
	       offset. Otherwise the only value we need is the symbol
	       offset which we already have. The linker takes care
	       of the rest of the address.  */
	    if (!r->ext) {
		/* generate final address by section address and offset */
		nasm_assert(r->snum <= seg_nsects);
		l += sectstab[r->snum]->addr;
		if (r->pcrel)
		    l -= s->addr;
	    } else if (r->pcrel && r->type == GENERIC_RELOC_VANILLA) {
		l -= s->addr;
	    }

	    /* write new offset back */
	    p = blk.buf;
	    WRITEDLONG(p, l);
	    saa_fwrite(s->data, r->addr, blk.buf, len);
	}

	/* dump the section data to file */
	fwritezero(s->pad, ofile);
	saa_fpwrite(s->data, ofile);
    }

    /* pad last section up to reloc entries on pointer boundary */
    fwritezero(rel_padcnt, ofile);

    /* emit relocation entries */
    for (s = sects; s != NULL; s = s->next)
	macho_write_relocs (s->relocs);
}

/* Write out the symbol table. We should already have sorted this
   before now.  */
static void macho_write_symtab (void)
{
    struct symbol *sym;
    uint64_t i;

    /* we don't need to pad here since MACHO_RELINFO_SIZE == 8 */

    for (sym = syms; sym != NULL; sym = sym->next) {
	if ((sym->type & N_EXT) == 0) {
	    fwriteint32_t(sym->strx, ofile);		/* string table entry number */
	    nasm_write(&sym->type, 1, ofile);		/* symbol type */
	    nasm_write(&sym->sect, 1, ofile);		/* section */
	    fwriteint16_t(sym->desc, ofile);		/* description */

	    /* Fix up the symbol value now that we know the final section
	       sizes.  */
	    if (((sym->type & N_TYPE) == N_SECT) && (sym->sect != NO_SECT)) {
		nasm_assert(sym->sect <= seg_nsects);
		sym->symv[0].key += sectstab[sym->sect]->addr;
	    }

	    fwriteptr(sym->symv[0].key, ofile);	/* value (i.e. offset) */
	}
    }

    for (i = 0; i < nextdefsym; i++) {
	sym = extdefsyms[i];
	fwriteint32_t(sym->strx, ofile);
	nasm_write(&sym->type, 1, ofile);	/* symbol type */
	nasm_write(&sym->sect, 1, ofile);	/* section */
	fwriteint16_t(sym->desc, ofile);	/* description */

	/* Fix up the symbol value now that we know the final section
	   sizes.  */
	if (((sym->type & N_TYPE) == N_SECT) && (sym->sect != NO_SECT)) {
	    nasm_assert(sym->sect <= seg_nsects);
	    sym->symv[0].key += sectstab[sym->sect]->addr;
	}

	fwriteptr(sym->symv[0].key, ofile); /* value (i.e. offset) */
    }

     for (i = 0; i < nundefsym; i++) {
	 sym = undefsyms[i];
	 fwriteint32_t(sym->strx, ofile);
	 nasm_write(&sym->type, 1, ofile);	/* symbol type */
	 nasm_write(&sym->sect, 1, ofile);	/* section */
	 fwriteint16_t(sym->desc, ofile);	/* description */

	/* Fix up the symbol value now that we know the final section
	   sizes.  */
	 if (((sym->type & N_TYPE) == N_SECT) && (sym->sect != NO_SECT)) {
	    nasm_assert(sym->sect <= seg_nsects);
	    sym->symv[0].key += sectstab[sym->sect]->addr;
	 }

	 fwriteptr(sym->symv[0].key, ofile); /* value (i.e. offset) */
     }

}

/* Fixup the snum in the relocation entries, we should be
   doing this only for externally referenced symbols. */
static void macho_fixup_relocs (struct reloc *r)
{
    struct symbol *sym;

    while (r != NULL) {
	if (r->ext) {
	    for (sym = syms; sym != NULL; sym = sym->next) {
		if (sym->initial_snum == r->snum) {
		    r->snum = sym->snum;
		    break;
		}
	    }
	}
	r = r->next;
    }
}

/* Write out the object file.  */

static void macho_write (void)
{
    uint64_t offset = 0;

    /* mach-o object file structure:
    **
    ** mach header
    **  uint32_t magic
    **  int   cpu type
    **  int   cpu subtype
    **  uint32_t mach file type
    **  uint32_t number of load commands
    **  uint32_t size of all load commands
    **   (includes section struct size of segment command)
    **  uint32_t flags
    **
    ** segment command
    **  uint32_t command type == LC_SEGMENT[_64]
    **  uint32_t size of load command
    **   (including section load commands)
    **  char[16] segment name
    **  pointer  in-memory offset
    **  pointer  in-memory size
    **  pointer  in-file offset to data area
    **  pointer  in-file size
    **   (in-memory size excluding zerofill sections)
    **  int   maximum vm protection
    **  int   initial vm protection
    **  uint32_t number of sections
    **  uint32_t flags
    **
    ** section commands
    **   char[16] section name
    **   char[16] segment name
    **   pointer  in-memory offset
    **   pointer  in-memory size
    **   uint32_t in-file offset
    **   uint32_t alignment
    **    (irrelevant in MH_OBJECT)
    **   uint32_t in-file offset of relocation entires
    **   uint32_t number of relocations
    **   uint32_t flags
    **   uint32_t reserved
    **   uint32_t reserved
    **
    ** symbol table command
    **  uint32_t command type == LC_SYMTAB
    **  uint32_t size of load command
    **  uint32_t symbol table offset
    **  uint32_t number of symbol table entries
    **  uint32_t string table offset
    **  uint32_t string table size
    **
    ** raw section data
    **
    ** padding to pointer boundary
    **
    ** relocation data (struct reloc)
    ** int32_t offset
    **  uint data (symbolnum, pcrel, length, extern, type)
    **
    ** symbol table data (struct nlist)
    **  int32_t  string table entry number
    **  uint8_t type
    **   (extern, absolute, defined in section)
    **  uint8_t section
    **   (0 for global symbols, section number of definition (>= 1, <=
    **   254) for local symbols, size of variable for common symbols
    **   [type == extern])
    **  int16_t description
    **   (for stab debugging format)
    **  pointer value (i.e. file offset) of symbol or stab offset
    **
    ** string table data
    **  list of null-terminated strings
    */

    /* Emit the Mach-O header.  */
    macho_write_header();

    offset = fmt.header_size + head_sizeofcmds;

    /* emit the segment load command */
    if (seg_nsects > 0)
	offset = macho_write_segment (offset);
    else
        nasm_error(ERR_WARNING, "no sections?");

    if (nsyms > 0) {
        /* write out symbol command */
        fwriteint32_t(LC_SYMTAB, ofile); /* cmd == LC_SYMTAB */
        fwriteint32_t(MACHO_SYMCMD_SIZE, ofile); /* size of load command */
        fwriteint32_t(offset, ofile);    /* symbol table offset */
        fwriteint32_t(nsyms, ofile);     /* number of symbol
                                         ** table entries */
        offset += nsyms * fmt.nlist_size;
        fwriteint32_t(offset, ofile);    /* string table offset */
        fwriteint32_t(strslen, ofile);   /* string table size */
    }

    /* emit section data */
    if (seg_nsects > 0)
	macho_write_section ();

    /* emit symbol table if we have symbols */
    if (nsyms > 0)
	macho_write_symtab ();

    /* we don't need to pad here, we are already aligned */

    /* emit string table */
    saa_fpwrite(strs, ofile);
}
/* We do quite a bit here, starting with finalizing all of the data
   for the object file, writing, and then freeing all of the data from
   the file.  */

static void macho_cleanup(void)
{
    struct section *s;
    struct reloc *r;
    struct symbol *sym;

    dfmt->cleanup();

    /* Sort all symbols.  */
    macho_layout_symbols (&nsyms, &strslen);

    /* Fixup relocation entries */
    for (s = sects; s != NULL; s = s->next) {
	macho_fixup_relocs (s->relocs);
    }

    /* First calculate and finalize needed values.  */
    macho_calculate_sizes();
    macho_write();

    /* free up everything */
    while (sects->next) {
        s = sects;
        sects = sects->next;

        saa_free(s->data);
        while (s->relocs != NULL) {
            r = s->relocs;
            s->relocs = s->relocs->next;
            nasm_free(r);
        }

        nasm_free(s);
    }

    saa_free(strs);

    raa_free(extsyms);

    while (syms) {
       sym = syms;
       syms = syms->next;
       nasm_free (sym);
    }

    nasm_free(extdefsyms);
    nasm_free(undefsyms);
    nasm_free(sectstab);
    raa_free(section_by_index);
    hash_free(&section_by_name);
}

static bool macho_set_section_attribute_by_symbol(const char *label, uint32_t flags)
{
    struct section *s;
    int32_t nasm_seg;
    int64_t offset;

    if (!lookup_label(label, &nasm_seg, &offset)) {
	nasm_error(ERR_NONFATAL, "unknown symbol `%s' in no_dead_strip", label);
	return false;
    }

    s = get_section_by_index(nasm_seg);
    if (!s) {
	nasm_error(ERR_NONFATAL, "symbol `%s' is external or absolute", label);
	return false;
    }

    s->flags |= flags;
    return true;
}

/*
 * Mark a symbol for no dead stripping
 */
static enum directive_result macho_no_dead_strip(const char *labels)
{
    char *s, *p, *ep;
    char ec;
    enum directive_result rv = DIRR_ERROR;
    bool real = passn > 1;

    p = s = nasm_strdup(labels);
    while (*p) {
	ep = nasm_skip_identifier(p);
	if (!ep) {
	    nasm_error(ERR_NONFATAL, "invalid symbol in NO_DEAD_STRIP");
	    goto err;
	}
	ec = *ep;
	if (ec && ec != ',' && !nasm_isspace(ec)) {
	    nasm_error(ERR_NONFATAL, "cannot parse contents after symbol");
	    goto err;
	}
	*ep = '\0';
	if (real) {
	    if (!macho_set_section_attribute_by_symbol(p, S_ATTR_NO_DEAD_STRIP))
		rv = DIRR_ERROR;
	}
	*ep = ec;
	p = nasm_skip_spaces(ep);
	if (*p == ',')
	    p = nasm_skip_spaces(++p);
    }

    rv = DIRR_OK;

err:
    nasm_free(s);
    return rv;
}

/*
 * Mach-O pragmas
 */
static enum directive_result
macho_pragma(const struct pragma *pragma)
{
    bool real = passn > 1;

    switch (pragma->opcode) {
    case D_SUBSECTIONS_VIA_SYMBOLS:
	if (*pragma->tail)
	    return DIRR_BADPARAM;

	if (real)
	    head_flags |= MH_SUBSECTIONS_VIA_SYMBOLS;

        /* Jmp-match optimization conflicts */
        optimizing.flag |= OPTIM_DISABLE_JMP_MATCH;

	return DIRR_OK;

    case D_NO_DEAD_STRIP:
	return macho_no_dead_strip(pragma->tail);

    default:
	return DIRR_UNKNOWN;	/* Not a Mach-O directive */
    }
}

static const struct pragma_facility macho_pragma_list[] = {
    { "macho", macho_pragma },
    { NULL, macho_pragma }	/* Implements macho32/macho64 namespaces */
};

static void macho_dbg_generate(void)
{
    uint8_t *p_buf = NULL, *p_buf_base = NULL;
    size_t saa_len = 0, high_addr = 0, total_len = 0;
    struct section *p_section = NULL;
    /* calculated at debug_str and referenced at debug_info */
    uint32_t producer_str_offset = 0, module_str_offset = 0, dir_str_offset = 0;

    /* debug section defines */
    {
        int bits = 0;
        macho_section(".debug_abbrev", 0, &bits);
        macho_section(".debug_info", 0, &bits);
        macho_section(".debug_line", 0, &bits);
        macho_section(".debug_str", 0, &bits);
    }

    /* dw section walk to find high_addr and total_len */
    {
        struct dw_sect_list *p_sect;

        list_for_each(p_sect, dw_head_sect) {
            uint64_t offset = get_section_by_index(p_sect->section)->size;
            struct SAA *p_linep = p_sect->psaa;

            saa_write8(p_linep, 2); /* std op 2 */
            saa_write8(p_linep, offset - p_sect->offset);
            saa_write8(p_linep, DW_LNS_extended_op);
            saa_write8(p_linep, 1); /* operand length */
            saa_write8(p_linep, DW_LNE_end_sequence);

            total_len += p_linep->datalen;
            high_addr += offset;
        }
    }

    /* debug line */
    {
        struct dw_sect_list *p_sect;
        size_t linep_off, buf_size;
        struct SAA *p_lines = saa_init(1L);
        struct dir_list *p_dir;
        struct file_list *p_file;

        p_section = get_section_by_name("__DWARF", "__debug_line");
        nasm_assert(p_section != NULL);

        saa_write8(p_lines, 1); /* minimum instruction length */
        saa_write8(p_lines, 1); /* initial value of "is_stmt" */
        saa_write8(p_lines, DW_LN_BASE); /* line base */
        saa_write8(p_lines, DW_LN_RANGE); /* line range */
        saa_write8(p_lines, DW_OPCODE_BASE); /* opcode base */
        saa_write8(p_lines, 0); /* std opcode 1 length */
        saa_write8(p_lines, 1); /* std opcode 2 length */
        saa_write8(p_lines, 1); /* std opcode 3 length */
        saa_write8(p_lines, 1); /* std opcode 4 length */
        saa_write8(p_lines, 1); /* std opcode 5 length */
        saa_write8(p_lines, 0); /* std opcode 6 length */
        saa_write8(p_lines, 0); /* std opcode 7 length */
        saa_write8(p_lines, 0); /* std opcode 8 length */
        saa_write8(p_lines, 1); /* std opcode 9 length */
        saa_write8(p_lines, 0); /* std opcode 10 length */
        saa_write8(p_lines, 0); /* std opcode 11 length */
        saa_write8(p_lines, 1); /* std opcode 12 length */
        list_for_each(p_dir, dw_head_dir) {
            saa_wcstring(p_lines, p_dir->dir_name);
        }
        saa_write8(p_lines, 0); /* end of table */

        list_for_each(p_file, dw_head_file) {
            saa_wcstring(p_lines, p_file->file_name);
            saa_write8(p_lines, p_file->dir->dir); /* directory id */
            saa_write8(p_lines, 0); /* time */
            saa_write8(p_lines, 0); /* size */
        }
        saa_write8(p_lines, 0); /* end of table */

        linep_off = p_lines->datalen;
        /* 10 bytes for initial & prolong length, and dwarf version info */
        buf_size = saa_len = linep_off + total_len + 10;
        p_buf_base = p_buf = nasm_malloc(buf_size);

        WRITELONG(p_buf, saa_len - 4); /* initial length; size excluding itself */
        WRITESHORT(p_buf, 2); /* dwarf version */
        WRITELONG(p_buf, linep_off); /* prolong length */

        saa_rnbytes(p_lines, p_buf, linep_off);
        p_buf += linep_off;
        saa_free(p_lines);

        list_for_each(p_sect, dw_head_sect) {
            struct SAA *p_linep = p_sect->psaa;

            saa_len = p_linep->datalen;
            saa_rnbytes(p_linep, p_buf, saa_len);
            p_buf += saa_len;

            saa_free(p_linep);
        }

        macho_output(p_section->index, p_buf_base, OUT_RAWDATA, buf_size, NO_SEG, 0);

        nasm_free(p_buf_base);
    }

    /* string section */
    {
        struct SAA *p_str = saa_init(1L);
        char *cur_path = nasm_realpath(module_name);
        char *cur_file = nasm_basename(cur_path);
        char *cur_dir = nasm_dirname(cur_path);

        p_section = get_section_by_name("__DWARF", "__debug_str");
        nasm_assert(p_section != NULL);

        producer_str_offset = 0;
        module_str_offset = dir_str_offset = saa_wcstring(p_str, nasm_signature);
        dir_str_offset += saa_wcstring(p_str, cur_file);
        saa_wcstring(p_str, cur_dir);

        saa_len = p_str->datalen;
        p_buf = nasm_malloc(saa_len);
        saa_rnbytes(p_str, p_buf, saa_len);
        macho_output(p_section->index, p_buf, OUT_RAWDATA, saa_len, NO_SEG, 0);

        nasm_free(cur_path);
        nasm_free(cur_file);
        nasm_free(cur_dir);
        saa_free(p_str);
        nasm_free(p_buf);
    }

    /* debug info */
    {
        struct SAA *p_info = saa_init(1L);

        p_section = get_section_by_name("__DWARF", "__debug_info");
        nasm_assert(p_section != NULL);

        /* size will be overwritten once determined, so skip in p_info layout */
        saa_write16(p_info, 2); /* dwarf version */
        saa_write32(p_info, 0); /* offset info abbrev */
        saa_write8(p_info, (ofmt == &of_macho64) ? 8 : 4);   /* pointer size  */

        saa_write8(p_info, 1);   /* abbrev entry number  */

        saa_write32(p_info, producer_str_offset); /* offset from string table for DW_AT_producer  */
        saa_write16(p_info, DW_LANG_Mips_Assembler); /* DW_AT_language  */
        saa_write32(p_info, module_str_offset); /* offset from string table for DW_AT_name  */
        saa_write32(p_info, dir_str_offset); /* offset from string table for DW_AT_comp_dir */
        saa_write32(p_info, 0); /* DW_AT_stmt_list  */

        if (ofmt == &of_macho64) {
            saa_write64(p_info, 0); /* DW_AT_low_pc */
            saa_write64(p_info, high_addr); /* DW_AT_high_pc */
        } else  {
            saa_write32(p_info, 0); /* DW_AT_low_pc */
            saa_write32(p_info, high_addr); /* DW_AT_high_pc */
        }

        saa_write8(p_info, 2);   /* abbrev entry number */

        if (ofmt == &of_macho64) {
            saa_write64(p_info, 0); /* DW_AT_low_pc */
            saa_write64(p_info, 0); /* DW_AT_frame_base */
        } else  {
            saa_write32(p_info, 0); /* DW_AT_low_pc */
            saa_write32(p_info, 0); /* DW_AT_frame_base */
        }
        saa_write8(p_info, DW_END_default);

        saa_len = p_info->datalen;
        p_buf_base = p_buf = nasm_malloc(saa_len + 4); /* 4B for size info */

        WRITELONG(p_buf, saa_len);
        saa_rnbytes(p_info, p_buf, saa_len);
        macho_output(p_section->index, p_buf_base, OUT_RAWDATA, saa_len + 4, NO_SEG, 0);

        saa_free(p_info);
        nasm_free(p_buf_base);
    }

    /* abbrev section */
    {
        struct SAA *p_abbrev = saa_init(1L);

        p_section = get_section_by_name("__DWARF", "__debug_abbrev");
        nasm_assert(p_section != NULL);

        saa_write8(p_abbrev, 1); /* entry number */

        saa_write8(p_abbrev, DW_TAG_compile_unit);
        saa_write8(p_abbrev, DW_CHILDREN_yes);

        saa_write8(p_abbrev, DW_AT_producer);
        saa_write8(p_abbrev, DW_FORM_strp);

        saa_write8(p_abbrev, DW_AT_language);
        saa_write8(p_abbrev, DW_FORM_data2);

        saa_write8(p_abbrev, DW_AT_name);
        saa_write8(p_abbrev, DW_FORM_strp);

        saa_write8(p_abbrev, DW_AT_comp_dir);
        saa_write8(p_abbrev, DW_FORM_strp);

        saa_write8(p_abbrev, DW_AT_stmt_list);
        saa_write8(p_abbrev, DW_FORM_data4);

        saa_write8(p_abbrev, DW_AT_low_pc);
        saa_write8(p_abbrev, DW_FORM_addr);

        saa_write8(p_abbrev, DW_AT_high_pc);
        saa_write8(p_abbrev, DW_FORM_addr);

        saa_write16(p_abbrev, DW_END_default);

        saa_write8(p_abbrev, 2); /* entry number */

        saa_write8(p_abbrev, DW_TAG_subprogram);
        saa_write8(p_abbrev, DW_CHILDREN_no);

        saa_write8(p_abbrev, DW_AT_low_pc);
        saa_write8(p_abbrev, DW_FORM_addr);

        saa_write8(p_abbrev, DW_AT_frame_base);
        saa_write8(p_abbrev, DW_FORM_addr);

        saa_write16(p_abbrev, DW_END_default);

	saa_write8(p_abbrev, 0); /* Terminal zero entry */

        saa_len = p_abbrev->datalen;

        p_buf = nasm_malloc(saa_len);

        saa_rnbytes(p_abbrev, p_buf, saa_len);
        macho_output(p_section->index, p_buf, OUT_RAWDATA, saa_len, NO_SEG, 0);

        saa_free(p_abbrev);
        nasm_free(p_buf);
    }
}

static void new_file_list (const char *file_name, const char *dir_name)
{
    struct dir_list *dir_list;
    bool need_new_dir_list = true;

    nasm_new(dw_cur_file);
    dw_cur_file->file = ++dw_num_files;
    dw_cur_file->file_name = file_name;
    if(!dw_head_file) {
        dw_head_file = dw_cur_file;
    } else {
        *dw_last_file_next = dw_cur_file;
    }
    dw_last_file_next = &(dw_cur_file->next);

    if(dw_head_dir) {
        list_for_each(dir_list, dw_head_dir) {
            if(!(strcmp(dir_name, dir_list->dir_name))) {
                dw_cur_file->dir = dir_list;
                need_new_dir_list = false;
                break;
            }
        }
    }

    if(need_new_dir_list)
    {
        nasm_new(dir_list);
        dir_list->dir = dw_num_dirs++;
        dir_list->dir_name = dir_name;
        if(!dw_head_dir) {
            dw_head_dir = dir_list;
        } else {
            *dw_last_dir_next = dir_list;
        }
        dw_last_dir_next = &(dir_list->next);
        dw_cur_file->dir = dir_list;
    }
}

static void macho_dbg_init(void)
{
}

static void macho_dbg_linenum(const char *file_name, int32_t line_num, int32_t segto)
{
    bool need_new_list = true;
    const char *cur_file = nasm_basename(file_name);
    const char *cur_dir  = nasm_dirname(file_name);
    (void)segto;

    if(!dw_cur_file || strcmp(cur_file, dw_cur_file->file_name) ||
        strcmp(cur_dir, dw_cur_file->dir->dir_name)) {
        if(dw_head_file) {
            struct file_list *match;

            list_for_each(match, dw_head_file) {
                if(!(strcmp(cur_file, match->file_name)) &&
                    !(strcmp(cur_dir, match->dir->dir_name))) {
                    dw_cur_file = match;
                    dw_cur_file->dir = match->dir;
                    need_new_list = false;
                    break;
                }
            }
        }

        if(need_new_list) {
            new_file_list(cur_file, cur_dir);
        }
    }

    dbg_immcall = true;
    cur_line = line_num;
}

static void macho_dbg_output(int type, void *param)
{
    struct section_info *sinfo_param = (struct section_info *)param;
    int32_t secto = sinfo_param->secto;
    bool need_new_sect = false;
    struct SAA *p_linep = NULL;
    (void)type;

    if(!(dw_cur_sect && (dw_cur_sect->section == secto))) {
        need_new_sect = true;
        if(dw_head_sect) {
            struct dw_sect_list *match = dw_head_sect;
            uint32_t idx = 0;

            for(; idx < dw_num_sects; idx++) {
                if(match->section == secto) {
                    dw_cur_sect = match;
                    need_new_sect = false;
                    break;
                }
                match = match->next;
            }
        }
    }

    if(need_new_sect) {
        nasm_new(dw_cur_sect);
        dw_num_sects ++;
        p_linep = dw_cur_sect->psaa = saa_init(1L);
        dw_cur_sect->line = dw_cur_sect->file = 1;
        dw_cur_sect->offset = 0;
        dw_cur_sect->next = NULL;
        dw_cur_sect->section = secto;

        saa_write8(p_linep, DW_LNS_extended_op);
        saa_write8(p_linep, (ofmt == &of_macho64) ? 9 : 5);
        saa_write8(p_linep, DW_LNE_set_address);
        if (ofmt == &of_macho64) {
            saa_write64(p_linep, 0);
        } else {
            saa_write32(p_linep, 0);
        }

        if(!dw_head_sect) {
            dw_head_sect = dw_last_sect = dw_cur_sect;
        } else {
            dw_last_sect->next = dw_cur_sect;
            dw_last_sect = dw_cur_sect;
        }
    }

    if(dbg_immcall == true) {
        int32_t line_delta = cur_line - dw_cur_sect->line;
        int32_t offset_delta = sinfo_param->size - dw_cur_sect->offset;
        uint32_t cur_file = dw_cur_file->file;
        p_linep = dw_cur_sect->psaa;

        if(cur_file != dw_cur_sect->file) {
            saa_write8(p_linep, DW_LNS_set_file);
            saa_write8(p_linep, cur_file);
            dw_cur_sect->file = cur_file;
        }

        if(line_delta) {
            int special_opcode = (line_delta - DW_LN_BASE) + (DW_LN_RANGE * offset_delta) +
                                             DW_OPCODE_BASE;

            if((line_delta >= DW_LN_BASE) && (line_delta < DW_MAX_LN) &&
                (special_opcode < DW_MAX_SP_OPCODE)) {
                saa_write8(p_linep, special_opcode);
            } else {
                saa_write8(p_linep, DW_LNS_advance_line);
                saa_wleb128s(p_linep, line_delta);
                if(offset_delta) {
                    saa_write8(p_linep, DW_LNS_advance_pc);
                    saa_wleb128u(p_linep, offset_delta);
                }
                saa_write8(p_linep, DW_LNS_copy);
            }

            dw_cur_sect->line = cur_line;
            dw_cur_sect->offset = sinfo_param->size;
        }

        dbg_immcall = false;
    }
}

static void macho_dbg_cleanup(void)
{
    /* dwarf sectors generation */
    macho_dbg_generate();

    {
        struct dw_sect_list *p_sect = dw_head_sect;
        struct file_list *p_file = dw_head_file;
        uint32_t idx = 0;

        for(; idx < dw_num_sects; idx++) {
            struct dw_sect_list *next = p_sect->next;
            nasm_free(p_sect);
            p_sect = next;
        }

        for(idx = 0; idx < dw_num_files; idx++) {
            struct file_list *next = p_file->next;
            nasm_free(p_file);
            p_file = next;
        }
    }
}

#ifdef OF_MACHO32
static const struct macho_fmt macho32_fmt = {
    4,
    MH_MAGIC,
    CPU_TYPE_I386,
    LC_SEGMENT,
    MACHO_HEADER_SIZE,
    MACHO_SEGCMD_SIZE,
    MACHO_SECTCMD_SIZE,
    MACHO_NLIST_SIZE,
    RL_MAX_32,
    GENERIC_RELOC_VANILLA,
    GENERIC_RELOC_VANILLA,
    GENERIC_RELOC_TLV,
    false			/* Allow segment-relative relocations */
};

static void macho32_init(void)
{
    fmt = macho32_fmt;
    macho_init();

    macho_gotpcrel_sect = NO_SEG;
}

static const struct dfmt macho32_df_dwarf = {
    "MachO32 (i386) dwarf debug format for Darwin/MacOS",
    "dwarf",
    macho_dbg_init,
    macho_dbg_linenum,
    null_debug_deflabel,
    null_debug_directive,
    null_debug_typevalue,
    macho_dbg_output,
    macho_dbg_cleanup,
    NULL /*pragma list*/
};

static const struct dfmt * const macho32_df_arr[2] =
 { &macho32_df_dwarf, NULL };

const struct ofmt of_macho32 = {
    "NeXTstep/OpenStep/Rhapsody/Darwin/MacOS X (i386) object files",
    "macho32",
    ".o",
    0,
    32,
    macho32_df_arr,
    &macho32_df_dwarf,
    macho_stdmac,
    macho32_init,
    null_reset,
    nasm_do_legacy_output,
    macho_output,
    macho_symdef,
    macho_section,
    macho_herelabel,
    macho_sectalign,
    null_segbase,
    null_directive,
    macho_cleanup,
    macho_pragma_list
};
#endif

#ifdef OF_MACHO64
static const struct macho_fmt macho64_fmt = {
    8,
    MH_MAGIC_64,
    CPU_TYPE_X86_64,
    LC_SEGMENT_64,
    MACHO_HEADER64_SIZE,
    MACHO_SEGCMD64_SIZE,
    MACHO_SECTCMD64_SIZE,
    MACHO_NLIST64_SIZE,
    RL_MAX_64,
    X86_64_RELOC_UNSIGNED,
    X86_64_RELOC_SIGNED,
    X86_64_RELOC_TLV,
    true			/* Force symbol-relative relocations */
};

static void macho64_init(void)
{
    fmt = macho64_fmt;
    macho_init();

    /* add special symbol for ..gotpcrel */
    macho_gotpcrel_sect = seg_alloc() + 1;
    backend_label("..gotpcrel", macho_gotpcrel_sect, 0L);
}

static const struct dfmt macho64_df_dwarf = {
    "MachO64 (x86-64) dwarf debug format for Darwin/MacOS",
    "dwarf",
    macho_dbg_init,
    macho_dbg_linenum,
    null_debug_deflabel,
    null_debug_directive,
    null_debug_typevalue,
    macho_dbg_output,
    macho_dbg_cleanup,
    NULL /*pragma list*/
};

static const struct dfmt * const macho64_df_arr[2] =
 { &macho64_df_dwarf, NULL };

const struct ofmt of_macho64 = {
    "NeXTstep/OpenStep/Rhapsody/Darwin/MacOS X (x86_64) object files",
    "macho64",
    ".o",
    0,
    64,
    macho64_df_arr,
    &macho64_df_dwarf,
    macho_stdmac,
    macho64_init,
    null_reset,
    nasm_do_legacy_output,
    macho_output,
    macho_symdef,
    macho_section,
    macho_herelabel,
    macho_sectalign,
    null_segbase,
    null_directive,
    macho_cleanup,
    macho_pragma_list,
};
#endif

#endif

/*
 * Local Variables:
 * mode:c
 * c-basic-offset:4
 * End:
 *
 * end of file */
