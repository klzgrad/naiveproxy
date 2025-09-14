# `oghttp2`: a general purpose HTTP/2 library

## Overview

The code in this directory implements the HTTP/2 internet protocol as specified
in [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113.html).

You will need the following additional pieces of code in order to build a
functioning client or server:

*   an event loop, like [`libevent`](https://libevent.org/) or
    [`libuv`](https://libuv.org/);
*   a TLS library, like [OpenSSL](https://www.openssl.org/); and
*   application logic, to generate HTTP headers and bodies for the requests you
    want to send, or the responses you want to serve.

### Motivation

Back in 2018, the open source [Envoy proxy](https://github.com/envoyproxy/envoy)
community [discovered](https://github.com/envoyproxy/envoy/issues/5155) that the
[`http-parser`](https://github.com/nodejs/http-parser) HTTP/1.1 codec library in
use at the time was no longer actively maintained. While working to resolve this
issue, Google engineers evaluated several HTTP/1 codec libraries as potential
replacements. After much consideration, we decided to release the Google HTTP/1
codec library (Balsa) as part of the [QUICHE](https://github.com/google/quiche)
open source project.

At this point, the QUICHE project provided open source implementations of HTTP/1
and HTTP/3, but not HTTP/2. We were motivated in part by this glaring omission,
but also by the desire to provide a modern, readable C++ implementation for use
by Envoy and other future projects.

### History

We reused some existing open source code when building the `oghttp2` library. As
part of the [SPDY](https://www.chromium.org/spdy/spdy-whitepaper/) project,
developers on the Chromium project worked with other Google engineers to invent
a new multiplexed transport for HTTP. As the project evolved over time, those
engineers built several shared utilities, many of which survive to the present
day.

These utilities are incorporated wholesale into `oghttp2`:

*   a wire format encoder, in `http2/core/spdy_framer.h`;
*   a wire format decoder, in `http2/decoder/http2_frame_decoder.h`;
*   a HPACK encoder, in `http2/hpack/hpack_encoder.h`; and
*   a HPACK decoder, in `http2/hpack/hpack_decoder_adapter.h`.

The name of the library is related to this history: it was built from the
"original generation" HTTP/2 implementation.

Given that part of our motivation for the `oghttp2` project was the needs of the
Envoy open source project, the first piece that we built was a C++ API around
the existing HTTP/2 library that Envoy used at that time:
[`nghttp2`](https://nghttp2.org/). This API started as an "adapter" layer
between Envoy's notion of a codec and what the `nghttp2` library provided, which
is why `oghttp2` is located in `http2/adapter`.

We would like to thank the `nghttp2` project for providing a straightforward C
API that we could build on.

## Code Overview

The main structure of the library consists of two components:

*   a session object that represents a single multiplexed HTTP/2 connection
    (called a `Http2Adapter` in the code), and
*   a visitor interface (`Http2VisitorInterface`) that application code can
    implement in order to receive HTTP/2 events.

An application can take certain actions to update the state of the HTTP/2
connection by invoking `Http2Adapter` methods. The application can observe the
effects of those actions and the behavior of the peer as `Http2VisitorInterface`
callbacks are invoked.

Consider the following example:

```
Http2Adapter* client_session = /* code to initialize the session */;
MyStreamStruct* stream_data = new MyStreamStruct;
const int32_t stream_id =
    client_session->SubmitRequest({{":authority", "www.example.com"},
                                   {":scheme", "http"},
                                   {":method", "GET"},
                                   {":path", "/index.html"}},
                                   /*end_stream=*/true,
                                   /*user_data=*/stream_data);
const int status = client_session->Send();
// Handle error if status is nonzero.
```

This is how a client would send a request on a new stream. When the request is
actually sent, the client application would receive an `OnFrameSent()` callback
with the `HEADERS` frame type, the stream ID on which the request was sent, the
payload size, and any flags set on the frame.

### Common Types

Several protocol elements from the spec are defined as types in
`http2_protocol.h`:

*   `Http2StreamId`: the 31-bit stream ID
*   `FrameType`: the 8-bit frame type value (e.g. `HEADERS`, `DATA`, etc.)
*   `FrameFlags`: each of the frame flag values defined in the specification
*   `Http2ErrorCode`: an enumeration of the error codes
*   `Http2KnownSettingsId`: an enumeration of the `SETTINGS` identifiers

This file also provides several useful constants related to limits or initial
values defined in the specification.

### Design

### Adapter Implementations
