/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** An tokenizer for SQL
**
** This file contains C code that splits an SQL input string up into
** individual tokens and sends those tokens one-by-one over to the
** parser for analysis.
*/
#include <stdlib.h>
#include "src/trace_processor/perfetto_sql/tokenizer/tokenize_internal_helper.h"

/* Character classes for tokenizing
**
** In the sqlite3GetToken() function, a switch() on aiClass[c] is implemented
** using a lookup table, whereas a switch() directly on c uses a binary search.
** The lookup table is much faster.  To maximize speed, and to ensure that
** a lookup table is used, all of the classes need to be small integers and
** all of them need to be used within the switch.
*/
#define CC_X 0        /* The letter 'x', or start of BLOB literal */
#define CC_KYWD0 1    /* First letter of a keyword */
#define CC_KYWD 2     /* Alphabetics or '_'.  Usable in a keyword */
#define CC_DIGIT 3    /* Digits */
#define CC_DOLLAR 4   /* '$' */
#define CC_VARALPHA 5 /* '@', '#', ':'.  Alphabetic SQL variables */
#define CC_VARNUM 6   /* '?'.  Numeric SQL variables */
#define CC_SPACE 7    /* Space characters */
#define CC_QUOTE 8    /* '"', '\'', or '`'.  String literals, quoted ids */
#define CC_QUOTE2 9   /* '['.   [...] style quoted ids */
#define CC_PIPE 10    /* '|'.   Bitwise OR or concatenate */
#define CC_MINUS 11   /* '-'.  Minus or SQL-style comment */
#define CC_LT 12      /* '<'.  Part of < or <= or <> */
#define CC_GT 13      /* '>'.  Part of > or >= */
#define CC_EQ 14      /* '='.  Part of = or == */
#define CC_BANG 15    /* '!'.  Part of != */
#define CC_SLASH 16   /* '/'.  / or c-style comment */
#define CC_LP 17      /* '(' */
#define CC_RP 18      /* ')' */
#define CC_SEMI 19    /* ';' */
#define CC_PLUS 20    /* '+' */
#define CC_STAR 21    /* '*' */
#define CC_PERCENT 22 /* '%' */
#define CC_COMMA 23   /* ',' */
#define CC_AND 24     /* '&' */
#define CC_TILDA 25   /* '~' */
#define CC_DOT 26     /* '.' */
#define CC_ID 27      /* unicode characters usable in IDs */
#define CC_ILLEGAL 28 /* Illegal character */
#define CC_NUL 29     /* 0x00 */
#define CC_BOM 30     /* First byte of UTF8 BOM:  0xEF 0xBB 0xBF */

static const unsigned char aiClass[] = {
#ifdef SQLITE_ASCII
    /*         x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf */
    /* 0x */ 29,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    7,
    7,
    28,
    7,
    7,
    28,
    28,
    /* 1x */ 28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    /* 2x */ 7,
    15,
    8,
    5,
    4,
    22,
    24,
    8,
    17,
    18,
    21,
    20,
    23,
    11,
    26,
    16,
    /* 3x */ 3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    5,
    19,
    12,
    14,
    13,
    6,
    /* 4x */ 5,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    /* 5x */ 1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    0,
    2,
    2,
    9,
    28,
    28,
    28,
    2,
    /* 6x */ 8,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    /* 7x */ 1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    0,
    2,
    2,
    28,
    10,
    28,
    25,
    28,
    /* 8x */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    /* 9x */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    /* Ax */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    /* Bx */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    /* Cx */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    /* Dx */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    /* Ex */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    30,
    /* Fx */ 27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27,
    27
#endif
#ifdef SQLITE_EBCDIC
    /*         x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf */
    /* 0x */ 29,
    28,
    28,
    28,
    28,
    7,
    28,
    28,
    28,
    28,
    28,
    28,
    7,
    7,
    28,
    28,
    /* 1x */ 28,
    28,
    28,
    28,
    28,
    7,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    /* 2x */ 28,
    28,
    28,
    28,
    28,
    7,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    /* 3x */ 28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    /* 4x */ 7,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    26,
    12,
    17,
    20,
    10,
    /* 5x */ 24,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    15,
    4,
    21,
    18,
    19,
    28,
    /* 6x */ 11,
    16,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    23,
    22,
    2,
    13,
    6,
    /* 7x */ 28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    8,
    5,
    5,
    5,
    8,
    14,
    8,
    /* 8x */ 28,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    28,
    28,
    28,
    28,
    28,
    28,
    /* 9x */ 28,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    28,
    28,
    28,
    28,
    28,
    28,
    /* Ax */ 28,
    25,
    1,
    1,
    1,
    1,
    1,
    0,
    2,
    2,
    28,
    28,
    28,
    28,
    28,
    28,
    /* Bx */ 28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    28,
    9,
    28,
    28,
    28,
    28,
    28,
    /* Cx */ 28,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    28,
    28,
    28,
    28,
    28,
    28,
    /* Dx */ 28,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    28,
    28,
    28,
    28,
    28,
    28,
    /* Ex */ 28,
    28,
    1,
    1,
    1,
    1,
    1,
    0,
    2,
    2,
    28,
    28,
    28,
    28,
    28,
    28,
    /* Fx */ 3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    28,
    28,
    28,
    28,
    28,
    28,
#endif
};

/*
** The charMap() macro maps alphabetic characters (only) into their
** lower-case ASCII equivalent.  On ASCII machines, this is just
** an upper-to-lower case map.  On EBCDIC machines we also need
** to adjust the encoding.  The mapping is only valid for alphabetics
** which are the only characters for which this feature is used.
**
** Used by keywordhash.h
*/
#ifdef SQLITE_ASCII
#define charMap(X) sqlite3UpperToLower[(unsigned char)X]
#endif
#ifdef SQLITE_EBCDIC
#define charMap(X) ebcdicToAscii[(unsigned char)X]
const unsigned char ebcdicToAscii[] = {
    /* 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 0x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 1x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 2x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 3x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 4x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 5x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 95, 0, 0, /* 6x */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* 7x */
    0, 97,  98,  99,  100, 101, 102, 103, 104, 105, 0, 0, 0, 0,  0, 0, /* 8x */
    0, 106, 107, 108, 109, 110, 111, 112, 113, 114, 0, 0, 0, 0,  0, 0, /* 9x */
    0, 0,   115, 116, 117, 118, 119, 120, 121, 122, 0, 0, 0, 0,  0, 0, /* Ax */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* Bx */
    0, 97,  98,  99,  100, 101, 102, 103, 104, 105, 0, 0, 0, 0,  0, 0, /* Cx */
    0, 106, 107, 108, 109, 110, 111, 112, 113, 114, 0, 0, 0, 0,  0, 0, /* Dx */
    0, 0,   115, 116, 117, 118, 119, 120, 121, 122, 0, 0, 0, 0,  0, 0, /* Ex */
    0, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0,  0, 0, /* Fx */
};
#endif

/*
** The sqlite3KeywordCode function looks up an identifier to determine if
** it is a keyword.  If it is a keyword, the token code of that keyword is
** returned.  If the input is not a keyword, TK_ID is returned.
**
** The implementation of this routine was generated by a program,
** mkkeywordhash.c, located in the tool subdirectory of the distribution.
** The output of the mkkeywordhash.c program is written into a file
** named keywordhash.h and then included into this source file by
** the #include below.
*/

/*
** If X is a character that can be used in an identifier then
** IdChar(X) will be true.  Otherwise it is false.
**
** For ASCII, any character with the high-order bit set is
** allowed in an identifier.  For 7-bit characters,
** sqlite3IsIdChar[X] must be 1.
**
** For EBCDIC, the rules are more complex but have the same
** end result.
**
** Ticket #1066.  the SQL standard does not allow '$' in the
** middle of identifiers.  But many SQL implementations do.
** SQLite will allow '$' in identifiers for compatibility.
** But the feature is undocumented.
*/
#ifdef SQLITE_ASCII
#define IdChar(C) ((sqlite3CtypeMap[(unsigned char)C] & 0x46) != 0)
#endif
#ifdef SQLITE_EBCDIC
const char sqlite3IsEbcdicIdChar[] = {
    /* x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF */
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, /* 4x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, /* 5x */
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, /* 6x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, /* 7x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, /* 8x */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, /* 9x */
    1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, /* Ax */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* Bx */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, /* Cx */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, /* Dx */
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, /* Ex */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, /* Fx */
};
#define IdChar(C) (((c = C) >= 0x42 && sqlite3IsEbcdicIdChar[c - 0x40]))
#endif

/* Make the IdChar function accessible from ctime.c and alter.c */
int sqlite3IsIdChar(u8 c) {
  return IdChar(c);
}

#ifndef SQLITE_OMIT_WINDOWFUNC
/*
** Return the id of the next token in string (*pz). Before returning, set
** (*pz) to point to the byte following the parsed token.
*/
static int getToken(const unsigned char** pz) {
  const unsigned char* z = *pz;
  int t; /* Token type to return */
  do {
    z += sqlite3GetToken(z, &t);
  } while (t == TK_SPACE);
  if (t == TK_ID || t == TK_STRING || t == TK_JOIN_KW || t == TK_WINDOW ||
      t == TK_OVER || sqlite3ParserFallback(t) == TK_ID) {
    t = TK_ID;
  }
  *pz = z;
  return t;
}

/*
** The following three functions are called immediately after the tokenizer
** reads the keywords WINDOW, OVER and FILTER, respectively, to determine
** whether the token should be treated as a keyword or an SQL identifier.
** This cannot be handled by the usual lemon %fallback method, due to
** the ambiguity in some constructions. e.g.
**
**   SELECT sum(x) OVER ...
**
** In the above, "OVER" might be a keyword, or it might be an alias for the
** sum(x) expression. If a "%fallback ID OVER" directive were added to
** grammar, then SQLite would always treat "OVER" as an alias, making it
** impossible to call a window-function without a FILTER clause.
**
** WINDOW is treated as a keyword if:
**
**   * the following token is an identifier, or a keyword that can fallback
**     to being an identifier, and
**   * the token after than one is TK_AS.
**
** OVER is a keyword if:
**
**   * the previous token was TK_RP, and
**   * the next token is either TK_LP or an identifier.
**
** FILTER is a keyword if:
**
**   * the previous token was TK_RP, and
**   * the next token is TK_LP.
*/
int sqliteTokenizeInternalAnalyzeWindowKeyword(const unsigned char* z) {
  int t;
  t = getToken(&z);
  if (t != TK_ID)
    return TK_ID;
  t = getToken(&z);
  if (t != TK_AS)
    return TK_ID;
  return TK_WINDOW;
}
int sqliteTokenizeInternalAnalyzeOverKeyword(const unsigned char* z,
                                             int lastToken) {
  if (lastToken == TK_RP) {
    int t = getToken(&z);
    if (t == TK_LP || t == TK_ID)
      return TK_OVER;
  }
  return TK_ID;
}
int sqliteTokenizeInternalAnalyzeFilterKeyword(const unsigned char* z,
                                               int lastToken) {
  if (lastToken == TK_RP && getToken(&z) == TK_LP) {
    return TK_FILTER;
  }
  return TK_ID;
}
#endif /* SQLITE_OMIT_WINDOWFUNC */

/*
** Return the length (in bytes) of the token that begins at z[0].
** Store the token type in *tokenType before returning.
*/
int sqlite3GetToken(const unsigned char* z, int* tokenType) {
  int i, c;
  switch (aiClass[*z]) { /* Switch on the character-class of the first byte
                         ** of the token. See the comment on the CC_ defines
                         ** above. */
    case CC_SPACE: {
      testcase(z[0] == ' ');
      testcase(z[0] == '\t');
      testcase(z[0] == '\n');
      testcase(z[0] == '\f');
      testcase(z[0] == '\r');
      for (i = 1; sqlite3Isspace(z[i]); i++) {
      }
      *tokenType = TK_SPACE;
      return i;
    }
    case CC_MINUS: {
      if (z[1] == '-') {
        for (i = 2; (c = z[i]) != 0 && c != '\n'; i++) {
        }
        *tokenType = TK_SPACE; /* IMP: R-22934-25134 */
        return i;
      } else if (z[1] == '>') {
        *tokenType = TK_PTR;
        return 2 + (z[2] == '>');
      }
      *tokenType = TK_MINUS;
      return 1;
    }
    case CC_LP: {
      *tokenType = TK_LP;
      return 1;
    }
    case CC_RP: {
      *tokenType = TK_RP;
      return 1;
    }
    case CC_SEMI: {
      *tokenType = TK_SEMI;
      return 1;
    }
    case CC_PLUS: {
      *tokenType = TK_PLUS;
      return 1;
    }
    case CC_STAR: {
      *tokenType = TK_STAR;
      return 1;
    }
    case CC_SLASH: {
      if (z[1] != '*' || z[2] == 0) {
        *tokenType = TK_SLASH;
        return 1;
      }
      for (i = 3, c = z[2]; (c != '*' || z[i] != '/') && (c = z[i]) != 0; i++) {
      }
      if (c)
        i++;
      *tokenType = TK_SPACE; /* IMP: R-22934-25134 */
      return i;
    }
    case CC_PERCENT: {
      *tokenType = TK_REM;
      return 1;
    }
    case CC_EQ: {
      *tokenType = TK_EQ;
      return 1 + (z[1] == '=');
    }
    case CC_LT: {
      if ((c = z[1]) == '=') {
        *tokenType = TK_LE;
        return 2;
      } else if (c == '>') {
        *tokenType = TK_NE;
        return 2;
      } else if (c == '<') {
        *tokenType = TK_LSHIFT;
        return 2;
      } else {
        *tokenType = TK_LT;
        return 1;
      }
    }
    case CC_GT: {
      if ((c = z[1]) == '=') {
        *tokenType = TK_GE;
        return 2;
      } else if (c == '>') {
        *tokenType = TK_RSHIFT;
        return 2;
      } else {
        *tokenType = TK_GT;
        return 1;
      }
    }
    case CC_BANG: {
      if (z[1] != '=') {
        *tokenType = TK_ILLEGAL;
        return 1;
      } else {
        *tokenType = TK_NE;
        return 2;
      }
    }
    case CC_PIPE: {
      if (z[1] != '|') {
        *tokenType = TK_BITOR;
        return 1;
      } else {
        *tokenType = TK_CONCAT;
        return 2;
      }
    }
    case CC_COMMA: {
      *tokenType = TK_COMMA;
      return 1;
    }
    case CC_AND: {
      *tokenType = TK_BITAND;
      return 1;
    }
    case CC_TILDA: {
      *tokenType = TK_BITNOT;
      return 1;
    }
    case CC_QUOTE: {
      int delim = z[0];
      testcase(delim == '`');
      testcase(delim == '\'');
      testcase(delim == '"');
      for (i = 1; (c = z[i]) != 0; i++) {
        if (c == delim) {
          if (z[i + 1] == delim) {
            i++;
          } else {
            break;
          }
        }
      }
      if (c == '\'') {
        *tokenType = TK_STRING;
        return i + 1;
      } else if (c != 0) {
        *tokenType = TK_ID;
        return i + 1;
      } else {
        *tokenType = TK_ILLEGAL;
        return i;
      }
    }
    case CC_DOT: {
#ifndef SQLITE_OMIT_FLOATING_POINT
      if (!sqlite3Isdigit(z[1]))
#endif
      {
        *tokenType = TK_DOT;
        return 1;
      }
      /* If the next character is a digit, this is a floating point
      ** number that begins with ".".  Fall thru into the next case */
      /* no break */ deliberate_fall_through
    }
    case CC_DIGIT: {
      testcase(z[0] == '0');
      testcase(z[0] == '1');
      testcase(z[0] == '2');
      testcase(z[0] == '3');
      testcase(z[0] == '4');
      testcase(z[0] == '5');
      testcase(z[0] == '6');
      testcase(z[0] == '7');
      testcase(z[0] == '8');
      testcase(z[0] == '9');
      testcase(z[0] == '.');
      *tokenType = TK_INTEGER;
#ifndef SQLITE_OMIT_HEX_INTEGER
      if (z[0] == '0' && (z[1] == 'x' || z[1] == 'X') &&
          sqlite3Isxdigit(z[2])) {
        for (i = 3; sqlite3Isxdigit(z[i]); i++) {
        }
        return i;
      }
#endif
      for (i = 0; sqlite3Isdigit(z[i]); i++) {
      }
#ifndef SQLITE_OMIT_FLOATING_POINT
      if (z[i] == '.') {
        i++;
        while (sqlite3Isdigit(z[i])) {
          i++;
        }
        *tokenType = TK_FLOAT;
      }
      if ((z[i] == 'e' || z[i] == 'E') &&
          (sqlite3Isdigit(z[i + 1]) || ((z[i + 1] == '+' || z[i + 1] == '-') &&
                                        sqlite3Isdigit(z[i + 2])))) {
        i += 2;
        while (sqlite3Isdigit(z[i])) {
          i++;
        }
        *tokenType = TK_FLOAT;
      }
#endif
      while (IdChar(z[i])) {
        *tokenType = TK_ILLEGAL;
        i++;
      }
      return i;
    }
    case CC_QUOTE2: {
      for (i = 1, c = z[0]; c != ']' && (c = z[i]) != 0; i++) {
      }
      *tokenType = c == ']' ? TK_ID : TK_ILLEGAL;
      return i;
    }
    case CC_VARNUM: {
      *tokenType = TK_VARIABLE;
      for (i = 1; sqlite3Isdigit(z[i]); i++) {
      }
      return i;
    }
    case CC_DOLLAR:
    case CC_VARALPHA: {
      int n = 0;
      testcase(z[0] == '$');
      testcase(z[0] == '@');
      testcase(z[0] == ':');
      testcase(z[0] == '#');
      *tokenType = TK_VARIABLE;
      for (i = 1; (c = z[i]) != 0; i++) {
        if (IdChar(c)) {
          n++;
#ifndef SQLITE_OMIT_TCL_VARIABLE
        } else if (c == '(' && n > 0) {
          do {
            i++;
          } while ((c = z[i]) != 0 && !sqlite3Isspace(c) && c != ')');
          if (c == ')') {
            i++;
          } else {
            *tokenType = TK_ILLEGAL;
          }
          break;
        } else if (c == ':' && z[i + 1] == ':') {
          i++;
#endif
        } else {
          break;
        }
      }
      if (n == 0)
        *tokenType = TK_ILLEGAL;
      return i;
    }
    case CC_KYWD0: {
      if (aiClass[z[1]] > CC_KYWD) {
        i = 1;
        break;
      }
      for (i = 2; aiClass[z[i]] <= CC_KYWD; i++) {
      }
      if (IdChar(z[i])) {
        /* This token started out using characters that can appear in keywords,
        ** but z[i] is a character not allowed within keywords, so this must
        ** be an identifier instead */
        i++;
        break;
      }
      *tokenType = TK_ID;
      return keywordCode((char*)z, i, tokenType);
    }
    case CC_X: {
#ifndef SQLITE_OMIT_BLOB_LITERAL
      testcase(z[0] == 'x');
      testcase(z[0] == 'X');
      if (z[1] == '\'') {
        *tokenType = TK_BLOB;
        for (i = 2; sqlite3Isxdigit(z[i]); i++) {
        }
        if (z[i] != '\'' || i % 2) {
          *tokenType = TK_ILLEGAL;
          while (z[i] && z[i] != '\'') {
            i++;
          }
        }
        if (z[i])
          i++;
        return i;
      }
#endif
      /* If it is not a BLOB literal, then it must be an ID, since no
      ** SQL keywords start with the letter 'x'.  Fall through */
      /* no break */ deliberate_fall_through
    }
    case CC_KYWD:
    case CC_ID: {
      i = 1;
      break;
    }
    case CC_BOM: {
      if (z[1] == 0xbb && z[2] == 0xbf) {
        *tokenType = TK_SPACE;
        return 3;
      }
      i = 1;
      break;
    }
    case CC_NUL: {
      *tokenType = TK_ILLEGAL;
      return 0;
    }
    default: {
      *tokenType = TK_ILLEGAL;
      return 1;
    }
  }
  while (IdChar(z[i])) {
    i++;
  }
  *tokenType = TK_ID;
  return i;
}
