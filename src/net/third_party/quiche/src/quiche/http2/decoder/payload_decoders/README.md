# HTTP/2 Frame Payload Decoders

This directory contains one decoder for each HTTP/2 frame type (i.e. a `.h`,
`.cc` and `_test.cc` file for each decoder, shared runtime infrastructure for
those decoders (`payload_decoder_base.*`), and shared test infrastructure
(`payload_decoder_base_test*`).

## `AbcXyzPayloadDecoder`

For frame type `ABC_XYZ`, there is a payload decoder called
`AbcXyzPayloadDecoder`. Each payload decoder class implements two methods which
will be called by `Http2FrameDecoder`:

*   `DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer*
    db)` is called after a frame header has been decoded and found to have frame
    type `ABC_XYZ`. It must decode the entire contents of the decode buffer,
    unless it detects an error.

*   `DecodeStatus ResumeDecodingPayload(FrameDecoderState* state, DecodeBuffer*
    db)` is called only if the most recent call to `StartDecodingPayload()`
    returned `DecodeStatus::kDecodeInProgress`, as did any and all subsequent
    calls to `ResumeDecodingPayload()`. It must decode the entire contents of
    the decode buffer, unless it detects an error.

These methods return:

*   `DecodeStatus::kDecodeDone` if the frame payload has been fully decoded
    without finding error (i.e. didn't call `OnFrameSizeError()` or
    `OnPaddingTooLong()`).

*   `DecodeStatus::kDecodeInProgress` if more payload remains to be read; in
    this situation the payload decoder must appropriately update
    `PayloadDecoderBase::remaining_payload_` and, if paddable,
    `PaddedPayloadDecoderBase::remaining_padding_`. The caller is responsible
    for calling `ResumeDecodingPayload()` when more of the payload is available.

*   `DecodeStatus::kDecodeError` if the payload size wasn't valid, in which case
    one of `OnFrameSizeError()` or `OnPaddingTooLong()` will have been called.

A payload decoder class has no virtual methods, no constructors, no destructor,
and no member initializers, nor any members with the same. This allows
`Http2FrameDecoder` to have a member that is a union of all the payload
decoders, which works because only one of these is needed at a time, and saves a
bit of memory.

### State machine

For those frame types (`DATA`, `PUSH_PROMISE`, etc.) that have optional fields,
or have both fixed length and variable length fields, the corresponding decoder
uses a state machine to track which field is being decoded (e.g. Pad Length,
Data, or Trailing Padding, in the case of `DATA` frames). The decoder has an
enum, `PayloadState`, whose values have names that either indicate the state or
the corresponding action to be performed (a bit confusing).

The decoder's `ResumeDecodingPayload()` method will have a switch statement,
with a case for each state. Often `StartDecodingPayload()` will choose the
initial state (e.g. for `DATA`, `kReadPadLength` if the `PADDED` flag is set,
else `kReadPayload`), and then call `ResumeDecodingPayload()` to execute the
state machine.

These frame types will typically have several associated methods in
`Http2FrameDecoderListener`. For example, when decoding a `CONTINUATION` frame,
the first callback is `OnContinuationStart()`, and the last is
`OnContinuationEnd()`; between these calls are as many `OnHpackFragment()` calls
as necessary to provide the entire payload to the listener; the number of
`OnHpackFragment()` calls depends on how many decode buffers the payload is
split across.

### Simpler decoders

The other frame types have quite simple payloads (e.g. `PING`, `PRIORITY`,
`RST_STREAM` and `WINDOW_UPDATE`), e.g. their payload is fixed size, has no
optional or variable length fields. The corresponding decoders do not have a
`PayloadState` enum, nor a switch statement using such an enum. Such decoders
use an `Http2StructureDecoder` to decode their payload into a type appropriate
structure (e.g. an `Http2RstStreamFields` instance for a `RST_STREAM` frame).

If the frame is contained entirely in a single decode buffer and is of the
correct size, `Http2StructureDecoder` will decode the structure into the output
structure, without any buffering.

However, if `StartDecodingPayload()` does not receive the entire payload, the
payload decoder uses two fields of `FrameDecoderState` to keep track of its
progress through the payload:

1.  `remaining_payload_` records the number of bytes of the frame's payload that
    have not yet been seen by the decoder.

1.  `structure_decoder_` (an `Http2StructureDecoder` instance) stores the
    buffered bytes of the structure; once all of the bytes of the encoded
    structure have been buffered, then the structure is decoded. Note that the
    fast path does not buffer the bytes, but decodes them straight out of the
    decode buffer.

### Fast path

Some frame types (`DATA`, `HEADERS`, `WINDOW_UPDATE`, `PING`) occur fairly
often, and seem worth having a fast path to handle the case where:

*   The entire payload in the decode buffer when `StartDecodingPayload` is
    called.

*   There are no optional fields present (e.g. the `PADDED` flag is not set on a
    `DATA` frame).

*   For fixed size payloads, the `payload_length` field of the frame's header
    has the correct value (e.g. 8 for a `PING`, or 4 for a `WINDOW_UPDATE`).

Using the fast path avoids any buffering inside the decoder of fixed size
structures, avoids updating the `FrameDecoderState`'s members (i.e.
`remaining_payload_` and `remaining_padding_` are not touched), and reduces the
branches taken, though at the cost of making the decision at the start regarding
whether to enter the fast path.

### Preconditions

Prior to calling `StartDecodingPayload()` or `ResumeDecodingPayload()`, the
caller (`Http2FrameDecoder` or a test) is responsible for:

*   Calling `FrameDecoderState::set_listener()` to provide the
    `Http2FrameDecoderListener` that is to be notified as the payload is
    decoded.

*   Ensuring that the common frame header is available via
    `FrameDecoderState::frame_header()`; in production this means decoding the
    common frame header into `FrameDecoderState::frame_header_` using
    `FrameDecoderState::StartDecodingFrameHeader`.

*   Rejecting excessively long frames; for example, if `SETTINGS_MAX_FRAME_SIZE`
    is 16KB, and a frame's payload length is greater than that, RFC 9113 calls
    for treating that as a stream or connection level
    [frame size error](https://www.rfc-editor.org/rfc/rfc9113.html#name-frame-size).

*   Clearing any invalid flags from the flags field of the frame header (i.e. a
    `SETTINGS` frame will either have no flags set, or the `ACK` flag, but
    nothing else).

*   Limiting the `DecodeBuffer` so that it does NOT extend beyond the end of the
    payload; note that this requires that
    `FrameDecoderState::remaining_payload_` and
    `FrameDecoderState::remaining_padding_`, if appropriate, have been set by
    the payload decoder when it returns `kDecodeInProgress` (see
    `FrameDecoderState::InitializeRemainders`).

*   Keeping track of whether to call `StartDecodingPayload()` or
    `ResumeDecodingPayload()` next.

### Error Detection

The general approach in the decoders is to assume that all is well until it
reaches a stopping point (e.g. the end of the decode buffer or after decoding
the expected content of a fixed size payload), at which point it will check for
errors.

For example, a `WINDOW_UPDATE` payload is supposed to be 4 bytes long, but the
decoder doesn't check for that until one of these situations has occurred:

*   It has decoded the window size increment (4 bytes). At this point, if it has
    reached the end of the payload, then there is no error because the frame is
    the correct size.
*   It has reached the end of the decode buffer before it has finished decoding
    the window size increment. At this point, if there is more payload remaining
    to be decoded, then it can return `kDecodeInProgress`. But if the decoder
    has also reached the end of the payload, then it must report a frame size
    error.

In order to support this, the caller (`Http2FrameDecoder`) is required to pass
in a `DecodeBuffer` that does not extend past the end of the payload.

## `Listener` in payload decoder *tests*

Each payload decoder test file includes a `Listener` class, extending
`FramePartsCollector` and implementing exactly the set of
`Http2FrameDecoderListener` methods invoked by the corresponding payload
decoder. For example, the `WINDOW_UPDATE` decoder will handle `OnWindowUpdate()`
and `OnFrameSizeError()`, but no other methods of `Http2FrameDecoderListener`.

See the `README.md` in the parent directory for descriptions of
`FramePartsCollector`, etc.
