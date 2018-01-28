// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/cert_verify_tool/cert_verify_tool_util.h"

#include <iostream>

#include "base/files/file_util.h"
#include "base/strings/stringprintf.h"
#include "net/cert/pem_tokenizer.h"

namespace {

// The PEM block header used for PEM-encoded DER certificates.
const char kCertificateHeader[] = "CERTIFICATE";

// Parses |data_string| as a single DER cert or a PEM certificate list.
// This is an alternative to X509Certificate::CreateFrom[...] which
// is designed to decouple the file input and decoding from the DER Certificate
// parsing.
void ExtractCertificatesFromData(const std::string& data_string,
                                 const base::FilePath& file_path,
                                 std::vector<CertInput>* certs) {
  // TODO(mattm): support PKCS #7 (.p7b) files.
  net::PEMTokenizer pem_tokenizer(data_string, {kCertificateHeader});
  int block = 0;
  while (pem_tokenizer.GetNext()) {
    CertInput cert;
    cert.der_cert = pem_tokenizer.data();
    cert.source_file_path = file_path;
    cert.source_details =
        base::StringPrintf("%s block %i", kCertificateHeader, block);
    certs->push_back(cert);
    ++block;
  }

  // If it was a PEM file, return the extracted results.
  if (block)
    return;

  // Otherwise, assume it is a single DER cert.
  CertInput cert;
  cert.der_cert = data_string;
  cert.source_file_path = file_path;
  certs->push_back(cert);
}

}  // namespace

bool ReadCertificatesFromFile(const base::FilePath& file_path,
                              std::vector<CertInput>* certs) {
  std::string file_data;
  if (!base::ReadFileToString(file_path, &file_data)) {
    std::cerr << "ERROR: ReadFileToString " << file_path.value() << ": "
              << strerror(errno) << "\n";
    return false;
  }
  ExtractCertificatesFromData(file_data, file_path, certs);
  return true;
}

bool ReadChainFromFile(const base::FilePath& file_path,
                       CertInput* target,
                       std::vector<CertInput>* intermediates) {
  std::vector<CertInput> tmp_certs;
  if (!ReadCertificatesFromFile(file_path, &tmp_certs))
    return false;

  if (tmp_certs.empty())
    return true;

  *target = tmp_certs.front();

  intermediates->insert(intermediates->end(), ++tmp_certs.begin(),
                        tmp_certs.end());
  return true;
}

bool WriteToFile(const base::FilePath& file_path, const std::string& data) {
  if (base::WriteFile(file_path, data.data(), data.size()) < 0) {
    std::cerr << "ERROR: WriteFile " << file_path.value() << ": "
              << strerror(errno) << "\n";
    return false;
  }
  return true;
}

void PrintCertError(const std::string& error, const CertInput& cert) {
  std::cerr << error << " " << cert.source_file_path.value();
  if (!cert.source_details.empty())
    std::cerr << " (" << cert.source_details << ")";
  std::cerr << "\n";
}
