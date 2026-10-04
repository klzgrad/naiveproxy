# WebSocket bridge

A small WebSocket-to-TCP bridge for NaiveProxy's `ws://` and `wss://`
transport. Put it behind an HTTP/WebSocket-capable CDN, or expose it directly.

## Build

```sh
go build -o wss-bridge .
```

## Run

```sh
./wss-bridge -addr 127.0.0.1:8080 -path /naive -user me -pass secret
```

For a direct TLS endpoint, add `-cert fullchain.pem -key privkey.pem`. When
serving behind Cloudflare, the edge can terminate HTTPS and connect to this
bridge over HTTP or HTTPS according to your origin settings.

Client configuration:

```json
{
  "listen": "socks://127.0.0.1:1080",
  "proxy": "wss://me:secret@example.com/naive"
}
```

The client lets the Chromium network stack perform DNS, TLS, the WebSocket
HTTP/1.1 upgrade, masking, and framing. The client context disables HTTP/2 and
HTTP/3 so a CDN sees the ordinary WebSocket upgrade rather than extended
CONNECT. `ws://` is intended for local testing; use `wss://` in production.

## Wire format

After the standard WebSocket upgrade, the client sends one binary message:

```text
version(1) | address_type(1) | address | port(2, big endian)
```

Address type `1` is IPv4, `3` is a length-prefixed domain, and `4` is IPv6.
The bridge replies with one binary byte: `0` on success, otherwise a nonzero
error. After that, binary messages carry the target TCP stream in both
directions.

The client sends Naive's `padding` and `padding-type-request: 1,0` headers in
the WebSocket handshake. If the bridge accepts, it returns `padding` and
`padding-type-reply: 1` and applies Naive Variant1 padding to the first eight
TCP data messages in each direction. The target-address header and status byte
remain unpadded for compatibility with the tunnel protocol. Compression is not
negotiated so zero padding remains visible on the wire.
