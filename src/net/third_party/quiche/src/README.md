# QUICHE

QUICHE stands for QUIC, Http, Etc. It is Google's production-ready
implementation of QUIC, HTTP/2, HTTP/3, and related protocols and tools. It
powers Google's servers, Chromium, Envoy, and other projects. It is actively
developed and maintained.

There are two public QUICHE repositories. Either one may be used by embedders,
as they are automatically kept in sync:

*   https://quiche.googlesource.com/quiche
*   https://github.com/google/quiche

To embed QUICHE in your project, platform APIs need to be implemented and build
files need to be created. Note that it is on the QUICHE team's roadmap to
include default implementation for all platform APIs and to open-source build
files. In the meanwhile, take a look at open source embedders like Chromium and
Envoy to get started:

*   [Platform implementations in Chromium](https://source.chromium.org/chromium/chromium/src/+/main:net/third_party/quiche/overrides/quiche_platform_impl/)
*   [Build file in Chromium](https://source.chromium.org/chromium/chromium/src/+/main:net/third_party/quiche/BUILD.gn)
*   [Platform implementations in Envoy](https://github.com/envoyproxy/envoy/tree/master/source/common/quic/platform)
*   [Build file in Envoy](https://github.com/envoyproxy/envoy/blob/main/bazel/external/quiche.BUILD)

To contribute to QUICHE, follow instructions at
[CONTRIBUTING.md](CONTRIBUTING.md).

QUICHE is only supported on little-endian platforms.

## Build and run standalone QUICHE

QUICHE has binaries that can run on Linux platforms.

Follow the [instructions](https://bazel.build/install) to install Bazel.

```
sudo apt install libicu-dev clang lld
cd <directory that will be the root of your quiche implmentation>
git clone https://github.com/google/quiche.git
cd quiche
CC=clang bazel build -c opt //...
./bazel-bin/quiche/<target_name> <arguments>
```

There are several targets that can be built and then run. Full usage
instructions are available using the `--helpfull` flag on any binary.

*   quic_packet_printer: from a provided packet, parses and prints out the
    contents that are accessible without decryption.

Usage: `quic_packet_printer server|client <hex dump of packet>`

*   crypto_message_printer: dumps the contents of a QUIC crypto handshake
    message in a human readable format.

Usage: `crypto_message_printer_bin <hex of message>`

*   quic_client: connects to a host using QUIC and HTTP/3, sends a request to
    the provided URL, and displays the response.

Usage: `quic_client <URL>`

*   quic_server: listens forever on --port (default 6121) until halted via
    ctrl-c.

*   masque_client: tunnels to a URL via an identified proxy (See RFC 9298).

Usage: `masque_client [options] <proxy-url> <urls>`

*   masque_server: a MASQUE tunnel proxy that defaults to port 9661.

Usage: `masque_server`

*   web_transport_test_server: a server that clients can connect to via
    WebTransport.

*   moqt_relay: a relay for the Media Over QUIC transport for publishers and
    subscribers can connect to.

Usage: `moqt_relay`
