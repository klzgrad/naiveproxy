package main

// #include <stdbool.h>
// #include <stdlib.h>
// #include "cronet_c.h"
// #include "bidirectional_stream_c.h"
// extern void _on_stream_ready(bidirectional_stream* stream);
// extern void _on_response_headers_received(bidirectional_stream* stream, bidirectional_stream_header_array* headers, char* negotiated_protocol);
// extern void _on_read_completed(bidirectional_stream* stream, char* data, int bytes_read);
// extern void _on_write_completed(bidirectional_stream* stream, char* data);
// extern void _on_response_trailers_received(bidirectional_stream* stream, bidirectional_stream_header_array* trailers);
// extern void _on_succeded(bidirectional_stream* stream);
// extern void _on_failed(bidirectional_stream* stream, int net_error);
// extern void _on_canceled(bidirectional_stream* stream);
import "C"

import (
	"log"
	"os"
	"strconv"
	"sync"
	"unsafe"
)

var wait sync.WaitGroup

var readBuffer unsafe.Pointer

func main() {
	cronetEngine := C.Cronet_Engine_Create()

	engineParams := C.Cronet_EngineParams_Create()
	userAgentC := C.CString("Cronet")
	C.Cronet_EngineParams_user_agent_set(engineParams, userAgentC)
	C.Cronet_Engine_StartWithParams(cronetEngine, engineParams)
	C.free(unsafe.Pointer(userAgentC))
	C.Cronet_EngineParams_Destroy(engineParams)

	streamEngine := C.Cronet_Engine_GetStreamEngine(cronetEngine)

	var callback C.bidirectional_stream_callback
	callback.on_stream_ready = (*[0]byte)(C._on_stream_ready)
	callback.on_response_headers_received = (*[0]byte)(C._on_response_headers_received)
	callback.on_read_completed = (*[0]byte)(C._on_read_completed)
	callback.on_response_trailers_received = (*[0]byte)(C._on_response_trailers_received)
	callback.on_succeded = (*[0]byte)(C._on_succeded)
	callback.on_failed = (*[0]byte)(C._on_failed)
	callback.on_canceled = (*[0]byte)(C._on_canceled)

	stream := C.bidirectional_stream_create(streamEngine, nil, &callback)

	url := C.CString(os.Args[1])
	defer C.free(unsafe.Pointer(url))

	method := C.CString("GET")
	defer C.free(unsafe.Pointer(method))

	readBuffer = C.malloc(32768)

	wait.Add(1)

	C.bidirectional_stream_start(stream, url, 0, method, nil, true)

	wait.Wait()

	C.free(readBuffer)
	C.bidirectional_stream_destroy(stream)
	C.Cronet_Engine_Shutdown(cronetEngine)
	C.Cronet_Engine_Destroy(cronetEngine)
}

//export _on_stream_ready
func _on_stream_ready(stream *C.bidirectional_stream) {
	log.Println("on_stream_ready_callback")
}

//export _on_response_headers_received
func _on_response_headers_received(stream *C.bidirectional_stream, headers *C.bidirectional_stream_header_array, negotiated_protocol *C.char) {
	log.Println("on_response_headers_received, negotiated_protocol=", C.GoString(negotiated_protocol))
	var hdrP *C.bidirectional_stream_header
	hdrP = headers.headers

	headersSlice := unsafe.Slice(hdrP, int(headers.count))
	for _, header := range headersSlice {
		key := C.GoString(header.key)
		if len(key) == 0 {
			continue
		}
		value := C.GoString(header.value)
		log.Println(key + ": " + value)
	}

	C.bidirectional_stream_read(stream, (*C.char)(readBuffer), 32768)
}

//export _on_read_completed
func _on_read_completed(stream *C.bidirectional_stream, data *C.char, bytesRead C.int) {
	log.Println("on_read_completed")

	dataSlice := C.GoBytes(readBuffer, bytesRead)
	log.Println(string(dataSlice))

	C.bidirectional_stream_read(stream, (*C.char)(readBuffer), 32768)
}

//export _on_write_completed
func _on_write_completed(stream *C.bidirectional_stream, data *C.char) {
	log.Println("on_write_completed")
}

//export _on_response_trailers_received
func _on_response_trailers_received(stream *C.bidirectional_stream, trailers *C.bidirectional_stream_header_array) {
	log.Println("on_response_trailers_received")
}

//export _on_succeded
func _on_succeded(stream *C.bidirectional_stream) {
	log.Println("on_succeded")
	wait.Done()
}

//export _on_failed
func _on_failed(stream *C.bidirectional_stream, net_error C.int) {
	log.Println("on_failed")
	log.Println("net error ", strconv.Itoa(int(net_error)))
	wait.Done()
}

//export _on_canceled
func _on_canceled(stream *C.bidirectional_stream) {
	log.Println("on_canceled")
	wait.Done()
}