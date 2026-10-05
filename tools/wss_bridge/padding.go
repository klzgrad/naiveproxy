package main

import (
	"crypto/rand"
	"encoding/binary"
	"math/big"
	"net/http"
	"strings"
	"time"

	"github.com/gorilla/websocket"
)

const (
	paddingHeader            = "Padding"
	paddingTypeRequestHeader = "Padding-Type-Request"
	paddingTypeReplyHeader   = "Padding-Type-Reply"
	paddingTypeVariant1      = "1"
	firstPaddings            = 8
)

const nonindexChars = "!\"#$&'()*+,;@XZ[\\"

func wantsPadding(header http.Header) bool {
	for _, value := range header.Values(paddingTypeRequestHeader) {
		for _, part := range strings.Split(value, ",") {
			if strings.TrimSpace(part) == paddingTypeVariant1 {
				return true
			}
		}
	}
	return false
}

func randomPaddingValue() string {
	size, err := rand.Int(rand.Reader, big.NewInt(33))
	if err != nil {
		return strings.Repeat("!", 30)
	}
	value := make([]byte, 30+size.Int64())
	random := make([]byte, len(value))
	if _, err := rand.Read(random); err != nil {
		return strings.Repeat("!", len(value))
	}
	for i := range value {
		if i < 16 {
			value[i] = nonindexChars[int(random[i])%16]
		} else {
			value[i] = nonindexChars[16]
		}
	}
	return string(value)
}

type paddingWriter struct {
	enabled bool
	frame   int
}

func (w *paddingWriter) write(conn *websocket.Conn, payload []byte) error {
	if !w.enabled || w.frame >= firstPaddings {
		return conn.WriteMessage(websocket.BinaryMessage, payload)
	}
	if len(payload) > 0xffff {
		return conn.WriteControl(websocket.CloseMessage,
			websocket.FormatCloseMessage(websocket.CloseMessageTooBig, ""),
			time.Now().Add(10*time.Second))
	}
	paddingSize, err := rand.Int(rand.Reader, big.NewInt(256))
	if err != nil {
		paddingSize = big.NewInt(0)
	}
	padded := make([]byte, 3+len(payload)+int(paddingSize.Int64()))
	binary.BigEndian.PutUint16(padded, uint16(len(payload)))
	padded[2] = byte(paddingSize.Int64())
	copy(padded[3:], payload)
	w.frame++
	return conn.WriteMessage(websocket.BinaryMessage, padded)
}

type paddingReader struct {
	enabled bool
	frame   int
	state   int
	lengths [2]int
}

func (r *paddingReader) unwrap(padded []byte) ([]byte, error) {
	if !r.enabled || r.frame >= firstPaddings {
		return padded, nil
	}
	var payload []byte
	for len(padded) > 0 {
		switch r.state {
		case 0:
			r.lengths[1] = int(padded[0])
			padded = padded[1:]
			r.state++
		case 1:
			r.lengths[1] = r.lengths[1]<<8 | int(padded[0])
			padded = padded[1:]
			r.state++
		case 2:
			r.lengths[0] = int(padded[0])
			padded = padded[1:]
			r.state++
		case 3:
			n := min(r.lengths[1], len(padded))
			payload = append(payload, padded[:n]...)
			r.lengths[1] -= n
			padded = padded[n:]
			if r.lengths[1] == 0 {
				r.state++
			}
		case 4:
			n := min(r.lengths[0], len(padded))
			padded = padded[n:]
			r.lengths[0] -= n
			if r.lengths[0] == 0 {
				r.frame++
				r.state = 0
			}
		}
	}
	return payload, nil
}

func newPaddingPair(enabled bool) (*paddingReader, *paddingWriter) {
	return &paddingReader{enabled: enabled}, &paddingWriter{enabled: enabled}
}
