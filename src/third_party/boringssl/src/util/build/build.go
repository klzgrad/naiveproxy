// Copyright (c) 2024, Google Inc.
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

package build

// A Target is a build target for consumption by the downstream build systems.
// All pre-generated files are baked input its source lists.
type Target struct {
	// Srcs is the list of C or C++ files (determined by file extension) that are
	// built into the target.
	Srcs []string `json:"srcs,omitempty"`
	// Hdrs is the list public headers that should be available to external
	// projects using this target.
	Hdrs []string `json:"hdrs,omitempty"`
	// InternalHdrs is the list of internal headers that should be available to
	// this target, as well as any internal targets using this target.
	InternalHdrs []string `json:"internal_hdrs,omitempty"`
	// Asm is the a list of assembly files to be passed to a gas-compatible
	// assembler.
	Asm []string `json:"asm,omitempty"`
	// Nasm is the a list of assembly files to be passed to a nasm-compatible
	// assembler.
	Nasm []string `json:"nasm,omitempty"`
	// Data is a list of test data files that should be available when the test is
	// run.
	Data []string `json:"data,omitempty"`
}
