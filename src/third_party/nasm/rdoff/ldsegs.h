/*
 * ldsegs.h	Data for 'ldrdf' to determine what to do with different
 *		types of segment. This may be useful in other contexts also.
 */

#ifndef RDOFF_LDSEGS_H
#define RDOFF_LDSEGS_H 1


struct segconfig {
    uint16_t typelow, typehi;	/* range of seg nos for which this is valid */
    char *typedesc;		/* a description of the segment type */
    uint16_t dowhat;		/* one of the SEG_xxxx values below */
    uint16_t mergetype;		/* if SEG_MERGE what type segment do we merge
				   with?
                                   0 -> same type of segment. This type is also
                                   used with SEG_NEWSEG. */
};

#define SEG_IGNORE 0
#define SEG_NEWSEG 1
#define SEG_MERGE  2

#define SEGCONFIGMAX 11

struct segconfig sconft[SEGCONFIGMAX] = {
    {0x0000, 0x0000, "NULL segment", 0, 0},
    {0x0001, 0x0001, "text", 2, 0},
    {0x0002, 0x0002, "data", 2, 0},
    {0x0003, 0x0003, "comment(ignored)", 0, 0},
    {0x0004, 0x0005, "comment(kept)", 2, 0},
    {0x0006, 0x0007, "debug information", 2, 0},
    {0x0008, 0x001F, "reserved(general extensions)", 1, 0},
    {0x0020, 0x0FFF, "reserved(MOSCOW)", 1, 0},
    {0x1000, 0x7FFF, "reserved(system dependant)", 1, 0},
    {0x8000, 0xFFFE, "reserved(other)", 1, 0},
    {0xFFFF, 0xFFFF, "invalid segment", 0, 0}
};

#define getsegconfig(target,number)				\
    {								\
       int _i;							\
       int _t = number;						\
       for (_i = 0; _i < SEGCONFIGMAX; _i++)			\
          if (_t >= sconft[_i].typelow && _t <= sconft[_i].typehi)	\
          {							\
              target = sconft[_i];				\
              if (target.mergetype == 0) target.mergetype = _t;	\
              break;						\
          }							\
       if (_i == SEGCONFIGMAX)					\
       {							\
          fprintf(stderr, "PANIC: can't find segment %04X in segconfig\n",\
                  _t);						\
          exit(1);						\
       }							\
    }

#endif
