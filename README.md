# NaïveProxy and Cronet ![build workflow](https://github.com/klzgrad/naiveproxy/actions/workflows/build.yml/badge.svg)

NaïveProxy uses Chromium's network stack to camouflage traffic with strong censorship resistence and low detectablility. Reusing Chrome's stack also ensures best practices in performance and security.

Cronet is a library similarly derived from Chromium's network stack, but its official releases are limited to Android and iOS. NaïveProxy's fork of Cronet provides binary releases of its native API, support for multiple platforms, and support for creating Go apps with cgo and the [cronet-go](https://github.com/SagerNet/cronet-go) bindings.

The following traffic attacks are mitigated by using Chromium's network stack:

* Website fingerprinting / traffic classification: [mitigated](https://arxiv.org/abs/1707.00641) by traffic multiplexing in HTTP/2.
* [TLS parameter fingerprinting](https://arxiv.org/abs/1607.01639): defeated by reusing [Chrome's network stack](https://www.chromium.org/developers/design-documents/network-stack).
* [Active probing](https://ensa.fi/active-probing/): defeated by *application fronting*, i.e. hiding proxy servers behind a commonly used frontend server with application-layer routing.
* Length-based traffic analysis: mitigated by length padding.

## Architecture

[Browser → Naïve client] ⟶ Censor ⟶ [Frontend → Naïve server] ⟶ Internet

NaïveProxy uses Chromium's network stack to parrot traffic between regular Chrome browsers and standard frontend servers.

The frontend server can be any well-known reverse proxy that is able to route HTTP/2 traffic based on HTTP authorization headers, preventing active probing of proxy existence. Known ones include Caddy with its forwardproxy plugin and HAProxy.

The Naïve server here works as a forward proxy and a packet length padding layer. Caddy forwardproxy is also a forward proxy but it lacks a padding layer. A [fork](https://github.com/klzgrad/forwardproxy) adds the NaïveProxy padding layer to forwardproxy, combining both in one.

## Download NaïveProxy and Cronet binaries

[Download here](https://github.com/klzgrad/naiveproxy/releases/latest). Supported platforms include: Windows, Android (with [SagerNet](https://github.com/SagerNet/SagerNet)), Linux, Mac OS, and OpenWrt ([support status](https://github.com/klzgrad/naiveproxy/wiki/OpenWrt-Support)).

Users should always use the latest version to keep signatures identical to Chrome.

Build from source: Please see [.github/workflows/build.yml](https://github.com/klzgrad/naiveproxy/blob/master/.github/workflows/build.yml).

## Server setup

The following describes the naïve fork of forwardproxy setup.

Build:
```sh
go install github.com/caddyserver/xcaddy/cmd/xcaddy@latest
~/go/bin/xcaddy build --with github.com/caddyserver/forwardproxy@caddy2=github.com/klzgrad/forwardproxy@naive
```

Example Caddyfile (replace `user` and `pass` accordingly):
```
{
  servers {
    protocols h1 h2 h3
  }
}
:443, example.com
tls me@example.com
route {
  forward_proxy {
    basic_auth user pass
    hide_ip
    hide_via
    probe_resistance
  }
  file_server {
    root /var/www/html
  }
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

Or `quic://user:pass@example.com`, if it works better. See also [parameter usage](https://github.com/klzgrad/naiveproxy/blob/master/USAGE.txt) and [performance tuning](https://github.com/klzgrad/naiveproxy/wiki/Performance-Tuning).

## Notes for downstream

Do not use the master branch to track updates, as it rebases from a new root commit for every new Chrome release. Use stable releases and the associated tags to track new versions, where short release notes are also provided.

## Padding protocol, an informal specification

The design of this padding protocol opts for low overhead and easier implementation, in the belief that proliferation of expendable, improvised circumvention protocol designs is a better logistical impediment to censorship research than sophisicated designs.

### Proxy payload padding

NaïveProxy proxies bidirectional streams through HTTP/2 (or HTTP/3) CONNECT tunnels. The bidirectional streams operate in a sequence of reads and writes of data. The first `kFirstPaddings` (8) reads and writes in a bidirectional stream after the stream is established are padded in this format:
```c
struct PaddedData {
  uint8_t original_data_size_high;  // original_data_size / 256
  uint8_t original_data_size_low;  // original_data_size % 256
  uint8_t padding_size;
  uint8_t original_data[original_data_size];
  uint8_t zeros[padding_size];
};
```
`padding_size` is a random integer uniformally distributed in [0, `kMaxPaddingSize`] (`kMaxPaddingSize`: 255). `original_data_size` cannot be greater than 65535, or it has to be split into several reads or writes.

`kFirstPaddings` is chosen to be 8 to flatten the packet length distribution spikes formed from common initial handshakes:
- Common client initial sequence: 1. TLS ClientHello; 2. TLS ChangeCipherSpec, Finished; 3. H2 Magic, SETTINGS, WINDOW_UPDATE; 4. H2 HEADERS GET; 5. H2 SETTINGS ACK.
- Common server initial sequence: 1. TLS ServerHello, ChangeCipherSpec, ...; 2. TLS Certificate, ...; 3. H2 SETTINGS; 4. H2 WINDOW_UPDATE; 5. H2 SETTINGS ACK; 6. H2 HEADERS 200 OK.

Further reads and writes after `kFirstPaddings` are unpadded to avoid performance overhead. Also later packet lengths are usually considered less informative.

### H2 RST_STREAM frame padding

In experiments, NaïveProxy tends to send too many RST_STREAM frames per session, an uncommon behavior from regular browsers. To solve this, an END_STREAM DATA frame padded with total length distributed in [48, 72] is prepended to the RST_STREAM frame so it looks like a HEADERS frame. The server often replies to this with a WINDOW_UPDATE because padding is accounted in flow control. Whether this results in a new uncommon behavior is still unclear. 

### H2 HEADERS frame padding

The CONNECT request and response frames are too short and too uncommon. To make its length similar to realistic HEADERS frames, a `padding` header is filled with a sequence of symbols that are not Huffman coded and are pseudo-random enough to avoid being indexed. The length of the padding sequence is randomly distributed in [16, 32] (request) or [30, 62] (response).

### Opt-in of padding protocol

NaïveProxy clients should interoperate with any regular HTTP/2 proxies unaware of this padding protocol. NaïveProxy servers (i.e. any proxy server capable of the this padding protocol) should interoperate with any regular HTTP/2 clients (e.g. regular browsers) unaware of this padding protocol.

NaïveProxy servers and clients determines whether the counterpart is capable of this padding protocol by the presence of the `padding` header in the CONNECT request and response respectively. The padding procotol is enabled only if the `padding` header exists.

The first CONNECT request to a server cannot use "Fast Open" to send payload before response, because the server's padding capability has not been determined from the first response and it's unknown whether to send padded or unpadded payload for Fast Open.

## Changes from upstream

- Minimize source code and build size (1% of the original)
- Disable exceptions and RTTI, except on Mac and Android.
- Support OpenWrt builds
- (Android, Linux) Use the builtin verifier instead of the system verifier (drop dependency of NSS on Linux) and read the system trust store from (following Go's behavior in crypto/x509/root_unix.go and crypto/x509/root_linux.go):
  - The file in environment variable SSL_CERT_FILE
  - The first available file of
    -  /etc/ssl/certs/ca-certificates.crt (Debian/Ubuntu/Gentoo etc.)
    -  /etc/pki/tls/certs/ca-bundle.crt (Fedora/RHEL 6)
    -  /etc/ssl/ca-bundle.pem (OpenSUSE)
    -  /etc/pki/tls/cacert.pem (OpenELEC)
    -  /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem (CentOS/RHEL 7)
    -  /etc/ssl/cert.pem (Alpine Linux)
  - Files in the directory of environment variable SSL_CERT_DIR
  - Files in the first available directory of
    -  /etc/ssl/certs (SLES10/SLES11, https://golang.org/issue/12139)
    -  /etc/pki/tls/certs (Fedora/RHEL)
    -  /system/etc/security/cacerts (Android)
- Handle AIA response in PKCS#7 format
- Allow higher socket limits for proxies
- Force tunneling for all sockets
- Support HTTP/2 and HTTP/3 CONNECT tunnel Fast Open using the `fastopen` header
- Pad RST_STREAM frames
- (Cronet) Allow passing in `-connect-authority` header to override the CONNECT authority field
- (Cronet) Disable system proxy resolution and use fixed proxy resolution specified by experimental option `proxy_server`
- (Cronet) Support setting base::FeatureList by experimental option `feature_list`
- (Cronet) Support setting the network isolation key of a stream with `-network-isolation-key` header
- (Cronet) Add certificate net fetcher
- (Cronet) Support setting socket limits by experimental option `socket_limits`
