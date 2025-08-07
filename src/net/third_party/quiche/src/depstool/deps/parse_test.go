package deps

import (
	"reflect"
	"testing"

	"github.com/bazelbuild/buildtools/build"
)

func TestRuleParser(t *testing.T) {
	exampleRule := `
http_archive(
    name = "com_google_absl",
    sha256 = "44634eae586a7158dceedda7d8fd5cec6d1ebae08c83399f75dd9ce76324de40",  # Last updated 2022-05-18
    strip_prefix = "abseil-cpp-3e04aade4e7a53aebbbed1a1268117f1f522bfb0",
    urls = ["https://github.com/abseil/abseil-cpp/archive/3e04aade4e7a53aebbbed1a1268117f1f522bfb0.zip"],
)`

	file, err := build.ParseWorkspace("WORKSPACE.bazel", []byte(exampleRule))
	if err != nil {
		t.Fatal(err)
	}
	rule, ok := HTTPArchiveRule(file.Stmt[0])
	if !ok {
		t.Fatal("The first rule encountered is not http_archive")
	}

	deps, err := ParseHTTPArchiveRule(rule)
	if err != nil {
		t.Fatal(err)
	}

	expected := Entry{
		Name:        "com_google_absl",
		SHA256:      "44634eae586a7158dceedda7d8fd5cec6d1ebae08c83399f75dd9ce76324de40",
		Prefix:      "abseil-cpp-3e04aade4e7a53aebbbed1a1268117f1f522bfb0",
		URL:         "https://github.com/abseil/abseil-cpp/archive/3e04aade4e7a53aebbbed1a1268117f1f522bfb0.zip",
		LastUpdated: "2022-05-18",
	}
	if !reflect.DeepEqual(*deps, expected) {
		t.Errorf("Parsing returned incorret result, expected:\n  %v\n, got:\n  %v", expected, *deps)
	}
}

func TestMultipleRules(t *testing.T) {
	exampleRules := `
http_archive(
    name = "com_google_absl",
    sha256 = "44634eae586a7158dceedda7d8fd5cec6d1ebae08c83399f75dd9ce76324de40",  # Last updated 2022-05-18
    strip_prefix = "abseil-cpp-3e04aade4e7a53aebbbed1a1268117f1f522bfb0",
    urls = ["https://github.com/abseil/abseil-cpp/archive/3e04aade4e7a53aebbbed1a1268117f1f522bfb0.zip"],
)

irrelevant_call()

http_archive(
    name = "com_google_protobuf",
    sha256 = "8b28fdd45bab62d15db232ec404248901842e5340299a57765e48abe8a80d930",  # Last updated 2022-05-18
    strip_prefix = "protobuf-3.20.1",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.20.1.tar.gz"],
)
`

	rules, err := ParseHTTPArchiveRules([]byte(exampleRules))
	if err != nil {
		t.Fatal(err)
	}
	if len(rules) != 2 {
		t.Fatalf("Expected 2 rules, got %d", len(rules))
	}
	if rules[0].Name != "com_google_absl" || rules[1].Name != "com_google_protobuf" {
		t.Errorf("Expected the two rules to be com_google_absl and com_google_protobuf, got %s and %s", rules[0].Name, rules[1].Name)
	}
}

func TestBazelParseError(t *testing.T) {
	exampleRule := `
http_archive(
    name = "com_google_absl",
    sha256 = "44634eae586a7158dceedda7d8fd5cec6d1ebae08c83399f75dd9ce76324de40",  # Last updated 2022-05-18
    strip_prefix = "abseil-cpp-3e04aade4e7a53aebbbed1a1268117f1f522bfb0",
    urls = ["https://github.com/abseil/abseil-cpp/archive/3e04aade4e7a53aebbbed1a1268117f1f522bfb0.zip"],
`

	_, err := ParseHTTPArchiveRules([]byte(exampleRule))
	if err == nil {
		t.Errorf("Expected parser error")
	}
}

func TestMissingField(t *testing.T) {
	exampleRule := `
http_archive(
    name = "com_google_absl",
    strip_prefix = "abseil-cpp-3e04aade4e7a53aebbbed1a1268117f1f522bfb0",
    urls = ["https://github.com/abseil/abseil-cpp/archive/3e04aade4e7a53aebbbed1a1268117f1f522bfb0.zip"],
)`

	_, err := ParseHTTPArchiveRules([]byte(exampleRule))
	if err == nil || err.Error() != "missing the sha256 field" {
		t.Errorf("Expected the missing sha256 error, got %v", err)
	}
}
