# NaiveProxy

A secure, analysis-resistent proxy framework.

The primary security goal is availability in presence of pervasive censorship. Nevertheless, privacy and integrity are simultaneously achieved through implementations of TLS best practices.

The main attacks considered:

* Website fingerprinting / traffic classification: [mitigated](https://arxiv.org/abs/1707.00641) by traffic multiplexing in HTTP/2.
* [TLS parameter fingerprinting](https://arxiv.org/abs/1607.01639): defeated by using identical behaviors from [Chromium's network stack](https://www.chromium.org/developers/design-documents/network-stack).
* [Active probing](https://ensa.fi/active-probing/): defeated by application fronting, using a common frontend with application-layer routing capability, e.g. HAProxy.
* Length-based traffic analysis: mitigated by length padding.

There are three setups:

* The portable setup doesn't ask you to build any code or run anything client-side, but it is prone to traffic analysis due to lack of length padding. See [Linux Quick HOWTO](https://github.com/klzgrad/naiveproxy/wiki/Linux-Quick-HOWTO).
* The fast setup improves performance by having a client. See "Fast client" in [Linux HOWTO](https://github.com/klzgrad/naiveproxy/wiki/Linux-HOWTO).
* The resistent setup implements length padding upon the fast setup by requiring an extra server. See "Obfuscated tunnel" in [Linux HOWTO](https://github.com/klzgrad/naiveproxy/wiki/Linux-HOWTO).

The application is entirely based on Chromium's code base and build system. The master branch contains a minimal set of files that are changed from Chromium stable, for ease of code review. The version branches contain a minimized but still large buildable codebase with the same changes.
