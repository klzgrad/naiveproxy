// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/system_trust_store.h"

#include "base/memory/ptr_util.h"
#include "build/build_config.h"
#include "crypto/crypto_buildflags.h"

#if BUILDFLAG(USE_NSS_CERTS)
#include "net/cert/internal/system_trust_store_nss.h"
#endif  // BUILDFLAG(USE_NSS_CERTS)

#if BUILDFLAG(USE_NSS_CERTS)
#include <cert.h>
#include <pk11pub.h>
#elif BUILDFLAG(IS_MAC)
#include <Security/Security.h>
#endif

#include <array>
#include <memory>
#include <vector>

#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "net/cert/pki/cert_errors.h"
#include "net/cert/pki/parsed_certificate.h"
#include "net/cert/pki/trust_store_collection.h"
#include "net/cert/pki/trust_store_in_memory.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"

#if BUILDFLAG(USE_NSS_CERTS)
#include "crypto/nss_util.h"
#include "net/cert/internal/trust_store_nss.h"
#include "net/cert/known_roots_nss.h"
#include "net/cert/scoped_nss_types.h"
#elif BUILDFLAG(IS_MAC)
#include "net/base/features.h"
#include "net/cert/internal/trust_store_mac.h"
#include "net/cert/x509_util_apple.h"
#elif BUILDFLAG(IS_FUCHSIA)
#include "base/lazy_instance.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#elif BUILDFLAG(IS_WIN)
#include "net/cert/internal/trust_store_win.h"
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
#include "base/lazy_instance.h"
#endif
#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
#include "net/cert/internal/trust_store_chrome.h"
#endif  // CHROME_ROOT_STORE_SUPPORTED

namespace net {

namespace {

class DummySystemTrustStore : public SystemTrustStore {
 public:
  TrustStore* GetTrustStore() override { return &trust_store_; }

  bool UsesSystemTrustStore() const override { return false; }

  bool IsKnownRoot(const ParsedCertificate* trust_anchor) const override {
    return false;
  }

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
  int64_t chrome_root_store_version() override { return 0; }
#endif

 private:
  TrustStoreCollection trust_store_;
};

}  // namespace

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
class SystemTrustStoreChromeWithUnOwnedSystemStore : public SystemTrustStore {
 public:
  // Creates a SystemTrustStore that gets publicly trusted roots from
  // |trust_store_chrome| and local trust settings from |trust_store_system|.
  // Does not take ownership of |trust_store_system|, which must outlive this
  // object.
  explicit SystemTrustStoreChromeWithUnOwnedSystemStore(
      std::unique_ptr<TrustStoreChrome> trust_store_chrome,
      TrustStore* trust_store_system)
      : trust_store_chrome_(std::move(trust_store_chrome)) {
    trust_store_collection_.AddTrustStore(trust_store_system);
    trust_store_collection_.AddTrustStore(trust_store_chrome_.get());
  }

  TrustStore* GetTrustStore() override { return &trust_store_collection_; }

  bool UsesSystemTrustStore() const override { return true; }

  // IsKnownRoot returns true if the given trust anchor is a standard one (as
  // opposed to a user-installed root)
  bool IsKnownRoot(const ParsedCertificate* trust_anchor) const override {
    return trust_store_chrome_->Contains(trust_anchor);
  }

  int64_t chrome_root_store_version() override {
    return trust_store_chrome_->version();
  }

 private:
  std::unique_ptr<TrustStoreChrome> trust_store_chrome_;
  TrustStoreCollection trust_store_collection_;
};

class SystemTrustStoreChrome
    : public SystemTrustStoreChromeWithUnOwnedSystemStore {
 public:
  // Creates a SystemTrustStore that gets publicly trusted roots from
  // |trust_store_chrome| and local trust settings from |trust_store_system|.
  explicit SystemTrustStoreChrome(
      std::unique_ptr<TrustStoreChrome> trust_store_chrome,
      std::unique_ptr<TrustStore> trust_store_system)
      : SystemTrustStoreChromeWithUnOwnedSystemStore(
            std::move(trust_store_chrome),
            trust_store_system.get()),
        trust_store_system_(std::move(trust_store_system)) {}

 private:
  std::unique_ptr<TrustStore> trust_store_system_;
};

std::unique_ptr<SystemTrustStore> CreateSystemTrustStoreChromeForTesting(
    std::unique_ptr<TrustStoreChrome> trust_store_chrome,
    std::unique_ptr<TrustStore> trust_store_system) {
  return std::make_unique<SystemTrustStoreChrome>(
      std::move(trust_store_chrome), std::move(trust_store_system));
}
#endif  // CHROME_ROOT_STORE_SUPPORTED

#if BUILDFLAG(USE_NSS_CERTS)
namespace {

class SystemTrustStoreNSS : public SystemTrustStore {
 public:
  explicit SystemTrustStoreNSS(std::unique_ptr<TrustStoreNSS> trust_store_nss)
      : trust_store_nss_(std::move(trust_store_nss)) {}

  TrustStore* GetTrustStore() override { return trust_store_nss_.get(); }

  bool UsesSystemTrustStore() const override { return true; }

  // IsKnownRoot returns true if the given trust anchor is a standard one (as
  // opposed to a user-installed root)
  bool IsKnownRoot(const ParsedCertificate* trust_anchor) const override {
    // TODO(eroman): The overall approach of IsKnownRoot() is inefficient -- it
    // requires searching for the trust anchor by DER in NSS, however path
    // building already had a handle to it.
    SECItem der_cert;
    der_cert.data = const_cast<uint8_t*>(trust_anchor->der_cert().UnsafeData());
    der_cert.len = trust_anchor->der_cert().Length();
    der_cert.type = siDERCertBuffer;
    ScopedCERTCertificate nss_cert(
        CERT_FindCertByDERCert(CERT_GetDefaultCertDB(), &der_cert));
    if (!nss_cert)
      return false;

    if (!net::IsKnownRoot(nss_cert.get()))
      return false;

    return trust_anchor->der_cert() ==
           der::Input(nss_cert->derCert.data, nss_cert->derCert.len);
  }

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
  int64_t chrome_root_store_version() override { return 0; }
#endif

 private:
  std::unique_ptr<TrustStoreNSS> trust_store_nss_;
};

}  // namespace

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<SystemTrustStoreNSS>(std::make_unique<TrustStoreNSS>(
      TrustStoreNSS::kUseSystemTrust,
      TrustStoreNSS::UseTrustFromAllUserSlots()));
}

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChrome>(
      std::move(chrome_root), std::make_unique<TrustStoreNSS>(
                                  TrustStoreNSS::kIgnoreSystemTrust,
                                  TrustStoreNSS::UseTrustFromAllUserSlots()));
}

std::unique_ptr<SystemTrustStore>
CreateSslSystemTrustStoreChromeRootWithUserSlotRestriction(
    std::unique_ptr<TrustStoreChrome> chrome_root,
    crypto::ScopedPK11Slot user_slot_restriction) {
  return std::make_unique<SystemTrustStoreChrome>(
      std::move(chrome_root),
      std::make_unique<TrustStoreNSS>(TrustStoreNSS::kIgnoreSystemTrust,
                                      std::move(user_slot_restriction)));
}

#endif  // CHROME_ROOT_STORE_SUPPORTED

std::unique_ptr<SystemTrustStore>
CreateSslSystemTrustStoreNSSWithUserSlotRestriction(
    crypto::ScopedPK11Slot user_slot_restriction) {
  return std::make_unique<SystemTrustStoreNSS>(std::make_unique<TrustStoreNSS>(
      TrustStoreNSS::kUseSystemTrust, std::move(user_slot_restriction)));
}

#elif BUILDFLAG(IS_MAC)

// Using the Builtin Verifier w/o the Chrome Root Store is unsupported on
// Mac.
std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<DummySystemTrustStore>();
}

namespace {

TrustStoreMac* GetGlobalTrustStoreMacForCRS() {
  constexpr TrustStoreMac::TrustImplType kDefaultMacTrustImplForCRS =
      TrustStoreMac::TrustImplType::kDomainCacheFullCerts;
  static base::NoDestructor<TrustStoreMac> static_trust_store_mac(
      kSecPolicyAppleSSL, kDefaultMacTrustImplForCRS);
  return static_trust_store_mac.get();
}

void InitializeTrustCacheForCRSOnWorkerThread() {
  GetGlobalTrustStoreMacForCRS()->InitializeTrustCache();
}

}  // namespace

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChromeWithUnOwnedSystemStore>(
      std::move(chrome_root), GetGlobalTrustStoreMacForCRS());
}

void InitializeTrustStoreMacCache() {
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&InitializeTrustCacheForCRSOnWorkerThread));
}

#elif BUILDFLAG(IS_FUCHSIA)

namespace {

constexpr char kRootCertsFileFuchsia[] = "/config/ssl/cert.pem";

class FuchsiaSystemCerts {
 public:
  FuchsiaSystemCerts() {
    base::FilePath filename(kRootCertsFileFuchsia);
    std::string certs_file;
    if (!base::ReadFileToString(filename, &certs_file)) {
      LOG(ERROR) << "Can't load root certificates from " << filename;
      return;
    }

    CertificateList certs = X509Certificate::CreateCertificateListFromBytes(
        base::as_bytes(base::make_span(certs_file)),
        X509Certificate::FORMAT_AUTO);

    for (const auto& cert : certs) {
      CertErrors errors;
      auto parsed = ParsedCertificate::Create(
          bssl::UpRef(cert->cert_buffer()),
          x509_util::DefaultParseCertificateOptions(), &errors);
      CHECK(parsed) << errors.ToDebugString();
      system_trust_store_.AddTrustAnchor(parsed);
    }
  }

  TrustStoreInMemory* system_trust_store() { return &system_trust_store_; }

 private:
  TrustStoreInMemory system_trust_store_;
};

base::LazyInstance<FuchsiaSystemCerts>::Leaky g_root_certs_fuchsia =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

class SystemTrustStoreFuchsia : public SystemTrustStore {
 public:
  SystemTrustStoreFuchsia() = default;

  TrustStore* GetTrustStore() override {
    return g_root_certs_fuchsia.Get().system_trust_store();
  }

  bool UsesSystemTrustStore() const override { return true; }

  bool IsKnownRoot(const ParsedCertificate* trust_anchor) const override {
    return g_root_certs_fuchsia.Get().system_trust_store()->Contains(
        trust_anchor);
  }
};

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<SystemTrustStoreFuchsia>();
}

#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)

namespace {

// Copied from https://golang.org/src/crypto/x509/root_linux.go
// Possible certificate files; stop after finding one.
constexpr std::array<const char*, 6> kStaticRootCertFiles = {
    "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu/Gentoo etc.
    "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora/RHEL 6
    "/etc/ssl/ca-bundle.pem",              // OpenSUSE
    "/etc/pki/tls/cacert.pem",             // OpenELEC
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  // CentOS/RHEL 7
    "/etc/ssl/cert.pem",                                  // Alpine Linux
};

// Possible directories with certificate files; stop after successfully
// reading at least one file from a directory.
constexpr std::array<const char*, 3> kStaticRootCertDirs = {
    "/etc/ssl/certs",      // SLES10/SLES11, https://golang.org/issue/12139
    "/etc/pki/tls/certs",  // Fedora/RHEL
    "/system/etc/security/cacerts",  // Android
};

// The environment variable which identifies where to locate the SSL
// certificate file. If set this overrides the system default.
constexpr char kStaticCertFileEnv[] = "SSL_CERT_FILE";

// The environment variable which identifies which directory to check for SSL
// certificate files. If set this overrides the system default. It is a colon
// separated list of directories.
// See https://www.openssl.org/docs/man1.0.2/man1/c_rehash.html.
constexpr char kStaticCertDirsEnv[] = "SSL_CERT_DIR";

class StaticUnixSystemCerts {
 public:
  StaticUnixSystemCerts() : system_trust_store_(Create()) {}

  TrustStoreInMemory* system_trust_store() { return system_trust_store_.get(); }

  static std::unique_ptr<TrustStoreInMemory> Create() {
    auto ptr = std::make_unique<TrustStoreInMemory>();
    auto env = base::Environment::Create();
    std::string env_value;

    std::vector<std::string> cert_filenames(kStaticRootCertFiles.begin(),
                                            kStaticRootCertFiles.end());
    if (env->GetVar(kStaticCertFileEnv, &env_value) && !env_value.empty()) {
      cert_filenames = {env_value};
    }

    bool cert_file_ok = false;
    for (const auto& filename : cert_filenames) {
      std::string file;
      if (!base::ReadFileToString(base::FilePath(filename), &file))
        continue;
      if (AddCertificatesFromBytes(file.data(), file.size(), ptr.get())) {
        cert_file_ok = true;
        break;
      }
    }

    std::vector<std::string> cert_dirnames(kStaticRootCertDirs.begin(),
                                           kStaticRootCertDirs.end());
    if (env->GetVar(kStaticCertDirsEnv, &env_value) && !env_value.empty()) {
      cert_dirnames = base::SplitString(env_value, ":", base::TRIM_WHITESPACE,
                                        base::SPLIT_WANT_NONEMPTY);
    }

    bool cert_dir_ok = false;
    for (const auto& dir : cert_dirnames) {
      base::FileEnumerator e(base::FilePath(dir),
                             /*recursive=*/true, base::FileEnumerator::FILES);
      for (auto filename = e.Next(); !filename.empty(); filename = e.Next()) {
        std::string file;
        if (!base::ReadFileToString(filename, &file)) {
          continue;
        }
        if (AddCertificatesFromBytes(file.data(), file.size(), ptr.get())) {
          cert_dir_ok = true;
        }
      }
      if (cert_dir_ok)
        break;
    }

    if (!cert_file_ok && !cert_dir_ok) {
      LOG(ERROR) << "No CA certificates were found. Try using environment "
                    "variable SSL_CERT_FILE or SSL_CERT_DIR";
    }

    return ptr;
  }

 private:
  static bool AddCertificatesFromBytes(const char* data,
                                       size_t length,
                                       TrustStoreInMemory* store) {
    auto certs = X509Certificate::CreateCertificateListFromBytes(
        {reinterpret_cast<const uint8_t*>(data), length},
        X509Certificate::FORMAT_AUTO);
    bool certs_ok = false;
    for (const auto& cert : certs) {
      CertErrors errors;
      auto parsed = ParsedCertificate::Create(
          bssl::UpRef(cert->cert_buffer()),
          x509_util::DefaultParseCertificateOptions(), &errors);
      if (parsed) {
        if (!store->Contains(parsed.get())) {
          store->AddTrustAnchor(parsed);
        }
        certs_ok = true;
      } else {
        LOG(ERROR) << errors.ToDebugString();
      }
    }
    return certs_ok;
  }

  std::unique_ptr<TrustStoreInMemory> system_trust_store_;
};

base::LazyInstance<StaticUnixSystemCerts>::Leaky g_root_certs_static_unix =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

class SystemTrustStoreStaticUnix : public SystemTrustStore {
 public:
  SystemTrustStoreStaticUnix() = default;

  TrustStore* GetTrustStore() override {
    return g_root_certs_static_unix.Get().system_trust_store();
  }

  bool UsesSystemTrustStore() const override { return true; }

  bool IsKnownRoot(const ParsedCertificate* trust_anchor) const override {
    return g_root_certs_static_unix.Get().system_trust_store()->Contains(
        trust_anchor);
  }

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
  int64_t chrome_root_store_version() override { return 0; }
#endif
};

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<SystemTrustStoreStaticUnix>();
}

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChrome>(
      std::move(chrome_root), StaticUnixSystemCerts::Create());
}

#else

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot() {
  return std::make_unique<DummySystemTrustStore>();
}

#endif  // CHROME_ROOT_STORE_SUPPORTED

#elif BUILDFLAG(IS_WIN)

// Using the Builtin Verifier w/o the Chrome Root Store is unsupported on
// Windows.
std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<DummySystemTrustStore>();
}

namespace {
TrustStoreWin* GetGlobalTrustStoreWinForCRS() {
  static base::NoDestructor<TrustStoreWin> static_trust_store_win;
  return static_trust_store_win.get();
}

void InitializeTrustStoreForCRSOnWorkerThread() {
  GetGlobalTrustStoreWinForCRS()->InitializeStores();
}
}  // namespace

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChromeWithUnOwnedSystemStore>(
      std::move(chrome_root), GetGlobalTrustStoreWinForCRS());
}

// We do this in a separate thread as loading the Windows Cert Stores can cause
// quite a bit of I/O. See crbug.com/1399974 for more context.
void InitializeTrustStoreWinSystem() {
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&InitializeTrustStoreForCRSOnWorkerThread));
}

#elif BUILDFLAG(IS_ANDROID)

// Using the Builtin Verifier w/o the Chrome Root Store is unsupported on
// Android.
std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<DummySystemTrustStore>();
}

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)

namespace {
TrustStoreAndroid* GetGlobalTrustStoreAndroidForCRS() {
  static base::NoDestructor<TrustStoreAndroid> static_trust_store_android;
  return static_trust_store_android.get();
}

void InitializeTrustStoreForCRSOnWorkerThread() {
  GetGlobalTrustStoreAndroidForCRS()->Initialize();
}
}  // namespace

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChromeWithUnOwnedSystemStore>(
      std::move(chrome_root), GetGlobalTrustStoreAndroidForCRS());
}

void InitializeTrustStoreAndroid() {
  // Start observing DB change before the Trust Store is initialized so we don't
  // accidentally miss any changes. See https://crrev.com/c/4226436 for context.
  //
  // This call is safe here because we're the only callers of
  // ObserveCertDBChanges on the singleton TrustStoreAndroid.
  GetGlobalTrustStoreAndroidForCRS()->ObserveCertDBChanges();

  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&InitializeTrustStoreForCRSOnWorkerThread));
}

#else

void InitializeTrustStoreAndroid() {}

#endif  // CHROME_ROOT_STORE_SUPPORTED

#else

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStore() {
  return std::make_unique<DummySystemTrustStore>();
}

#endif

std::unique_ptr<SystemTrustStore> CreateEmptySystemTrustStore() {
  return std::make_unique<DummySystemTrustStore>();
}

}  // namespace net
