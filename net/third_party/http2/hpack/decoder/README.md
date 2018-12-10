Copyright 2017 The Chromium Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.

## gfe/http2/hpack/decoder

These are the most popular C++ files defined in this directory.

*   [hpack_entry_decoder_listener.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_entry_decoder_listener.h) (1
    imports): Defines HpackEntryDecoderListener, the base class of listeners
    that HpackEntryDecoder calls.
*   [hpack_decoder_string_buffer.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_decoder_string_buffer.h) (1
    imports): HpackDecoderStringBuffer helps an HPACK decoder to avoid copies of
    a string literal (name or value) except when necessary (e.g.
*   [hpack_block_decoder.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_block_decoder.h) (1 imports):
    HpackBlockDecoder decodes an entire HPACK block (or the available portion
    thereof in the DecodeBuffer) into entries, but doesn't include HPACK static
    or dynamic table support, so table indices remain indices at this level.
*   [hpack_varint_decoder.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_varint_decoder.h) (zero
    imports): HpackVarintDecoder decodes HPACK variable length unsigned
    integers.
*   [hpack_entry_collector.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_entry_collector.h) (zero
    imports): HpackEntryCollector records calls to HpackEntryDecoderListener in
    support of tests of HpackEntryDecoder, or which use it.
*   [hpack_string_collector.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_string_collector.h) (zero
    imports): Supports tests of decoding HPACK strings.
*   [hpack_string_decoder.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_string_decoder.h) (zero
    imports): HpackStringDecoder decodes strings encoded per the HPACK spec;
    this does not mean decompressing Huffman encoded strings, just identifying
    the length, encoding and contents for a listener.
*   [hpack_block_collector.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_block_collector.h) (zero
    imports): HpackBlockCollector implements HpackEntryDecoderListener in order
    to record the calls using HpackEntryCollector instances (one per HPACK
    entry).
*   [hpack_entry_decoder.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_entry_decoder.h) (zero
    imports): HpackEntryDecoder decodes a single HPACK entry (i.e.
*   [hpack_entry_type_decoder.h]
    (http://google3/gfe/http2/hpack/decoder/hpack_entry_type_decoder.h) (zero
    imports): Decodes the type of an HPACK entry, and the variable length
    integer whose prefix is in the low-order bits of the same byte, "below" the
    type bits.
