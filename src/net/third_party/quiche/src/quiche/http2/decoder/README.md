# HTTP/2 Decoder Implementation Notes

The general philosophy used here is to do as little as possible. In particular,
this means that member variables aren't read or written if not absolutely
necessary, especially in optimized builds.

If we can reasonably expect the listener to double check our work (i.e. looking
up a stream id, and therefore noticing if it is supposed to be non-zero but
isn't), then we'll skip that check.

## `FrameDecoderState`

This class provides common state and behaviors needed by all of the payload
decoders, including:

*   Providing the common frame header (`Http2FrameHeader`) for the frame being
    decoded.

*   Tracking the amount of payload remaining to be decoded (often tracked only
    if the payload is split across decode buffers);

*   Decoding fixed size structures in the payload (e.g. `Http2PriorityFields`),
    updating the amount of payload remaining to be decoded as it does so, or
    reporting a Frame Size Error if a fixed size structure is truncated.

For `DATA`, `HEADERS` and `PUSH_PROMISE` frames, which support padding,
`FrameDecoderState` also supports:

*   Reading the (optional) Pad Length field and reporting its value to the
    listener.

*   Tracking the amount of trailing padding that remains to be skipped.

*   Skipping the trailing padding, reporting to the listener as it does so.

## `Http2FrameDecoderListener`

Defines the interface which HTTP/2 decoder clients must implement in order to
receive callbacks from the decoder as it decodes each frame.

Simple frames that contain a single fixed size payload have correspondingly
simple callback methods. For example:

*   `WINDOW_UPDATE` frames are reported via `OnWindowUpdate()`.
*   `PING` frames are reported via `OnPing()` or `OnPingAck()`, depending on
    whether the `ACK` flag is set.

However a frame type X with a variable length payload will be reported via
`OnXStart()` and `OnXEnd()` methods, which bracket calls to other methods that
report on the payload. For example:

*   `DATA` frames are reported via `OnDataStart()`, `OnPadLength()` (if the
    `PADDED` flag is set), `OnDataPayload()` (more than once if necessary),
    `OnPadding()` (if `OnPadLength()` reports a non-zero amount of trailing
    padding), and `OnDataEnd()` once all the payload and padding has been
    reported to the listener.
*   `GOAWAY` frames are reported via `OnGoAwayStart()`, `OnGoAwayOpaqueData()`
    (more than once if necessary), and `OnGoAwayEnd()` once all the opaque data
    has been reported to the listener.

In addition there are two error callbacks:

*   `OnPaddingTooLong()`: There isn't enough room in the frame's payload for all
    of the padding, let alone anything else.
*   `OnFrameSizeError()`: The frame size isn't correct, except for errors
    reported by `OnPaddingTooLong()`. For example, a `PRIORITY` frame isn't
    exactly 5 bytes long, or a `SETTINGS` frame payload isn't a multiple of 6
    bytes long.

### `FailingHttp2FrameDecoderListener`

This is a test-only sub-class of `Http2FrameDecoderListener` that treats any
call as a test failure. Its purpose is to make it easy to detect when a payload
decoder has called the wrong method or a test listener has failed to implement
all necessary methods.

### `FrameParts`

This is a test-only sub-class of `Http2FrameDecoderListener` that implements ALL
of the `Http2FrameDecoderListener` methods. Its purpose is to record all the
provided info during the decoding of a single frame, and also to provide some
validation of the callbacks as they are received (e.g. it generates a test
failure if `OnPadLength()` is called for a frame that doesn't have padding).

`FrameParts` instances are also directly created by tests for comparing against
those instances that are created while decoding. See
`FrameParts::VerifyEquals()`.

### `FramePartsCollector`

This is a test-only sub-class of `FailingHttp2FrameDecoderListener`, which
implements NONE of the `Http2FrameDecoderListener` methods, but instead serves
as a base class for test listeners for each payload decoder. It provides these
protected methods:

| Method               | Purpose                                               |
| -------------------- | ----------------------------------------------------- |
| `StartFrame()`       | For use when implementing the `OnXStart()` method for |
:                      : frame type X. Creates a new `FrameParts` instance,    :
:                      : and making it available to `CurrentFrame()`,          :
:                      : `EndFrame()` and `FrameError()` (see below).          :
| `CurrentFrame()`     | For use when implementing the listener methods for    |
:                      : variable length and optional payload components (e.g. :
:                      : `OnDataPayload()`, `OnHeadersPriority()`, or          :
:                      : `OnPadding()`). `StartFrame()` must have already been :
:                      : called for the frame.                                 :
| `EndFrame()`         | For use when implementing the `OnXEnd()` method for   |
:                      : frame type X. Pushes the frame that `CurrentFrame()`  :
:                      : would report onto the list of completely decoded      :
:                      : frames that it has recorded. `StartFrame()` must have :
:                      : already been called for the frame.                    :
| `StartAndEndFrame()` | For use when implementing the `OnX()` method for      |
:                      : frame type `X` that has a fixed size payload (e.g.    :
:                      : `OnRstStream()`), and hence is not reported via       :
:                      : bracketed `OnXStart()` and `OnXEnd()` methods.        :
| `FrameError()`       | For use in handling `OnPaddingTooLong()` and          |
:                      : `OnFrameSizeError()`, where an earlier `OnXStart()`   :
:                      : method may or may not have already been called.       :

*For consideration: If `FramePartsCollector` implemented ALL of the
`Http2FrameDecoderListener` methods, then we could eliminate the `Listener`
classes in payload decoder tests. Instead we could add a `FramePartsCollector`
constructor overload which takes the expected `Http2FrameType`. If the frame
type has been provided, then `FramePartsCollector` will validate it, but
otherwise will forward all calls to the current `FrameParts` instance, using the
methods described above just as a `Listener` might today.*
