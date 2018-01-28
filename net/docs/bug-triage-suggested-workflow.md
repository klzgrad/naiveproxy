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
  net-related, look at the crash stack at
  [go/crash](https://goto.google.com/crash), and see if it looks to be network
  related.  Be sure to check if other bug reports have that stack trace, and
  mark as a dupe if so.  Even if the bug isn't network related, paste the stack
  trace in the bug, so no one else has to look up the crash stack from the ID.
    * If there's just a blank form and a crash ID, just ignore the bug.

* If network causes are possible, ask for a net-internals log (If it's not a
  browser crash) and attach the most specific internals-network label that's
  applicable.  If there isn't an applicable narrower component, a clear owner
  for the issue, or there are multiple possibilities, attach the
  Internals>Network component and proceed with further investigation.

* If non-network causes also seem possible, attach those components as well.

## Investigate UMA notifications

For each alert that fires, determine if it's a real alert and file a bug if so.

* Don't file if the alert is coincident with a major volume change.  The volume
  at a particular date can be determined by hovering the mouse over the
  appropriate location on the alert line.

* Don't file if the alert is on a graph with very low volume (< ~200 data
  points); it's probably noise, and we probably don't care even if it isn't.

* Don't file if the graph is really noisy (but eyeball it to decide if there is
  an underlying important shift under the noise).

* Don't file if the alert is in the "Known Ignorable" list:
    * SimpleCache on Windows
    * DiskCache on Android.

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
  job is done, though you should still ask for a net-internals dump if it seems
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
    * Ask more data from the user as needed (net-internals dumps, repro case,
      crash ID from about:crashes, run tests, etc).
    * If asking for an about:net-internals dump, provide this link:
      https://sites.google.com/a/chromium.org/dev/for-testers/providing-network-details.
      Can just grab the link from about:net-internals, as needed.

* Try to figure out what's going on, and which more specific network component
  is most appropriate.

* If it's a regression, browse through the git history of relevant files to try
  and figure out when it regressed.  CC authors / primary reviewers of any
  strongly suspect CLs.

* If you are having trouble with an issue, particularly for help understanding
  net-internals logs, email the public net-dev@chromium.org list for help
  debugging.  If it's a crasher, or for some other reason discussion needs to
  be done in private, use chrome-network-debugging@google.com.  TODO(mmenke):
  Write up a net-internals tips and tricks docs.

* If it appears to be a bug in the unowned core of the network stack (i.e. no
  subcomponent applies, or only the Internals>Network>HTTP subcomponent
  applies, and there's no clear owner), try to figure out the exact cause.

## Looking for new crashers

1. Go to [go/chromecrash](https://goto.google.com/chromecrash).

2. For each platform, look through the releases for which releases to
   investigate.  As per [bug-triage.md](bug-triage.md), this should be the most
   recent canary, the previous canary (if the most recent is less than a day
   old), and any of dev/beta/stable that were released in the last couple of
   days.

3. For each release, in the "Process Type" frame, click on "browser".

4. At the bottom of the "Magic Signature" frame,  click "limit 1000" (Or reduce
   the limit to 100 first, as that's all the triager needs to look at).
   Reported crashers are sorted in decreasing order of the number of reports for
   that crash signature.

5. Search the page for *"net::"*.

6. For each found signature:
    * Ignore signatures that only occur once or twice, as memory corruption can
      easily cause one-off failures when the sample size is large enough.  Also
      ignore crashers that are not in the top 100 for that platform / release.
    * If there is a bug already filed, make sure it is correctly describing the
      current bug (e.g. not closed, or not describing a long-past issue), and
      make sure that if it is a *net* bug, that it is labeled as such.
    * Ignore signatures that only come from one or two client IDs, as individual
      machine malware and breakage can cause one-off failures.
    * Click on the number of reports field to see details of crash. Ignore it
      if it doesn't appear to be a network bug.
    * Otherwise, file a new bug directly from chromecrash.
    * For each bug you file, include the following information:
        * The backtrace.  Note that the backtrace should not be added to the
          bug if Restrict-View-Google isn't set on the bug as it may contain
          PII.  Filing the bug from the crash reporter should do this
          automatically, but check.
        * The channel in which the bug is seen (canary/dev/beta/stable), and its
          rank among crashers in the channel.
        * The frequency of this signature in recent releases.  This information
          is available by:
            1. Clicking on the signature in the "Magic Signature" list
            2. Clicking "Edit" on the dremel query at the top of the page
            3. Removing the "product.version='X.Y.Z.W' AND" string and clicking
               "Update".
            4. Clicking "Limit 1000" in the Product Version list in the
               resulting page (without this, the listing will be restricted to
               the releases in which the signature is most common, which will
               often not include the canary/dev release being investigated).
            5. Choose some subset of that list, or all of it, to include in the
               bug.  Make sure to indicate if there is a defined point in the
               past before which the signature is not present.

As an alternative to the above, you can use [Eric Roman's new crash
tool](https://ericroman.users.x20web.corp.google.com/www/net-crash-triage/index.html)
(internal link).  Note that it isn't a perfect fit with the triage
responsibilities, specifically:
  
* It's only showing Windows releases; Android, iOS, and WebView are
  usually different, and Mac is sometimes different.
* The instructions are to look at the latest canary which has a days
  worth of data.  If canaries are being pushed fast, that may be more
  than one canary into the past, and hence not visible on the tool.
* Eric's tool filters based on files in "src/net" rather than looking
  for magic signature's including the string "net::" ("src/net" is
  probably the better filter).    

## Investigating crashers

* Only investigate crashers that are still occurring, as identified by above
  section.  If a search on go/crash indicates a crasher is no longer occurring,
  mark it as WontFix.

* On Windows, you may want to look for weird dlls associated with the crashes.
  This generally needs crashes from a fair number of different users to reach
  any conclusions.
    * To get a list of loaded modules in related crash dumps, select
      modules->3rd party in the left pane.  It can be difficult to distinguish
      between safe dlls and those likely to cause problems, but even if you're
      not that familiar with windows, some may stick out.  Anti-virus programs,
      download managers, and more gray hat badware often have meaningful dll
      names or dll paths (Generally product names or company names).  If you
      see one of these in a significant number of the crash dumps, it may well
      be the cause.
    * You can also try selecting the "has malware" option, though that's much
      less reliable than looking manually.

* See if the same users are repeatedly running into the same issue.  This can
  be accomplished by search for (Or clicking on) the client ID associated with
  a crash report, and seeing if there are multiple reports for the same crash.
  If this is the case, it may be also be malware, or an issue with an unusual
  system/chrome/network config.

* Dig through crash reports to figure out when the crash first appeared, and
  dig through revision history in related files to try and locate a suspect CL.
  TODO(mmenke):  Add more detail here.

* Load crash dumps, try to figure out a cause.  See
  http://www.chromium.org/developers/crash-reports for more information
