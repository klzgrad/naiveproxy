# Naive WebSocket bridge

A small reference server for Naive's `ws://` and `wss://` upstream transport:

```sh
go build .
./wss_bridge -addr 127.0.0.1:8080 -path /naive -user me -pass secret
```

Put a trusted TLS reverse proxy in front for production. The bridge uses Basic
auth, rejects unauthenticated non-loopback binds, and caps concurrent
connections with `-max-connections`.

After the WebSocket upgrade, the client sends
`version(1) address_type(1) address port(2)`. Address types are 1 for IPv4, 3
for a length-prefixed domain, and 4 for IPv6. The server replies with one
status byte, then binary messages carry the target TCP stream. Variant 1
padding is negotiated with Naive's existing padding headers.
