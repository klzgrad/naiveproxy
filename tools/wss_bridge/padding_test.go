package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func TestWantsVariant1Padding(t *testing.T) {
	tests := []struct {
		name   string
		header http.Header
		want   bool
	}{
		{"variant1", http.Header{"Padding-Type-Request": {"1, 0"}}, true},
		{"multiple values", http.Header{"Padding-Type-Request": {"0", "1"}}, true},
		{"none", http.Header{"Padding-Type-Request": {"0"}}, false},
		{"missing", http.Header{}, false},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if got := wantsVariant1Padding(test.header); got != test.want {
				t.Fatalf("wantsVariant1Padding() = %v, want %v", got, test.want)
			}
		})
	}
}

func TestPaddingRoundTrip(t *testing.T) {
	payload := []byte("padding round trip")
	padded, err := encodePaddingFrame(payload, 37)
	if err != nil {
		t.Fatalf("encodePaddingFrame() error = %v", err)
	}
	if len(padded) != 3+len(payload)+37 {
		t.Fatalf("encoded length = %d, want %d", len(padded), 3+len(payload)+37)
	}
	if !bytes.Equal(padded[3:3+len(payload)], payload) {
		t.Fatalf("encoded payload changed: %q", padded[3:3+len(payload)])
	}

	reader := &paddingReader{}
	decoded, err := reader.unwrap(padded[:3+len(payload)])
	if err != nil {
		t.Fatalf("unwrap first chunk: %v", err)
	}
	second, err := reader.unwrap(padded[3+len(payload):])
	if err != nil {
		t.Fatalf("unwrap second chunk: %v", err)
	}
	decoded = append(decoded, second...)
	if !bytes.Equal(decoded, payload) {
		t.Fatalf("decoded payload = %q, want %q", decoded, payload)
	}
	if reader.frame != 1 {
		t.Fatalf("reader frame = %d, want 1", reader.frame)
	}
}

func TestRandomPaddingValue(t *testing.T) {
	value, err := randomPaddingValue()
	if err != nil {
		t.Fatalf("randomPaddingValue() error = %v", err)
	}
	if len(value) < 16 || len(value) > 32 {
		t.Fatalf("padding length = %d, want between 16 and 32", len(value))
	}
	for _, char := range value {
		if !strings.ContainsRune(nonindexChars, char) {
			t.Fatalf("padding contains invalid character %q: %q", char, value)
		}
	}
}

func TestBasicAuthFailure(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		serveWebSocket(w, r, &config{username: "user", password: "secret"})
	}))
	defer server.Close()

	_, response, err := websocket.DefaultDialer.Dial(
		strings.Replace(server.URL, "http", "ws", 1)+"/naive", nil)
	if err == nil {
		t.Fatal("dial with missing credentials unexpectedly succeeded")
	}
	if response == nil || response.StatusCode != http.StatusNotFound {
		t.Fatalf("status = %v, want 404", response)
	}
}

func TestBridgePaddedEndToEnd(t *testing.T) {
	echoListener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen echo server: %v", err)
	}
	defer echoListener.Close()
	go func() {
		conn, err := echoListener.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		io.Copy(conn, conn)
	}()

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		serveWebSocket(w, r, &config{
			firstTimeout: 5 * time.Second,
			dialTimeout:  5 * time.Second,
		})
	}))
	defer server.Close()

	requestHeader := http.Header{}
	requestHeader.Set("Padding-Type-Request", "1, 0")
	conn, response, err := websocket.DefaultDialer.Dial(
		strings.Replace(server.URL, "http", "ws", 1)+"/naive", requestHeader)
	if err != nil {
		t.Fatalf("dial padded bridge: %v", err)
	}
	defer conn.Close()
	if response.StatusCode != http.StatusSwitchingProtocols {
		t.Fatalf("upgrade status = %d, want 101", response.StatusCode)
	}
	if response.Header.Get(paddingTypeReplyHeader) != paddingTypeVariant1 {
		t.Fatalf("padding type reply = %q, want %q",
			response.Header.Get(paddingTypeReplyHeader), paddingTypeVariant1)
	}
	if response.Header.Get(paddingHeader) == "" {
		t.Fatal("upgrade response has no padding header")
	}
	if response.Header.Get("Sec-WebSocket-Extensions") != "" {
		t.Fatal("bridge unexpectedly negotiated WebSocket compression")
	}

	address := echoListener.Addr().(*net.TCPAddr)
	ipv4 := address.IP.To4()
	if ipv4 == nil {
		t.Fatalf("unexpected echo address %v", address)
	}
	target := append([]byte{protocolVersion, addressTypeIPv4}, ipv4...)
	port := make([]byte, 2)
	binary.BigEndian.PutUint16(port, uint16(address.Port))
	target = append(target, port...)
	if err := conn.WriteMessage(websocket.BinaryMessage, target); err != nil {
		t.Fatalf("send target: %v", err)
	}
	_, payload, err := conn.ReadMessage()
	if err != nil {
		t.Fatalf("read status: %v", err)
	}
	if len(payload) != 1 || payload[0] != 0 {
		t.Fatalf("unexpected status: % x", payload)
	}

	writePadding := &paddingWriter{}
	readPadding := &paddingReader{}
	for i, message := range []string{"one", "two", "three", "four", "five", "six", "seven", "eight", "nine"} {
		if err := writePadding.write(conn, []byte(message)); err != nil {
			t.Fatalf("send padded message %d: %v", i, err)
		}
		_, response, err := conn.ReadMessage()
		if err != nil {
			t.Fatalf("read padded message %d: %v", i, err)
		}
		if i == 0 && len(response) < len(message)+3 {
			t.Fatalf("first response is not padded: % x", response)
		}
		decoded, err := readPadding.unwrap(response)
		if err != nil {
			t.Fatalf("unwrap response %d: %v", i, err)
		}
		if string(decoded) != message {
			t.Fatalf("response %d = %q, want %q", i, decoded, message)
		}
		if i == 8 && readPadding.frame != firstPaddings {
			t.Fatalf("reader frame = %d, want %d", readPadding.frame, firstPaddings)
		}
	}

	large := bytes.Repeat([]byte{0xa5}, 100*1024)
	if err := writePadding.write(conn, large); err != nil {
		t.Fatalf("send large message: %v", err)
	}
	var echoed []byte
	for len(echoed) < len(large) {
		_, response, err := conn.ReadMessage()
		if err != nil {
			t.Fatalf("read large message: %v", err)
		}
		decoded, err := readPadding.unwrap(response)
		if err != nil {
			t.Fatalf("unwrap large message: %v", err)
		}
		echoed = append(echoed, decoded...)
	}
	if !bytes.Equal(echoed, large) {
		t.Fatalf("large echo mismatch: got %d bytes", len(echoed))
	}

	if err := conn.WriteControl(websocket.CloseMessage,
		websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""),
		time.Now().Add(time.Second)); err != nil {
		t.Fatalf("send close: %v", err)
	}
	conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	if _, _, err := conn.ReadMessage(); err == nil {
		t.Fatal("connection stayed open after close")
	}
}
