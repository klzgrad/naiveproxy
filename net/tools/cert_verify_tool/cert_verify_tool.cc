// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "base/at_exit.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/time/time.h"
#include "net/cert/cert_verify_proc.h"
#include "net/cert/cert_verify_proc_builtin.h"
#include "net/tools/cert_verify_tool/cert_verify_tool_util.h"
#include "net/tools/cert_verify_tool/verify_using_cert_verify_proc.h"
#include "net/tools/cert_verify_tool/verify_using_path_builder.h"

namespace {

// Base class to abstract running a particular implementation of certificate
// verification.
class CertVerifyImpl {
 public:
  virtual ~CertVerifyImpl() = default;

  virtual std::string GetName() const = 0;

  // Does certificate verification.
  //
  // Note that |hostname| may be empty to indicate that no name validation is
  // requested, and a null value of |verify_time| means to use the current time.
  virtual bool VerifyCert(const CertInput& target_der_cert,
                          const std::string& hostname,
                          const std::vector<CertInput>& intermediate_der_certs,
                          const std::vector<CertInput>& root_der_certs,
                          base::Time verify_time,
                          const base::FilePath& dump_prefix_path) = 0;
};

// Runs certificate verification using a particular CertVerifyProc.
class CertVerifyImplUsingProc : public CertVerifyImpl {
 public:
  CertVerifyImplUsingProc(const std::string& name,
                          scoped_refptr<net::CertVerifyProc> proc)
      : name_(name), proc_(std::move(proc)) {}

  std::string GetName() const override { return name_; }

  bool VerifyCert(const CertInput& target_der_cert,
                  const std::string& hostname,
                  const std::vector<CertInput>& intermediate_der_certs,
                  const std::vector<CertInput>& root_der_certs,
                  base::Time verify_time,
                  const base::FilePath& dump_prefix_path) override {
    if (!verify_time.is_null()) {
      std::cerr << "WARNING: --time is not supported by " << GetName()
                << ", will use current time.\n";
    }

    if (hostname.empty()) {
      std::cerr << "ERROR: --hostname is required for " << GetName()
                << ", skipping\n";
      return true;  // "skipping" is considered a successful return.
    }

    return VerifyUsingCertVerifyProc(proc_.get(), target_der_cert, hostname,
                                     intermediate_der_certs, root_der_certs,
                                     dump_prefix_path);
  }

 private:
  const std::string name_;
  scoped_refptr<net::CertVerifyProc> proc_;
};

// Runs certificate verification using CertPathBuilder.
class CertVerifyImplUsingPathBuilder : public CertVerifyImpl {
 public:
  std::string GetName() const override { return "CertPathBuilder"; }

  bool VerifyCert(const CertInput& target_der_cert,
                  const std::string& hostname,
                  const std::vector<CertInput>& intermediate_der_certs,
                  const std::vector<CertInput>& root_der_certs,
                  base::Time verify_time,
                  const base::FilePath& dump_prefix_path) override {
    if (!hostname.empty()) {
      std::cerr << "WARNING: --hostname is not verified with CertPathBuilder\n";
    }

    if (verify_time.is_null()) {
      verify_time = base::Time::Now();
    }

    return VerifyUsingPathBuilder(target_der_cert, intermediate_der_certs,
                                  root_der_certs, verify_time,
                                  dump_prefix_path);
  }
};

const char kUsage[] =
    " [flags] <target/chain>\n"
    "\n"
    " <target/chain> is a file containing certificates [1]. Minimally it\n"
    " contains the target certificate. Optionally it may subsequently list\n"
    " additional certificates needed to build a chain (this is equivalent to\n"
    " specifying them through --intermediates)\n"
    "\n"
    "Flags:\n"
    "\n"
    " --hostname=<hostname>\n"
    "      The hostname required to match the end-entity certificate.\n"
    "      Required for the CertVerifyProc implementation.\n"
    "\n"
    " --roots=<certs path>\n"
    "      <certs path> is a file containing certificates [1] to interpret as\n"
    "      trust anchors (without any anchor constraints).\n"
    "\n"
    " --intermediates=<certs path>\n"
    "      <certs path> is a file containing certificates [1] for use when\n"
    "      path building is looking for intermediates.\n"
    "\n"
    " --time=<time>\n"
    "      Use <time> instead of the current system time. <time> is\n"
    "      interpreted in local time if a timezone is not specified.\n"
    "      Many common formats are supported, including:\n"
    "        1994-11-15 12:45:26 GMT\n"
    "        Tue, 15 Nov 1994 12:45:26 GMT\n"
    "        Nov 15 12:45:26 1994 GMT\n"
    "\n"
    " --dump=<file prefix>\n"
    "      Dumps the verified chain to PEM files starting with\n"
    "      <file prefix>.\n"
    "\n"
    "\n"
    "[1] A \"file containing certificates\" means a path to a file that can\n"
    "    either be:\n"
    "    * A binary file containing a single DER-encoded RFC 5280 Certificate\n"
    "    * A PEM file containing one or more CERTIFICATE blocks (DER-encoded\n"
    "      RFC 5280 Certificate)\n";

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << kUsage;

  // TODO(mattm): allow <certs path> to be a directory containing DER/PEM files?
  // TODO(mattm): allow target to specify an HTTPS URL to check the cert of?
  // TODO(mattm): allow target to be a verify_certificate_chain_unittest .test
  // file?
}

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit_manager;
  base::TaskScheduler::CreateAndStartWithDefaultParams("cert_verify_tool");
  base::ScopedClosureRunner cleanup(
      base::BindOnce([] { base::TaskScheduler::GetInstance()->Shutdown(); }));
  if (!base::CommandLine::Init(argc, argv)) {
    std::cerr << "ERROR in CommandLine::Init\n";
    return 1;
  }
  base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);

  base::CommandLine::StringVector args = command_line.GetArgs();
  if (args.size() != 1U || command_line.HasSwitch("help")) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string hostname = command_line.GetSwitchValueASCII("hostname");

  base::Time verify_time;
  std::string time_flag = command_line.GetSwitchValueASCII("time");
  if (!time_flag.empty()) {
    if (!base::Time::FromString(time_flag.c_str(), &verify_time)) {
      std::cerr << "Error parsing --time flag\n";
      return 1;
    }
  }

  base::FilePath roots_path = command_line.GetSwitchValuePath("roots");
  base::FilePath intermediates_path =
      command_line.GetSwitchValuePath("intermediates");
  base::FilePath target_path = base::FilePath(args[0]);

  base::FilePath dump_prefix_path = command_line.GetSwitchValuePath("dump");

  std::vector<CertInput> root_der_certs;
  std::vector<CertInput> intermediate_der_certs;
  CertInput target_der_cert;

  if (!roots_path.empty())
    ReadCertificatesFromFile(roots_path, &root_der_certs);
  if (!intermediates_path.empty())
    ReadCertificatesFromFile(intermediates_path, &intermediate_der_certs);

  if (!ReadChainFromFile(target_path, &target_der_cert,
                         &intermediate_der_certs)) {
    std::cerr << "ERROR: Couldn't read certificate chain\n";
    return 1;
  }

  if (target_der_cert.der_cert.empty()) {
    std::cerr << "ERROR: no target cert\n";
    return 1;
  }

  // Sequentially run each of the certificate verifier implementations.
  std::vector<std::unique_ptr<CertVerifyImpl>> impls;

  impls.push_back(
      std::unique_ptr<CertVerifyImplUsingProc>(new CertVerifyImplUsingProc(
          "CertVerifyProc (default)", net::CertVerifyProc::CreateDefault())));
  impls.push_back(std::make_unique<CertVerifyImplUsingProc>(
      "CertVerifyProcBuiltin", net::CreateCertVerifyProcBuiltin()));
  impls.push_back(std::make_unique<CertVerifyImplUsingPathBuilder>());

  bool all_impls_success = true;

  for (size_t i = 0; i < impls.size(); ++i) {
    if (i != 0)
      std::cout << "\n";

    std::cout << impls[i]->GetName() << ":\n";
    if (!impls[i]->VerifyCert(target_der_cert, hostname, intermediate_der_certs,
                              root_der_certs, verify_time, dump_prefix_path)) {
      all_impls_success = false;
    }
  }

  return all_impls_success ? 0 : 1;
}
