// Copyright (c) 2020, Google Inc.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

package subprocess

// NOTES:
// - subprocess_test does not include testing for all subcomponents. It does
//   include unit tests for the following:
//   - hashPrimitive (for sha2-256 only)
//   - blockCipher (for AES)
//   - drbg (for ctrDRBG)
// - All sample data (the valid & invalid strings) comes from calls to acvp as
//   of 2020-04-02.

import (
	"encoding/hex"
	"encoding/json"
	"fmt"
	"reflect"
	"testing"
)

var validSHA2_256 = []byte(`{
  "vsId" : 182183,
  "algorithm" : "SHA2-256",
  "revision" : "1.0",
  "isSample" : true,
  "testGroups" : [ {
    "tgId" : 1,
    "testType" : "AFT",
    "tests" : [ {
      "tcId" : 1,
      "msg" : "",
      "len" : 0
    }, {
      "tcId" : 2,
      "msg" : "",
      "len" : 0
    }, {
      "tcId" : 3,
      "msg" : "8E",
      "len" : 8
    }, {
      "tcId" : 4,
      "msg" : "7F10",
      "len" : 16
    }, {
      "tcId" : 5,
      "msg" : "F4422F",
      "len" : 24
    }, {
      "tcId" : 6,
      "msg" : "B3EF9698",
      "len" : 32
    }]
  }]
}`)

var callsSHA2_256 = []fakeTransactCall{
	fakeTransactCall{cmd: "SHA2-256", expectedNumResults: 1, args: [][]byte{[]byte{}}},
	fakeTransactCall{cmd: "SHA2-256", expectedNumResults: 1, args: [][]byte{[]byte{}}},
	fakeTransactCall{cmd: "SHA2-256", expectedNumResults: 1, args: [][]byte{fromHex("8E")}},
	fakeTransactCall{cmd: "SHA2-256", expectedNumResults: 1, args: [][]byte{fromHex("7F10")}},
	fakeTransactCall{cmd: "SHA2-256", expectedNumResults: 1, args: [][]byte{fromHex("F4422F")}},
	fakeTransactCall{cmd: "SHA2-256", expectedNumResults: 1, args: [][]byte{fromHex("B3EF9698")}},
}

var invalidSHA2_256 = []byte(`{
  "vsId" : 180207,
  "algorithm" : "SHA2-256",
  "revision" : "1.0",
  "isSample" : true,
  "testGroups" : [ {
    "tgId" : abc,
    "testType" : "AFT",
    "tests" : [ {
      "tcId" : 1,
      "msg" : "",
      "len" : 0
    }, {
      "tcId" : 2,
      "msg" : "",
      "len" : 0
    }]
  }]
}`)

var validACVPAESECB = []byte(`{
  "vsId" : 181726,
  "algorithm" : "ACVP-AES-ECB",
  "revision" : "1.0",
  "isSample" : true,
  "testGroups" : [ {
    "tgId" : 1,
    "testType" : "AFT",
    "direction" : "encrypt",
    "keyLen" : 128,
    "tests" : [ {
      "tcId" : 1,
      "pt" : "F34481EC3CC627BACD5DC3FB08F273E6",
      "key" : "00000000000000000000000000000000"
    }, {
      "tcId" : 2,
      "pt" : "9798C4640BAD75C7C3227DB910174E72",
      "key" : "00000000000000000000000000000000"
    }]
  }]
}`)

var invalidACVPAESECB = []byte(`{
  "vsId" : 181726,
  "algorithm" : "ACVP-AES-ECB",
  "revision" : "1.0",
  "isSample" : true,
  "testGroups" : [ {
    "tgId" : 1,
    "testType" : "AFT",
    "direction" : "encrypt",
    "keyLen" : 128,
    "tests" : [ {
      "tcId" : abc,
      "pt" : "F34481EC3CC627BACD5DC3FB08F273E6",
      "key" : "00000000000000000000000000000000"
    }, {
      "tcId" : 2,
      "pt" : "9798C4640BAD75C7C3227DB910174E72",
      "key" : "00000000000000000000000000000000"
    }]
  }]
}`)

var callsACVPAESECB = []fakeTransactCall{
	fakeTransactCall{cmd: "AES/encrypt", expectedNumResults: 1, args: [][]byte{
		fromHex("00000000000000000000000000000000"),
		fromHex("F34481EC3CC627BACD5DC3FB08F273E6"),
	}},
	fakeTransactCall{cmd: "AES/encrypt", expectedNumResults: 1, args: [][]byte{
		fromHex("00000000000000000000000000000000"),
		fromHex("9798C4640BAD75C7C3227DB910174E72"),
	}},
}

var validCTRDRBG = []byte(`{
  "vsId" : 181791,
  "algorithm" : "ctrDRBG",
  "revision" : "1.0",
  "isSample" : true,
  "testGroups" : [ {
    "tgId" : 1,
    "testType" : "AFT",
    "derFunc" : false,
    "reSeed" : false,
    "predResistance" : false,
    "entropyInputLen" : 384,
    "nonceLen" : 0,
    "persoStringLen" : 0,
    "additionalInputLen" : 0,
    "returnedBitsLen" : 2048,
    "mode" : "AES-256",
    "tests" : [ {
      "tcId" : 1,
      "entropyInput" : "0D9E8EB273307D95C616C7ACC65669C246265E8A850EDCF36990D8A6F7EC3AEA0A7DDB888EE8D7ECC19EA7830310782C",
      "nonce" : "",
      "persoString" : "",
      "otherInput" : [ {
        "intendedUse" : "generate",
        "additionalInput" : "",
        "entropyInput" : ""
      }, {
        "intendedUse" : "generate",
        "additionalInput" : "",
        "entropyInput" : ""
      } ]
    }]
  }]
}`)

var callsCTRDRBG = []fakeTransactCall{
	fakeTransactCall{cmd: "ctrDRBG/AES-256", expectedNumResults: 1, args: [][]byte{
		fromHex("00010000"), // uint32(256)
		fromHex("0D9E8EB273307D95C616C7ACC65669C246265E8A850EDCF36990D8A6F7EC3AEA0A7DDB888EE8D7ECC19EA7830310782C"),
		[]byte{},
		[]byte{},
		[]byte{},
		[]byte{},
	}},
}

var invalidCTRDRBG = []byte(`{
  "vsId" : 181791,
  "algorithm" : "ctrDRBG",
  "revision" : "1.0",
  "isSample" : true,
  "testGroups" : [ {
    "tgId" : 1,
    "testType" : "AFT",
    "derFunc" : false,
    "reSeed" : false,
    "predResistance" : false,
    "entropyInputLen" : 384,
    "nonceLen" : 0,
    "persoStringLen" : 0,
    "additionalInputLen" : 0,
    "returnedBitsLen" : 2048,
    "mode" : "AES-256",
    "tests" : [ {
      "tcId" : abc,
      "entropyInput" : "0D9E8EB273307D95C616C7ACC65669C246265E8A850EDCF36990D8A6F7EC3AEA0A7DDB888EE8D7ECC19EA7830310782C",
      "nonce" : "",
      "persoString" : "",
      "otherInput" : [ {
        "intendedUse" : "generate",
        "additionalInput" : "",
        "entropyInput" : ""
      }, {
        "intendedUse" : "generate",
        "additionalInput" : "",
        "entropyInput" : ""
      } ]
    }]
  }]
}`)

// fakeTransactable provides a fake to return results that don't go to the ACVP
// server.
type fakeTransactable struct {
	calls   []fakeTransactCall
	results []fakeTransactResult
}

type fakeTransactCall struct {
	cmd                string
	expectedNumResults int
	args               [][]byte
}

type fakeTransactResult struct {
	bytes [][]byte
	err   error
}

func (f *fakeTransactable) Transact(cmd string, expectedNumResults int, args ...[]byte) ([][]byte, error) {
	f.calls = append(f.calls, fakeTransactCall{cmd, expectedNumResults, args})

	if len(f.results) == 0 {
		return nil, fmt.Errorf("Transact called but no TransactResults remain")
	}

	ret := f.results[0]
	f.results = f.results[1:]
	return ret.bytes, ret.err
}

func newFakeTransactable(name string, numResponses int) *fakeTransactable {
	ret := new(fakeTransactable)

	// Add results requested by caller.
	dummyResult := [][]byte{[]byte("dummy result")}
	for i := 0; i < numResponses; i++ {
		ret.results = append(ret.results, fakeTransactResult{bytes: dummyResult, err: nil})
	}

	return ret
}

// TestPrimitiveParsesJSON verifies that basic JSON parsing with a
// small passing case & a single failing case.
func TestPrimitives(t *testing.T) {
	var tests = []struct {
		algo          string
		p             primitive
		validJSON     []byte
		invalidJSON   []byte
		expectedCalls []fakeTransactCall
		results       []fakeTransactResult
	}{
		{
			algo:          "SHA2-256",
			p:             &hashPrimitive{"SHA2-256", 32},
			validJSON:     validSHA2_256,
			invalidJSON:   invalidSHA2_256,
			expectedCalls: callsSHA2_256,
		},
		{
			algo:          "ACVP-AES-ECB",
			p:             &blockCipher{"AES", 16, false},
			validJSON:     validACVPAESECB,
			invalidJSON:   invalidACVPAESECB,
			expectedCalls: callsACVPAESECB,
		},
		{
			algo:          "ctrDRBG",
			p:             &drbg{"ctrDRBG", map[string]bool{"AES-128": true, "AES-192": true, "AES-256": true}},
			validJSON:     validCTRDRBG,
			invalidJSON:   invalidCTRDRBG,
			expectedCalls: callsCTRDRBG,
			results: []fakeTransactResult{
				fakeTransactResult{bytes: [][]byte{make([]byte, 256)}},
			},
		},
	}

	for _, test := range tests {
		transactable := newFakeTransactable(test.algo, len(test.expectedCalls))
		if len(test.results) > 0 {
			transactable.results = test.results
		}

		if _, err := test.p.Process(test.validJSON, transactable); err != nil {
			t.Errorf("%s: valid input failed unexpectedly: %v", test.algo, err)
			continue
		}

		if len(transactable.calls) != len(test.expectedCalls) {
			t.Errorf("%s: got %d results, but want %d", test.algo, len(transactable.calls), len(test.expectedCalls))
			continue
		}

		if !reflect.DeepEqual(transactable.calls, test.expectedCalls) {
			t.Errorf("%s: got:\n%#v\n\nwant:\n%#v", test.algo, transactable.calls, test.expectedCalls)
		}

		if _, err := test.p.Process(test.invalidJSON, transactable); !isJSONSyntaxError(err) {
			t.Errorf("Test %v with invalid input either passed or failed with the wrong error (%v)", test.algo, err)
		}
	}
}

// isJSONSyntaxError returns true if the error is a json syntax error.
func isJSONSyntaxError(err error) bool {
	_, ok := err.(*json.SyntaxError)
	return ok
}

// fromHex wraps hex.DecodeString so it can be used in initializers. Panics on error.
func fromHex(s string) []byte {
	key, err := hex.DecodeString(s)
	if err != nil {
		panic(fmt.Sprintf("Failed on hex.DecodeString(%q) with %v", s, err))
	}
	return key
}
