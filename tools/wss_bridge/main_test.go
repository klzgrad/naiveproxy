package main

import (
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

func TestParseTargetIPv4(t *testing.T) {
	payload := []byte{1, 1, 192, 168, 0, 2, 0, 80}
	host, port, err := parseTarget(payload)
	if err != nil {
		t.Fatalf("parseTarget() error = %v", err)
	}
	if host != "192.168.0.2" || port != 80 {
		t.Fatalf("parseTarget() = %q, %d", host, port)
	}
}

func TestParseTargetDomain(t *testing.T) {
	payload := []byte{1, 3, 11, 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', 1, 187}
	host, port, err := parseTarget(payload)
	if err != nil {
		t.Fatalf("parseTarget() error = %v", err)
	}
	if host != "example.com" || port != 443 {
		t.Fatalf("parseTarget() = %q, %d", host, port)
	}
}

func TestParseTargetErrors(t *testing.T) {
	tests := [][]byte{
		{},
		{1},
		{2, 1, 192, 168, 0, 2, 0, 80},
		{1, 1, 192, 168},
	}
	for _, payload := range tests {
		if _, _, err := parseTarget(payload); err == nil {
			t.Fatalf("parseTarget(% x) succeeded; want error", payload)
		}
	}
}

func TestBridgeEndToEnd(t *testing.T) {
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
		serveWebSocket(w, r, &config{firstTimeout: 5 * time.Second, dialTimeout: 5 * time.Second})
	}))
	defer server.Close()

	wsURL := strings.Replace(server.URL, "http", "ws", 1) + "/naive"
	conn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		t.Fatalf("dial bridge: %v", err)
	}
	defer conn.Close()

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

	messageType, payload, err := conn.ReadMessage()
	if err != nil {
		t.Fatalf("read status: %v", err)
	}
	if messageType != websocket.BinaryMessage || len(payload) != 1 || payload[0] != 0 {
		t.Fatalf("unexpected status: type=%d payload=% x", messageType, payload)
	}

	payload = []byte("hello bridge")
	if err := conn.WriteMessage(websocket.BinaryMessage, payload); err != nil {
		t.Fatalf("send payload: %v", err)
	}
	messageType, payload, err = conn.ReadMessage()
	if err != nil {
		t.Fatalf("read payload: %v", err)
	}
	if messageType != websocket.BinaryMessage || string(payload) != "hello bridge" {
		t.Fatalf("unexpected payload: type=%d payload=%q", messageType, payload)
	}
}
