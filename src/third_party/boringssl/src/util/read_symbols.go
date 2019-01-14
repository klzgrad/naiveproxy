// Copyright (c) 2018, Google Inc.
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

// read_symbols.go scans one or more .a files and, for each object contained in
// the .a files, reads the list of symbols in that object file.
package main

import (
	"bytes"
	"debug/elf"
	"debug/macho"
	"flag"
	"fmt"
	"os"
	"runtime"
	"sort"
	"strings"

	"boringssl.googlesource.com/boringssl/util/ar"
)

const (
	ObjFileFormatELF   = "elf"
	ObjFileFormatMachO = "macho"
)

var outFlag = flag.String("out", "-", "File to write output symbols")
var objFileFormat = flag.String("obj-file-format", defaultObjFileFormat(runtime.GOOS), "Object file format to expect (options are elf, macho)")

func defaultObjFileFormat(goos string) string {
	switch goos {
	case "linux":
		return ObjFileFormatELF
	case "darwin":
		return ObjFileFormatMachO
	default:
		// By returning a value here rather than panicking, the user can still
		// cross-compile from an unsupported platform to a supported platform by
		// overriding this default with a flag. If the user doesn't provide the
		// flag, we will panic during flag parsing.
		return "unsupported"
	}
}

func main() {
	flag.Parse()
	if flag.NArg() < 1 {
		fmt.Fprintf(os.Stderr, "Usage: %s [-out OUT] [-obj-file-format FORMAT] ARCHIVE_FILE [ARCHIVE_FILE [...]]\n", os.Args[0])
		os.Exit(1)
	}
	archiveFiles := flag.Args()

	out := os.Stdout
	if *outFlag != "-" {
		var err error
		out, err = os.Create(*outFlag)
		nilOrPanic(err, "failed to open output file")
		defer out.Close()
	}

	var symbols []string
	// Only add first instance of any symbol; keep track of them in this map.
	added := make(map[string]bool)
	for _, archive := range archiveFiles {
		f, err := os.Open(archive)
		nilOrPanic(err, "failed to open archive file %s", archive)
		objectFiles, err := ar.ParseAR(f)
		nilOrPanic(err, "failed to read archive file %s", archive)

		for name, contents := range objectFiles {
			if !strings.HasSuffix(name, ".o") {
				continue
			}
			for _, s := range listSymbols(name, contents) {
				if !added[s] {
					added[s] = true
					symbols = append(symbols, s)
				}
			}
		}
	}
	sort.Strings(symbols)
	for _, s := range symbols {
		// Filter out C++ mangled names.
		prefix := "_Z"
		if runtime.GOOS == "darwin" {
			prefix = "__Z"
		}
		if !strings.HasPrefix(s, prefix) {
			fmt.Fprintln(out, s)
		}
	}
}

// listSymbols lists the exported symbols from an object file.
func listSymbols(name string, contents []byte) []string {
	switch *objFileFormat {
	case ObjFileFormatELF:
		return listSymbolsELF(name, contents)
	case ObjFileFormatMachO:
		return listSymbolsMachO(name, contents)
	default:
		panic(fmt.Errorf("unsupported object file format %v", *objFileFormat))
	}
}

func listSymbolsELF(name string, contents []byte) []string {
	f, err := elf.NewFile(bytes.NewReader(contents))
	nilOrPanic(err, "failed to parse ELF file %s", name)
	syms, err := f.Symbols()
	nilOrPanic(err, "failed to read symbol names from ELF file %s", name)

	var names []string
	for _, sym := range syms {
		// Only include exported, defined symbols
		if elf.ST_BIND(sym.Info) != elf.STB_LOCAL && sym.Section != elf.SHN_UNDEF {
			names = append(names, sym.Name)
		}
	}
	return names
}

func listSymbolsMachO(name string, contents []byte) []string {
	f, err := macho.NewFile(bytes.NewReader(contents))
	nilOrPanic(err, "failed to parse Mach-O file %s", name)
	if f.Symtab == nil {
		return nil
	}
	var names []string
	for _, sym := range f.Symtab.Syms {
		// Source: https://opensource.apple.com/source/xnu/xnu-3789.51.2/EXTERNAL_HEADERS/mach-o/nlist.h.auto.html
		const (
			N_PEXT uint8 = 0x10 // Private external symbol bit
			N_EXT  uint8 = 0x01 // External symbol bit, set for external symbols
			N_TYPE uint8 = 0x0e // mask for the type bits

			N_UNDF uint8 = 0x0 // undefined, n_sect == NO_SECT
			N_ABS  uint8 = 0x2 // absolute, n_sect == NO_SECT
			N_SECT uint8 = 0xe // defined in section number n_sect
			N_PBUD uint8 = 0xc // prebound undefined (defined in a dylib)
			N_INDR uint8 = 0xa // indirect
		)

		// Only include exported, defined symbols.
		if sym.Type&N_EXT != 0 && sym.Type&N_TYPE != N_UNDF {
			if len(sym.Name) == 0 || sym.Name[0] != '_' {
				panic(fmt.Errorf("unexpected symbol without underscore prefix: %v", sym.Name))
			}
			names = append(names, sym.Name[1:])
		}
	}
	return names
}

func nilOrPanic(err error, f string, args ...interface{}) {
	if err != nil {
		panic(fmt.Errorf(f+": %v", append(args, err)...))
	}
}
