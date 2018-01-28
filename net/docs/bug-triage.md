# Chrome Network Bug Triage

The Chrome network team uses a two day bug triage rotation.  The main goals are
to identify and label new network bugs, and investigate network bugs when no
label seems suitable.

## Responsibilities

### Required, in rough order of priority:
* Identify new network bugs on the tracker.
* Investigate UMA notifications.
* Investigate recent Internals>Network issues with no subcomponent.
* Follow up on Needs-Feedback issues for all network components.
* Identify and file bugs for significant new crashers.

### Best effort, also in rough priority order:
* Investigate unowned and owned-but-forgotten net/ crashers.
* Investigate old bugs.
* Close obsolete bugs.

All of the above is to be done on each rotation.  These responsibilities should
be tracked, and anything left undone at the end of a rotation should be handed
off to the next triager.  The downside to passing along bug investigations like
this is each new triager has to get back up to speed on bugs the previous
triager was investigating.  The upside is that triagers don't get stuck
investigating issues after their time after their rotation, and it results in a
uniform, predictable two day commitment for all triagers.

## Details

### Required:

* Identify new network bugs on the bug tracker.  All Unconfirmed issues filed
  during your triage rotation should be scanned, and, for suspected network
  bugs, a network component assigned and an about:net-internals log requested.
  A triager is responsible for looking at bugs reported from noon PST / 3:00 pm
  EST of the last day of the previous triager's rotation until the same time on
  the last day of their rotation.  Once you've assigned a bug to a component,
  mark it Untriaged, so other triagers sorting through Unconfirmed bugs won't
  see it. 
  
    * For desktop bugs, ask for a net-internals log and give the user a link to
      https://sites.google.com/a/chromium.org/dev/for-testers/providing-network-details
      (A link there appears on about:net-internals, for easy reference) for
      instructions.  On mobile, point them to about:net-export.  In either case,
      attach the Needs-Feedback label.

* Investigate UMA notifications.

    * UMA notifications ("chirps") are alerts based on UMA histograms that are
      sent to   chrome-network-debugging@google.com.  Triagers should subscribe
      to this list.  When an alert fires, the triager should determine if the
      alert looks to be real and file a bug with the appropriate label if so.
      Note that if no label more specific than Internals>Network is appropriate,
      the responsibility remains with the triager to continue investigating the
      bug, as above.
      
    * The triager is responsible for looking at any notification previous
      triagers did not, so when an issue is investigated, the person who did
      so should respond to chrome-network-debugging@google.com with a short
      email, describing their conclusions.  Future triagers can then use the
      fact an alert was responded to as an indicator of which of them need
      to be followed up on.  Alerts fired before the beginning of the
      previous triager's rotation may be ignored. 

* Investigate [Unconfirmed / Untriaged Internals>Network issues that don't belong to a more specific network component](https://bugs.chromium.org/p/chromium/issues/list?can=2&q=component%3DInternals%3ENetwork+status%3AUnconfirmed,Untriaged+-label:Needs-Feedback&sort=-modified),
  prioritizing the most recent issues, ones with the most responsive reporters,
  and major crashers.  This will generally take up the majority of your time as
  triager. Continue digging until you can do one of the following:

    * Mark it as *WontFix* (working as intended, obsolete issue) or a
      duplicate.

    * Mark it as a feature request.

    * Mark it as Needs-Feedback.

    * Remove the Internals>Network component, replacing it with at least one
      more specific network component or non-network component. Replacing the
      Internals>Network component gets it off the next triager's radar, and
      in front of someone more familiar with the relevant code.  Note that
      due to the way the bug report wizard works, a lot of bugs incorrectly end
      up with the network component.

    * The issue is assigned to an appropriate owner, and make sure to mark it
      as "assigned" so the next triager doesn't run into it.

    * If there is no more specific component for a bug, it should be
      investigated by the triager until we have a good understanding of the
      cause of the problem, and some idea how it should be fixed, at which point
      its status should be set to Available.  Future triagers should ignore bugs
      with this status, unless investigating stale bugs.

* Follow up on [Needs-Feedback issues for all components owned by the network stack team](https://bugs.chromium.org/p/chromium/issues/list?q=component%3AInternals%3ENetwork+-component%3AInternals%3ENetwork%3EDataProxy+-component%3AInternals%3ENetwork%3EDataUse+-component%3AInternals%3ENetwork%3EVPN+Needs%3DFeedback&sort=-modified).

    * Remove label once feedback is provided.  Continue to investigate, if
      the previous section applies.

    * If the Needs-Feedback label has been present for one week, ping the
      reporter.

    * Archive after two weeks with no feedback, telling users to file a new
      bug if they still have the issue, with the requested information, unless
      the reporter indicates they'll provide data when they can.  In that case,
      use your own judgment for further pings or archiving.

* Identify significant new browser process
  [crashers](https://goto.google.com/chromecrash) that are potentially network
  related.  You should look at crashes for the most recent canary that has at
  least a day of data, and if there's been a dev or beta release from the start
  of the last triager's shift to the start of yours, you should also look at
  that once it has at least a day of data.  Recent releases available
  [here](https://omahaproxy.appspot.com/).  If both dev and beta have been
  released in that period, just look at beta.  File Internals>Network bugs on
  the tracker when new crashers are found.  Bugs  should only be filed for
  crashes that are both in the top 100 for each release and occurred for more
  than two users.

    * Make sure to check for new crashes on all platforms, not just Windows.

### Best Effort (As you have time):

* Investigate old bugs, and bugs associated with Internals>Network
  subcomponents.

* Investigate unowned and owned but forgotten net/ crashers that are still
  occurring (As indicated by
  [go/chromecrash](https://goto.google.com/chromecrash)), prioritizing frequent
  and long standing crashers.

* Close obsolete bugs.

See [bug-triage-suggested-workflow.md](bug-triage-suggested-workflow.md) for
suggested workflows.

See [bug-triage-labels.md](bug-triage-labels.md) for labeling tips for network
and non-network bugs.

See [crash-course-in-net-internals.md](crash-course-in-net-internals.md) for
some help on getting started with about:net-internals debugging.
