# Reporting

Reporting is a central mechanism for sending out-of-band error reports
to origins from various other components (e.g. HTTP Public Key Pinning,
Interventions, or Content Security Policy could potentially use it).

The parts of it that are exposed to the web platform are specified in
the [draft spec](http://wicg.github.io/reporting/). This document
assumes that you've read that one.

## Reporting in Chromium

Reporting is implemented as part of the network stack in Chromium, such
that it can be used by other parts of the network stack (e.g. HPKP) or
by non-browser embedders as well as by Chromium.

Almost all of Reporting lives in `//net/reporting`; there is a small
amount of code in `//chrome/browser/net` to set up Reporting in
profiles and provide a persistent store for reports and endpoints
across browser restarts.

### Inside `//net`

* The top-level class is the *`ReportingService`*. This lives in the
  `URLRequestContext`, and provides the high-level operations used by
  other parts of `//net` and other components: queueing reports,
  handling configuration headers, clearing browsing data, and so on.

  * Within `ReportingService` lives *`ReportingContext`*, which in turn
    contains the inner workings of Reporting, spread across several
    classes:

    * The *`ReportingCache`* stores undelivered reports and unexpired
      endpoint configurations.

    * The *`ReportingHeaderParser`* parses `Report-To:` headers and
      updates the `Cache` accordingly.

    * The *`ReportingDeliveryAgent`* reads reports from the `Cache`,
      decides which endpoints to deliver them to, and attempts to
      do so. It uses a couple of helper classes:

      * The *`ReportingUploader`* does the low-level work of delivering
        reports: accepts a URL and JSON from the `DeliveryAgent`,
        creates a `URLRequest`, and parses the result.

      * The *`ReportingEndpointManager`* keeps track of which endpoints
        are in use, and manages exponential backoff (using
        `BackoffEntry`) for failing endpoints.

    * The *`ReportingGarbageCollector`* periodically examines the
      `Cache` and removes reports that have remained undelivered for too
      long, or that have failed delivery too many times.

    * The *`ReportingSerializer`* reads the `Cache` and serializes it
      into a `base::Value` for persistent storage (in Chromium, as a
      pref); it can also deserialize a serialized `Value` back into the
      `Cache`.

    * The *`ReportingBrowsingDataRemover`* examines the `Cache` upon
      request and removes browsing data (reports and endpoints) of
      selected types and origins.

### Outside `//net`

* In `*ProfileImplIOData*::InitializeInternal`, the `ReportingService`
  is created and set in the `URLRequestContext`, where the net stack
  can use it.

  (There is currently no interface to Reporting besides "hop over to
  the IO thread and poke the `ReportingService` in your favorite
  `URLRequestContext`", but that should change as various components
  need to queue reports.)

* *`ChromeReportingDelegate`* implements `ReportingDelegate` and plumbs
  the persistent data interface into prefs. It lives in
  `//chrome/browser/net`.
