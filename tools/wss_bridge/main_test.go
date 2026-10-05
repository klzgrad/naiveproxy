package main

import (
	"bytes"
	"encoding/binary"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func TestParseTarget(t *testing.T) {
	payload := append([]byte{1, 3, 11}, []byte("example.com")...)
	payload = append(payload, 0, 0)
	host, port, err := parseTarget(payload)
	if err != nil || host != "example.com" || port != 0 {
		t.Fatalf("parseTarget domain: %v %q %d", err, host, port)
	}
	portBytes := make([]byte, 2)
	binary.BigEndian.PutUint16(portBytes, 443)
	_, port, err = parseTarget(append([]byte{1, 1, 127, 0, 0, 1}, portBytes...))
	if err != nil || port != 443 {
		t.Fatalf("parseTarget IPv4: %v %d", err, port)
	}
}

func TestBridgePaddedLargeFirstTransfer(t *testing.T) {
	targetListener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer targetListener.Close()
	go func() {
		conn, err := targetListener.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		conn.Write(bytes.Repeat([]byte{0x5a}, 0x10000))
	}()

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		serve(w, r, &config{dialTimeout: 5 * time.Second}, make(chan struct{}, 1))
	}))
	defer server.Close()

	header := http.Header{}
	header.Set(paddingTypeRequestHeader, "1, 0")
	conn, _, err := websocket.DefaultDialer.Dial(
		strings.Replace(server.URL, "http", "ws", 1)+"/naive", header)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()

	address := targetListener.Addr().(*net.TCPAddr)
	target := append([]byte{1, 1}, address.IP.To4()...)
	port := make([]byte, 2)
	binary.BigEndian.PutUint16(port, uint16(address.Port))
	if err := conn.WriteMessage(websocket.BinaryMessage, append(target, port...)); err != nil {
		t.Fatal(err)
	}
	if _, status, err := conn.ReadMessage(); err != nil || len(status) != 1 || status[0] != 0 {
		t.Fatalf("status: %v %x", err, status)
	}

	reader := &paddingReader{enabled: true}
	received := 0
	for received < 0x10000 {
		_, payload, err := conn.ReadMessage()
		if err != nil {
			t.Fatal(err)
		}
		payload, err = reader.unwrap(payload)
		if err != nil {
			t.Fatal(err)
		}
		for _, b := range payload {
			if b != 0x5a {
				t.Fatalf("unexpected byte %#x", b)
			}
		}
		received += len(payload)
	}
	if received != 0x10000 {
		t.Fatalf("received %d bytes", received)
	}
}
