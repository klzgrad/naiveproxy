// Package deps package provides methods to extract and manipulate external code dependencies from the QUICHE WORKSPACE.bazel file.
package deps

import (
	"fmt"
	"regexp"

	"github.com/bazelbuild/buildtools/build"
)

var lastUpdatedRE = regexp.MustCompile(`Last updated (\d{4}-\d{2}-\d{2})`)

// Entry is a parsed representation of a dependency entry in the WORKSPACE.bazel file.
type Entry struct {
	Name        string
	SHA256      string
	Prefix      string
	URL         string
	LastUpdated string
}

// HTTPArchiveRule returns a CallExpr describing the provided http_archive
// rule, or nil if the expr in question is not an http_archive rule.
func HTTPArchiveRule(expr build.Expr) (*build.CallExpr, bool) {
	callexpr, ok := expr.(*build.CallExpr)
	if !ok {
		return nil, false
	}
	name, ok := callexpr.X.(*build.Ident)
	if !ok || name.Name != "http_archive" {
		return nil, false
	}
	return callexpr, true
}

func parseString(expr build.Expr) (string, error) {
	str, ok := expr.(*build.StringExpr)
	if !ok {
		return "", fmt.Errorf("expected string as the function argument")
	}
	return str.Value, nil
}

func parseSingleElementList(expr build.Expr) (string, error) {
	list, ok := expr.(*build.ListExpr)
	if !ok {
		return "", fmt.Errorf("expected a list as the function argument")
	}
	if len(list.List) != 1 {
		return "", fmt.Errorf("expected a single-element list as the function argument, got %d elements", len(list.List))
	}
	return parseString(list.List[0])
}

// ParseHTTPArchiveRule parses the provided http_archive rule and returns all of the dependency metadata embedded.
func ParseHTTPArchiveRule(callexpr *build.CallExpr) (*Entry, error) {
	result := Entry{}
	for _, arg := range callexpr.List {
		assign, ok := arg.(*build.AssignExpr)
		if !ok {
			return nil, fmt.Errorf("a non-named argument passed as a function parameter")
		}
		argname, _ := build.GetParamName(assign.LHS)
		var err error = nil
		switch argname {
		case "name":
			result.Name, err = parseString(assign.RHS)
		case "sha256":
			result.SHA256, err = parseString(assign.RHS)

			if len(assign.Comments.Suffix) != 1 {
				return nil, fmt.Errorf("missing the \"Last updated\" comment on the sha256 field")
			}
			comment := assign.Comments.Suffix[0].Token
			match := lastUpdatedRE.FindStringSubmatch(comment)
			if match == nil {
				return nil, fmt.Errorf("unable to parse the \"Last updated\" comment, comment value: %s", comment)
			}
			result.LastUpdated = match[1]
		case "strip_prefix":
			result.Prefix, err = parseString(assign.RHS)
		case "urls":
			result.URL, err = parseSingleElementList(assign.RHS)
		default:
			continue
		}
		if err != nil {
			return nil, err
		}
	}
	if result.Name == "" {
		return nil, fmt.Errorf("missing the name field")
	}
	if result.SHA256 == "" {
		return nil, fmt.Errorf("missing the sha256 field")
	}
	if result.URL == "" {
		return nil, fmt.Errorf("missing the urls field")
	}
	return &result, nil
}

// ParseHTTPArchiveRules parses the entire WORKSPACE.bazel file and returns all of the http_archive rules in it.
func ParseHTTPArchiveRules(source []byte) ([]*Entry, error) {
	file, err := build.ParseWorkspace("WORKSPACE.bazel", source)
	if err != nil {
		return []*Entry{}, err
	}

	result := make([]*Entry, 0)
	for _, expr := range file.Stmt {
		callexpr, ok := HTTPArchiveRule(expr)
		if !ok {
			continue
		}
		parsed, err := ParseHTTPArchiveRule(callexpr)
		if err != nil {
			return []*Entry{}, err
		}
		result = append(result, parsed)
	}
	return result, nil
}
