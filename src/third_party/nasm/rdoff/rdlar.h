/*
 * rdlar.h - definitions of new RDOFF library/archive format.
 */

#ifndef RDOFF_RDLAR_H
#define RDOFF_RDLAR_H 1

#include "compiler.h"

/* For non-POSIX operating systems */
#ifndef HAVE_GETUID
# define getuid() 0
#endif
#ifndef HAVE_GETGID
# define getgid() 0
#endif

#define RDLAMAG		0x414C4452      /* Archive magic */
#define RDLMMAG		0x4D4C4452      /* Member magic */

#define MAXMODNAMELEN	256     /* Maximum length of module name */

struct rdlm_hdr {
    uint32_t magic;        /* Must be RDLAMAG */
    uint32_t hdrsize;      /* Header size + sizeof(module_name) */
    uint32_t date;         /* Creation date */
    uint32_t owner;        /* UID */
    uint32_t group;        /* GID */
    uint32_t mode;         /* File mode */
    uint32_t size;         /* File size */
    /* NULL-terminated module name immediately follows */
};

#endif
