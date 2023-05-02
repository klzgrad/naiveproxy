// depstool is a command-line tool for manipulating QUICHE WORKSPACE.bazel file.
package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"time"

	"github.com/bazelbuild/buildtools/build"
	"quiche.googlesource.com/quiche/depstool/deps"
)

func list(path string, contents []byte) {
	flags, err := deps.ParseHTTPArchiveRules(contents)
	if err != nil {
		log.Fatalf("Failed to parse %s: %v", path, err)
	}

	fmt.Println("+------------------------------+--------------------------+")
	fmt.Println("|                   Dependency | Last updated             |")
	fmt.Println("+------------------------------+--------------------------+")
	for _, flag := range flags {
		lastUpdated, err := time.Parse("2006-01-02", flag.LastUpdated)
		if err != nil {
			log.Fatalf("Failed to parse date %s: %v", flag.LastUpdated, err)
		}
		delta := time.Since(lastUpdated)
		days := int(delta.Hours() / 24)
		fmt.Printf("| %28s | %s, %3d days ago |\n", flag.Name, flag.LastUpdated, days)
	}
	fmt.Println("+------------------------------+--------------------------+")
}

func validate(path string, contents []byte) {
	file, err := build.ParseWorkspace(path, contents)
	if err != nil {
		log.Fatalf("Failed to parse the WORKSPACE.bazel file: %v", err)
	}

	success := true
	for _, stmt := range file.Stmt {
		rule, ok := deps.HTTPArchiveRule(stmt)
		if !ok {
			// Skip unrelated rules
			continue
		}
		if _, err := deps.ParseHTTPArchiveRule(rule); err != nil {
			log.Printf("Failed to parse http_archive in %s on the line %d, issue: %v", path, rule.Pos.Line, err)
			success = false
		}
	}
	if !success {
		os.Exit(1)
	}
	log.Printf("All http_archive rules have been validated successfully")
	os.Exit(0)
}

func usage() {
	fmt.Fprintf(flag.CommandLine.Output(), `
usage: depstool [WORKSPACE file] [subcommand]

Available subcommands:
    list       Lists all of the rules in the file
    validate   Validates that the WORKSPACE file is parsable

If no subcommand is specified, "list" is assumed.
`)
	flag.PrintDefaults()
}

func main() {
	flag.Usage = usage
	flag.Parse()
	path := flag.Arg(0)
	if path == "" {
		usage()
		os.Exit(1)
	}
	contents, err := ioutil.ReadFile(path)
	if err != nil {
		log.Fatalf("Failed to read WORKSPACE.bazel file: %v", err)
	}

	subcommand := flag.Arg(1)
	switch subcommand {
	case "":
		fallthrough // list is the default action
	case "list":
		list(path, contents)
	case "validate":
		validate(path, contents)
	default:
		log.Fatalf("Unknown command: %s", subcommand)
	}
}
