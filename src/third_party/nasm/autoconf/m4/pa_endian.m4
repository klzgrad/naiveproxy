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
PA_ADD_HEADERS(endian.h sys/endian.h machine/endian.h)
PA_HAVE_FUNC(cpu_to_le16, (0))
PA_HAVE_FUNC(cpu_to_le32, (0))
PA_HAVE_FUNC(cpu_to_le64, (0))
PA_HAVE_FUNC(__cpu_to_le16, (0))
PA_HAVE_FUNC(__cpu_to_le32, (0))
PA_HAVE_FUNC(__cpu_to_le64, (0))
PA_HAVE_FUNC(htole16, (0))
PA_HAVE_FUNC(htole32, (0))
PA_HAVE_FUNC(htole64, (0))
PA_HAVE_FUNC(__bswap_16, (0))
PA_HAVE_FUNC(__bswap_32, (0))
PA_HAVE_FUNC(__bswap_64, (0))
PA_HAVE_FUNC(__builtin_bswap16, (0))
PA_HAVE_FUNC(__builtin_bswap32, (0))
PA_HAVE_FUNC(__builtin_bswap64, (0))
PA_HAVE_FUNC(_byteswap_ushort, (0))
PA_HAVE_FUNC(_byteswap_ulong, (0))
PA_HAVE_FUNC(_byteswap_uint64, (0))])
