/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL project
 * 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com). */

#include <assert.h>
#include <limits.h>

#include <openssl/err.h>
#include <openssl/mem.h>
#include <openssl/obj.h>
#include <openssl/x509.h>

#include "../internal.h"
#include "internal.h"


static int trust_1oidany(const X509_TRUST *trust, X509 *x, int flags);
static int trust_compat(const X509_TRUST *trust, X509 *x, int flags);

static int obj_trust(int id, X509 *x, int flags);

static const X509_TRUST trstandard[] = {
    {X509_TRUST_COMPAT, 0, trust_compat, (char *)"compatible", 0, NULL},
    {X509_TRUST_SSL_CLIENT, 0, trust_1oidany, (char *)"SSL Client",
     NID_client_auth, NULL},
    {X509_TRUST_SSL_SERVER, 0, trust_1oidany, (char *)"SSL Server",
     NID_server_auth, NULL},
    {X509_TRUST_EMAIL, 0, trust_1oidany, (char *)"S/MIME email",
     NID_email_protect, NULL},
    {X509_TRUST_OBJECT_SIGN, 0, trust_1oidany, (char *)"Object Signer",
     NID_code_sign, NULL},
    {X509_TRUST_TSA, 0, trust_1oidany, (char *)"TSA server", NID_time_stamp,
     NULL}};

int X509_check_trust(X509 *x, int id, int flags) {
  int idx;
  if (id == -1) {
    return 1;
  }
  // We get this as a default value
  if (id == 0) {
    int rv;
    rv = obj_trust(NID_anyExtendedKeyUsage, x, 0);
    if (rv != X509_TRUST_UNTRUSTED) {
      return rv;
    }
    return trust_compat(NULL, x, 0);
  }
  idx = X509_TRUST_get_by_id(id);
  if (idx == -1) {
    return obj_trust(id, x, flags);
  }
  const X509_TRUST *pt = X509_TRUST_get0(idx);
  return pt->check_trust(pt, x, flags);
}

int X509_TRUST_get_count(void) { return OPENSSL_ARRAY_SIZE(trstandard); }

const X509_TRUST *X509_TRUST_get0(int idx) {
  if (idx < 0 || (size_t)idx >= OPENSSL_ARRAY_SIZE(trstandard)) {
    return NULL;
  }
  return trstandard + idx;
}

int X509_TRUST_get_by_id(int id) {
  for (size_t i = 0; i < OPENSSL_ARRAY_SIZE(trstandard); i++) {
    if (trstandard[i].trust == id) {
      static_assert(OPENSSL_ARRAY_SIZE(trstandard) <= INT_MAX,
                    "indices must fit in int");
      return (int)i;
    }
  }
  return -1;
}

int X509_TRUST_set(int *t, int trust) {
  if (X509_TRUST_get_by_id(trust) == -1) {
    OPENSSL_PUT_ERROR(X509, X509_R_INVALID_TRUST);
    return 0;
  }
  *t = trust;
  return 1;
}

int X509_TRUST_get_flags(const X509_TRUST *xp) { return xp->flags; }

char *X509_TRUST_get0_name(const X509_TRUST *xp) { return xp->name; }

int X509_TRUST_get_trust(const X509_TRUST *xp) { return xp->trust; }

static int trust_1oidany(const X509_TRUST *trust, X509 *x, int flags) {
  if (x->aux && (x->aux->trust || x->aux->reject)) {
    return obj_trust(trust->arg1, x, flags);
  }
  // we don't have any trust settings: for compatibility we return trusted
  // if it is self signed
  return trust_compat(trust, x, flags);
}

static int trust_compat(const X509_TRUST *trust, X509 *x, int flags) {
  if (!x509v3_cache_extensions(x)) {
    return X509_TRUST_UNTRUSTED;
  }
  if (x->ex_flags & EXFLAG_SS) {
    return X509_TRUST_TRUSTED;
  } else {
    return X509_TRUST_UNTRUSTED;
  }
}

static int obj_trust(int id, X509 *x, int flags) {
  ASN1_OBJECT *obj;
  size_t i;
  X509_CERT_AUX *ax;
  ax = x->aux;
  if (!ax) {
    return X509_TRUST_UNTRUSTED;
  }
  if (ax->reject) {
    for (i = 0; i < sk_ASN1_OBJECT_num(ax->reject); i++) {
      obj = sk_ASN1_OBJECT_value(ax->reject, i);
      if (OBJ_obj2nid(obj) == id) {
        return X509_TRUST_REJECTED;
      }
    }
  }
  if (ax->trust) {
    for (i = 0; i < sk_ASN1_OBJECT_num(ax->trust); i++) {
      obj = sk_ASN1_OBJECT_value(ax->trust, i);
      if (OBJ_obj2nid(obj) == id) {
        return X509_TRUST_TRUSTED;
      }
    }
  }
  return X509_TRUST_UNTRUSTED;
}
