package deps

import (
	"bytes"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"testing"
)

func serveTestString(w http.ResponseWriter, _ *http.Request) {
	io.WriteString(w, "test")
}

func TestFetch(t *testing.T) {
	http.HandleFunc("/test", serveTestString)

	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Fatal(err)
	}
	port := listener.Addr().(*net.TCPAddr).Port
	url := fmt.Sprintf("http://localhost:%d/test", port)
	go http.Serve(listener, nil)

	tmpdir, err := os.MkdirTemp("", "*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpdir)

	entry := Entry{
		Name:        "com_example",
		SHA256:      "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08", // SHA256("test")
		Prefix:      "",
		URL:         url,
		LastUpdated: "2022-05-18",
	}

	filename, err := FetchEntry(&entry, tmpdir)
	if err != nil {
		t.Fatal(err)
	}

	contents, err := os.ReadFile(filename)
	if err != nil {
		t.Fatal(err)
	}

	if !bytes.Equal(contents, []byte("test")) {
		t.Errorf("Expected to get 'test', instead got '%s'", contents)
	}
}
