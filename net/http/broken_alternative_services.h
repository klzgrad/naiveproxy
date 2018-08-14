// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_BROKEN_ALTERNATIVE_SERVICES_H_
#define NET_HTTP_BROKEN_ALTERNATIVE_SERVICES_H_

#include <list>
#include <unordered_map>

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "net/http/http_server_properties.h"

namespace base {
class TickClock;
}

namespace net {

// This class tracks HTTP alternative services that have been marked as broken.
// The brokenness of an alt-svc will expire after some time according to an
// exponential back-off formula: each time an alt-svc is marked broken, the
// expiration delay will be some constant multiple of its previous expiration
// delay. This prevents broken alt-svcs from being retried too often by the
// network stack.
class NET_EXPORT_PRIVATE BrokenAlternativeServices {
 public:
  // Delegate to be used by owner so it can be notified when the brokenness of
  // an AlternativeService expires.
  class NET_EXPORT Delegate {
   public:
    // Called when a broken alternative service's expiration time is reached.
    virtual void OnExpireBrokenAlternativeService(
        const AlternativeService& expired_alternative_service) = 0;
    virtual ~Delegate() {}
  };

  // |delegate| will be notified when a broken alternative service expires. It
  // must not be null.
  // |clock| is used for setting expiration times and scheduling the
  // expiration of broken alternative services. It must not be null.
  // |delegate| and |clock| are both unowned and must outlive this.
  BrokenAlternativeServices(Delegate* delegate, const base::TickClock* clock);

  BrokenAlternativeServices(const BrokenAlternativeServices&) = delete;
  void operator=(const BrokenAlternativeServices&) = delete;

  ~BrokenAlternativeServices();

  // Clears all broken and recently-broken alternative services (i.e. mark all
  // as not broken nor recently-broken).
  void Clear();

  // Marks |alternative_service| as broken until after some expiration delay
  // (determined by how many times it's been marked broken before). Being broken
  // will cause IsAlternativeServiceBroken(alternative_service) to return true
  // until the expiration time is reached, or until
  // ConfirmAlternativeService(alternative_service) is called.
  void MarkAlternativeServiceBroken(
      const AlternativeService& alternative_service);

  // Marks |alternative_service| as recently broken. Being recently broken will
  // cause WasAlternativeServiceRecentlyBroken(alternative_service) to return
  // true until ConfirmAlternativeService(alternative_service) is called.
  void MarkAlternativeServiceRecentlyBroken(
      const AlternativeService& alternative_service);

  // Returns true if MarkAlternativeServiceBroken(alternative_service) has been
  // called, the expiration time has not been reached, and
  // ConfirmAlternativeService(alternative_service) has not been called
  // afterwards.
  bool IsAlternativeServiceBroken(
      const AlternativeService& alternative_service) const;

  // Same as IsAlternativeServiceBroken() defined above, but will also set
  // |brokenness_expiration| to when |alternative_service|'s brokenness will
  // expire if this function returns true.
  bool IsAlternativeServiceBroken(const AlternativeService& alternative_service,
                                  base::TimeTicks* brokenness_expiration) const;

  // Returns true if MarkAlternativeServiceRecentlyBroken(alternative_service)
  // or MarkAlternativeServiceBroken(alternative_service) has been called and
  // ConfirmAlternativeService(alternative_service) has not been called
  // afterwards (even if brokenness of |alternative_service| has expired).
  bool WasAlternativeServiceRecentlyBroken(
      const AlternativeService& alternative_service);

  // Marks |alternative_service| as not broken and not recently broken.
  void ConfirmAlternativeService(const AlternativeService& alternative_service);

  // Sets broken and recently broken alternative services.
  // |broken_alternative_service_list|, |recently_broken_alternative_services|
  // must not be nullptr.
  //
  // If a broken/recently-broken alt svc that's being added is already stored,
  // the stored expiration/broken-count for that alt svc will be overwritten
  // with the new value.
  void SetBrokenAndRecentlyBrokenAlternativeServices(
      std::unique_ptr<BrokenAlternativeServiceList>
          broken_alternative_service_list,
      std::unique_ptr<RecentlyBrokenAlternativeServices>
          recently_broken_alternative_services);

  const BrokenAlternativeServiceList& broken_alternative_service_list() const;

  const RecentlyBrokenAlternativeServices&
  recently_broken_alternative_services() const;

 private:
  // TODO (wangyix): modify HttpServerPropertiesImpl unit tests so this
  // friendness is no longer required.
  friend class HttpServerPropertiesImplPeer;

  struct AlternativeServiceHash {
    size_t operator()(const net::AlternativeService& entry) const {
      return entry.protocol ^ std::hash<std::string>()(entry.host) ^ entry.port;
    }
  };

  typedef std::unordered_map<AlternativeService,
                             BrokenAlternativeServiceList::iterator,
                             AlternativeServiceHash>
      BrokenAlternativeServiceMap;

  // Inserts |alternative_service| and its |expiration| time into
  // |broken_alternative_service_list_| and |broken_alternative_service_map_|.
  // |it| is the position in |broken_alternative_service_list_| where it was
  // inserted.
  bool AddToBrokenAlternativeServiceListAndMap(
      const AlternativeService& alternative_service,
      base::TimeTicks expiration,
      BrokenAlternativeServiceList::iterator* it);

  void ExpireBrokenAlternateProtocolMappings();
  void ScheduleBrokenAlternateProtocolMappingsExpiration();

  Delegate* delegate_;            // Unowned
  const base::TickClock* clock_;  // Unowned

  // List of <broken alt svc, expiration time> pairs sorted by expiration time.
  BrokenAlternativeServiceList broken_alternative_service_list_;
  // A map from broken alt-svcs to their iterator pointing to that alt-svc's
  // position in |broken_alternative_service_list_|.
  BrokenAlternativeServiceMap broken_alternative_service_map_;

  // Maps broken alternative services to how many times they've been marked
  // broken.
  RecentlyBrokenAlternativeServices recently_broken_alternative_services_;

  // Used for scheduling the task that expires the brokenness of alternative
  // services.
  base::OneShotTimer expiration_timer_;

  base::WeakPtrFactory<BrokenAlternativeServices> weak_ptr_factory_;
};

}  // namespace net

#endif  // NET_HTTP_BROKEN_ALTERNATIVE_SERVICES_H_
