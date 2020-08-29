# Chrome Network Bug Triage

The Chrome network team uses a two day bug triage rotation. The goal is to
review outstanding issues and keep things moving forward. The rotation is time
based rather than objective based. Sheriffs are expected to spend the majority
of their two days working on bug triage/investigation.

## 1. Review untriaged bugs

Look through [this list of untriaged
bugs](https://bugs.chromium.org/p/chromium/issues/list?sort=pri%20-stars%20-opened&q=component%3AInternals%3ENetwork%20status%3Aunconfirmed%2Cuntriaged%20-component%3AInternals%3ENetwork%3ECookies%20-component%3AInternals%3ENetwork%3EDNS%20-component%3AInternals%3ENetwork%3ECookies%20-component%3AInternals%3ENetwork%3ECertificate%20-component%3AInternals%3ENetwork%3EReportingAndNEL%20-component%3AInternals%3ENetwork%3EDataUse%20-component%3AInternals%3ENetwork%3EEV%20-component%3AInternals%3ENetwork%3EDataProxy%20-component%3AInternals%3ENetwork%3ECertTrans%20-component%3AInternals%3ENetwork%3ENetworkQuality%20-component%3AInternals%3ENetwork%3EDoH%20-component%3AInternals%3ENetwork%3ENetInfo%20-component%3AInternals%3ENetwork%3EVPN%20-Needs%3DFeedback).

* Go through them in the given order (top to bottom).
  The link sorts them by priority and then recency.
* The goal is to move them out of the untriaged bug queue and give them a priority.

For each bug try to:

* Remove the `Internals>Network` component if it belongs elsewhere
* Dupe it against an existing bug
* Close it `WontFix` if appropriate
* Give the bug a priority. Refer to [this (internal) document for guidelines](https://goto.google.com/xnzwn)
* If the bug is a potential security issue (Allows for code execution from remote
  site, allows crossing security boundaries, unchecked array bounds, etc) mark
  it `Type-Bug-Security`.
* If the bug has privacy implications mark it with component `Privacy`.
* Mark it as a feature request or task if appropriate
* Ask the reporter to narrow down regressions, possibly by using
  [bisect-builds-py](https://www.chromium.org/developers/bisect-builds-py). To
  view suspicious changelists in a regression window, you can use the Change Log
  form on [OmahaProxy](https://omahaproxy.appspot.com/)
* CC others who may be able to help
* Mark it as `Needs-Feedback` and request more information if needed.
* Request a NetLog that captures the problem. You can paste this on the bug:
  ```
  Please collect and attach a chrome://net-export log.
  Instructions can be found here:
  https://chromium.org/for-testers/providing-network-details
  ```
* If a NetLog was provided, try to spend a bit of time reviewing it. See
  [crash-course-in-net-internals.md](crash-course-in-net-internals.md) for an
  introduction.
* Move to a subcomponent of `Internals>Network` if appropriate. See
  [bug-triage-labels.md](bug-triage-labels.md) for an overview of the components.
* If the bug is a crash, see [internal: Dealing with a crash
  ID](https://goto.google.com/network_triage_internal#dealing-with-a-crash-id)
and [internal: Investigating
crashers](https://goto.google.com/network_triage_internal#investigating-crashers)

## 2. Follow-up on issues with the Needs-Feedback label

Look through [this list of Needs=Feedback
bugs](https://bugs.chromium.org/p/chromium/issues/list?sort=pri%20-modified&q=component%3AInternals%3ENetwork%20Needs%3DFeedback%20-component%3AInternals%3ENetwork%3ECookies%20-component%3AInternals%3ENetwork%3EDNS%20-component%3AInternals%3ENetwork%3ECookies%20-component%3AInternals%3ENetwork%3ECertificate%20-component%3AInternals%3ENetwork%3EReportingAndNEL%20-component%3AInternals%3ENetwork%3EDataUse%20-component%3AInternals%3ENetwork%3EEV%20-component%3AInternals%3ENetwork%3EDataProxy%20-component%3AInternals%3ENetwork%3ECertTrans%20-component%3AInternals%3ENetwork%3ENetworkQuality%20-component%3AInternals%3ENetwork%3EDoH%20-component%3AInternals%3ENetwork%3ENetInfo%20-component%3AInternals%3ENetwork%3EVPN).

* Go through them in the given order (top to bottom).
  The link sorts them by priority and then recency.
* If the requested feedback was provided, review the new information and repeat
  the same steps as (1) to re-triage based on the new information.
* If the bug had the `Needs-Feedback` label for over a week and the
  feedback needed to make progress was not yet provided, archive the bug.

## 3. (Optional) Look through crash reports

Top crashes will already be entered into the bug system by a different process,
so will be handled by the triage steps above.

However if you have time to look through lower threshold crashes, see
[internal: Looking for new crashers](https://goto.google.com/network_triage_internal#looking-for-new-crashers)

## 4. Send out a sheriff report

On the final day of your rotation, send a brief summary to net-dev@chromium.org
detailing any interesting or concerning trends. Do not discuss any restricted
bugs on the public mailing list.

## Management

* Your rotation will appear in Google Calendar as two days. You are expected to
  work on it full-time (as best you can) during those calendar days, during your
  ordinary working hours.

* Google Calendar [google.com_52n2p39ad82hah9v7j26vek830@group.calendar.google.com](https://calendar.google.com/calendar/embed?src=google.com_52n2p39ad82hah9v7j26vek830%40group.calendar.google.com&ctz=America%2FLos_Angeles)

* Owners for the network bug triage rotation can find instructions on
generating and modifying shifts
[here (internal-only)](https://goto.google.com/pflvb).

* An overview of bug trends can be seen on [Chromium
  Dashboard](https://chromiumdash.appspot.com/components/Internals/Network?project=Chromium)

* There is also an [internal dashboard with bug trends for Web
  Platform](https://goto.google.com/vufyq) that includes network issues.

* The issue tracker doesn't track any official mappings between components and
  OWNERS. This [internal document](https://goto.google.com/kojfj) enumerates
  the known owners for subcomponents.
