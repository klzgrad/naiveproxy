# NaïveProxy ![build workflow](https://github.com/klzgrad/naiveproxy/actions/workflows/build.yml/badge.svg)

NaïveProxy uses Chrome's network stack to camouflage traffic with strong censorship resistence and low detectablility. Reusing Chrome's stack also ensures best practices in performance and security.

The following traffic attacks are mitigated in NaïveProxy:

* Website fingerprinting / traffic classification: [mitigated](https://arxiv.org/abs/1707.00641) by traffic multiplexing in HTTP/2.
* [TLS parameter fingerprinting](https://arxiv.org/abs/1607.01639): defeated by reusing [Chrome's network stack](https://www.chromium.org/developers/design-documents/network-stack).
* [Active probing](https://ensa.fi/active-probing/): defeated by *application fronting*, i.e. hiding proxy servers behind a commonly used frontend server with application-layer routing.
* Length-based traffic analysis: mitigated by length padding.

## Architecture

[Browser → Naïve client] ⟶ Censor ⟶ [Frontend → Naïve server] ⟶ Internet

NaïveProxy uses Chrome's network stack to ensure its observable behavior is identical to regular HTTP/2 traffic between Chrome and standard frontend servers.

The frontend server can be any reverse proxy that is able to route HTTP/2 traffic based on HTTP authorization headers, preventing active probing of proxy existence. Known ones include Caddy with its forwardproxy plugin and HAProxy.

The Naïve server here works as a forward proxy and a packet length padding layer. Caddy forwardproxy is also a forward proxy but it lacks a padding layer. A [fork](https://github.com/klzgrad/forwardproxy) adds the NaïveProxy padding layer to forwardproxy, combining both in one.

## Download binaries

[Download here](https://github.com/klzgrad/naiveproxy/releases/latest). Supported platforms include: Windows, Android (with [SagerNet](https://github.com/SagerNet/SagerNet)), Linux, Mac OS, and OpenWrt ([support status](https://github.com/klzgrad/naiveproxy/wiki/OpenWrt-Support)).

Users should always use the latest version to keep signatures identical to Chrome.

## Server setup

The following describes the naïve fork of forwardproxy setup.

Build:
```sh
go get -u github.com/caddyserver/xcaddy/cmd/xcaddy
~/go/bin/xcaddy build --with github.com/caddyserver/forwardproxy@caddy2=github.com/klzgrad/forwardproxy@naive
```

Example Caddyfile (replace `user` and `pass` accordingly):
```
:443, example.com
tls me@example.com
route {
  forward_proxy {
    basic_auth user pass
    hide_ip
    hide_via
    probe_resistance
  }
  file_server { root /var/www/html }
}
```
`:443` must appear first for this Caddyfile to work. For more advanced usage consider using [JSON for Caddy 2's config](https://caddyserver.com/docs/json/).

Run with the Caddyfile:
```
sudo setcap cap_net_bind_service=+ep ./caddy
./caddy start
```

See also [Systemd unit example](https://github.com/klzgrad/naiveproxy/wiki/Run-Caddy-as-a-daemon) and [HAProxy setup](https://github.com/klzgrad/naiveproxy/wiki/HAProxy-Setup).

## Client setup

Run `./naive` with the following `config.json` to get a SOCKS5 proxy at local port 1080.
```json
{
  "listen": "socks://127.0.0.1:1080",
  "proxy": "https://user:pass@example.com"
}
```

See also [parameter usage](https://github.com/klzgrad/naiveproxy/blob/master/USAGE.txt) and [performance tuning](https://github.com/klzgrad/naiveproxy/wiki/Performance-Tuning).

## Build from source

If you don't like to download binaries, you can build NaïveProxy.

Prerequisites:
* Ubuntu (apt install): git, python, ninja-build (>= 1.7), pkg-config, curl, unzip, ccache (optional)
* MacOS (brew install): git, ninja, ccache (optional)
* Windows ([choco install](https://chocolatey.org/)): git, python, ninja, visualstudio2019community. See [Chromium's page](https://chromium.googlesource.com/chromium/src/+/master/docs/windows_build_instructions.md#Visual-Studio) for detail on Visual Studio requirements.

Build (output to `./out/Release/naive`):
```
git clone --depth 1 https://github.com/klzgrad/naiveproxy.git
cd naiveproxy/src
./get-clang.sh
./build.sh
```
The scripts download tools from Google servers with curl. You may need to set a proxy environment variable for curl, e.g. `export ALL_PROXY=socks5h://127.0.0.1:1080`.

## Notes for downstream

Do not use the master branch to track updates, as it rebases from a new root commit for every new Chrome release. Use stable releases and the associated tags to track new versions, where short release notes are also provided.

## FAQ

### Why not use Go, Node, etc. for TLS?

Their TLS stacks have distinct features that can be [easily detected](https://arxiv.org/abs/1607.01639). TLS parameters are generally very informative and distinguishable. Most client-originated traffic comes from browsers, putting the custom network stacks in the minority.

Previously, Tor tried to mimic Firefox's TLS signature and still got [identified and blocked by firewalls](https://groups.google.com/d/msg/traffic-obf/BpFSCVgi5rs/nCqNwoeRKQAJ), because that signature was of an outdated version of Firefox and the firewall determined the rate of collateral damage would be acceptable. If we use the signature of the most commonly used browser, the collateral damage of blocking it would be unacceptable.

### Why not use Go, Node, etc. for performance?

Any languages can be used for high performance architectures, but not all architectures have high performance.

Go, Node, etc. make it easy to implement a 1:1 connection proxy model, i.e. creating one upstream connection for every user connection. Then under this model the performance goal is lower overhead in setting up each upstream connection. Toward that goal people start to reinvent their own 0-RTT cryptographic protocols (badly) as TLS goes out of the window because it either spends take several round trips in handshakes or makes it [a pain to set up 0-RTT properly](https://tools.ietf.org/html/rfc8446#section-8). Then people also start to look at low level optimization such as TCP Fast Open.

Meanwhile, Google has removed the code for TCP Fast Open in Chromium altogether (they [authored](https://tools.ietf.org/html/rfc7413) the RFC of TCP Fast Open in 2014). The literal reason given for this reversal was

> We never enabled it by default, and have no plans to, so we should just remove it.  QUIC also makes it less useful, and TLS 1.2 0-RTT session restore means it potentially mutates state.

And the real reason Google never enabled TCP Fast Open by default is that it was dragged down by middleboxes and [never really worked](https://blog.donatas.net/blog/2017/03/09/tfo/). In Linux kernel there is a sysctl called `tcp_fastopen_blackhole_timeout_sec`, and whenever a SYN packet is dropped, TCP Fast Open is blackholed for this much time, starting at one hour and increasing exponentially, rendering it practically useless. Today TCP Fast Open accounts for [0.1% of the Internet traffic](https://ieeexplore.ieee.org/document/8303960/), so using it actually makes you highly detectable!

It was obvious to Google then and is obvious to us now that the road to zero latency at the cost of compromising security and interoperability is a dead end under the 1:1 connection model, which is why Google pursued connection persistence and 1:N connection multiplexing in HTTP/2 and more radical overhaul of HTTP/TLS/TCP in QUIC. In a 1:N connection model, the cost of setting up the first connection is amortized, and the following connections cost nothing to set up without any security or stability compromises, and the race to zero connection latency becomes irrelevant.

Complex, battle-tested logic for connection management was [implemented](https://web.archive.org/web/20161222115511/https://insouciant.org/tech/connection-management-in-chromium/) in Chromium. The same thing is not so easy to do again from scratch with the aforementioned languages.

### Why not reinvent cryptos?

Because the first rule of cryptography is: [Don't roll your](http://loup-vaillant.fr/articles/rolling-your-own-crypto) [own cryptos](https://security.stackexchange.com/questions/18197/why-shouldnt-we-roll-our-own).

If you do roll your own cryptos, see what [happened](https://groups.google.com/d/msg/traffic-obf/CWO0peBJLGc/Py-clLSTBwAJ) with Shadowsocks. (Spoiler: it encrypts, but doesn't authenticate, leading to active probing exploits, and more exploits after duct-tape fixes.)

### Why not use HTTP/2 proxy from browser directly?

You may have wondered why not use Chrome directly if NaïveProxy reuses Chrome's network stack. The answer is yes, you can. You will get 80% of what NaïveProxy does (TLS, connection multiplexing, application fronting) without NaïveProxy, which is also what makes NaïveProxy indistinguishable from normal traffic. Simply point your browser to Caddy as an HTTP/2 or HTTP/3 forward proxy directly.

But this setup is prone to basic traffic analysis due to lack of obfuscation and predictable packet sizes in TLS handshakes. [The bane of "TLS-in-TLS" tunnels](http://blog.zorinaq.com/my-experience-with-the-great-firewall-of-china/) is that this combination is just so different from any normal protocols (nobody does 3-way handshakes twice in a row) and the record sizes of TLS handshakes are so predictable that no machine learning is needed to [detect it](https://github.com/shadowsocks/shadowsocks-org/issues/86#issuecomment-362809854).

The browser will introduce an extra 1RTT delay during proxied connection setup because of its interpretation of HTTP RFCs. The browser will wait for a 200 response after a CONNECT request, incurring unnecessary latency. NaïveProxy does HTTP Fast CONNECT similar to TCP Fast Open, i.e. sending subsequent data immediately after CONNECT without this 1RTT delay. Also, you may have to type in the password for the proxy every time you open the browser. NaïveProxy sends the password automatically.

Thus, traffic obfuscation, HTTP Fast CONNECT, and auto-authentication are the crucial last 20% provided by NaïveProxy. These can't be really achieved inside Chrome as extensions/apps because they don't have access to sockets. So instead, NaïveProxy extracts Chromium's network stack without all the other baggage to build a small binary (4% of a full Chrome build).

But if you don't need the best performance, and unobfuscated TLS-in-TLS somehow still works for you, you can just keep using Caddy proxy with your browser.

### Why no "CDN support"?

Take Cloudflare for example. https://www.cloudflare.com/terms/ says: "Use of the Service for serving video (unless purchased separately as a Paid Service) or a disproportionate percentage of pictures, audio files, or other non-HTML content, is prohibited." Proxying traffic is definitely prohibited by the terms in this context.
