# NaiveProxy [![Build Status](https://travis-ci.com/klzgrad/naiveproxy.svg?branch=master)](https://travis-ci.com/klzgrad/naiveproxy) [![Build status](https://ci.appveyor.com/api/projects/status/ohpyaf49baihmxa9?svg=true)](https://ci.appveyor.com/project/klzgrad/naiveproxy)

A secure, censorship-resistent proxy.

This tool improves censorship resistence by obfuscating traffic as common HTTP/2 traffic with minimal distinguishable features. Privacy and integrity are simultaneously achieved through implementations of TLS best practices.

The following attacks are mitigated:

* Website fingerprinting / traffic classification: [mitigated](https://arxiv.org/abs/1707.00641) by traffic multiplexing in HTTP/2.
* [TLS parameter fingerprinting](https://arxiv.org/abs/1607.01639): defeated by using identical behaviors from [Chromium's network stack](https://www.chromium.org/developers/design-documents/network-stack).
* [Active probing](https://ensa.fi/active-probing/): defeated by application fronting, using a common frontend with application-layer routing capability, e.g. HAProxy.
* Length-based traffic analysis: mitigated by length padding.

## Architecture

<p align="center">[Browser → Naive (client)] ⟶ Censor ⟶ [Frontend → Naive (server)] ⟶ Internet</p>

NaiveProxy uses Chromium's network stack. What the censor can see is exactly regular HTTP/2 traffic between Chrome and Frontend (e.g. HAProxy), two of the most commonly used browsers and servers. Being as common as possible reduces the viability of traffic classification censorship.

Frontend also reroutes unauthenticated users and active probes to a backend HTTP server, making it impossible to detect the existence of a proxy:

<p align="center">Probe ⟶ [Frontend → Nginx] ⟶ index.html</p>

## Download

See [latest release](https://github.com/klzgrad/naiveproxy/releases/latest).

Note: On Linux libnss3 must be installed before using the prebuilt binary.

## Build

If you don't like to use downloaded binaries, you can build it.

* Prerequisites:
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
The build scripts download tools from Google servers with curl. If there is trouble try to set a proxy environment variable for curl, e.g.: `export ALL_PROXY=socks5h://127.0.0.1:1080`.

Verify:
```
./out/Release/naive --log &
curl -v --proxy socks5h://127.0.0.1:1080 google.com
```

## Setup

The `naive` binary functions as both the client and the server. Naive client can be run as `./naive --proxy=https://user:pass@domain.example`, which accepts SOCKS5 traffic at port 1080 and proxies it via `domain.example` as HTTP/2 traffic. Naive server can be run as `./naive --listen=http://127.0.0.1:8080` behind the frontend. You can also store the parameters in `config.json` and `./naive` will detect it automatically.

For details on setting up the server part [Frontend → Naive (server)], see [Server Setup](https://github.com/klzgrad/naiveproxy/wiki/Server-Setup).

For more information on parameter usage, see USAGE.txt. See also [Parameter Tuning](https://github.com/klzgrad/naiveproxy/wiki/Parameter-Tuning) to improve client-side performance.

### Portable setup

<p align="center">Browser ⟶ [HAProxy → Tinyproxy] ⟶ Internet</p>

This mode is clientless: point your browser directly to the server as an HTTPS proxy. You don't need to download, build, or run anything client-side.

But this setup is prone to traffic analysis due to lack of obfuscation. Also, the browser will introduce an extra 1RTT delay during connection setup.

Tinyproxy is used in place of Naive server in this mode, so you only need to `apt-get install tinyproxy` without downloading anything manually.
