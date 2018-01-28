// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_SERVER_PROPERTIES_MANAGER_H_
#define NET_HTTP_HTTP_SERVER_PROPERTIES_MANAGER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_server_properties_impl.h"
#include "net/log/net_log_with_source.h"

namespace base {
class DictionaryValue;
}

namespace net {

class IPAddress;

////////////////////////////////////////////////////////////////////////////////
// HttpServerPropertiesManager

// The manager for creating and updating an HttpServerProperties (for example it
// tracks if a server supports SPDY or not).
class NET_EXPORT HttpServerPropertiesManager : public HttpServerProperties {
 public:
  // Provides an interface to interact with persistent preferences storage
  // implemented by the embedder. The prefs are assumed not to have been loaded
  // before HttpServerPropertiesManager construction.
  class NET_EXPORT PrefDelegate {
   public:
    virtual ~PrefDelegate();

    // Returns the branch of the preferences system for the server properties.
    // Returns nullptr if the pref system has no data for the server properties.
    virtual const base::DictionaryValue* GetServerProperties() const = 0;

    // Sets the server properties to the given value.
    virtual void SetServerProperties(const base::DictionaryValue& value) = 0;

    // Starts listening for external storage changes. There will only be one
    // callback active at a time. The first time the |callback| is invoked is
    // expected to mean the initial pref store values have been loaded.
    virtual void StartListeningForUpdates(const base::Closure& callback) = 0;
  };

  // Create an instance of the HttpServerPropertiesManager.
  //
  // Server propertise will be loaded from |pref_delegate| the first time it
  // notifies the HttpServerPropertiesManager of an update, indicating the prefs
  // have been loaded from disk.
  //
  // |clock| is used for setting expiration times and scheduling the
  // expiration of broken alternative services. If null, the default clock will
  // be used.
  HttpServerPropertiesManager(std::unique_ptr<PrefDelegate> pref_delegate,
                              NetLog* net_log,
                              base::TickClock* clock = nullptr);

  ~HttpServerPropertiesManager() override;

  // Helper function for unit tests to set the version in the dictionary.
  static void SetVersion(base::DictionaryValue* http_server_properties_dict,
                         int version_number);

  // ----------------------------------
  // HttpServerProperties methods:
  // ----------------------------------

  void Clear() override;
  bool SupportsRequestPriority(const url::SchemeHostPort& server) override;
  bool GetSupportsSpdy(const url::SchemeHostPort& server) override;
  void SetSupportsSpdy(const url::SchemeHostPort& server,
                       bool support_spdy) override;
  bool RequiresHTTP11(const HostPortPair& server) override;
  void SetHTTP11Required(const HostPortPair& server) override;
  void MaybeForceHTTP11(const HostPortPair& server,
                        SSLConfig* ssl_config) override;
  AlternativeServiceInfoVector GetAlternativeServiceInfos(
      const url::SchemeHostPort& origin) override;
  bool SetHttp2AlternativeService(const url::SchemeHostPort& origin,
                                  const AlternativeService& alternative_service,
                                  base::Time expiration) override;
  bool SetQuicAlternativeService(
      const url::SchemeHostPort& origin,
      const AlternativeService& alternative_service,
      base::Time expiration,
      const QuicTransportVersionVector& advertised_versions) override;
  bool SetAlternativeServices(const url::SchemeHostPort& origin,
                              const AlternativeServiceInfoVector&
                                  alternative_service_info_vector) override;
  void MarkAlternativeServiceBroken(
      const AlternativeService& alternative_service) override;
  void MarkAlternativeServiceRecentlyBroken(
      const AlternativeService& alternative_service) override;
  bool IsAlternativeServiceBroken(
      const AlternativeService& alternative_service) const override;
  bool WasAlternativeServiceRecentlyBroken(
      const AlternativeService& alternative_service) override;
  void ConfirmAlternativeService(
      const AlternativeService& alternative_service) override;
  const AlternativeServiceMap& alternative_service_map() const override;
  std::unique_ptr<base::Value> GetAlternativeServiceInfoAsValue()
      const override;
  bool GetSupportsQuic(IPAddress* last_address) const override;
  void SetSupportsQuic(bool used_quic, const IPAddress& last_address) override;
  void SetServerNetworkStats(const url::SchemeHostPort& server,
                             ServerNetworkStats stats) override;
  void ClearServerNetworkStats(const url::SchemeHostPort& server) override;
  const ServerNetworkStats* GetServerNetworkStats(
      const url::SchemeHostPort& server) override;
  const ServerNetworkStatsMap& server_network_stats_map() const override;
  bool SetQuicServerInfo(const QuicServerId& server_id,
                         const std::string& server_info) override;
  const std::string* GetQuicServerInfo(const QuicServerId& server_id) override;
  const QuicServerInfoMap& quic_server_info_map() const override;
  size_t max_server_configs_stored_in_properties() const override;
  void SetMaxServerConfigsStoredInProperties(
      size_t max_server_configs_stored_in_properties) override;
  bool IsInitialized() const override;

  static base::TimeDelta GetUpdateCacheDelayForTesting();
  static base::TimeDelta GetUpdatePrefsDelayForTesting();

  void ScheduleUpdateCacheForTesting();

 protected:
  // The location where ScheduleUpdatePrefs was called.
  // Must be kept up to date with HttpServerPropertiesUpdatePrefsLocation in
  // histograms.xml.
  enum Location {
    SUPPORTS_SPDY = 0,
    HTTP_11_REQUIRED = 1,
    SET_ALTERNATIVE_SERVICES = 2,
    MARK_ALTERNATIVE_SERVICE_BROKEN = 3,
    MARK_ALTERNATIVE_SERVICE_RECENTLY_BROKEN = 4,
    CONFIRM_ALTERNATIVE_SERVICE = 5,
    CLEAR_ALTERNATIVE_SERVICE = 6,
    // deprecated: SET_SPDY_SETTING = 7,
    // deprecated: CLEAR_SPDY_SETTINGS = 8,
    // deprecated: CLEAR_ALL_SPDY_SETTINGS = 9,
    SET_SUPPORTS_QUIC = 10,
    SET_SERVER_NETWORK_STATS = 11,
    DETECTED_CORRUPTED_PREFS = 12,
    SET_QUIC_SERVER_INFO = 13,
    CLEAR_SERVER_NETWORK_STATS = 14,
    NUM_LOCATIONS = 15,
  };

  // --------------------
  // SPDY related methods

  // These are used to delay updating of the cached data in
  // |http_server_properties_impl_| while the preferences are changing, and
  // execute only one update per simultaneous prefs changes.
  void ScheduleUpdateCache();

  // Update cached prefs in |http_server_properties_impl_| with data from
  // preferences.
  void UpdateCacheFromPrefs();

  // These are used to delay updating the preferences when cached data in
  // |http_server_properties_impl_| is changing, and execute only one update per
  // simultaneous changes.
  // |location| specifies where this method is called from.
  void ScheduleUpdatePrefs(Location location);

  // Update prefs::kHttpServerProperties in preferences with the cached data
  // from |http_server_properties_impl_|.
  void UpdatePrefsFromCache();

 private:
  FRIEND_TEST_ALL_PREFIXES(HttpServerPropertiesManagerTest,
                           AddToAlternativeServiceMap);
  FRIEND_TEST_ALL_PREFIXES(HttpServerPropertiesManagerTest,
                           ReadAdvertisedVersionsFromPref);
  FRIEND_TEST_ALL_PREFIXES(HttpServerPropertiesManagerTest,
                           DoNotLoadAltSvcForInsecureOrigins);
  FRIEND_TEST_ALL_PREFIXES(HttpServerPropertiesManagerTest,
                           DoNotLoadExpiredAlternativeService);
  void OnHttpServerPropertiesChanged();

  bool AddServersData(const base::DictionaryValue& server_dict,
                      SpdyServersMap* spdy_servers_map,
                      AlternativeServiceMap* alternative_service_map,
                      ServerNetworkStatsMap* network_stats_map,
                      int version);
  // Helper method used for parsing an alternative service from JSON.
  // |dict| is the JSON dictionary to be parsed. It should contain fields
  // corresponding to members of AlternativeService.
  // |host_optional| determines whether or not the "host" field is optional. If
  // optional, the default value is empty string.
  // |parsing_under| is used only for debug log outputs in case of error; it
  // should describe what section of the JSON prefs is currently being parsed.
  // |alternative_service| is the output of parsing |dict|.
  // Return value is true if parsing is successful.
  bool ParseAlternativeServiceDict(const base::DictionaryValue& dict,
                                   bool host_optional,
                                   const std::string& parsing_under,
                                   AlternativeService* alternative_service);
  bool ParseAlternativeServiceInfoDictOfServer(
      const base::DictionaryValue& dict,
      const std::string& server_str,
      AlternativeServiceInfo* alternative_service_info);
  bool AddToAlternativeServiceMap(
      const url::SchemeHostPort& server,
      const base::DictionaryValue& server_dict,
      AlternativeServiceMap* alternative_service_map);
  bool ReadSupportsQuic(const base::DictionaryValue& server_dict,
                        IPAddress* last_quic_address);
  bool AddToNetworkStatsMap(const url::SchemeHostPort& server,
                            const base::DictionaryValue& server_dict,
                            ServerNetworkStatsMap* network_stats_map);
  bool AddToQuicServerInfoMap(const base::DictionaryValue& server_dict,
                              QuicServerInfoMap* quic_server_info_map);
  bool AddToBrokenAlternativeServices(
      const base::DictionaryValue& broken_alt_svc_entry_dict,
      BrokenAlternativeServiceList* broken_alternative_service_list,
      RecentlyBrokenAlternativeServices* recently_broken_alternative_services);

  void SaveAlternativeServiceToServerPrefs(
      const AlternativeServiceInfoVector& alternative_service_info_vector,
      base::DictionaryValue* server_pref_dict);
  void SaveSupportsQuicToPrefs(
      const IPAddress& last_quic_address,
      base::DictionaryValue* http_server_properties_dict);
  void SaveNetworkStatsToServerPrefs(
      const ServerNetworkStats& server_network_stats,
      base::DictionaryValue* server_pref_dict);
  void SaveQuicServerInfoMapToServerPrefs(
      const QuicServerInfoMap& quic_server_info_map,
      base::DictionaryValue* http_server_properties_dict);
  void SaveBrokenAlternativeServicesToPrefs(
      const BrokenAlternativeServiceList* broken_alternative_service_list,
      const RecentlyBrokenAlternativeServices*
          recently_broken_alternative_services,
      base::DictionaryValue* http_server_properties_dict);

  base::DefaultTickClock default_clock_;

  // Used to post cache update tasks.
  base::OneShotTimer pref_cache_update_timer_;

  std::unique_ptr<PrefDelegate> pref_delegate_;
  // Set to true while modifying prefs, to avoid loading those prefs again as a
  // result of them being changed by the changes just made by this class.
  bool setting_prefs_ = false;

  base::TickClock* clock_;  // Unowned

  // Set to true once the initial prefs have been loaded.
  bool is_initialized_ = false;

  // Used to post |prefs::kHttpServerProperties| pref update tasks.
  base::OneShotTimer network_prefs_update_timer_;

  std::unique_ptr<HttpServerPropertiesImpl> http_server_properties_impl_;

  const NetLogWithSource net_log_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(HttpServerPropertiesManager);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_SERVER_PROPERTIES_MANAGER_H_
