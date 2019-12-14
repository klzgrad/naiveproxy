# Chrome Network Bug Triage : Suggested Workflow

[TOC]

## Identifying unlabeled network bugs on the tracker

* Look at new unconfirmed bugs since noon PST on the last triager's rotation.
  [Use this issue tracker
  query](https://bugs.chromium.org/p/chromium/issues/list?q=status%3Aunconfirmed&sort=-id&num=1000).

* Read the title of the bug.

* If a bug looks like it might be network related, middle click (or
  command-click on OSX) to open it in a new tab.

* If a user provides a crash ID for a crasher for a bug that could be
  net-related, see the [internal instructions on dealing with a crash ID](https://goto.google.com/network_triage_internal#dealing-with-a-crash-id)

* If network causes are possible, ask for a net-export log (If it's not a
  browser crash) and attach the most specific internals-network label that's
  applicable.  If there isn't an applicable narrower component, a clear owner
  for the issue, or there are multiple possibilities, attach the
  Internals>Network component and proceed with further investigation.

* If non-network causes also seem possible, attach those components as well.

## Investigating component=Internals>Network bugs

* Note that you may want to investigate Needs-Feedback bugs first, as
  that may result in some bugs being added to this list.

* It's recommended that while on triage duty, you subscribe to the
  Internals>Network component (but not its subcomponents). To do this, go
  to the issue tracker and then click "Saved Queries".
  Add a query with these settings:
    * Saved query name: Network Bug Triage
    * Project: chromium
    * Query: component=Internals>Network
    * Subscription options: Notify Immediately

* Look through unconfirmed and untriaged component=Internals>Network bugs,
  prioritizing those updated within the last week. [Use this issue tracker
  query](https://bugs.chromium.org/p/chromium/issues/list?can=2&q=component%3DInternals%3ENetwork+status%3AUnconfirmed,Untriaged+-label:Needs-Feedback&sort=-modified).

* If more information is needed from the reporter, ask for it and add the
  Needs-Feedback label.

* While investigating a new issue, change the status to Untriaged.

* If a bug is a potential security issue (Allows for code execution from remote
  site, allows crossing security boundaries, unchecked array bounds, etc) mark
  it Type-Bug-Security.  If it has privacy implication (History, cookies
  discoverable by an entity that shouldn't be able to do so, incognito state
  being saved in memory or on disk beyond the lifetime of incognito tabs, etc),
  mark it with component Privacy.

* For bugs that already have a more specific network component, go ahead and
  remove the Internals>Network component to get them off the next triager's
  radar and move on.

* Try to figure out if it's really a network bug.  See common non-network
  components section for description of common components for issues incorrectly
  tagged as Internals>Network.

* If it's not, attach appropriate labels/components and go no further.

* If it may be a network bug, attach additional possibly relevant component if
  any, and continue investigating.  Once you either determine it's a
  non-network bug, or figure out accurate more specific network components, your
  job is done, though you should still ask for a net-export dump if it seems
  likely to be useful.

* Note that Chrome-OS-specific network-related code (Captive portal detection,
  connectivity detection, login, etc) may not all have appropriate more
  specific subcomponents, but are not in areas handled by the network stack
  team. Just make sure those have the OS-Chrome label, and any more specific
  labels if applicable, and then move on.

* Gather data and investigate.
    * Remember to add the Needs-Feedback label whenever waiting for the user to
      respond with more information, and remove it when not waiting on the
      user.
    * Try to reproduce locally.  If you can, and it's a regression, use
      src/tools/bisect-builds.py to figure out when it regressed.
    * Ask more data from the user as needed (net-export dumps, repro case,
      crash ID from chrome://crashes, run tests, etc).
    * If asking for a chrome://net-export dump, provide this link:
      https://sites.google.com/a/chromium.org/dev/for-testers/providing-network-details.

* Try to figure out what's going on, and which more specific network component
  is most appropriate.

* If it's a regression, browse through the git history of relevant files to try
  and figure out when it regressed.  CC authors / primary reviewers of any
  strongly suspect CLs.

* If you are having trouble with an issue, particularly for help understanding
  net-export logs, email the public net-dev@chromium.org list for help
  debugging.  If it's a crasher, or for some other reason discussion needs to
  be done in private, see [internal documentation for details](https://goto.google.com/network_triage_internal#getting-help-with-a-bug)

* If it appears to be a bug in the unowned core of the network stack (i.e. no
  subcomponent applies, or only the Internals>Network>HTTP subcomponent
  applies, and there's no clear owner), try to figure out the exact cause.

## Crashes

For guidance on crashes see the internal documentation:

* [Dealing with a crash ID](https://goto.google.com/network_triage_internal#dealing-with-a-crash-id)
* [Looking for new crashers](https://goto.google.com/network_triage_internal#looking-for-new-crashers)
* [Investigating crashers](https://goto.google.com/network_triage_internal#investigating-crashers)
