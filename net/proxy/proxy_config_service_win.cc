// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_config_service_win.h"

#include <windows.h>
#include <winhttp.h>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/win/registry.h"
#include "base/win/scoped_handle.h"
#include "net/base/net_errors.h"
#include "net/proxy/proxy_config.h"

namespace net {

namespace {

const int kPollIntervalSec = 10;

void FreeIEConfig(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG* ie_config) {
  if (ie_config->lpszAutoConfigUrl)
    GlobalFree(ie_config->lpszAutoConfigUrl);
  if (ie_config->lpszProxy)
    GlobalFree(ie_config->lpszProxy);
  if (ie_config->lpszProxyBypass)
    GlobalFree(ie_config->lpszProxyBypass);
}

}  // namespace

ProxyConfigServiceWin::ProxyConfigServiceWin()
    : PollingProxyConfigService(
          base::TimeDelta::FromSeconds(kPollIntervalSec),
          &ProxyConfigServiceWin::GetCurrentProxyConfig) {
}

ProxyConfigServiceWin::~ProxyConfigServiceWin() {
  // The registry functions below will end up going to disk.  TODO: Do this on
  // another thread to avoid slowing the IO thread.  http://crbug.com/61453
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  keys_to_watch_.clear();
}

void ProxyConfigServiceWin::AddObserver(Observer* observer) {
  // Lazily-initialize our registry watcher.
  StartWatchingRegistryForChanges();

  // Let the super-class do its work now.
  PollingProxyConfigService::AddObserver(observer);
}

void ProxyConfigServiceWin::StartWatchingRegistryForChanges() {
  if (!keys_to_watch_.empty())
    return;  // Already initialized.

  // The registry functions below will end up going to disk.  Do this on another
  // thread to avoid slowing the IO thread.  http://crbug.com/61453
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  // There are a number of different places where proxy settings can live
  // in the registry. In some cases it appears in a binary value, in other
  // cases string values. Furthermore winhttp and wininet appear to have
  // separate stores, and proxy settings can be configured per-machine
  // or per-user.
  //
  // This function is probably not exhaustive in the registry locations it
  // watches for changes, however it should catch the majority of the
  // cases. In case we have missed some less common triggers (likely), we
  // will catch them during the periodic (10 second) polling, so things
  // will recover.

  AddKeyToWatchList(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings");

  AddKeyToWatchList(
      HKEY_LOCAL_MACHINE,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings");

  AddKeyToWatchList(
      HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Policies\\Microsoft\\Windows\\CurrentVersion\\"
      L"Internet Settings");
}

bool ProxyConfigServiceWin::AddKeyToWatchList(HKEY rootkey,
                                              const wchar_t* subkey) {
  std::unique_ptr<base::win::RegKey> key =
      std::make_unique<base::win::RegKey>();
  if (key->Create(rootkey, subkey, KEY_NOTIFY) != ERROR_SUCCESS)
    return false;

  if (!key->StartWatching(base::Bind(&ProxyConfigServiceWin::OnObjectSignaled,
                                     base::Unretained(this),
                                     base::Unretained(key.get())))) {
    return false;
  }

  keys_to_watch_.push_back(std::move(key));
  return true;
}

void ProxyConfigServiceWin::OnObjectSignaled(base::win::RegKey* key) {
  // Figure out which registry key signalled this change.
  auto it = std::find_if(keys_to_watch_.begin(), keys_to_watch_.end(),
                         [key](const std::unique_ptr<base::win::RegKey>& ptr) {
                           return ptr.get() == key;
                         });
  DCHECK(it != keys_to_watch_.end());

  // Keep watching the registry key.
  if (!key->StartWatching(base::Bind(&ProxyConfigServiceWin::OnObjectSignaled,
                                     base::Unretained(this),
                                     base::Unretained(key)))) {
    keys_to_watch_.erase(it);
  }

  // Have the PollingProxyConfigService test for changes.
  CheckForChangesNow();
}

// static
void ProxyConfigServiceWin::GetCurrentProxyConfig(ProxyConfig* config) {
  WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie_config = {0};
  if (!WinHttpGetIEProxyConfigForCurrentUser(&ie_config)) {
    LOG(ERROR) << "WinHttpGetIEProxyConfigForCurrentUser failed: " <<
        GetLastError();
    *config = ProxyConfig::CreateDirect();
    config->set_source(PROXY_CONFIG_SOURCE_SYSTEM_FAILED);
    return;
  }
  SetFromIEConfig(config, ie_config);
  FreeIEConfig(&ie_config);
}

// static
void ProxyConfigServiceWin::SetFromIEConfig(
    ProxyConfig* config,
    const WINHTTP_CURRENT_USER_IE_PROXY_CONFIG& ie_config) {
  if (ie_config.fAutoDetect)
    config->set_auto_detect(true);
  if (ie_config.lpszProxy) {
    // lpszProxy may be a single proxy, or a proxy per scheme. The format
    // is compatible with ProxyConfig::ProxyRules's string format.
    config->proxy_rules().ParseFromString(
        base::UTF16ToASCII(ie_config.lpszProxy));
  }
  if (ie_config.lpszProxyBypass) {
    std::string proxy_bypass = base::UTF16ToASCII(ie_config.lpszProxyBypass);

    base::StringTokenizer proxy_server_bypass_list(proxy_bypass, ";, \t\n\r");
    while (proxy_server_bypass_list.GetNext()) {
      std::string bypass_url_domain = proxy_server_bypass_list.token();
      config->proxy_rules().bypass_rules.AddRuleFromString(bypass_url_domain);
    }
  }
  if (ie_config.lpszAutoConfigUrl)
    config->set_pac_url(GURL(ie_config.lpszAutoConfigUrl));
  config->set_source(PROXY_CONFIG_SOURCE_SYSTEM);
}

}  // namespace net
