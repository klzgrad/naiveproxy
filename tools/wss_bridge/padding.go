package main

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"math/big"
	"net/http"
	"strings"

	"github.com/gorilla/websocket"
)

const (
	paddingHeader            = "Padding"
	paddingTypeRequestHeader = "Padding-Type-Request"
	paddingTypeReplyHeader   = "Padding-Type-Reply"
	paddingTypeVariant1      = "1"
	firstPaddings            = 8
	maxPaddingSize           = 255
)

// The first sixteen characters match the non-index values used by the Naive
// H2 client; the seventeenth fills the remainder of the random header value.
const nonindexChars = "!\"#$&'()*+,;@XZ[\\"

func wantsVariant1Padding(header http.Header) bool {
	for _, values := range header.Values(paddingTypeRequestHeader) {
		for _, value := range strings.Split(values, ",") {
			if strings.TrimSpace(value) == paddingTypeVariant1 {
				return true
			}
		}
	}
	return false
}

func randomPaddingValue() (string, error) {
	size, err := rand.Int(rand.Reader, big.NewInt(17))
	if err != nil {
		return "", err
	}
	value := make([]byte, 16+size.Int64())
	random := make([]byte, len(value))
	if _, err := rand.Read(random); err != nil {
		return "", err
	}
	for i := 0; i < 16 && i < len(value); i++ {
		value[i] = nonindexChars[int(random[i])%16]
	}
	for i := 16; i < len(value); i++ {
		value[i] = nonindexChars[16]
	}
	return string(value), nil
}

func randomPaddingSize() (int, error) {
	size, err := rand.Int(rand.Reader, big.NewInt(maxPaddingSize+1))
	if err != nil {
		return 0, err
	}
	return int(size.Int64()), nil
}

type paddingWriter struct {
	frame int
}

func newPaddingWriter(enabled bool) *paddingWriter {
	if !enabled {
		return nil
	}
	return &paddingWriter{}
}

func (w *paddingWriter) write(conn *websocket.Conn, payload []byte) error {
	if w != nil && w.frame < firstPaddings {
		padded, err := encodePaddingFrame(payload, -1)
		if err != nil {
			return err
		}
		w.frame++
		return conn.WriteMessage(websocket.BinaryMessage, padded)
	}
	return conn.WriteMessage(websocket.BinaryMessage, payload)
}

func encodePaddingFrame(payload []byte, requestedPaddingSize int) ([]byte, error) {
	paddingSize := requestedPaddingSize
	if paddingSize < 0 {
		var err error
		paddingSize, err = randomPaddingSize()
		if err != nil {
			return nil, err
		}
	}
	if paddingSize < 0 || paddingSize > maxPaddingSize {
		return nil, fmt.Errorf("invalid padding size %d", paddingSize)
	}
	if len(payload) > 0xffff {
		return nil, fmt.Errorf("padding payload size %d exceeds 65535", len(payload))
	}
	padded := make([]byte, 3+len(payload)+paddingSize)
	binary.BigEndian.PutUint16(padded, uint16(len(payload)))
	padded[2] = byte(paddingSize)
	copy(padded[3:], payload)
	return padded, nil
}

type paddingReadState uint8

const (
	paddingStatePayloadLength1 paddingReadState = iota
	paddingStatePayloadLength2
	paddingStatePaddingLength
	paddingStatePayload
	paddingStatePadding
)

type paddingReader struct {
	frame         int
	payloadLength int
	paddingLength int
	state         paddingReadState
}

func (r *paddingReader) unwrap(padded []byte) ([]byte, error) {
	if r == nil || r.frame >= firstPaddings {
		return padded, nil
	}

	var payload []byte
	for len(padded) > 0 {
		switch r.state {
		case paddingStatePayloadLength1:
			r.payloadLength = int(padded[0]) << 8
			padded = padded[1:]
			r.state = paddingStatePayloadLength2
		case paddingStatePayloadLength2:
			r.payloadLength += int(padded[0])
			padded = padded[1:]
			r.state = paddingStatePaddingLength
		case paddingStatePaddingLength:
			r.paddingLength = int(padded[0])
			padded = padded[1:]
			r.state = paddingStatePayload
		case paddingStatePayload:
			size := min(r.payloadLength, len(padded))
			payload = append(payload, padded[:size]...)
			r.payloadLength -= size
			padded = padded[size:]
			if r.payloadLength == 0 {
				r.state = paddingStatePadding
			}
		case paddingStatePadding:
			size := min(r.paddingLength, len(padded))
			r.paddingLength -= size
			padded = padded[size:]
			if r.paddingLength == 0 {
				r.frame++
				r.state = paddingStatePayloadLength1
			}
		default:
			return nil, fmt.Errorf("invalid padding read state %d", r.state)
		}
	}
	return payload, nil
}
