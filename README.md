# NaiveProxy [![Build Status](https://travis-ci.com/klzgrad/naiveproxy.svg?branch=master)](https://travis-ci.com/klzgrad/naiveproxy) [![Build status](https://ci.appveyor.com/api/projects/status/ohpyaf49baihmxa9?svg=true)](https://ci.appveyor.com/project/klzgrad/naiveproxy)

A secure, censorship-resistent proxy.

NaiveProxy is naive as it simply reuses standard protocols (HTTP/2, HTTP/3) and common network stacks (Chrome, Caddy) with little room for variations. By being as common and boring as possible NaiveProxy is practically indistinguishable from mainstream traffic. Reusing common software stacks also ensures best practices in performance and security.

The following attacks are mitigated:

* Website fingerprinting / traffic classification: [mitigated](https://arxiv.org/abs/1707.00641) by traffic multiplexing in HTTP/2.
* [TLS parameter fingerprinting](https://arxiv.org/abs/1607.01639): defeated by reusing [Chrome's network stack](https://www.chromium.org/developers/design-documents/network-stack).
* [Active probing](https://ensa.fi/active-probing/): defeated by *application fronting*, i.e. hiding proxy servers behind a commonly used frontend with application-layer routing.
* Length-based traffic analysis: mitigated by length padding.

## Architecture

[Browser → Naive (client)] ⟶ Censor ⟶ [Frontend → Naive (server)] ⟶ Internet

NaiveProxy uses Chrome's network stack. What the censor can see is exactly regular HTTP/2 traffic between Chrome and standard Frontend (e.g. Caddy, HAProxy).

Frontend also reroutes unauthenticated users and active probes to a backend HTTP server, making it impossible to detect the existence of a proxy:

Probe ⟶ Frontend ⟶ index.html

## Download

See [latest release](https://github.com/klzgrad/naiveproxy/releases/latest).

Note: On Linux libnss3 must be installed before using the prebuilt binary.

## Setup

Locally run `./naive --proxy=https://user:pass@domain.example` and point the browser to a SOCKS5 proxy at port 1080.

On the server run `./caddy -quic` as the frontend and `./naive --listen=http://127.0.0.1:8080` behind it. See [Server Setup](https://github.com/klzgrad/naiveproxy/wiki/Server-Setup) for detail.

For more information on parameter usage and format of `config.json`, see [USAGE.txt](https://github.com/klzgrad/naiveproxy/blob/master/USAGE.txt). See also [Parameter Tuning](https://github.com/klzgrad/naiveproxy/wiki/Parameter-Tuning) to improve client-side performance.

### Portable setup

## Build

If you don't like to use downloaded binaries, you can build it.

Prerequisites:
* Ubuntu (apt-get install): git, python2, ninja-build (>= 1.7), pkg-config, libnss3-dev, ccache (optional)
* MacOS (brew install): git, ninja, ccache (optional)
* Windows ([choco install](https://chocolatey.org/)): git, python2, ninja, visualstudio2017community. See [Chromium's page](https://chromium.googlesource.com/chromium/src/+/master/docs/windows_build_instructions.md#Visual-Studio) for detail on Visual Studio setup requirements.


Build it (output to `./out/Release/naive`):
```
git clone https://github.com/klzgrad/naiveproxy.git
cd naiveproxy/src
./get-clang.sh
./build.sh
```
The scripts download tools from Google servers with curl. If there is trouble try to set a proxy environment variable for curl, e.g. `export ALL_PROXY=socks5h://127.0.0.1:1080`.

## FAQ

### Why not use Go, Node, etc. for TLS?

Their TLS stacks have distinct features that can be [easily detected](https://arxiv.org/abs/1607.01639). TLS parameters are generally very informative and distinguishable.

Previously, Tor tried to mimic Firefox's TLS signature and still got [identified and blocked by firewalls](https://groups.google.com/d/msg/traffic-obf/BpFSCVgi5rs/nCqNwoeRKQAJ), because that signature was of an outdated version of Firefox and the firewall determined the rate of collateral damage would be acceptable. If we use the signature of the most commonly used browser the collateral damage of blocking it would be unacceptable.

### Why not use Go, Node, etc. for performance?

Any languages can be used for high performance architectures, but not all architectures have high performance.

Go, Node, etc. make it easy to implement a 1:1 connection proxy model, i.e. creating one upstream connection for every user connection. Then under this model the goal of performance is to reduce overhead in setting up each upstream connection. Toward that goal people start to reinvent their own 0-RTT cryptographic protocols (badly) as TLS goes out of the window because it either spends take several round trips in handshakes or makes it really hard to set up 0-RTT properly. Then people also start to look at low level optimization such as TCP Fast Open.

Meanwhile, Google has removed the code for TCP Fast Open in Chromium all together (they authored the RFC of TCP Fast Open in 2014). The literal reason given for this reversal was

> We never enabled it by default, and have no plans to, so we should just remove it.  QUIC also makes it less useful, and TLS 1.2 0-RTT session restore means it potentially mutates state.

And the real reason Google never enabled TCP Fast Open by default is that it was dragged down by middleboxes and [never really worked](https://blog.donatas.net/blog/2017/03/09/tfo/). In Linux kernel there is a sysctl called `tcp_fastopen_blackhole_timeout_sec`, and whenever a SYN packet is dropped, TCP Fast Open is blackholed for this much time, starting at one hour and increasing exponentially, rendering it practically useless. Today TCP Fast Open accounts for [0.1% of the Internet traffic](https://ieeexplore.ieee.org/document/8303960/), so using it actually makes you highly detectable!

It was obvious to Google then and is obvious to us now that the road to zero latency at the cost of compromising security and interoperability is a dead end under the 1:1 connection model, which is why Google pursued connection persistence and 1:N connection multiplexing in HTTP/2 and more radical overhaul of HTTP/TLS/TCP in QUIC. In a 1:N connection model, the cost of setting up the first connection is amortized, and the following connections cost nothing to set up without any security or stability compromises.

Complex, battle-tested logic for connection management was [implemented](https://web.archive.org/web/20161222115511/https://insouciant.org/tech/connection-management-in-chromium/) in Chromium. The same thing is not so easy to do again from scratch with the aforementioned languages.

### Why not reinvent cryptos?

Because the first rule of cryptography is: [Don't roll your](http://loup-vaillant.fr/articles/rolling-your-own-crypto) [own cryptos](https://security.stackexchange.com/questions/18197/why-shouldnt-we-roll-our-own).

If you do roll your own cryptos, see what [happened](https://groups.google.com/d/msg/traffic-obf/CWO0peBJLGc/Py-clLSTBwAJ) with Shadowsocks. (Spoiler: it encrypts, but doesn't authenticates, leading to active probing exploits, and more exploits after duct-tape fixes.)

### Why not use HTTP/2 proxy from browser directly?

You may have wondered why not use Chrome directly if NaiveProxy reuses Chrome's network stack. The answer is yes, you can. You will get 80% of what NaiveProxy does (TLS, connection multiplexing, application fronting) without NaiveProxy. Simply point your browser to Caddy as an HTTP/2 or HTTP/3 forward proxy directly.

But this setup is prone to basic traffic analysis due to lack of obfuscation and predictable packet sizes in TLS handshakes. [The bane of "TLS-in-TLS" tunnels](http://blog.zorinaq.com/my-experience-with-the-great-firewall-of-china/) is that this combination is just so different from any normal protocols (nobody does 3-way handshakes twice in a row) and the record sizes of TLS handshakes are so predictable that no machine learning is needed to [detect it](https://github.com/shadowsocks/shadowsocks-org/issues/86#issuecomment-362809854).

The browser will introduce an extra 1RTT delay during proxied connection setup because of interpretation of HTTP RFCs. The browser will wait for a 200 response after a CONNECT request, incuring 1RTT which is not necessary. NaiveProxy does a HTTP Fast CONNECT similar to TCP Fast Open, i.e. send subsequent data immediately after CONNECT. Also, you may have to type in the password for the proxy everytime you open the browser. NaiveProxy sends the password directly.

But if you don't need the best performance, and unobfuscated TLS-in-TLS somehow still works for you, you can just set up Caddy and use it with your browser.
