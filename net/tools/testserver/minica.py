# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import asn1
import datetime
import hashlib
import itertools
import os
import time

GENERALIZED_TIME_FORMAT = "%Y%m%d%H%M%SZ"

OCSP_STATE_GOOD = 1
OCSP_STATE_REVOKED = 2
OCSP_STATE_INVALID_RESPONSE = 3
OCSP_STATE_UNAUTHORIZED = 4
OCSP_STATE_UNKNOWN = 5
OCSP_STATE_TRY_LATER = 6
OCSP_STATE_INVALID_RESPONSE_DATA = 7
OCSP_STATE_MISMATCHED_SERIAL = 8

OCSP_DATE_VALID = 1
OCSP_DATE_OLD = 2
OCSP_DATE_EARLY = 3
OCSP_DATE_LONG = 4

OCSP_PRODUCED_VALID = 1
OCSP_PRODUCED_BEFORE_CERT = 2
OCSP_PRODUCED_AFTER_CERT = 3

# This file implements very minimal certificate and OCSP generation. It's
# designed to test revocation checking.

def RandomNumber(length_in_bytes):
  '''RandomNumber returns a random number of length 8*|length_in_bytes| bits'''
  rand = os.urandom(length_in_bytes)
  n = 0
  for x in rand:
    n <<= 8
    n |= ord(x)
  return n


def ModExp(n, e, p):
  '''ModExp returns n^e mod p'''
  r = 1
  while e != 0:
    if e & 1:
      r = (r*n) % p
    e >>= 1
    n = (n*n) % p
  return r

# PKCS1v15_SHA256_PREFIX is the ASN.1 prefix for a SHA256 signature.
PKCS1v15_SHA256_PREFIX = '3031300d060960864801650304020105000420'.decode('hex')

class RSA(object):
  def __init__(self, modulus, e, d):
    self.m = modulus
    self.e = e
    self.d = d

    self.modlen = 0
    m = modulus
    while m != 0:
      self.modlen += 1
      m >>= 8

  def Sign(self, message):
    digest = hashlib.sha256(message).digest()
    prefix = PKCS1v15_SHA256_PREFIX

    em = ['\xff'] * (self.modlen - 1 - len(prefix) - len(digest))
    em[0] = '\x00'
    em[1] = '\x01'
    em += "\x00" + prefix + digest

    n = 0
    for x in em:
      n <<= 8
      n |= ord(x)

    s = ModExp(n, self.d, self.m)
    out = []
    while s != 0:
      out.append(s & 0xff)
      s >>= 8
    out.reverse()
    return '\x00' * (self.modlen - len(out)) + asn1.ToBytes(out)

  def ToDER(self):
    return asn1.ToDER(asn1.SEQUENCE([self.m, self.e]))


def Name(cn = None, c = None, o = None):
  names = asn1.SEQUENCE([])

  if cn is not None:
    names.children.append(
      asn1.SET([
        asn1.SEQUENCE([
          COMMON_NAME, cn,
        ])
      ])
    )

  if c is not None:
    names.children.append(
      asn1.SET([
        asn1.SEQUENCE([
          COUNTRY, c,
        ])
      ])
    )

  if o is not None:
    names.children.append(
      asn1.SET([
        asn1.SEQUENCE([
          ORGANIZATION, o,
        ])
      ])
    )

  return names


# The private key and root certificate name are hard coded here:

# This is the root private key
ROOT_KEY = RSA(0x00c1541fac63d3b969aa231a02cb2e0d9ee7b26724f136c121b2c28bdae5caa87733cc407ad83842ef20ec67d941b448a1ce3557cf5ddebf3c9bde8f36f253ee73e670d1c4c6631d1ddc0e39cbde09b833f66347ea379c3fa891d61a0ca005b38b0b2cad1058e3589c9f30600be81e4ff4ac220972c17b74f92f03d72b496f643543d0b27a5227f1efee13c138888b23cb101877b3b4dc091f0b3bb6fc3c792187b05ab38e97862f8af6156bcbfbb824385132c6741e6c65cfcd5f13142421a210b95185884c4866f3ea644dfb8006133d14e72a4704f3e700cf827ca5ffd2ef74c2ab6a5259ffff40f0f7f607891388f917fc9fc9e65742df1bfa0b322140bb65,
              65537,
              0x00980f2db66ef249e4954074a5fbdf663135363a3071554ac4d19079661bd5b179c890ffaa5fc4a8c8e3116e81104fd7cd049f2a48dd2165332bb9fad511f6f817cb09b3c45cf1fa25d13e9331099c8578c173c74dae9dc3e83784ba0a7216e9e8144af8786221b741c167d033ad47a245e4da04aa710a44aff5cdc480b48adbba3575d1315555690f081f9f69691e801e34c21240bcd3df9573ec5f9aa290c5ed19404fb911ab28b7680e0be086487273db72da6621f24d8c66197a5f1b7687efe1d9e3b6655af2891d4540482e1246ff5f62ce61b8b5dcb2c66ade6bb41e0bf071445fb8544aa0a489780f770a6f1031ee19347641794f4ad17354d579a9d061)

# Root certificate CN
ROOT_CN = "Testing CA"

# All certificates are issued under this policy OID, in the Google arc:
CERT_POLICY_OID = asn1.OID([1, 3, 6, 1, 4, 1, 11129, 2, 4, 1])

# These result in the following root certificate:
# -----BEGIN CERTIFICATE-----
# MIIC1DCCAbygAwIBAgIBATANBgkqhkiG9w0BAQsFADAVMRMwEQYDVQQDEwpUZXN0
# aW5nIENBMB4XDTEwMDEwMTA2MDAwMFoXDTMyMTIwMTA2MDAwMFowFTETMBEGA1UE
# AxMKVGVzdGluZyBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMFU
# H6xj07lpqiMaAssuDZ7nsmck8TbBIbLCi9rlyqh3M8xAetg4Qu8g7GfZQbRIoc41
# V89d3r88m96PNvJT7nPmcNHExmMdHdwOOcveCbgz9mNH6jecP6iR1hoMoAWziwss
# rRBY41icnzBgC+geT/SsIglywXt0+S8D1ytJb2Q1Q9CyelIn8e/uE8E4iIsjyxAY
# d7O03AkfCzu2/Dx5IYewWrOOl4YvivYVa8v7uCQ4UTLGdB5sZc/NXxMUJCGiELlR
# hYhMSGbz6mRN+4AGEz0U5ypHBPPnAM+CfKX/0u90wqtqUln//0Dw9/YHiROI+Rf8
# n8nmV0LfG/oLMiFAu2UCAwEAAaMvMC0wEgYDVR0TAQH/BAgwBgEB/wIBATAXBgNV
# HSAEEDAOMAwGCisGAQQB1nkCBAEwDQYJKoZIhvcNAQELBQADggEBADNrvoAyqAVm
# bydPBBfLRqyH4DXt2vuMVmnSdnWnOxYiEezGmNSNiO1k1ZFBwVSsd+JHrT24lax9
# kvU1yQDW//PBu3ijfZOCaIUleQiGXHMGfV4MjzgYbxpvHOvEUC6IXmYCsIEwcZgK
# lrwnfJQ3MVU4hOgGTlOTWYPtCwvTsBObNRLdIs+ifMQiWmzPBlM8XeX4e5acDjTb
# emcN4szU3EcgmCA0LvBIRI4F6NWpaIJl2WnLyMUDyKq4vjpRJOZkNwAC+525duDr
# JFE4PKR2Lh53nJQIJv6mcTZQkX1mmw0yzqWxcGCoHACma3TgSwOHryvSopL+t26+
# ZlQvP2ygwqY=
# -----END CERTIFICATE-----

# If you update any of the above, you can generate a new root by running this
# file as a script.

INTERMEDIATE_KEY = RSA(0x00c661afcc659f88855a83ade8fb792dc13d0cf388b17bece9149cf0b8556d27b19101d081fb2a842d13a2ac95d8308ddd66783843ecc5806513959eb6b30dd69b2845d97e10d0bbbf653d686dc8828935022cc96f9e030b567157257d3d6526734080bb9727cee0d30f4209d5820e1d662f358fc789c0e9366d84f89adf1beb8d843f74e6f325876ac35d5c11691fcb296967c06edf69450c16bb2314c14599fe90725d5ec90f2db6698afae72bba0cfbf77967c7e8b49f2172f9381827c27ab7f9471c62bd8da4a6c657966ec1385cf41d739449835888f30d64971619dcd380408cd74f25c3be19833a92620c9cf710da67e15ac8cef69bc7e4e5e7f813c1ed,
                       65537,
                       0x77c5e2edf52d2cafd6c649e9b06aa9455226cfa26805fa337f4e81c7c94bedfb3721715208e2d28aa4a042b2f5a3db03212ad44dae564ffeb6a44efedf7c2b65e21aca056301a3591b36c82600394fbdc16268fc0adaabadb5207871f4ef6d17888a30b84240955cd889768681cf23d0de0fe88f008c8841643e341acd397e2d1104a23242e566088b7617c26ae8b48a85b6c9b7dc64ef1fa5e9b124ff8c1659a82d8225f28a820cc6ca07beff0354364c631a9142309fea1d8b054f6e00e23c54b493a21fcbe89a646b39d1acba5bc2ace9bba0252671d42a15202f3afccc912114d6c20eb3131e74289f2c744c5b39e7d3780fe21402ab1c3ae65854fee401)

# Intermediate certificate CN
INTERMEDIATE_CN = "Testing Intermediate CA"

LEAF_KEY = RSA(0x00cd12d317b39cfbb160fb1dc9c9f0dc8fef3604dda4d8c557392ce1d616483713f78216cadbefd1c76ea0f3bbbe410e24b233b1b73583922b09314e249b2cfde1be0995e13f160fb630c10d447750da20ffaa4880006717feaa3e4db602e4f511b5cc312f770f44b037784effec62640f948aa189c3769f03bdd0e22a36ecfa5951f5577de195a4fba33c879b657968b79138fd7ab389a9968522f7389c6052be1ff78bc168d3ea961e132a044eba33ac07ead95367c7b815e91eca924d914fd0d811349b8bf500707ba71a43a2901a545f34e1792e72654f6649fab9716f4ba17379ee8042186bbba9b9bac416a60474cc60686f0e6e4b01259cc3cb5873edf9,
               65537,
               0x009c23e81bd4c30314743dded9646b82d408937db2f0afa7d9988be6cba59d886a287aa13605ad9c7117776efc94885de76cd3554da46e301d9a5b331f4613449edb9ddac36cd0345848d8c46c4bd880acbd5cfee48ee9efe813e16a33da124fd213348c8292494ac84d03ca4aabc5e25fc67ea32e0c6845fc884b01d8988768b8b931c41de49708dbcd5fcb61823f9a1f7507c6f364be4cb5a8cf24af4925997030dd4f67a0c9c6813401cc8b2f5d1971ee0022770239b7042fde8228c33942e9c0a0b18854cb1b5542be928338ab33ac936bbba174e55457007b16f36011dbb8f4258abe64e42b1cfa79803d30170b7ecf3e7c595d42003fff72591e07acd9cd)

LEAF_KEY_PEM = '''-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEAzRLTF7Oc+7Fg+x3JyfDcj+82BN2k2MVXOSzh1hZINxP3ghbK
2+/Rx26g87u+QQ4ksjOxtzWDkisJMU4kmyz94b4JleE/Fg+2MMENRHdQ2iD/qkiA
AGcX/qo+TbYC5PURtcwxL3cPRLA3eE7/7GJkD5SKoYnDdp8DvdDiKjbs+llR9Vd9
4ZWk+6M8h5tleWi3kTj9erOJqZaFIvc4nGBSvh/3i8Fo0+qWHhMqBE66M6wH6tlT
Z8e4FekeypJNkU/Q2BE0m4v1AHB7pxpDopAaVF804XkucmVPZkn6uXFvS6Fzee6A
Qhhru6m5usQWpgR0zGBobw5uSwElnMPLWHPt+QIDAQABAoIBAQCcI+gb1MMDFHQ9
3tlka4LUCJN9svCvp9mYi+bLpZ2Iaih6oTYFrZxxF3du/JSIXeds01VNpG4wHZpb
Mx9GE0Se253aw2zQNFhI2MRsS9iArL1c/uSO6e/oE+FqM9oST9ITNIyCkklKyE0D
ykqrxeJfxn6jLgxoRfyISwHYmIdouLkxxB3klwjbzV/LYYI/mh91B8bzZL5MtajP
JK9JJZlwMN1PZ6DJxoE0AcyLL10Zce4AIncCObcEL96CKMM5QunAoLGIVMsbVUK+
koM4qzOsk2u7oXTlVFcAexbzYBHbuPQlir5k5Csc+nmAPTAXC37PPnxZXUIAP/9y
WR4HrNnNAoGBAPmOqTe7ntto6rDEsU1cKOJFKIZ7UVcSByyz8aLrvj1Rb2mkrNJU
SdTqJvtqrvDXgO0HuGtFOzsZrRV9+XRPd2P0mP0uhfRiYGWT8hnILGyI2+7zlC/w
HDtLEefelhtdOVKgUaLQXptSn7aGalUHghZKWjRNT5ah+U85MoI2ZkDbAoGBANJe
KvrBBPSFLj+x2rsMhG+ksK0I6tivapVvSTtDV3ME1DvA/4BIMV/nIZyoH4AYI72c
m/vD66+eCqh75cq5BzbVD63tR+ZRi/VdT1HJcl2IFXynk6eaBw8v7gpQyx6t3iSK
lx/dIdpLt1BQuR4qI6x1wYp7Utn98soEkiFXzgq7AoGBAJTLBYPQXvgNBxlcPSaV
016Nw4rjTe0vN43kwCbWjjf7LQV9BPnm/Zpv/cwboLDCnQE2gDOdNKKZPYS59pjt
pI65UNpr+bxrR3RpEIlku2/+7br8ChfG/t4vdT6djTxFih8ErYf42t+bFNT8Mbv+
3QYzULMsgU6bxo0A2meezbrPAoGBAK/IxmtQXP6iRxosWRUSCZxs5sFAgVVdh1el
bXEa/Xj8IQhpZlbgfHmh3oFULzZPdZYcxm7jsQ7HpipRlZwHbtLPyNFSRFFd9PCr
7vrttSYY77OBKC3V1G5JY8S07HYPXV/1ewDCPGZ3/I8dVQKyvap/n6FDGeFUhctv
dFhuUZq/AoGAWLXlbcIl1cvOhfFJ5owohJhzh9oW9tlCtjV5/dlix2RaE5CtDZWS
oMm4sQu9HiA8jLDP1MEEMRFPrPXdrZnxnSqVd1DgabSegD1/ZCb1QlWwQWkk5QU+
wotPOMI33L50kZqUaDP+1XSL0Dyfo/pYpm4tYy/5QmP6WKXCtFUXybI=
-----END RSA PRIVATE KEY-----
'''

# Various OIDs

AIA_OCSP = asn1.OID([1, 3, 6, 1, 5, 5, 7, 48, 1])
AIA_CA_ISSUERS = asn1.OID([1, 3, 6, 1, 5, 5, 7, 48, 2])
AUTHORITY_INFORMATION_ACCESS = asn1.OID([1, 3, 6, 1, 5, 5, 7, 1, 1])
BASIC_CONSTRAINTS = asn1.OID([2, 5, 29, 19])
CERT_POLICIES = asn1.OID([2, 5, 29, 32])
COMMON_NAME = asn1.OID([2, 5, 4, 3])
COUNTRY = asn1.OID([2, 5, 4, 6])
HASH_SHA1 = asn1.OID([1, 3, 14, 3, 2, 26])
OCSP_TYPE_BASIC = asn1.OID([1, 3, 6, 1, 5, 5, 7, 48, 1, 1])
ORGANIZATION = asn1.OID([2, 5, 4, 10])
PUBLIC_KEY_RSA = asn1.OID([1, 2, 840, 113549, 1, 1, 1])
SHA256_WITH_RSA_ENCRYPTION = asn1.OID([1, 2, 840, 113549, 1, 1, 11])
SUBJECT_ALTERNATIVE_NAME = asn1.OID([2, 5, 29, 17])

def MakeCertificate(
    issuer_cn, subject_cn, serial, pubkey, privkey, ocsp_url = None,
    ca_issuers_url = None, is_ca=False, path_len=None, ip_sans=None,
    dns_sans=None):
  '''MakeCertificate returns a DER encoded certificate, signed by privkey.'''
  extensions = asn1.SEQUENCE([])

  # Default subject name fields
  c = "XX"
  o = "Testing Org"

  if is_ca:
    # Root certificate.
    c = None
    o = None
    extensions.children.append(
      asn1.SEQUENCE([
        BASIC_CONSTRAINTS,
        True,
        asn1.OCTETSTRING(asn1.ToDER(asn1.SEQUENCE([
            True, # IsCA
        ] + ([path_len] if path_len is not None else []) # Path len
        ))),
      ]))

  if ip_sans is not None or dns_sans is not None:
    sans = []
    if dns_sans is not None:
      for dns_name in dns_sans:
        sans.append(
          asn1.Raw(asn1.TagAndLength(0x82, len(dns_name)) + dns_name))
    if ip_sans is not None:
      for ip_addr in ip_sans:
        sans.append(
          asn1.Raw(asn1.TagAndLength(0x87, len(ip_addr)) + ip_addr))
    extensions.children.append(
      asn1.SEQUENCE([
        SUBJECT_ALTERNATIVE_NAME,
        # There is implicitly a critical=False here. Since false is the
        # default, encoding the value would be invalid DER.
        asn1.OCTETSTRING(asn1.ToDER(asn1.SEQUENCE(sans)))
      ]))

  if ocsp_url is not None or ca_issuers_url is not None:
    aia_entries = []
    if ocsp_url is not None:
      aia_entries.append(
          asn1.SEQUENCE([
            AIA_OCSP,
            asn1.Raw(asn1.TagAndLength(0x86, len(ocsp_url)) + ocsp_url),
          ]))
    if ca_issuers_url is not None:
      aia_entries.append(
          asn1.SEQUENCE([
            AIA_CA_ISSUERS,
            asn1.Raw(asn1.TagAndLength(0x86,
                                       len(ca_issuers_url)) + ca_issuers_url),
            ]))
    extensions.children.append(
      asn1.SEQUENCE([
        AUTHORITY_INFORMATION_ACCESS,
        # There is implicitly a critical=False here. Since false is the default,
        # encoding the value would be invalid DER.
        asn1.OCTETSTRING(asn1.ToDER(asn1.SEQUENCE(aia_entries))),
        ]))

  extensions.children.append(
    asn1.SEQUENCE([
      CERT_POLICIES,
      # There is implicitly a critical=False here. Since false is the default,
      # encoding the value would be invalid DER.
      asn1.OCTETSTRING(asn1.ToDER(asn1.SEQUENCE([
        asn1.SEQUENCE([ # PolicyInformation
          CERT_POLICY_OID,
        ]),
      ]))),
    ])
  )

  tbsCert = asn1.ToDER(asn1.SEQUENCE([
      asn1.Explicit(0, 2), # Version
      serial,
      asn1.SEQUENCE([SHA256_WITH_RSA_ENCRYPTION, None]), # SignatureAlgorithm
      Name(cn = issuer_cn), # Issuer
      asn1.SEQUENCE([ # Validity
        asn1.UTCTime("100101060000Z"), # NotBefore
        asn1.UTCTime("321201060000Z"), # NotAfter
      ]),
      Name(cn = subject_cn, c = c, o = o), # Subject
      asn1.SEQUENCE([ # SubjectPublicKeyInfo
        asn1.SEQUENCE([ # Algorithm
          PUBLIC_KEY_RSA,
          None,
        ]),
        asn1.BitString(asn1.ToDER(pubkey)),
      ]),
      asn1.Explicit(3, extensions),
    ]))

  return asn1.ToDER(asn1.SEQUENCE([
    asn1.Raw(tbsCert),
    asn1.SEQUENCE([
      SHA256_WITH_RSA_ENCRYPTION,
      None,
    ]),
    asn1.BitString(privkey.Sign(tbsCert)),
  ]))

def MakeOCSPSingleResponse(
    issuer_name_hash, issuer_key_hash, serial, ocsp_state, ocsp_date):
  cert_status = None
  if ocsp_state == OCSP_STATE_REVOKED:
    cert_status = asn1.Explicit(1, asn1.GeneralizedTime("20100101060000Z"))
  elif ocsp_state == OCSP_STATE_UNKNOWN:
    cert_status = asn1.Raw(asn1.TagAndLength(0x80 | 2, 0))
  elif ocsp_state == OCSP_STATE_GOOD:
    cert_status = asn1.Raw(asn1.TagAndLength(0x80 | 0, 0))
  elif ocsp_state == OCSP_STATE_MISMATCHED_SERIAL:
    cert_status = asn1.Raw(asn1.TagAndLength(0x80 | 0, 0))
    serial -= 1
  else:
    raise ValueError('Bad OCSP state: ' + str(ocsp_state))

  now = datetime.datetime.fromtimestamp(time.mktime(time.gmtime()))
  if ocsp_date == OCSP_DATE_VALID:
    thisUpdate = now - datetime.timedelta(days=1)
    nextUpdate = thisUpdate + datetime.timedelta(weeks=1)
  elif ocsp_date == OCSP_DATE_OLD:
    thisUpdate = now - datetime.timedelta(days=1, weeks=1)
    nextUpdate = thisUpdate + datetime.timedelta(weeks=1)
  elif ocsp_date == OCSP_DATE_EARLY:
    thisUpdate = now + datetime.timedelta(days=1)
    nextUpdate = thisUpdate + datetime.timedelta(weeks=1)
  elif ocsp_date == OCSP_DATE_LONG:
    thisUpdate = now - datetime.timedelta(days=365)
    nextUpdate = thisUpdate + datetime.timedelta(days=366)
  else:
    raise ValueError('Bad OCSP date: ' + str(ocsp_date))

  return asn1.SEQUENCE([ # SingleResponse
    asn1.SEQUENCE([ # CertID
      asn1.SEQUENCE([ # hashAlgorithm
        HASH_SHA1,
        None,
      ]),
      issuer_name_hash,
      issuer_key_hash,
      serial,
    ]),
    cert_status,
    asn1.GeneralizedTime( # thisUpdate
      thisUpdate.strftime(GENERALIZED_TIME_FORMAT)
    ),
    asn1.Explicit( # nextUpdate
      0,
      asn1.GeneralizedTime(nextUpdate.strftime(GENERALIZED_TIME_FORMAT))
    ),
  ])

def MakeOCSPResponse(
    issuer_cn, issuer_key, serial, ocsp_states, ocsp_dates, ocsp_produced):
  # https://tools.ietf.org/html/rfc2560
  issuer_name_hash = asn1.OCTETSTRING(
      hashlib.sha1(asn1.ToDER(Name(cn = issuer_cn))).digest())

  issuer_key_hash = asn1.OCTETSTRING(
      hashlib.sha1(asn1.ToDER(issuer_key)).digest())

  now = datetime.datetime.fromtimestamp(time.mktime(time.gmtime()))
  if ocsp_produced == OCSP_PRODUCED_VALID:
    producedAt = now - datetime.timedelta(days=1)
  elif ocsp_produced == OCSP_PRODUCED_BEFORE_CERT:
    producedAt = datetime.datetime.strptime(
        "19100101050000Z", GENERALIZED_TIME_FORMAT)
  elif ocsp_produced == OCSP_PRODUCED_AFTER_CERT:
    producedAt = datetime.datetime.strptime(
        "20321201070000Z", GENERALIZED_TIME_FORMAT)
  else:
    raise ValueError('Bad OCSP produced: ' + str(ocsp_produced))

  single_responses = [
      MakeOCSPSingleResponse(issuer_name_hash, issuer_key_hash, serial,
          ocsp_state, ocsp_date)
      for ocsp_state, ocsp_date in itertools.izip(ocsp_states, ocsp_dates)
  ]

  basic_resp_data_der = asn1.ToDER(asn1.SEQUENCE([
    asn1.Explicit(2, issuer_key_hash),
    asn1.GeneralizedTime(producedAt.strftime(GENERALIZED_TIME_FORMAT)),
    asn1.SEQUENCE(single_responses),
  ]))

  basic_resp = asn1.SEQUENCE([
    asn1.Raw(basic_resp_data_der),
    asn1.SEQUENCE([
      SHA256_WITH_RSA_ENCRYPTION,
      None,
    ]),
    asn1.BitString(issuer_key.Sign(basic_resp_data_der)),
  ])

  resp = asn1.SEQUENCE([
    asn1.ENUMERATED(0),
    asn1.Explicit(0, asn1.SEQUENCE([
      OCSP_TYPE_BASIC,
      asn1.OCTETSTRING(asn1.ToDER(basic_resp)),
    ]))
  ])

  return asn1.ToDER(resp)


def DERToPEM(der):
  pem = '-----BEGIN CERTIFICATE-----\n'
  pem += der.encode('base64')
  pem += '-----END CERTIFICATE-----\n'
  return pem

# unauthorizedDER is an OCSPResponse with a status of 6:
# SEQUENCE { ENUM(6) }
unauthorizedDER = '30030a0106'.decode('hex')

def GenerateCertKeyAndOCSP(subject = "127.0.0.1",
                           ocsp_url = "http://127.0.0.1",
                           ocsp_states = None,
                           ocsp_dates = None,
                           ocsp_produced = OCSP_PRODUCED_VALID,
                           ip_sans = ["\x7F\x00\x00\x01"],
                           dns_sans = None,
                           serial = 0):
  '''GenerateCertKeyAndOCSP returns a (cert_and_key_pem, ocsp_der) where:
       * cert_and_key_pem contains a certificate and private key in PEM format
         with the given subject common name and OCSP URL.
       * ocsp_der contains a DER encoded OCSP response or None if ocsp_url is
         None'''

  if ocsp_states is None:
    ocsp_states = [OCSP_STATE_GOOD]
  if ocsp_dates is None:
    ocsp_dates = [OCSP_DATE_VALID]

  if serial == 0:
    serial = RandomNumber(16)
  cert_der = MakeCertificate(ROOT_CN, bytes(subject), serial, LEAF_KEY,
                             ROOT_KEY, bytes(ocsp_url), ip_sans=ip_sans,
                             dns_sans=dns_sans)
  cert_pem = DERToPEM(cert_der)

  ocsp_der = None
  if ocsp_url is not None:
    if ocsp_states[0] == OCSP_STATE_UNAUTHORIZED:
      ocsp_der = unauthorizedDER
    elif ocsp_states[0] == OCSP_STATE_INVALID_RESPONSE:
      ocsp_der = '3'
    elif ocsp_states[0] == OCSP_STATE_TRY_LATER:
      resp = asn1.SEQUENCE([
        asn1.ENUMERATED(3),
      ])
      ocsp_der = asn1.ToDER(resp)
    elif ocsp_states[0] == OCSP_STATE_INVALID_RESPONSE_DATA:
      invalid_data = asn1.ToDER(asn1.OCTETSTRING('not ocsp data'))
      basic_resp = asn1.SEQUENCE([
        asn1.Raw(invalid_data),
        asn1.SEQUENCE([
          SHA256_WITH_RSA_ENCRYPTION,
          None,
        ]),
        asn1.BitString(ROOT_KEY.Sign(invalid_data)),
      ])
      resp = asn1.SEQUENCE([
        asn1.ENUMERATED(0),
        asn1.Explicit(0, asn1.SEQUENCE([
          OCSP_TYPE_BASIC,
          asn1.OCTETSTRING(asn1.ToDER(basic_resp)),
        ])),
      ])
      ocsp_der = asn1.ToDER(resp)
    else:
      ocsp_der = MakeOCSPResponse(
          ROOT_CN, ROOT_KEY, serial, ocsp_states, ocsp_dates, ocsp_produced)

  return (cert_pem + LEAF_KEY_PEM, ocsp_der)


def GenerateCertKeyAndIntermediate(subject, ca_issuers_url, serial=0):
  '''Returns a (cert_and_key_pem, intermediate_cert_pem) where:
       * cert_and_key_pem contains a certificate and private key in PEM format
         with the given subject common name and caIssuers URL.
       * intermediate_cert_pem contains a PEM encoded certificate that signed
         cert_and_key_pem and was signed by ocsp-test-root.pem.'''
  if serial == 0:
    serial = RandomNumber(16)
  target_cert_der = MakeCertificate(INTERMEDIATE_CN, bytes(subject), serial,
                                    LEAF_KEY, INTERMEDIATE_KEY,
                                    ca_issuers_url=bytes(ca_issuers_url))
  target_cert_pem = DERToPEM(target_cert_der)

  intermediate_serial = RandomNumber(16)
  intermediate_cert_der = MakeCertificate(ROOT_CN, INTERMEDIATE_CN,
                                          intermediate_serial,
                                          INTERMEDIATE_KEY, ROOT_KEY,
                                          is_ca=True)

  return target_cert_pem + LEAF_KEY_PEM, intermediate_cert_der


if __name__ == '__main__':
  def bin_to_array(s):
    return ' '.join(['0x%02x,'%ord(c) for c in s])

  import sys
  sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                               '..', 'data', 'ssl', 'scripts'))
  import crlsetutil

  der_root = MakeCertificate(ROOT_CN, ROOT_CN, 1, ROOT_KEY, ROOT_KEY,
                             is_ca=True, path_len=1)
  print 'ocsp-test-root.pem:'
  print DERToPEM(der_root)

  print
  print 'kOCSPTestCertFingerprint:'
  print bin_to_array(hashlib.sha1(der_root).digest())

  print
  print 'kOCSPTestCertSPKI:'
  print bin_to_array(crlsetutil.der_cert_to_spki_hash(der_root))
