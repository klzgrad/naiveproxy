#!/bin/sh

# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script generates a set of test (end-entity, intermediate, root)
# certificates that can be used to test fetching of an intermediate via AIA.

try() {
  "$@" || (e=$?; echo "$@" > /dev/stderr; exit $e)
}

try rm -rf out
try mkdir out

# Create the serial number files.
try /bin/sh -c "echo 01 > out/aia-test-root-serial"
try /bin/sh -c "echo 01 > out/aia-test-intermediate-serial"

# Create the signers' DB files.
touch out/aia-test-root-index.txt
touch out/aia-test-intermediate-index.txt

# Generate the keys
try openssl genrsa -out out/aia-test-root.key 2048
try openssl genrsa -out out/aia-test-intermediate.key 2048
try openssl genrsa -out out/aia-test-cert.key 2048

# Generate the root certificate
CA_COMMON_NAME="AIA Test Root CA" \
  CA_DIR=out \
  CA_NAME=aia-test-root \
  try openssl req \
    -new \
    -key out/aia-test-root.key \
    -out out/aia-test-root.csr \
    -config aia-test.cnf

CA_COMMON_NAME="AIA Test Root CA" \
  CA_DIR=out \
  CA_NAME=aia-test-root \
  try openssl x509 \
    -req -days 3650 \
    -in out/aia-test-root.csr \
    -out out/aia-test-root.pem \
    -signkey out/aia-test-root.key \
    -extfile aia-test.cnf \
    -extensions ca_cert \
    -text

# Generate the intermediate
CA_COMMON_NAME="AIA Test Intermediate CA" \
  CA_DIR=out \
  CA_NAME=aia-test-root \
  try openssl req \
    -new \
    -key out/aia-test-intermediate.key \
    -out out/aia-test-intermediate.csr \
    -config aia-test.cnf

CA_COMMON_NAME="AIA Test Intermediate CA" \
  CA_DIR=out \
  CA_NAME=aia-test-root \
  try openssl ca \
    -batch \
    -in out/aia-test-intermediate.csr \
    -out out/aia-test-intermediate.pem \
    -config aia-test.cnf \
    -extensions ca_cert

# Generate the leaf
CA_COMMON_NAME="aia-host.invalid" \
CA_DIR=out \
CA_NAME=aia-test-intermediate \
try openssl req \
  -new \
  -key out/aia-test-cert.key \
  -out out/aia-test-cert.csr \
  -config aia-test.cnf

CA_COMMON_NAME="AIA Test Intermediate CA" \
  HOST_NAME="aia-host.invalid" \
  CA_DIR=out \
  CA_NAME=aia-test-intermediate \
  AIA_URL=http://aia-test.invalid \
  try openssl ca \
    -batch \
    -in out/aia-test-cert.csr \
    -out out/aia-test-cert.pem \
    -config aia-test.cnf \
    -extensions user_cert

# Copy to the file names that are actually checked in.
try cp out/aia-test-cert.pem ../certificates/aia-cert.pem
try openssl x509 \
  -outform der \
  -in out/aia-test-intermediate.pem \
  -out ../certificates/aia-intermediate.der
try cp out/aia-test-root.pem ../certificates/aia-root.pem
