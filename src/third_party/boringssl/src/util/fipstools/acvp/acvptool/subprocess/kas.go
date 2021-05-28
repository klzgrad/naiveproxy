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

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"fmt"
)

type kasVectorSet struct {
	Groups []kasTestGroup `json:"testGroups"`
}

type kasTestGroup struct {
	ID     uint64    `json:"tgId"`
	Type   string    `json:"testType"`
	Curve  string    `json:"domainParameterGenerationMode"`
	Role   string    `json:"kasRole"`
	Scheme string    `json:"scheme"`
	Tests  []kasTest `json:"tests"`
}

type kasTest struct {
	ID            uint64 `json:"tcId"`
	XHex          string `json:"ephemeralPublicServerX"`
	YHex          string `json:"ephemeralPublicServerY"`
	PrivateKeyHex string `json:"ephemeralPrivateIut"`
	ResultHex     string `json:"z"`
}

type kasTestGroupResponse struct {
	ID    uint64            `json:"tgId"`
	Tests []kasTestResponse `json:"tests"`
}

type kasTestResponse struct {
	ID        uint64 `json:"tcId"`
	XHex      string `json:"ephemeralPublicIutX,omitempty"`
	YHex      string `json:"ephemeralPublicIutY,omitempty"`
	ResultHex string `json:"z,omitempty"`
	Passed    *bool  `json:"testPassed,omitempty"`
}

type kas struct{}

func (k *kas) Process(vectorSet []byte, m Transactable) (interface{}, error) {
	var parsed kasVectorSet
	if err := json.Unmarshal(vectorSet, &parsed); err != nil {
		return nil, err
	}

	// See https://usnistgov.github.io/ACVP/draft-hammett-acvp-kas-ssc-ecc.html
	var ret []kasTestGroupResponse
	for _, group := range parsed.Groups {
		response := kasTestGroupResponse{
			ID: group.ID,
		}

		var privateKeyGiven bool
		switch group.Type {
		case "AFT":
			privateKeyGiven = false
		case "VAL":
			privateKeyGiven = true
		default:
			return nil, fmt.Errorf("unknown test type %q", group.Type)
		}

		switch group.Curve {
		case "P-224", "P-256", "P-384", "P-521":
			break
		default:
			return nil, fmt.Errorf("unknown curve %q", group.Curve)
		}

		switch group.Role {
		case "initiator", "responder":
			break
		default:
			return nil, fmt.Errorf("unknown role %q", group.Role)
		}

		if group.Scheme != "ephemeralUnified" {
			return nil, fmt.Errorf("unknown scheme %q", group.Scheme)
		}

		method := "ECDH/" + group.Curve

		for _, test := range group.Tests {
			if len(test.XHex) == 0 || len(test.YHex) == 0 {
				return nil, fmt.Errorf("%d/%d is missing peer's point", group.ID, test.ID)
			}

			peerX, err := hex.DecodeString(test.XHex)
			if err != nil {
				return nil, err
			}

			peerY, err := hex.DecodeString(test.YHex)
			if err != nil {
				return nil, err
			}

			if (len(test.PrivateKeyHex) != 0) != privateKeyGiven {
				return nil, fmt.Errorf("%d/%d incorrect private key presence", group.ID, test.ID)
			}

			if privateKeyGiven {
				privateKey, err := hex.DecodeString(test.PrivateKeyHex)
				if err != nil {
					return nil, err
				}

				expectedOutput, err := hex.DecodeString(test.ResultHex)
				if err != nil {
					return nil, err
				}

				result, err := m.Transact(method, 3, peerX, peerY, privateKey)
				if err != nil {
					return nil, err
				}

				ok := bytes.Equal(result[2], expectedOutput)
				response.Tests = append(response.Tests, kasTestResponse{
					ID:     test.ID,
					Passed: &ok,
				})
			} else {
				result, err := m.Transact(method, 3, peerX, peerY, nil)
				if err != nil {
					return nil, err
				}

				response.Tests = append(response.Tests, kasTestResponse{
					ID:        test.ID,
					XHex:      hex.EncodeToString(result[0]),
					YHex:      hex.EncodeToString(result[1]),
					ResultHex: hex.EncodeToString(result[2]),
				})
			}
		}

		ret = append(ret, response)
	}

	return ret, nil
}
