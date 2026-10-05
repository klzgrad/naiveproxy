package main

import (
	"crypto/subtle"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/gorilla/websocket"
)

const (
	protocolVersion  = 1
	addressTypeIPv4  = 1
	addressTypeIPv6  = 4
	maxPaddedPayload = 0xffff
)

type config struct {
	addr, path, username, password string
	maxConnections                 int
	dialTimeout                    time.Duration
}

var upgrader = websocket.Upgrader{
	ReadBufferSize:    64 * 1024,
	WriteBufferSize:   64 * 1024,
	EnableCompression: false,
	CheckOrigin:       func(*http.Request) bool { return true },
}

func isLoopback(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		host = strings.Trim(addr, "[]")
	}
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func authorized(r *http.Request, cfg *config) bool {
	if cfg.username == "" && cfg.password == "" {
		return true
	}
	user, pass, ok := r.BasicAuth()
	return ok &&
		subtle.ConstantTimeCompare([]byte(user), []byte(cfg.username)) == 1 &&
		subtle.ConstantTimeCompare([]byte(pass), []byte(cfg.password)) == 1
}

func parseTarget(payload []byte) (string, uint16, error) {
	if len(payload) < 4 || payload[0] != protocolVersion {
		return "", 0, fmt.Errorf("invalid target header")
	}
	var host string
	var portOffset int
	switch payload[1] {
	case addressTypeIPv4:
		if len(payload) < 8 {
			return "", 0, fmt.Errorf("short IPv4 address")
		}
		host, portOffset = net.IP(payload[2:6]).String(), 6
	case addressTypeIPv6:
		if len(payload) < 20 {
			return "", 0, fmt.Errorf("short IPv6 address")
		}
		host, portOffset = net.IP(payload[2:18]).String(), 18
	case 3:
		length := int(payload[2])
		if len(payload) < 4+length {
			return "", 0, fmt.Errorf("short domain")
		}
		host, portOffset = string(payload[3:3+length]), 3+length
	default:
		return "", 0, fmt.Errorf("unsupported address type %d", payload[1])
	}
	if len(payload) != portOffset+2 {
		return "", 0, fmt.Errorf("invalid target length")
	}
	return host, binary.BigEndian.Uint16(payload[portOffset:]), nil
}

func wsToTCP(conn *websocket.Conn, target net.Conn, reader *paddingReader) error {
	for {
		messageType, payload, err := conn.ReadMessage()
		if err != nil {
			return err
		}
		if messageType != websocket.BinaryMessage {
			continue
		}
		if payload, err = reader.unwrap(payload); err != nil {
			return err
		}
		if len(payload) == 0 {
			continue
		}
		if _, err = target.Write(payload); err != nil {
			return err
		}
	}
}

func tcpToWS(conn *websocket.Conn, target net.Conn, writer *paddingWriter) error {
	buffer := make([]byte, maxPaddedPayload)
	for {
		n, err := target.Read(buffer)
		if n > 0 {
			conn.SetWriteDeadline(time.Now().Add(30 * time.Second))
			if writeErr := writer.write(conn, buffer[:n]); writeErr != nil {
				return writeErr
			}
		}
		if err == io.EOF {
			return conn.WriteControl(websocket.CloseMessage,
				websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""),
				time.Now().Add(10*time.Second))
		}
		if err != nil {
			return err
		}
	}
}

func bridge(conn *websocket.Conn, target net.Conn, reader *paddingReader,
	writer *paddingWriter) {
	defer conn.Close()
	defer target.Close()
	done := make(chan error, 2)
	go func() { done <- tcpToWS(conn, target, writer) }()
	go func() { done <- wsToTCP(conn, target, reader) }()
	<-done
}

func serve(w http.ResponseWriter, r *http.Request, cfg *config,
	connections chan struct{}) {
	select {
	case connections <- struct{}{}:
		defer func() { <-connections }()
	default:
		http.Error(w, "too many connections", http.StatusTooManyRequests)
		return
	}
	if !authorized(r, cfg) {
		http.NotFound(w, r)
		return
	}

	responseHeader := http.Header{}
	reader, writer := newPaddingPair(false)
	if wantsPadding(r.Header) {
		responseHeader.Set(paddingHeader, randomPaddingValue())
		responseHeader.Set(paddingTypeReplyHeader, paddingTypeVariant1)
		reader, writer = newPaddingPair(true)
	}
	conn, err := upgrader.Upgrade(w, r, responseHeader)
	if err != nil {
		return
	}
	conn.SetReadLimit(1024 * 1024)
	conn.SetReadDeadline(time.Now().Add(15 * time.Second))
	messageType, payload, err := conn.ReadMessage()
	if err != nil || messageType != websocket.BinaryMessage {
		return
	}
	host, port, err := parseTarget(payload)
	if err != nil {
		conn.WriteMessage(websocket.BinaryMessage, []byte{1})
		return
	}
	target, err := net.DialTimeout("tcp",
		net.JoinHostPort(host, strconv.Itoa(int(port))), cfg.dialTimeout)
	if err != nil {
		conn.WriteMessage(websocket.BinaryMessage, []byte{4})
		return
	}
	if err := conn.WriteMessage(websocket.BinaryMessage, []byte{0}); err != nil {
		target.Close()
		return
	}
	conn.SetReadDeadline(time.Time{})
	bridge(conn, target, reader, writer)
}

func main() {
	var cfg config
	flag.StringVar(&cfg.addr, "addr", "127.0.0.1:8080", "listen address")
	flag.StringVar(&cfg.path, "path", "/naive", "WebSocket path")
	flag.StringVar(&cfg.username, "user", "", "Basic auth username")
	flag.StringVar(&cfg.password, "pass", "", "Basic auth password")
	flag.IntVar(&cfg.maxConnections, "max-connections", 256, "connection limit")
	flag.DurationVar(&cfg.dialTimeout, "dial-timeout", 10*time.Second,
		"target connect timeout")
	flag.Parse()
	if !isLoopback(cfg.addr) && cfg.username == "" && cfg.password == "" {
		log.Fatal("credentials are required on a non-loopback address")
	}
	if cfg.maxConnections < 1 {
		log.Fatal("max-connections must be positive")
	}

	connections := make(chan struct{}, cfg.maxConnections)
	mux := http.NewServeMux()
	mux.HandleFunc(cfg.path, func(w http.ResponseWriter, r *http.Request) {
		serve(w, r, &cfg, connections)
	})
	log.Printf("wss bridge listening on http://%s%s", cfg.addr, cfg.path)
	log.Fatal(http.ListenAndServe(cfg.addr, mux))
}
