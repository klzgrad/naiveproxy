# Certificate Transparency

## Overview

[Certificate Transparency](http://www.certificate-transparency.org/) (CT) is a
protocol designed to fix several structural flaws in the SSL/TLS certificate
ecosystem. Described by [RFC 6962](https://tools.ietf.org/html/rfc6962) and
the ongoing work in [RFC 6962-bis](https://datatracker.ietf.org/doc/draft-ietf-trans-rfc6962-bis/),
it provides a means of providing a public, append-only data structure that
can log certificates issued by [certificate authorities](https://en.wikipedia.org/wiki/Certificate_authority) (CAs).
By logging these certificates, it becomes possible for site operators to
detect when a certificate may have been issued for their domain without their
approval, and allows browsers and the wider ecosystem to verify that CAs are
following their expected and disclosed practices.

## Certificate Transparency Basics

Broadly speaking, the goal of supporting Certificate Transparency is to ensure
that certificates an application trusts will be publicly disclosed in a way
sufficient for site operators and application developers to ensure that
nothing is wrong.

At the most basic level, it's possible to simply introduce Certificate
Transparency logs as trusted third parties, much like CAs are trusted third
parties. If the logs are operated by CAs, this may not be much of a security
improvement, but if the logs are operated by non-CA entities, this might serve
as a sufficient counter-balance to the risks.

However, with more work, it's possible to minimize the trust afforded to
Certificate Transparency logs, and to automatically and cryptographically
verify they're complying with their stated policies. This can provide even
greater assurance to application developers, site operators, and their users,
that the security expected from certificates is actually being provided.

For a more thorough threat analysis, see 
https://datatracker.ietf.org/doc/draft-ietf-trans-threat-analysis/ that
discusses the different risks in Certificate Transparency, and how the
protocol addresses them.

## Certificate Transparency in `//net`

A goal of `//net` is to try to ensure that code is 'safe by default' when
used. As part of serving that goal, in order to make a TLS or QUIC connection
using code in `//net`, it's necessary for the `//net` embedder to make
a decision about Certificate Transparency, much like it is necessary to
provide a [`CertVerifier`](/net/cert/cert_verifier.h) that describes how to
verify the server's certificate.

Because this is necessary to make a TLS or QUIC connection, this requirement
surfaces upwards through each layer in the stack - applying to things like
[`HttpNetworkSession`](/net/http/http_network_session.h) and upwards to
[`URLRequestContext`](/net/url_request/url_request_context.h).

This requirement is expressed by requiring two separate, but related, objects
to be supplied: [`CTVerifier`](/net/cert/ct_verifier.h) and
[`CTPolicyEnforcer`](/net/cert/ct_policy_enforcer.h), which together can be used
to express an application's policies with respect to Certificate Transparency.

As part of the goal of ensuring 'safe by default', `//net` also has various
policies related to certificates issued by particular CAs whose past actions
have created unnecessary security risk for TLS connections, and as a
consequence, are required to have their certificates disclosed using
Certificate Transparency in order to ensure that the security provided by
these CAs matches the level of security and assurance that other CAs provide.
These policies are implemented in
[`TransportSecurityState`](/net/http/transport_security_state.cc), via the
`ShouldRequireCT` method.

### CTVerifier

`CTVerifier` is the core interface for parsing and validating the structures
defined in RFC6962 (or future versions), and for providing basic information
about the [`SignedCertificateTimestamps`](https://tools.ietf.org/html/rfc6962#section-3.2)
present within the connection.

### CTPolicyEnforcer

`CTPolicyEnforcer` is the core class for expressing an application's policies
around how it expects Certificate Transparency to be used by the certificates
it trusts and the CAs that issue these certificates.

`CTPolicyEnforcer` currently expresses two policies:
  * How to treat [Extended Validation](https://cabforum.org/extended-validation-2/)
    certificates (those for which a [`CertVerifier`](/net/cert/cert_verifier.h)
    returned `CERT_STATUS_IS_EV`).
  * How to treat all certificates, regardless of EV status.

### TransportSecurityState

The `TransportSecurityState::ShouldRequireCT` method implements the core logic
for determining whether or not a connection attempt should be rejected if it
does not comply with an application's Certificate Transparency policy.

The implementation in `//net` provides a default implementation that tries to
ensure maximum security, by failing connections that do not abide by an
application's Certificate Transparency policy and are from CAs known to have
security issues in the past.

Embedders can customize or override this by providing a
`TransportSecurityState::RequireCTDelegate` implementation, which allows
applications to inspect the connection information and determine whether
Certificate Transparency should be required, should not be required, or
whether the default logic in `//net` should be used.

## Certificate Transparency in Chromium

As part of the open-source implementation of Chrome, the policies related to
how Chromium code treats Certificate Transparency are documented at
https://www.chromium.org/Home/chromium-security/certificate-transparency . This
page includes the policies for how Chromium determines an acceptable set of
Certificate Transparency logs and what Certificate Transparency-related
information is expected to accompany certificates, both for EV and non-EV.

The implementation of these policies lives within [`//net/cert`](/net/cert), and
includes:
  * [`ct_known_logs.h`](/net/cert/ct_known_logs.h): The set of Certificate
    Transparency logs known and qualified according to Chromium's
    [Certificate Transparency Log Policy](https://www.chromium.org/Home/chromium-security/certificate-transparency/log-policy).
  * [`multi_log_ct_verifier.h`](/net/cert/multi_log_ct_verifier.h): Capable of
    parsing `SignedCertificateTimestamps` from a variety of logs and
    validating their signatures, using the keys and information provided by
    `ct_known_logs.h`.
  * [`ct_policy_enforcer.h`](/net/cert/ct_policy_enforcer.h): A base class that
    implements the Certificate Transparency in Chrome Policy, for both EV and
    non-EV certificates.

## Certificate Transparency for `//net` Consumers

This section is intended for code that is open-sourced as part of the
Chromium projects, intended to be included within Google Chrome, and which
uses the `//net` APIs for purposes other than loading and rendering web
content. Particularly, consumers of `//net` APIs that are communicating with
a limited or defined set of endpoints and which don't use certificates issued
by CAs. This may also include testing tools and utilities, as these are not
generally shipped to users as part of Chrome.

Not every TLS connection may need the security assurances that
Certificate Transparency aims to provide. For example, some consumers of
`//net` APIs in Chromium use mutual authentication with self-signed
certificates and which are authenticated out-of-band. For these connections,
Certificate Transparency is not relevant, and it's not necessary to parse
or enforce Certificate Transparency related information.

For these cases, the approach is:
  * [`do_nothing_ct_verifier.h`](/net/cert/do_nothing_ct_verifier.h): A no-op
    CTVerifier that does not parse or verify Certificate Transparency-related
    information.
  * A derived `CTPolicyEnforcer` implementation that indicates all
    certificates comply with its policies.

    **TODO(rsleevi):** Provide a `DoNothingCTPolicyEnforcer`

As documented in these classes, care should be taken before using these, as
they provide much weaker security guarantees. In general, emailing
[net-dev@chromium.org](mailto:net-dev@chromium.org) or discussing it during a
security review is the right answer, and documenting at the instantiation
points why it is safe and acceptable to use these classes.

## Certificate Transparency for `//net` Embedders

This section is intended for code that is used in other open-source Chromium
based projects, but are not included in Google Chrome or related. This
includes projects based on `//net`, such as
[`//components/cronet`](/components/cronet) or other
[`//content`](/content) embedders.

For projects and third party products that embed `//net`, the policies
that are included as part of the open-source repository may not be
appropriate. This is because the implementations may rely implicitly
or explicitly on several key guarantees that come from Google-branded
distributions and products, and may not be appropriate for other cases.

These key expectations are:
  * A release cycle aligned with Chrome releases; that is, every six weeks,
    and on the same versions as Chrome releases.
  * Widespread support for automatic updates.
  * That [`base::GetBuildTime()`](/base/build_time.h) will reflect, to
    some degree, when the tree was branched and/or released, and will not
    be re-generated on recompilation. That is, this implies is_official_build
    for binaries released to end-users, but is not enforced in code so that
    developers can accurately test release behavior.
  * Support for dynamic [`base::FieldTrial`](/base/metrics/field_trial.h)
    configurations.

For projects that don't support automatic updates, or which measure 'stable'
on the order of months to years, or which don't have tools suitable to
respond to changes in the Certificate Authority and Certificate Transparency
ecosystem, it may not be appropriate to enable Certificate Transparency
support yet.

These issues are not unique or particular to Certificate Transparency - in
many ways, they're similar to issues already faced with determining which
CAs are trusted and how to successfully validate a TLS server's certificate.
However, as the Certificate Transparency ecosystem is still growing, it may be
suitable to disable support until some of the solutions to these challenges
stablize.

To opt-out of enforcing Certificate Transparency, using the `DoNothing`
variants discussed above provides a suitable implementation that will opt to
'fail open' instead. This may provide less security, but provides greater
stability, and minimizes the risk that these `//net` embedding clients
might cause to the Certificate Transparency ecosystem or receive from enabling
Certificate Transparency.
