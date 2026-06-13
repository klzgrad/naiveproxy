dnl --------------------------------------------------------------------------
dnl PA_ENDIAN
dnl
dnl Test for a bunch of variants of endian tests and byteorder functions.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ENDIAN],
[AC_C_BIGENDIAN(AC_DEFINE(WORDS_BIGENDIAN),AC_DEFINE(WORDS_LITTLEENDIAN),,)
AH_TEMPLATE(WORDS_BIGENDIAN,
[Define to 1 if your processor stores words with the most significant
byte first (like Motorola and SPARC, unlike Intel and VAX).])
AH_TEMPLATE(WORDS_LITTLEENDIAN,
[Define to 1 if your processor stores words with the least significant
byte first (like Intel and VAX, unlike Motorola and SPARC).])
AC_CHECK_HEADERS_ONCE(stdbit.h)dnl C23 standard header for this stuff
dnl Note: alwasy look for the canonical POSIX version, to make sure to
dnl avoid conflict when substituting
AS_IF([test x$ac_cv_c_bigendian = xno],
[
dnl Littleendian
PA_HAVE_FUNC(htole16,,[endian.h sys/endian.h machine/endian.h])
PA_HAVE_FUNC(htole32,,[endian.h sys/endian.h machine/endian.h])
PA_HAVE_FUNC(htole64,,[endian.h sys/endian.h machine/endian.h])
],[
dnl Maybe not littleendian
PA_FIND_FUNC([htole16,,[endian.h sys/endian.h machine/endian.h]],
[__builtin_bswap16], [bswap_16,,[byteswap.h]], [_byteswap_ushort,,[stdlib.h]],
[cpu_to_le16], [__cpu_to_le16])
PA_FIND_FUNC([htole32,,[endian.h sys/endian.h machine/endian.h]],
[__builtin_bswap32], [bswap_32,,[byteswap.h]], [_byteswap_ulong,,[stdlib.h]],
[cpu_to_le32], [__cpu_to_le32])
PA_FIND_FUNC([htole64,,[endian.h sys/endian.h machine/endian.h]],
[__builtin_bswap64], [bswap_64,,[byteswap.h]], [_byteswap_uint64,,[stdlib.h]],
[cpu_to_le64], [__cpu_to_le64])
])
AS_IF([test x$ac_cv_c_bigendian = xyes],
[
dnl Bigendian
PA_HAVE_FUNC(htobe16,,[endian.h sys/endian.h machine/endian.h])
PA_HAVE_FUNC(htobe32,,[endian.h sys/endian.h machine/endian.h])
PA_HAVE_FUNC(htobe64,,[endian.h sys/endian.h machine/endian.h])
],[
dnl Maybe not bigendian
PA_FIND_FUNC([htobe16,,[endian.h sys/endian.h machine/endian.h]],
[htons,,[arpa/inet.h]],
[__builtin_bswap16], [bswap_16,,[byteswap.h]], [_byteswap_ushort,,[stdlib.h]],
[cpu_to_be16], [__cpu_to_be16])
PA_FIND_FUNC([htobe32,,[endian.h sys/endian.h machine/endian.h]],
[htonl,,[arpa/inet.h]],
[__builtin_bswap32], [bswap_32,,[byteswap.h]], [_byteswap_ulong,,[stdlib.h]],
[cpu_to_be32], [__cpu_to_le32])
PA_FIND_FUNC([htobe64,,[endian.h sys/endian.h machine/endian.h]],
[htonq,,[arpa/inet.h]],
[__builtin_bswap64], [bswap_64,,[byteswap.h]], [_byteswap_uint64,,[stdlib.h]],
[cpu_to_be64], [__cpu_to_be64])
])
])
