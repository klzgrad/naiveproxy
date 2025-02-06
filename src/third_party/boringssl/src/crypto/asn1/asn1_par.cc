/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1.h>


const char *ASN1_tag2str(int tag) {
  static const char *const tag2str[] = {
      "EOC",
      "BOOLEAN",
      "INTEGER",
      "BIT STRING",
      "OCTET STRING",
      "NULL",
      "OBJECT",
      "OBJECT DESCRIPTOR",
      "EXTERNAL",
      "REAL",
      "ENUMERATED",
      "<ASN1 11>",
      "UTF8STRING",
      "<ASN1 13>",
      "<ASN1 14>",
      "<ASN1 15>",
      "SEQUENCE",
      "SET",
      "NUMERICSTRING",
      "PRINTABLESTRING",
      "T61STRING",
      "VIDEOTEXSTRING",
      "IA5STRING",
      "UTCTIME",
      "GENERALIZEDTIME",
      "GRAPHICSTRING",
      "VISIBLESTRING",
      "GENERALSTRING",
      "UNIVERSALSTRING",
      "<ASN1 29>",
      "BMPSTRING",
  };

  if ((tag == V_ASN1_NEG_INTEGER) || (tag == V_ASN1_NEG_ENUMERATED)) {
    tag &= ~V_ASN1_NEG;
  }

  if (tag < 0 || tag > 30) {
    return "(unknown)";
  }
  return tag2str[tag];
}
