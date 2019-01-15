# NaiveProxy [![Build Status](https://travis-ci.com/klzgrad/naiveproxy.svg?branch=master)](https://travis-ci.com/klzgrad/naiveproxy) [![Build status](https://ci.appveyor.com/api/projects/status/ohpyaf49baihmxa9?svg=true)](https://ci.appveyor.com/project/klzgrad/naiveproxy)

A secure, analysis-resistent proxy framework.

The main goal is to improve censorship resistence by reducing distinguishable traffic features. Privacy and integrity are simultaneously achieved through implementations of TLS best practices.

The following attacks are mitigated:

* Website fingerprinting / traffic classification: [mitigated](https://arxiv.org/abs/1707.00641) by traffic multiplexing in HTTP/2.
* [TLS parameter fingerprinting](https://arxiv.org/abs/1607.01639): defeated by using identical behaviors from [Chromium's network stack](https://www.chromium.org/developers/design-documents/network-stack).
* [Active probing](https://ensa.fi/active-probing/): defeated by application fronting, using a common frontend with application-layer routing capability, e.g. HAProxy.
* Length-based traffic analysis: mitigated by length padding.

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

Server setup is required first, see [Server Setup](https://github.com/klzgrad/naiveproxy/wiki/Server-Setup).

There are three tiers of client setup:

* The portable setup is clientless: point your browser directly to the server as an HTTPS proxy. You don't need to download, build, or run anything client-side, but this setup is prone to traffic analysis due to lack of obfuscation.
* The fast setup improves performance by running Naive client locally as a SOCKS5 proxy. Point your browser to the address of Naive client. You don't need to run Naive server in this setup.
* The full setup obfuscates traffic by running both Naive client and server. Point your browser to the local SOCKS5 proxy provided by Naive client.

To run Naive client:
```
./naive --proxy=https://user:pass@domainname.example
```
You can also store the config in `config.json`, example:
```
{
  "proxy": "https://user:pass@domainname.example"
}
```
Naive client will detect and read from `config.json` by default. The default listening port is 1080 as SOCKS5.

For more information on parameter usage and Naive server, see USAGE.txt.

See also [Parameter Tuning](https://github.com/klzgrad/naiveproxy/wiki/Parameter-Tuning) to improve client-side performance.
