// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_TEST_CERTIFICATE_DATA_H_
#define NET_TEST_TEST_CERTIFICATE_DATA_H_

#include <stdint.h>

#include "base/strings/cstring_view.h"

// This is the SHA1 hash of the SubjectPublicKeyInfo of nist.der.
inline constexpr base::cstring_view kNistSPKIHash =
    "\x15\x60\xde\x65\x4e\x03\x9f\xd0\x08\x82"
    "\xa9\x6a\xc4\x65\x8e\x6f\x92\x06\x84\x35";

// Certificates for test data. They're obtained with:
//
// $ openssl s_client -connect [host]:443 -showcerts > /tmp/host.pem < /dev/null
// $ openssl x509 -inform PEM -outform DER < /tmp/host.pem > /tmp/host.der
// $ xxd -i /tmp/host.der
//
// TODO(wtc): move these certificates to data files in the
// src/net/data/ssl/certificates directory.

// Google's 2009 cert. Lacks a SubjectAltName, but contains www.google.com in
// the Subject CN field.

inline constexpr unsigned char google_der[] = {
    0x30, 0x82, 0x03, 0x21, 0x30, 0x82, 0x02, 0x8a, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x10, 0x01, 0x2a, 0x39, 0x76, 0x0d, 0x3f, 0x4f, 0xc9, 0x0b,
    0xe7, 0xbd, 0x2b, 0xcf, 0x95, 0x2e, 0x7a, 0x30, 0x0d, 0x06, 0x09, 0x2a,
    0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x4c,
    0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x5a,
    0x41, 0x31, 0x25, 0x30, 0x23, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x1c,
    0x54, 0x68, 0x61, 0x77, 0x74, 0x65, 0x20, 0x43, 0x6f, 0x6e, 0x73, 0x75,
    0x6c, 0x74, 0x69, 0x6e, 0x67, 0x20, 0x28, 0x50, 0x74, 0x79, 0x29, 0x20,
    0x4c, 0x74, 0x64, 0x2e, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04,
    0x03, 0x13, 0x0d, 0x54, 0x68, 0x61, 0x77, 0x74, 0x65, 0x20, 0x53, 0x47,
    0x43, 0x20, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x39, 0x30, 0x33,
    0x32, 0x37, 0x32, 0x32, 0x32, 0x30, 0x30, 0x37, 0x5a, 0x17, 0x0d, 0x31,
    0x30, 0x30, 0x33, 0x32, 0x37, 0x32, 0x32, 0x32, 0x30, 0x30, 0x37, 0x5a,
    0x30, 0x68, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08,
    0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61,
    0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x0d, 0x4d,
    0x6f, 0x75, 0x6e, 0x74, 0x61, 0x69, 0x6e, 0x20, 0x56, 0x69, 0x65, 0x77,
    0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0a, 0x47,
    0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x31, 0x17, 0x30,
    0x15, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0e, 0x77, 0x77, 0x77, 0x2e,
    0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x30, 0x81,
    0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
    0x01, 0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30, 0x81, 0x89, 0x02,
    0x81, 0x81, 0x00, 0xd6, 0xb9, 0xe1, 0xad, 0xb8, 0x61, 0x0b, 0x1f, 0x4e,
    0xb6, 0x3c, 0x09, 0x3d, 0xab, 0xe8, 0xe3, 0x2b, 0xb6, 0xe8, 0xa4, 0x3a,
    0x78, 0x2f, 0xd3, 0x51, 0x20, 0x22, 0x45, 0x95, 0xd8, 0x00, 0x91, 0x33,
    0x9a, 0xa7, 0xa2, 0x48, 0xea, 0x30, 0x57, 0x26, 0x97, 0x66, 0xc7, 0x5a,
    0xef, 0xf1, 0x9b, 0x0c, 0x3f, 0xe1, 0xb9, 0x7f, 0x7b, 0xc3, 0xc7, 0xcc,
    0xaf, 0x9c, 0xd0, 0x1f, 0x3c, 0x81, 0x15, 0x10, 0x58, 0xfc, 0x06, 0xb3,
    0xbf, 0xbc, 0x9c, 0x02, 0xb9, 0x51, 0xdc, 0xfb, 0xa6, 0xb9, 0x17, 0x42,
    0xe6, 0x46, 0xe7, 0x22, 0xcf, 0x6c, 0x27, 0x10, 0xfe, 0x54, 0xe6, 0x92,
    0x6c, 0x0c, 0x60, 0x76, 0x9a, 0xce, 0xf8, 0x7f, 0xac, 0xb8, 0x5a, 0x08,
    0x4a, 0xdc, 0xb1, 0x64, 0xbd, 0xa0, 0x74, 0x41, 0xb2, 0xac, 0x8f, 0x86,
    0x9d, 0x1a, 0xde, 0x58, 0x09, 0xfd, 0x6c, 0x0a, 0x25, 0xe0, 0x79, 0x02,
    0x03, 0x01, 0x00, 0x01, 0xa3, 0x81, 0xe7, 0x30, 0x81, 0xe4, 0x30, 0x28,
    0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x21, 0x30, 0x1f, 0x06, 0x08, 0x2b,
    0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01, 0x06, 0x08, 0x2b, 0x06, 0x01,
    0x05, 0x05, 0x07, 0x03, 0x02, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x86,
    0xf8, 0x42, 0x04, 0x01, 0x30, 0x36, 0x06, 0x03, 0x55, 0x1d, 0x1f, 0x04,
    0x2f, 0x30, 0x2d, 0x30, 0x2b, 0xa0, 0x29, 0xa0, 0x27, 0x86, 0x25, 0x68,
    0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x72, 0x6c, 0x2e, 0x74, 0x68,
    0x61, 0x77, 0x74, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x54, 0x68, 0x61,
    0x77, 0x74, 0x65, 0x53, 0x47, 0x43, 0x43, 0x41, 0x2e, 0x63, 0x72, 0x6c,
    0x30, 0x72, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01,
    0x04, 0x66, 0x30, 0x64, 0x30, 0x22, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
    0x05, 0x07, 0x30, 0x01, 0x86, 0x16, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f,
    0x2f, 0x6f, 0x63, 0x73, 0x70, 0x2e, 0x74, 0x68, 0x61, 0x77, 0x74, 0x65,
    0x2e, 0x63, 0x6f, 0x6d, 0x30, 0x3e, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
    0x05, 0x07, 0x30, 0x02, 0x86, 0x32, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f,
    0x2f, 0x77, 0x77, 0x77, 0x2e, 0x74, 0x68, 0x61, 0x77, 0x74, 0x65, 0x2e,
    0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f,
    0x72, 0x79, 0x2f, 0x54, 0x68, 0x61, 0x77, 0x74, 0x65, 0x5f, 0x53, 0x47,
    0x43, 0x5f, 0x43, 0x41, 0x2e, 0x63, 0x72, 0x74, 0x30, 0x0c, 0x06, 0x03,
    0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x02, 0x30, 0x00, 0x30, 0x0d,
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05,
    0x00, 0x03, 0x81, 0x81, 0x00, 0x39, 0xb6, 0xfb, 0x11, 0xbc, 0x33, 0x2c,
    0xc3, 0x90, 0x48, 0xe3, 0x6e, 0xc3, 0x9b, 0x38, 0xb1, 0x42, 0xd1, 0x00,
    0x09, 0x58, 0x63, 0xa0, 0xe1, 0x98, 0x1c, 0x85, 0xf2, 0xef, 0x10, 0x1d,
    0x60, 0x4e, 0x51, 0x09, 0x62, 0xf5, 0x05, 0xbd, 0x9d, 0x4f, 0x87, 0x6c,
    0x98, 0x72, 0x07, 0x80, 0xc3, 0x59, 0x48, 0x14, 0xe2, 0xd6, 0xef, 0xd0,
    0x8f, 0x33, 0x6a, 0x68, 0x31, 0xfa, 0xb7, 0xbb, 0x85, 0xcc, 0xf7, 0xc7,
    0x47, 0x7b, 0x67, 0x93, 0x3c, 0xc3, 0x16, 0x51, 0x9b, 0x6f, 0x87, 0x20,
    0xfd, 0x67, 0x4c, 0x2b, 0xea, 0x6a, 0x49, 0xdb, 0x11, 0xd1, 0xbd, 0xd7,
    0x95, 0x22, 0x43, 0x7a, 0x06, 0x7b, 0x4e, 0xf6, 0x37, 0x8e, 0xa2, 0xb9,
    0xcf, 0x1f, 0xa5, 0xd2, 0xbd, 0x3b, 0x04, 0x97, 0x39, 0xb3, 0x0f, 0xfa,
    0x38, 0xb5, 0xaf, 0x55, 0x20, 0x88, 0x60, 0x93, 0xf2, 0xde, 0xdb, 0xff,
    0xdf};

// webkit.org's 2008 cert. Contains a SubjectAltName field with *.webkit.org and
// webkit.org. The Subject CN field contains *.webkit.org.

inline constexpr unsigned char webkit_der[] = {
    0x30, 0x82, 0x05, 0x0d, 0x30, 0x82, 0x03, 0xf5, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x03, 0x43, 0xdd, 0x63, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
    0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x81, 0xca,
    0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55,
    0x53, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x07,
    0x41, 0x72, 0x69, 0x7a, 0x6f, 0x6e, 0x61, 0x31, 0x13, 0x30, 0x11, 0x06,
    0x03, 0x55, 0x04, 0x07, 0x13, 0x0a, 0x53, 0x63, 0x6f, 0x74, 0x74, 0x73,
    0x64, 0x61, 0x6c, 0x65, 0x31, 0x1a, 0x30, 0x18, 0x06, 0x03, 0x55, 0x04,
    0x0a, 0x13, 0x11, 0x47, 0x6f, 0x44, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63,
    0x6f, 0x6d, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31, 0x33, 0x30, 0x31,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x2a, 0x68, 0x74, 0x74, 0x70, 0x3a,
    0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74,
    0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72,
    0x79, 0x31, 0x30, 0x30, 0x2e, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x27,
    0x47, 0x6f, 0x20, 0x44, 0x61, 0x64, 0x64, 0x79, 0x20, 0x53, 0x65, 0x63,
    0x75, 0x72, 0x65, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
    0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x41, 0x75, 0x74, 0x68, 0x6f, 0x72,
    0x69, 0x74, 0x79, 0x31, 0x11, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x04, 0x05,
    0x13, 0x08, 0x30, 0x37, 0x39, 0x36, 0x39, 0x32, 0x38, 0x37, 0x30, 0x1e,
    0x17, 0x0d, 0x30, 0x38, 0x30, 0x33, 0x31, 0x38, 0x32, 0x33, 0x33, 0x35,
    0x31, 0x39, 0x5a, 0x17, 0x0d, 0x31, 0x31, 0x30, 0x33, 0x31, 0x38, 0x32,
    0x33, 0x33, 0x35, 0x31, 0x39, 0x5a, 0x30, 0x79, 0x31, 0x0b, 0x30, 0x09,
    0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30,
    0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69,
    0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x12, 0x30, 0x10, 0x06, 0x03,
    0x55, 0x04, 0x07, 0x13, 0x09, 0x43, 0x75, 0x70, 0x65, 0x72, 0x74, 0x69,
    0x6e, 0x6f, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13,
    0x0a, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31,
    0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0c, 0x4d, 0x61,
    0x63, 0x20, 0x4f, 0x53, 0x20, 0x46, 0x6f, 0x72, 0x67, 0x65, 0x31, 0x15,
    0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0c, 0x2a, 0x2e, 0x77,
    0x65, 0x62, 0x6b, 0x69, 0x74, 0x2e, 0x6f, 0x72, 0x67, 0x30, 0x81, 0x9f,
    0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01,
    0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30, 0x81, 0x89, 0x02, 0x81,
    0x81, 0x00, 0xa7, 0x62, 0x79, 0x41, 0xda, 0x28, 0xf2, 0xc0, 0x4f, 0xe0,
    0x25, 0xaa, 0xa1, 0x2e, 0x3b, 0x30, 0x94, 0xb5, 0xc9, 0x26, 0x3a, 0x1b,
    0xe2, 0xd0, 0xcc, 0xa2, 0x95, 0xe2, 0x91, 0xc0, 0xf0, 0x40, 0x9e, 0x27,
    0x6e, 0xbd, 0x6e, 0xde, 0x7c, 0xb6, 0x30, 0x5c, 0xb8, 0x9b, 0x01, 0x2f,
    0x92, 0x04, 0xa1, 0xef, 0x4a, 0xb1, 0x6c, 0xb1, 0x7e, 0x8e, 0xcd, 0xa6,
    0xf4, 0x40, 0x73, 0x1f, 0x2c, 0x96, 0xad, 0xff, 0x2a, 0x6d, 0x0e, 0xba,
    0x52, 0x84, 0x83, 0xb0, 0x39, 0xee, 0xc9, 0x39, 0xdc, 0x1e, 0x34, 0xd0,
    0xd8, 0x5d, 0x7a, 0x09, 0xac, 0xa9, 0xee, 0xca, 0x65, 0xf6, 0x85, 0x3a,
    0x6b, 0xee, 0xe4, 0x5c, 0x5e, 0xf8, 0xda, 0xd1, 0xce, 0x88, 0x47, 0xcd,
    0x06, 0x21, 0xe0, 0xb9, 0x4b, 0xe4, 0x07, 0xcb, 0x57, 0xdc, 0xca, 0x99,
    0x54, 0xf7, 0x0e, 0xd5, 0x17, 0x95, 0x05, 0x2e, 0xe9, 0xb1, 0x02, 0x03,
    0x01, 0x00, 0x01, 0xa3, 0x82, 0x01, 0xce, 0x30, 0x82, 0x01, 0xca, 0x30,
    0x09, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x04, 0x02, 0x30, 0x00, 0x30, 0x0b,
    0x06, 0x03, 0x55, 0x1d, 0x0f, 0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30,
    0x1d, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x16, 0x30, 0x14, 0x06, 0x08,
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01, 0x06, 0x08, 0x2b, 0x06,
    0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30, 0x57, 0x06, 0x03, 0x55, 0x1d,
    0x1f, 0x04, 0x50, 0x30, 0x4e, 0x30, 0x4c, 0xa0, 0x4a, 0xa0, 0x48, 0x86,
    0x46, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74,
    0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64,
    0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70,
    0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72, 0x79, 0x2f, 0x67, 0x6f, 0x64, 0x61,
    0x64, 0x64, 0x79, 0x65, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x65, 0x64, 0x69,
    0x73, 0x73, 0x75, 0x69, 0x6e, 0x67, 0x33, 0x2e, 0x63, 0x72, 0x6c, 0x30,
    0x52, 0x06, 0x03, 0x55, 0x1d, 0x20, 0x04, 0x4b, 0x30, 0x49, 0x30, 0x47,
    0x06, 0x0b, 0x60, 0x86, 0x48, 0x01, 0x86, 0xfd, 0x6d, 0x01, 0x07, 0x17,
    0x02, 0x30, 0x38, 0x30, 0x36, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
    0x07, 0x02, 0x01, 0x16, 0x2a, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f,
    0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x73,
    0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d,
    0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72, 0x79, 0x30,
    0x7f, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01, 0x04,
    0x73, 0x30, 0x71, 0x30, 0x23, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
    0x07, 0x30, 0x01, 0x86, 0x17, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f,
    0x6f, 0x63, 0x73, 0x70, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79,
    0x2e, 0x63, 0x6f, 0x6d, 0x30, 0x4a, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
    0x05, 0x07, 0x30, 0x02, 0x86, 0x3e, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f,
    0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65,
    0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f,
    0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72, 0x79,
    0x2f, 0x67, 0x64, 0x5f, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x6d, 0x65, 0x64,
    0x69, 0x61, 0x74, 0x65, 0x2e, 0x63, 0x72, 0x74, 0x30, 0x1d, 0x06, 0x03,
    0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0x48, 0xdf, 0x60, 0x32, 0xcc,
    0x89, 0x01, 0xb6, 0xdc, 0x2f, 0xe3, 0x73, 0xb5, 0x9c, 0x16, 0x58, 0x32,
    0x68, 0xa9, 0xc3, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18,
    0x30, 0x16, 0x80, 0x14, 0xfd, 0xac, 0x61, 0x32, 0x93, 0x6c, 0x45, 0xd6,
    0xe2, 0xee, 0x85, 0x5f, 0x9a, 0xba, 0xe7, 0x76, 0x99, 0x68, 0xcc, 0xe7,
    0x30, 0x23, 0x06, 0x03, 0x55, 0x1d, 0x11, 0x04, 0x1c, 0x30, 0x1a, 0x82,
    0x0c, 0x2a, 0x2e, 0x77, 0x65, 0x62, 0x6b, 0x69, 0x74, 0x2e, 0x6f, 0x72,
    0x67, 0x82, 0x0a, 0x77, 0x65, 0x62, 0x6b, 0x69, 0x74, 0x2e, 0x6f, 0x72,
    0x67, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
    0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x1e, 0x6a, 0xe7,
    0xe0, 0x4f, 0xe7, 0x4d, 0xd0, 0x69, 0x7c, 0xf8, 0x8f, 0x99, 0xb4, 0x18,
    0x95, 0x36, 0x24, 0x0f, 0x0e, 0xa3, 0xea, 0x34, 0x37, 0xf4, 0x7d, 0xd5,
    0x92, 0x35, 0x53, 0x72, 0x76, 0x3f, 0x69, 0xf0, 0x82, 0x56, 0xe3, 0x94,
    0x7a, 0x1d, 0x1a, 0x81, 0xaf, 0x9f, 0xc7, 0x43, 0x01, 0x64, 0xd3, 0x7c,
    0x0d, 0xc8, 0x11, 0x4e, 0x4a, 0xe6, 0x1a, 0xc3, 0x01, 0x74, 0xe8, 0x35,
    0x87, 0x5c, 0x61, 0xaa, 0x8a, 0x46, 0x06, 0xbe, 0x98, 0x95, 0x24, 0x9e,
    0x01, 0xe3, 0xe6, 0xa0, 0x98, 0xee, 0x36, 0x44, 0x56, 0x8d, 0x23, 0x9c,
    0x65, 0xea, 0x55, 0x6a, 0xdf, 0x66, 0xee, 0x45, 0xe8, 0xa0, 0xe9, 0x7d,
    0x9a, 0xba, 0x94, 0xc5, 0xc8, 0xc4, 0x4b, 0x98, 0xff, 0x9a, 0x01, 0x31,
    0x6d, 0xf9, 0x2b, 0x58, 0xe7, 0xe7, 0x2a, 0xc5, 0x4d, 0xbb, 0xbb, 0xcd,
    0x0d, 0x70, 0xe1, 0xad, 0x03, 0xf5, 0xfe, 0xf4, 0x84, 0x71, 0x08, 0xd2,
    0xbc, 0x04, 0x7b, 0x26, 0x1c, 0xa8, 0x0f, 0x9c, 0xd8, 0x12, 0x6a, 0x6f,
    0x2b, 0x67, 0xa1, 0x03, 0x80, 0x9a, 0x11, 0x0b, 0xe9, 0xe0, 0xb5, 0xb3,
    0xb8, 0x19, 0x4e, 0x0c, 0xa4, 0xd9, 0x2b, 0x3b, 0xc2, 0xca, 0x20, 0xd3,
    0x0c, 0xa4, 0xff, 0x93, 0x13, 0x1f, 0xfc, 0xba, 0x94, 0x93, 0x8c, 0x64,
    0x15, 0x2e, 0x28, 0xa9, 0x55, 0x8c, 0x2c, 0x48, 0xd3, 0xd3, 0xc1, 0x50,
    0x69, 0x19, 0xe8, 0x34, 0xd3, 0xf1, 0x04, 0x9f, 0x0a, 0x7a, 0x21, 0x87,
    0xbf, 0xb9, 0x59, 0x37, 0x2e, 0xf4, 0x71, 0xa5, 0x3e, 0xbe, 0xcd, 0x70,
    0x83, 0x18, 0xf8, 0x8a, 0x72, 0x85, 0x45, 0x1f, 0x08, 0x01, 0x6f, 0x37,
    0xf5, 0x2b, 0x7b, 0xea, 0xb9, 0x8b, 0xa3, 0xcc, 0xfd, 0x35, 0x52, 0xdd,
    0x66, 0xde, 0x4f, 0x30, 0xc5, 0x73, 0x81, 0xb6, 0xe8, 0x3c, 0xd8, 0x48,
    0x8a};

// thawte.com 2008 Extended Validation cert. Lacks a SubjectAltName, but
// contains www.thawte.com in the Subject CN field.

inline constexpr unsigned char thawte_der[] = {
    0x30, 0x82, 0x04, 0xa5, 0x30, 0x82, 0x03, 0x8d, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x10, 0x17, 0x76, 0x05, 0x88, 0x95, 0x58, 0xee, 0xbb, 0x00,
    0xda, 0x10, 0xe5, 0xf0, 0xf3, 0x9c, 0xf0, 0x30, 0x0d, 0x06, 0x09, 0x2a,
    0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x81,
    0x8b, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02,
    0x55, 0x53, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13,
    0x0c, 0x74, 0x68, 0x61, 0x77, 0x74, 0x65, 0x2c, 0x20, 0x49, 0x6e, 0x63,
    0x2e, 0x31, 0x39, 0x30, 0x37, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x30,
    0x54, 0x65, 0x72, 0x6d, 0x73, 0x20, 0x6f, 0x66, 0x20, 0x75, 0x73, 0x65,
    0x20, 0x61, 0x74, 0x20, 0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f,
    0x77, 0x77, 0x77, 0x2e, 0x74, 0x68, 0x61, 0x77, 0x74, 0x65, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x63, 0x70, 0x73, 0x20, 0x28, 0x63, 0x29, 0x30, 0x36,
    0x31, 0x2a, 0x30, 0x28, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x21, 0x74,
    0x68, 0x61, 0x77, 0x74, 0x65, 0x20, 0x45, 0x78, 0x74, 0x65, 0x6e, 0x64,
    0x65, 0x64, 0x20, 0x56, 0x61, 0x6c, 0x69, 0x64, 0x61, 0x74, 0x69, 0x6f,
    0x6e, 0x20, 0x53, 0x53, 0x4c, 0x20, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d,
    0x30, 0x38, 0x31, 0x31, 0x31, 0x39, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x5a, 0x17, 0x0d, 0x31, 0x30, 0x30, 0x31, 0x31, 0x37, 0x32, 0x33, 0x35,
    0x39, 0x35, 0x39, 0x5a, 0x30, 0x81, 0xc7, 0x31, 0x13, 0x30, 0x11, 0x06,
    0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x3c, 0x02, 0x01, 0x03,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x19, 0x30, 0x17, 0x06, 0x0b, 0x2b, 0x06,
    0x01, 0x04, 0x01, 0x82, 0x37, 0x3c, 0x02, 0x01, 0x02, 0x14, 0x08, 0x44,
    0x65, 0x6c, 0x61, 0x77, 0x61, 0x72, 0x65, 0x31, 0x1b, 0x30, 0x19, 0x06,
    0x03, 0x55, 0x04, 0x0f, 0x13, 0x12, 0x56, 0x31, 0x2e, 0x30, 0x2c, 0x20,
    0x43, 0x6c, 0x61, 0x75, 0x73, 0x65, 0x20, 0x35, 0x2e, 0x28, 0x62, 0x29,
    0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x14, 0x0a, 0x54,
    0x68, 0x61, 0x77, 0x74, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x31, 0x10, 0x30,
    0x0e, 0x06, 0x03, 0x55, 0x04, 0x05, 0x13, 0x07, 0x33, 0x38, 0x39, 0x38,
    0x32, 0x36, 0x31, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04,
    0x08, 0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69,
    0x61, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x07, 0x14, 0x0d,
    0x4d, 0x6f, 0x75, 0x6e, 0x74, 0x61, 0x69, 0x6e, 0x20, 0x56, 0x69, 0x65,
    0x77, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x03, 0x14, 0x0e,
    0x77, 0x77, 0x77, 0x2e, 0x74, 0x68, 0x61, 0x77, 0x74, 0x65, 0x2e, 0x63,
    0x6f, 0x6d, 0x30, 0x81, 0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
    0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00,
    0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00, 0xe7, 0x89, 0x68, 0xb5, 0x6e,
    0x1d, 0x38, 0x19, 0xf6, 0x2d, 0x61, 0xc2, 0x00, 0xba, 0x6e, 0xab, 0x66,
    0x92, 0xd6, 0x85, 0x87, 0x2d, 0xd5, 0xa8, 0x58, 0xa9, 0x7a, 0x75, 0x27,
    0x9d, 0xed, 0x9e, 0xfe, 0x06, 0x71, 0x70, 0x2d, 0x21, 0x70, 0x4c, 0x3e,
    0x9c, 0xb6, 0xd5, 0x5d, 0x44, 0x92, 0xb4, 0xe0, 0xee, 0x7c, 0x0a, 0x50,
    0x4c, 0x0d, 0x67, 0x98, 0xaa, 0x01, 0x0e, 0x37, 0xa3, 0x2a, 0xef, 0xe6,
    0xe0, 0x11, 0x7b, 0xee, 0xb0, 0xa2, 0xb4, 0x32, 0x64, 0xa7, 0x0d, 0xda,
    0x6c, 0x15, 0xf8, 0xc5, 0xa5, 0x5a, 0x2c, 0xfc, 0xc9, 0xa6, 0x3c, 0x88,
    0x88, 0xbf, 0xdf, 0xa7, 0x38, 0xf0, 0x78, 0xed, 0x81, 0x93, 0x29, 0x0c,
    0xae, 0xc7, 0xab, 0x51, 0x21, 0x5e, 0xca, 0x95, 0xe5, 0x48, 0x52, 0x41,
    0xb6, 0x18, 0x60, 0x04, 0x19, 0x6f, 0x3d, 0x80, 0x14, 0xd3, 0xaf, 0x23,
    0x03, 0x10, 0x95, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x82, 0x01, 0x49,
    0x30, 0x82, 0x01, 0x45, 0x30, 0x0c, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01,
    0x01, 0xff, 0x04, 0x02, 0x30, 0x00, 0x30, 0x39, 0x06, 0x03, 0x55, 0x1d,
    0x1f, 0x04, 0x32, 0x30, 0x30, 0x30, 0x2e, 0xa0, 0x2c, 0xa0, 0x2a, 0x86,
    0x28, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x72, 0x6c, 0x2e,
    0x74, 0x68, 0x61, 0x77, 0x74, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x54,
    0x68, 0x61, 0x77, 0x74, 0x65, 0x45, 0x56, 0x43, 0x41, 0x32, 0x30, 0x30,
    0x36, 0x2e, 0x63, 0x72, 0x6c, 0x30, 0x42, 0x06, 0x03, 0x55, 0x1d, 0x20,
    0x04, 0x3b, 0x30, 0x39, 0x30, 0x37, 0x06, 0x0b, 0x60, 0x86, 0x48, 0x01,
    0x86, 0xf8, 0x45, 0x01, 0x07, 0x30, 0x01, 0x30, 0x28, 0x30, 0x26, 0x06,
    0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x02, 0x01, 0x16, 0x1a, 0x68,
    0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x74,
    0x68, 0x61, 0x77, 0x74, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x63, 0x70,
    0x73, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x16, 0x30, 0x14,
    0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01, 0x06, 0x08,
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30, 0x1f, 0x06, 0x03,
    0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xcd, 0x32, 0xe2,
    0xf2, 0x5d, 0x25, 0x47, 0x02, 0xaa, 0x8f, 0x79, 0x4b, 0x32, 0xee, 0x03,
    0x99, 0xfd, 0x30, 0x49, 0xd1, 0x30, 0x76, 0x06, 0x08, 0x2b, 0x06, 0x01,
    0x05, 0x05, 0x07, 0x01, 0x01, 0x04, 0x6a, 0x30, 0x68, 0x30, 0x22, 0x06,
    0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01, 0x86, 0x16, 0x68,
    0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x6f, 0x63, 0x73, 0x70, 0x2e, 0x74,
    0x68, 0x61, 0x77, 0x74, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x30, 0x42, 0x06,
    0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x36, 0x68,
    0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x74, 0x68,
    0x61, 0x77, 0x74, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70,
    0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72, 0x79, 0x2f, 0x54, 0x68, 0x61, 0x77,
    0x74, 0x65, 0x5f, 0x45, 0x56, 0x5f, 0x43, 0x41, 0x5f, 0x32, 0x30, 0x30,
    0x36, 0x2e, 0x63, 0x72, 0x74, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
    0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01,
    0x00, 0xb2, 0xa0, 0x96, 0xdd, 0xec, 0x04, 0x38, 0x6b, 0xc3, 0x7a, 0xad,
    0x23, 0x44, 0x91, 0xe5, 0x62, 0x8c, 0xb1, 0xf6, 0x9c, 0x03, 0x21, 0x1f,
    0xef, 0x03, 0xd9, 0xca, 0x63, 0xb2, 0xf8, 0xdb, 0x5a, 0x93, 0xc2, 0xcc,
    0xf1, 0x7c, 0x6f, 0xeb, 0x0f, 0x51, 0x7b, 0x4b, 0xe7, 0xb5, 0xfc, 0xbc,
    0x9b, 0x87, 0x48, 0xcc, 0x5b, 0xf9, 0xc8, 0x66, 0xa4, 0x40, 0xac, 0xe9,
    0x42, 0x5d, 0xed, 0xf3, 0x53, 0x13, 0xe7, 0xbd, 0x6e, 0x7f, 0x50, 0x53,
    0x64, 0xb3, 0x95, 0xf1, 0x42, 0x4f, 0x36, 0x54, 0xb4, 0x1e, 0x7f, 0x18,
    0x37, 0x39, 0x3b, 0x06, 0x5b, 0xe5, 0x13, 0xd9, 0x57, 0xbc, 0xd5, 0x68,
    0xe3, 0x71, 0x5f, 0x5f, 0x2b, 0xf5, 0xa6, 0xc2, 0x8f, 0x67, 0x81, 0x3a,
    0x44, 0x63, 0x8c, 0x36, 0xfa, 0xa8, 0xed, 0xfd, 0xd7, 0x5e, 0xa2, 0x9f,
    0xb0, 0x9d, 0x47, 0x86, 0xfb, 0x71, 0x60, 0x8e, 0xc8, 0xd3, 0x45, 0x19,
    0xb7, 0xda, 0xcd, 0x9e, 0xea, 0x70, 0x10, 0x87, 0x37, 0x10, 0xdd, 0x2c,
    0x11, 0xdf, 0xee, 0x02, 0x21, 0xa6, 0x75, 0xe6, 0xd6, 0x9f, 0x54, 0x72,
    0x61, 0xe6, 0x5c, 0x1e, 0x6e, 0x16, 0xf6, 0x8e, 0xb8, 0xfc, 0x47, 0x80,
    0x05, 0x4b, 0xf7, 0x2d, 0x02, 0xee, 0x50, 0x26, 0xd1, 0x48, 0x01, 0x60,
    0xdc, 0x3c, 0xa7, 0xdb, 0xeb, 0xca, 0x8b, 0xa6, 0xff, 0x9e, 0x47, 0x5d,
    0x87, 0x40, 0xf8, 0xd2, 0x82, 0xd7, 0x13, 0x64, 0x0e, 0xd4, 0xb3, 0x29,
    0x22, 0xa7, 0xe0, 0xc8, 0xcd, 0x8c, 0x4d, 0xf5, 0x11, 0x21, 0x26, 0x02,
    0x43, 0x33, 0x8e, 0xa9, 0x3f, 0x91, 0xd4, 0x05, 0x97, 0xc9, 0xd3, 0x42,
    0x6b, 0x05, 0x99, 0xf6, 0x16, 0x71, 0x67, 0x65, 0xc7, 0x96, 0xdf, 0x2a,
    0xd7, 0x54, 0x63, 0x25, 0xc0, 0x28, 0xf7, 0x1c, 0xee, 0xcd, 0x8b, 0xe4,
    0x9d, 0x32, 0xa3, 0x81, 0x55};

// DER-encoded X.509 DistinguishedNames.
//
// To output the subject or issuer of a certificate:
//
//    openssl asn1parse -i -inform DER -in <cert>
//
// The output will contain
//   SEQUENCE  [This is the issuer name]
//     ...
//   SEQUENCE  [This is the validity period]
//     UTCTIME (or GENERALTIME)
//     UTCTIME
//   SEQUENCE  [This is the subject]
//     ...
//
// The OFFSET is the first column before the column, e.g. for '21:d=2', the
// offset is 21 for the SEQUENCE you're interested in.
// The LENGTH is 'hl + l'.
//
// To generate the table, then use the following for a DER-encoded
// certificate:
//
//   xxd -i -s $OFFSET -l $LENGTH <cert>
//
// For PEM certificates, convert them to DER before, as in:
//
//   openssl x509 -inform PEM -outform DER -in <cert> |
//       xxd -i -s $OFFSET -l $LENGTH
//

//  0:d=0  hl=2 l=  95 cons: SEQUENCE
//  2:d=1  hl=2 l=  11 cons:  SET
//  4:d=2  hl=2 l=   9 cons:   SEQUENCE
//  6:d=3  hl=2 l=   3 prim:    OBJECT            :countryName
// 11:d=3  hl=2 l=   2 prim:    PRINTABLESTRING   :US
// 15:d=1  hl=2 l=  23 cons:  SET
// 17:d=2  hl=2 l=  21 cons:   SEQUENCE
// 19:d=3  hl=2 l=   3 prim:    OBJECT            :organizationName
// 24:d=3  hl=2 l=  14 prim:    PRINTABLESTRING   :VeriSign, Inc.
// 40:d=1  hl=2 l=  55 cons:  SET
// 42:d=2  hl=2 l=  53 cons:   SEQUENCE
// 44:d=3  hl=2 l=   3 prim:    OBJECT            :organizationalUnitName
// 49:d=3  hl=2 l=  46 prim:    PRINTABLESTRING   :
//                          Class 1 Public Primary Certification Authority
inline constexpr uint8_t VerisignDN[] = {
    0x30, 0x5f, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55,
    0x04, 0x0a, 0x13, 0x0e, 0x56, 0x65, 0x72, 0x69, 0x53, 0x69, 0x67,
    0x6e, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31, 0x37, 0x30, 0x35,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x2e, 0x43, 0x6c, 0x61, 0x73,
    0x73, 0x20, 0x31, 0x20, 0x50, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x20,
    0x50, 0x72, 0x69, 0x6d, 0x61, 0x72, 0x79, 0x20, 0x43, 0x65, 0x72,
    0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20,
    0x41, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79};

//  0:d=0  hl=2 l= 125 cons: SEQUENCE
//  2:d=1  hl=2 l=  11 cons:  SET
//  4:d=2  hl=2 l=   9 cons:   SEQUENCE
//  6:d=3  hl=2 l=   3 prim:    OBJECT            :countryName
// 11:d=3  hl=2 l=   2 prim:    PRINTABLESTRING   :IL
// 15:d=1  hl=2 l=  22 cons:  SET
// 17:d=2  hl=2 l=  20 cons:   SEQUENCE
// 19:d=3  hl=2 l=   3 prim:    OBJECT            :organizationName
// 24:d=3  hl=2 l=  13 prim:    PRINTABLESTRING   :StartCom Ltd.
// 39:d=1  hl=2 l=  43 cons:  SET
// 41:d=2  hl=2 l=  41 cons:   SEQUENCE
// 43:d=3  hl=2 l=   3 prim:    OBJECT            :organizationalUnitName
// 48:d=3  hl=2 l=  34 prim:    PRINTABLESTRING   :
//                                Secure Digital Certificate Signing
// 84:d=1  hl=2 l=  41 cons:  SET
// 86:d=2  hl=2 l=  39 cons:   SEQUENCE
// 88:d=3  hl=2 l=   3 prim:    OBJECT            :commonName
// 93:d=3  hl=2 l=  32 prim:    PRINTABLESTRING   :
//                                StartCom Certification Authority
inline constexpr uint8_t StartComDN[] = {
    0x30, 0x7d, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x49, 0x4c, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x0a,
    0x13, 0x0d, 0x53, 0x74, 0x61, 0x72, 0x74, 0x43, 0x6f, 0x6d, 0x20, 0x4c,
    0x74, 0x64, 0x2e, 0x31, 0x2b, 0x30, 0x29, 0x06, 0x03, 0x55, 0x04, 0x0b,
    0x13, 0x22, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x44, 0x69, 0x67,
    0x69, 0x74, 0x61, 0x6c, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69,
    0x63, 0x61, 0x74, 0x65, 0x20, 0x53, 0x69, 0x67, 0x6e, 0x69, 0x6e, 0x67,
    0x31, 0x29, 0x30, 0x27, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x20, 0x53,
    0x74, 0x61, 0x72, 0x74, 0x43, 0x6f, 0x6d, 0x20, 0x43, 0x65, 0x72, 0x74,
    0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x41, 0x75,
    0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79};

//  0:d=0  hl=3 l= 174 cons: SEQUENCE
//  3:d=1  hl=2 l=  11 cons:  SET
//  5:d=2  hl=2 l=   9 cons:   SEQUENCE
//  7:d=3  hl=2 l=   3 prim:    OBJECT            :countryName
// 12:d=3  hl=2 l=   2 prim:    PRINTABLESTRING   :US
// 16:d=1  hl=2 l=  11 cons:  SET
// 18:d=2  hl=2 l=   9 cons:   SEQUENCE
// 20:d=3  hl=2 l=   3 prim:    OBJECT            :stateOrProvinceName
// 25:d=3  hl=2 l=   2 prim:    PRINTABLESTRING   :UT
// 29:d=1  hl=2 l=  23 cons:  SET
// 31:d=2  hl=2 l=  21 cons:   SEQUENCE
// 33:d=3  hl=2 l=   3 prim:    OBJECT            :localityName
// 38:d=3  hl=2 l=  14 prim:    PRINTABLESTRING   :Salt Lake City
// 54:d=1  hl=2 l=  30 cons:  SET
// 56:d=2  hl=2 l=  28 cons:   SEQUENCE
// 58:d=3  hl=2 l=   3 prim:    OBJECT            :organizationName
// 63:d=3  hl=2 l=  21 prim:    PRINTABLESTRING   :The USERTRUST Network
// 86:d=1  hl=2 l=  33 cons:  SET
// 88:d=2  hl=2 l=  31 cons:   SEQUENCE
// 90:d=3  hl=2 l=   3 prim:    OBJECT            :organizationalUnitName
// 95:d=3  hl=2 l=  24 prim:    PRINTABLESTRING   :http://www.usertrust.com
//121:d=1  hl=2 l=  54 cons:  SET
//123:d=2  hl=2 l=  52 cons:   SEQUENCE
//125:d=3  hl=2 l=   3 prim:    OBJECT            :commonName
//130:d=3  hl=2 l=  45 prim:    PRINTABLESTRING   :
//                            UTN-USERFirst-Client Authentication and Email
inline constexpr uint8_t UserTrustDN[] = {
    0x30, 0x81, 0xae, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04,
    0x08, 0x13, 0x02, 0x55, 0x54, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55,
    0x04, 0x07, 0x13, 0x0e, 0x53, 0x61, 0x6c, 0x74, 0x20, 0x4c, 0x61, 0x6b,
    0x65, 0x20, 0x43, 0x69, 0x74, 0x79, 0x31, 0x1e, 0x30, 0x1c, 0x06, 0x03,
    0x55, 0x04, 0x0a, 0x13, 0x15, 0x54, 0x68, 0x65, 0x20, 0x55, 0x53, 0x45,
    0x52, 0x54, 0x52, 0x55, 0x53, 0x54, 0x20, 0x4e, 0x65, 0x74, 0x77, 0x6f,
    0x72, 0x6b, 0x31, 0x21, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13,
    0x18, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e,
    0x75, 0x73, 0x65, 0x72, 0x74, 0x72, 0x75, 0x73, 0x74, 0x2e, 0x63, 0x6f,
    0x6d, 0x31, 0x36, 0x30, 0x34, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x2d,
    0x55, 0x54, 0x4e, 0x2d, 0x55, 0x53, 0x45, 0x52, 0x46, 0x69, 0x72, 0x73,
    0x74, 0x2d, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x20, 0x41, 0x75, 0x74,
    0x68, 0x65, 0x6e, 0x74, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20,
    0x61, 0x6e, 0x64, 0x20, 0x45, 0x6d, 0x61, 0x69, 0x6c};

//  0:d=0  hl=3 l= 190 cons: SEQUENCE
//  3:d=1  hl=2 l=  63 cons:  SET
//  5:d=2  hl=2 l=  61 cons:   SEQUENCE
//  7:d=3  hl=2 l=   3 prim:    OBJECT     :commonName
// 12:d=3  hl=2 l=  54 prim:    UTF8STRING :
//                       TÜRKTRUST Elektronik Sertifika Hizmet Sağlayıcısı
// 68:d=1  hl=2 l=  11 cons:  SET
// 70:d=2  hl=2 l=   9 cons:   SEQUENCE
// 72:d=3  hl=2 l=   3 prim:    OBJECT            :countryName
// 77:d=3  hl=2 l=   2 prim:    PRINTABLESTRING   :TR
// 81:d=1  hl=2 l=  15 cons:  SET
// 83:d=2  hl=2 l=  13 cons:   SEQUENCE
// 85:d=3  hl=2 l=   3 prim:    OBJECT            :localityName
// 90:d=3  hl=2 l=   6 prim:    UTF8STRING        :Ankara
// 98:d=1  hl=2 l=  93 cons:  SET
//100:d=2  hl=2 l=  91 cons:   SEQUENCE
//102:d=3  hl=2 l=   3 prim:    OBJECT     :organizationName
//107:d=3  hl=2 l=  84 prim:    UTF8STRING :
//           TÜRKTRUST Bilgi İletişim ve Bilişim Güvenliği Hizmetleri A.Ş.
//           (c) Kasım 2005
inline constexpr uint8_t TurkTrustDN[] = {
    0x30, 0x81, 0xbe, 0x31, 0x3f, 0x30, 0x3d, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x0c, 0x36, 0x54, 0xc3, 0x9c, 0x52, 0x4b, 0x54, 0x52, 0x55, 0x53, 0x54,
    0x20, 0x45, 0x6c, 0x65, 0x6b, 0x74, 0x72, 0x6f, 0x6e, 0x69, 0x6b, 0x20,
    0x53, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x6b, 0x61, 0x20, 0x48, 0x69,
    0x7a, 0x6d, 0x65, 0x74, 0x20, 0x53, 0x61, 0xc4, 0x9f, 0x6c, 0x61, 0x79,
    0xc4, 0xb1, 0x63, 0xc4, 0xb1, 0x73, 0xc4, 0xb1, 0x31, 0x0b, 0x30, 0x09,
    0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x54, 0x52, 0x31, 0x0f, 0x30,
    0x0d, 0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, 0x06, 0x41, 0x6e, 0x6b, 0x61,
    0x72, 0x61, 0x31, 0x5d, 0x30, 0x5b, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c,
    0x54, 0x54, 0xc3, 0x9c, 0x52, 0x4b, 0x54, 0x52, 0x55, 0x53, 0x54, 0x20,
    0x42, 0x69, 0x6c, 0x67, 0x69, 0x20, 0xc4, 0xb0, 0x6c, 0x65, 0x74, 0x69,
    0xc5, 0x9f, 0x69, 0x6d, 0x20, 0x76, 0x65, 0x20, 0x42, 0x69, 0x6c, 0x69,
    0xc5, 0x9f, 0x69, 0x6d, 0x20, 0x47, 0xc3, 0xbc, 0x76, 0x65, 0x6e, 0x6c,
    0x69, 0xc4, 0x9f, 0x69, 0x20, 0x48, 0x69, 0x7a, 0x6d, 0x65, 0x74, 0x6c,
    0x65, 0x72, 0x69, 0x20, 0x41, 0x2e, 0xc5, 0x9e, 0x2e, 0x20, 0x28, 0x63,
    0x29, 0x20, 0x4b, 0x61, 0x73, 0xc4, 0xb1, 0x6d, 0x20, 0x32, 0x30, 0x30,
    0x35, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x35, 0x31, 0x31, 0x30, 0x37, 0x31,
    0x30, 0x30, 0x37, 0x35, 0x37};

// 33:d=2  hl=3 l= 207 cons:   SEQUENCE
// 36:d=3  hl=2 l=  11 cons:    SET
// 38:d=4  hl=2 l=   9 cons:     SEQUENCE
// 40:d=5  hl=2 l=   3 prim:      OBJECT            :countryName
// 45:d=5  hl=2 l=   2 prim:      PRINTABLESTRING   :AT
// 49:d=3  hl=3 l= 139 cons:    SET
// 52:d=4  hl=3 l= 136 cons:     SEQUENCE
// 55:d=5  hl=2 l=   3 prim:      OBJECT            :organizationName
// 60:d=5  hl=3 l= 128 prim:      BMPSTRING         :
//         A-Trust Ges. für Sicherheitssysteme im elektr. Datenverkehr GmbH
//191:d=3  hl=2 l=  24 cons:    SET
//193:d=4  hl=2 l=  22 cons:     SEQUENCE
//195:d=5  hl=2 l=   3 prim:      OBJECT            :organizationalUnitName
//200:d=5  hl=2 l=  15 prim:      PRINTABLESTRING   :A-Trust-Qual-01
//217:d=3  hl=2 l=  24 cons:    SET
//219:d=4  hl=2 l=  22 cons:     SEQUENCE
//221:d=5  hl=2 l=   3 prim:      OBJECT            :commonName
//226:d=5  hl=2 l=  15 prim:      PRINTABLESTRING   :A-Trust-Qual-01
inline constexpr uint8_t ATrustQual01DN[] = {
    0x30, 0x81, 0xcf, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x41, 0x54, 0x31, 0x81, 0x8b, 0x30, 0x81, 0x88, 0x06, 0x03,
    0x55, 0x04, 0x0a, 0x1e, 0x81, 0x80, 0x00, 0x41, 0x00, 0x2d, 0x00, 0x54,
    0x00, 0x72, 0x00, 0x75, 0x00, 0x73, 0x00, 0x74, 0x00, 0x20, 0x00, 0x47,
    0x00, 0x65, 0x00, 0x73, 0x00, 0x2e, 0x00, 0x20, 0x00, 0x66, 0x00, 0xfc,
    0x00, 0x72, 0x00, 0x20, 0x00, 0x53, 0x00, 0x69, 0x00, 0x63, 0x00, 0x68,
    0x00, 0x65, 0x00, 0x72, 0x00, 0x68, 0x00, 0x65, 0x00, 0x69, 0x00, 0x74,
    0x00, 0x73, 0x00, 0x73, 0x00, 0x79, 0x00, 0x73, 0x00, 0x74, 0x00, 0x65,
    0x00, 0x6d, 0x00, 0x65, 0x00, 0x20, 0x00, 0x69, 0x00, 0x6d, 0x00, 0x20,
    0x00, 0x65, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x6b, 0x00, 0x74, 0x00, 0x72,
    0x00, 0x2e, 0x00, 0x20, 0x00, 0x44, 0x00, 0x61, 0x00, 0x74, 0x00, 0x65,
    0x00, 0x6e, 0x00, 0x76, 0x00, 0x65, 0x00, 0x72, 0x00, 0x6b, 0x00, 0x65,
    0x00, 0x68, 0x00, 0x72, 0x00, 0x20, 0x00, 0x47, 0x00, 0x6d, 0x00, 0x62,
    0x00, 0x48, 0x31, 0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13,
    0x0f, 0x41, 0x2d, 0x54, 0x72, 0x75, 0x73, 0x74, 0x2d, 0x51, 0x75, 0x61,
    0x6c, 0x2d, 0x30, 0x31, 0x31, 0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04,
    0x03, 0x13, 0x0f, 0x41, 0x2d, 0x54, 0x72, 0x75, 0x73, 0x74, 0x2d, 0x51,
    0x75, 0x61, 0x6c, 0x2d, 0x30, 0x31, 0x30, 0x1e, 0x17};

// 34:d=2  hl=3 l= 180 cons:   SEQUENCE
// 37:d=3  hl=2 l=  20 cons:    SET
// 39:d=4  hl=2 l=  18 cons:     SEQUENCE
// 41:d=5  hl=2 l=   3 prim:      OBJECT            :organizationName
// 46:d=5  hl=2 l=  11 prim:      PRINTABLESTRING   :Entrust.net
// 59:d=3  hl=2 l=  64 cons:    SET
// 61:d=4  hl=2 l=  62 cons:     SEQUENCE
// 63:d=5  hl=2 l=   3 prim:      OBJECT            :organizationalUnitName
// 68:d=5  hl=2 l=  55 prim:      T61STRING         :
//                  www.entrust.net/CPS_2048 incorp. by ref. (limits liab.)
//125:d=3  hl=2 l=  37 cons:    SET
//127:d=4  hl=2 l=  35 cons:     SEQUENCE
//129:d=5  hl=2 l=   3 prim:      OBJECT          :organizationalUnitName
//134:d=5  hl=2 l=  28 prim:      PRINTABLESTRING :
//                                  (c) 1999 Entrust.net Limited
//164:d=3  hl=2 l=  51 cons:    SET
//166:d=4  hl=2 l=  49 cons:     SEQUENCE
//168:d=5  hl=2 l=   3 prim:      OBJECT          :commonName
//173:d=5  hl=2 l=  42 prim:      PRINTABLESTRING :
//                               Entrust.net Certification Authority (2048)
inline constexpr uint8_t EntrustDN[] = {
    0x30, 0x81, 0xb4, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a,
    0x13, 0x0b, 0x45, 0x6e, 0x74, 0x72, 0x75, 0x73, 0x74, 0x2e, 0x6e, 0x65,
    0x74, 0x31, 0x40, 0x30, 0x3e, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x14, 0x37,
    0x77, 0x77, 0x77, 0x2e, 0x65, 0x6e, 0x74, 0x72, 0x75, 0x73, 0x74, 0x2e,
    0x6e, 0x65, 0x74, 0x2f, 0x43, 0x50, 0x53, 0x5f, 0x32, 0x30, 0x34, 0x38,
    0x20, 0x69, 0x6e, 0x63, 0x6f, 0x72, 0x70, 0x2e, 0x20, 0x62, 0x79, 0x20,
    0x72, 0x65, 0x66, 0x2e, 0x20, 0x28, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x73,
    0x20, 0x6c, 0x69, 0x61, 0x62, 0x2e, 0x29, 0x31, 0x25, 0x30, 0x23, 0x06,
    0x03, 0x55, 0x04, 0x0b, 0x13, 0x1c, 0x28, 0x63, 0x29, 0x20, 0x31, 0x39,
    0x39, 0x39, 0x20, 0x45, 0x6e, 0x74, 0x72, 0x75, 0x73, 0x74, 0x2e, 0x6e,
    0x65, 0x74, 0x20, 0x4c, 0x69, 0x6d, 0x69, 0x74, 0x65, 0x64, 0x31, 0x33,
    0x30, 0x31, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x2a, 0x45, 0x6e, 0x74,
    0x72, 0x75, 0x73, 0x74, 0x2e, 0x6e, 0x65, 0x74, 0x20, 0x43, 0x65, 0x72,
    0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x41,
    0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79, 0x20, 0x28, 0x32, 0x30,
    0x34, 0x38, 0x29};

//  46:d=2  hl=2 l=  76 cons:   SEQUENCE
//  48:d=3  hl=2 l=  11 cons:    SET
//  50:d=4  hl=2 l=   9 cons:     SEQUENCE
//  52:d=5  hl=2 l=   3 prim:      OBJECT            :countryName
//  57:d=5  hl=2 l=   2 prim:      PRINTABLESTRING   :ZA
//  61:d=3  hl=2 l=  37 cons:    SET
//  63:d=4  hl=2 l=  35 cons:     SEQUENCE
//  65:d=5  hl=2 l=   3 prim:      OBJECT            :organizationName
//  70:d=5  hl=2 l=  28 prim:      PRINTABLESTRING   :
//                                   Thawte Consulting (Pty) Ltd.
// 100:d=3  hl=2 l=  22 cons:    SET
// 102:d=4  hl=2 l=  20 cons:     SEQUENCE
// 104:d=5  hl=2 l=   3 prim:      OBJECT            :commonName
// 109:d=5  hl=2 l=  13 prim:      PRINTABLESTRING   :Thawte SGC CA
inline constexpr uint8_t ThawteDN[] = {
    0x30, 0x4C, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x5A, 0x41, 0x31, 0x25, 0x30, 0x23, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x1C, 0x54, 0x68, 0x61, 0x77, 0x74, 0x65, 0x20, 0x43, 0x6F, 0x6E,
    0x73, 0x75, 0x6C, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x28, 0x50, 0x74, 0x79,
    0x29, 0x20, 0x4C, 0x74, 0x64, 0x2E, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03,
    0x55, 0x04, 0x03, 0x13, 0x0D, 0x54, 0x68, 0x61, 0x77, 0x74, 0x65, 0x20,
    0x53, 0x47, 0x43, 0x20, 0x43, 0x41};

//  47:d=2  hl=2 l= 108 cons:   SEQUENCE
//  49:d=3  hl=2 l=  11 cons:    SET
//  51:d=4  hl=2 l=   9 cons:     SEQUENCE
//  53:d=5  hl=2 l=   3 prim:      OBJECT            :countryName
//  58:d=5  hl=2 l=   2 prim:      PRINTABLESTRING   :US
//  62:d=3  hl=2 l=  22 cons:    SET
//  64:d=4  hl=2 l=  20 cons:     SEQUENCE
//  66:d=5  hl=2 l=   3 prim:      OBJECT            :stateOrProvinceName
//  71:d=5  hl=2 l=  13 prim:      PRINTABLESTRING   :Massachusetts
//  86:d=3  hl=2 l=  46 cons:    SET
//  88:d=4  hl=2 l=  44 cons:     SEQUENCE
//  90:d=5  hl=2 l=   3 prim:      OBJECT            :organizationName
//  95:d=5  hl=2 l=  37 prim:      PRINTABLESTRING   :
//                                    Massachusetts Institute of Technology
// 134:d=3  hl=2 l=  21 cons:    SET
// 136:d=4  hl=2 l=  19 cons:     SEQUENCE
// 138:d=5  hl=2 l=   3 prim:      OBJECT          :organizationalUnitName
// 143:d=5  hl=2 l=  12 prim:      PRINTABLESTRING :Client CA v1
inline constexpr uint8_t MITDN[] = {
    0x30, 0x6C, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55,
    0x04, 0x08, 0x13, 0x0D, 0x4D, 0x61, 0x73, 0x73, 0x61, 0x63, 0x68,
    0x75, 0x73, 0x65, 0x74, 0x74, 0x73, 0x31, 0x2E, 0x30, 0x2C, 0x06,
    0x03, 0x55, 0x04, 0x0A, 0x13, 0x25, 0x4D, 0x61, 0x73, 0x73, 0x61,
    0x63, 0x68, 0x75, 0x73, 0x65, 0x74, 0x74, 0x73, 0x20, 0x49, 0x6E,
    0x73, 0x74, 0x69, 0x74, 0x75, 0x74, 0x65, 0x20, 0x6F, 0x66, 0x20,
    0x54, 0x65, 0x63, 0x68, 0x6E, 0x6F, 0x6C, 0x6F, 0x67, 0x79, 0x31,
    0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x13, 0x0C, 0x43,
    0x6C, 0x69, 0x65, 0x6E, 0x74, 0x20, 0x43, 0x41, 0x20, 0x76, 0x31};

#endif  // NET_TEST_TEST_CERTIFICATE_DATA_H_
