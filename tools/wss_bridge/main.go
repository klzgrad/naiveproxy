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
	"time"

	"github.com/gorilla/websocket"
)

const (
	protocolVersion   = 1
	addressTypeIPv4   = 1
	addressTypeDomain = 3
	addressTypeIPv6   = 4
)

type config struct {
	addr            string
	path            string
	username        string
	password        string
	certFile        string
	keyFile         string
	maxMessageBytes int64
	dialTimeout     time.Duration
	firstTimeout    time.Duration
	keepalive       time.Duration
}

var upgrader = websocket.Upgrader{
	ReadBufferSize:    64 * 1024,
	WriteBufferSize:   64 * 1024,
	EnableCompression: false,
	CheckOrigin:       func(*http.Request) bool { return true },
}

func basicAuthorized(r *http.Request, cfg *config) bool {
	if cfg.username == "" && cfg.password == "" {
		return true
	}
	username, password, ok := r.BasicAuth()
	if !ok {
		return false
	}
	usernameOK := subtle.ConstantTimeCompare([]byte(username), []byte(cfg.username)) == 1
	passwordOK := subtle.ConstantTimeCompare([]byte(password), []byte(cfg.password)) == 1
	return usernameOK && passwordOK
}

func writeStatus(conn *websocket.Conn, status byte) error {
	conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
	return conn.WriteMessage(websocket.BinaryMessage, []byte{status})
}

func parseTarget(payload []byte) (string, uint16, error) {
	if len(payload) < 4 {
		return "", 0, fmt.Errorf("short target header")
	}
	if payload[0] != protocolVersion {
		return "", 0, fmt.Errorf("unsupported protocol version %d", payload[0])
	}
	addressType := payload[1]
	portOffset := 0
	host := ""
	switch addressType {
	case addressTypeIPv4:
		if len(payload) < 8 {
			return "", 0, fmt.Errorf("short IPv4 address")
		}
		host = net.IP(payload[2:6]).String()
		portOffset = 6
	case addressTypeIPv6:
		if len(payload) < 20 {
			return "", 0, fmt.Errorf("short IPv6 address")
		}
		host = net.IP(payload[2:18]).String()
		portOffset = 18
	case addressTypeDomain:
		length := int(payload[2])
		if len(payload) < 4+length {
			return "", 0, fmt.Errorf("short domain")
		}
		host = string(payload[3 : 3+length])
		portOffset = 3 + length
	default:
		return "", 0, fmt.Errorf("unsupported address type %d", addressType)
	}
	if len(payload) < portOffset+2 {
		return "", 0, fmt.Errorf("short port")
	}
	return host, binary.BigEndian.Uint16(payload[portOffset:]), nil
}

func wsToTCP(conn *websocket.Conn, target net.Conn, padding *paddingReader) error {
	for {
		messageType, payload, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsCloseError(err, websocket.CloseNormalClosure, websocket.CloseGoingAway) {
				return nil
			}
			return err
		}
		if messageType != websocket.BinaryMessage || len(payload) == 0 {
			continue
		}
		if padding != nil {
			payload, err = padding.unwrap(payload)
			if err != nil {
				return err
			}
		}
		if len(payload) == 0 {
			continue
		}
		if _, err := target.Write(payload); err != nil {
			return err
		}
	}
}

func tcpToWS(conn *websocket.Conn, target net.Conn, padding *paddingWriter) error {
	buffer := make([]byte, 64*1024)
	for {
		n, err := target.Read(buffer)
		if n > 0 {
			conn.SetWriteDeadline(time.Now().Add(30 * time.Second))
			if writeErr := padding.write(conn, buffer[:n]); writeErr != nil {
				return writeErr
			}
		}
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
	}
}

func bridge(conn *websocket.Conn, target net.Conn, keepalive time.Duration,
	paddingRead *paddingReader, paddingWrite *paddingWriter) {
	defer conn.Close()
	defer target.Close()

	stop := make(chan struct{})
	defer close(stop)
	if keepalive > 0 {
		ticker := time.NewTicker(keepalive)
		defer ticker.Stop()
		go func() {
			for {
				select {
				case <-ticker.C:
					_ = conn.WriteControl(websocket.PingMessage, nil,
						time.Now().Add(10*time.Second))
				case <-stop:
					return
				}
			}
		}()
	}

	done := make(chan error, 2)
	go func() { done <- tcpToWS(conn, target, paddingWrite) }()
	go func() { done <- wsToTCP(conn, target, paddingRead) }()
	<-done
}

func serveWebSocket(w http.ResponseWriter, r *http.Request, cfg *config) {
	if !basicAuthorized(r, cfg) {
		http.NotFound(w, r)
		return
	}

	paddingEnabled := wantsVariant1Padding(r.Header)
	var responseHeader http.Header
	if paddingEnabled {
		padding, err := randomPaddingValue()
		if err != nil {
			http.Error(w, "padding generation failed", http.StatusInternalServerError)
			return
		}
		responseHeader = http.Header{}
		responseHeader.Set(paddingHeader, padding)
		responseHeader.Set(paddingTypeReplyHeader, paddingTypeVariant1)
	}

	conn, err := upgrader.Upgrade(w, r, responseHeader)
	if err != nil {
		return
	}
	defer conn.Close()
	conn.SetReadLimit(cfg.maxMessageBytes)

	conn.SetReadDeadline(time.Now().Add(cfg.firstTimeout))
	messageType, payload, err := conn.ReadMessage()
	if err != nil {
		return
	}
	if messageType != websocket.BinaryMessage {
		writeStatus(conn, 1)
		return
	}

	host, port, err := parseTarget(payload)
	if err != nil {
		writeStatus(conn, 1)
		return
	}

	target, err := net.DialTimeout("tcp", net.JoinHostPort(host, strconv.Itoa(int(port))), cfg.dialTimeout)
	if err != nil {
		writeStatus(conn, 4)
		return
	}
	if err := writeStatus(conn, 0); err != nil {
		target.Close()
		return
	}
	conn.SetReadDeadline(time.Time{})
	var paddingRead *paddingReader
	if paddingEnabled {
		paddingRead = &paddingReader{}
	}
	bridge(conn, target, cfg.keepalive, paddingRead,
		newPaddingWriter(paddingEnabled))
}

func main() {
	var cfg config
	flag.StringVar(&cfg.addr, "addr", "127.0.0.1:8080", "HTTP listen address")
	flag.StringVar(&cfg.path, "path", "/naive", "WebSocket endpoint path")
	flag.StringVar(&cfg.username, "user", "", "Basic auth username")
	flag.StringVar(&cfg.password, "pass", "", "Basic auth password")
	flag.StringVar(&cfg.certFile, "cert", "", "TLS certificate file")
	flag.StringVar(&cfg.keyFile, "key", "", "TLS private key file")
	flag.Int64Var(&cfg.maxMessageBytes, "max-message-bytes", 1024*1024, "Maximum WebSocket message size")
	flag.DurationVar(&cfg.dialTimeout, "dial-timeout", 10*time.Second, "Target connect timeout")
	flag.DurationVar(&cfg.firstTimeout, "first-timeout", 15*time.Second, "Wait time for first tunnel message")
	flag.DurationVar(&cfg.keepalive, "keepalive", 30*time.Second, "WebSocket ping interval; 0 disables")
	flag.Parse()

	mux := http.NewServeMux()
	mux.HandleFunc(cfg.path, func(w http.ResponseWriter, r *http.Request) {
		serveWebSocket(w, r, &cfg)
	})
	server := &http.Server{
		Addr:              cfg.addr,
		Handler:           mux,
		ReadHeaderTimeout: 15 * time.Second,
	}

	scheme := "http"
	if cfg.certFile != "" || cfg.keyFile != "" {
		scheme = "https"
	}
	log.Printf("wss bridge listening on %s://%s%s", scheme, cfg.addr, cfg.path)
	var err error
	if scheme == "https" {
		err = server.ListenAndServeTLS(cfg.certFile, cfg.keyFile)
	} else {
		err = server.ListenAndServe()
	}
	if err != nil && err != http.ErrServerClosed {
		log.Fatal(err)
	}
}
