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

Browser ⟶ Caddy ⟶ Internet

You may have wondered why not use Chrome directly if NaiveProxy reuses Chrome's network stack. The answer is yes, you can get 80% of what NaiveProxy does without NaiveProxy: point your browser to Caddy as an HTTP/2 or HTTP/3 forward proxy directly.

But this setup is prone to basic traffic analysis due to lack of obfuscation and predictable packet sizes in TLS handshakes. Also, the browser will introduce an extra 1RTT delay during proxy connection setup.

## Build

If you don't like to use downloaded binaries, you can build it.

Prerequisites:
* Ubuntu (apt-get install): git, python2, ninja-build (>= 1.7), pkg-config, libnss3-dev, ccache (optional)
* MacOS (brew install): git, ninja, ccache (optional)
* Windows ([choco install](https://chocolatey.org/)): git, python2, ninja, visualstudio2017community. See [Chromium's page](https://chromium.googlesource.com/chromium/src/+/master/docs/windows_build_instructions.md#Visual-Studio) for detail on Visual Studio setup requirements.


Build it:
```
git clone https://github.com/klzgrad/naiveproxy.git
cd naiveproxy/src
./get-clang.sh
./build.sh
```
The build scripts download tools from Google servers with curl. If there is trouble try to set a proxy environment variable for curl, e.g. `export ALL_PROXY=socks5h://127.0.0.1:1080`.

Verify:
```
./out/Release/naive --log &
curl -v --proxy socks5h://127.0.0.1:1080 google.com
```

## FAQ

### Why not use Go, Node, etc.?

Their TLS stacks have distinct features that can be [easily detected](https://arxiv.org/abs/1607.01639) and generally TLS parameters are very distinguishable.

Previously, Tor tried to mimic Firefox's TLS signature and still got [identified and blocked by firewalls](https://groups.google.com/d/msg/traffic-obf/BpFSCVgi5rs/nCqNwoeRKQAJ), because that signature was of an outdated version of Firefox and the firewall determined the rate of collateral damage would be acceptable. It would be unacceptable to block the most commonly used browsers.
