// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_UTIL_H_
#define URL_URL_UTIL_H_

#include <string>
#include <vector>

#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "url/third_party/mozilla/url_parse.h"
#include "url/url_canon.h"
#include "url/url_constants.h"
#include "url/url_export.h"

namespace url {

// Init ------------------------------------------------------------------------

// Initialization is NOT required, it will be implicitly initialized when first
// used. However, this implicit initialization is NOT threadsafe. If you are
// using this library in a threaded environment and don't have a consistent
// "first call" (an example might be calling Add*Scheme with your special
// application-specific schemes) then you will want to call initialize before
// spawning any threads.
//
// It is OK to call this function more than once, subsequent calls will be
// no-ops, unless Shutdown was called in the mean time. This will also be a
// no-op if other calls to the library have forced an initialization beforehand.
URL_EXPORT void Initialize();

// Cleanup is not required, except some strings may leak. For most user
// applications, this is fine. If you're using it in a library that may get
// loaded and unloaded, you'll want to unload to properly clean up your
// library.
URL_EXPORT void Shutdown();

// Schemes ---------------------------------------------------------------------

// Changes the behavior of SchemeHostPort / Origin to allow non-standard schemes
// to be specified, instead of canonicalizing them to an invalid SchemeHostPort
// or opaque Origin, respectively. This is used for Android WebView backwards
// compatibility, which allows the use of custom schemes: content hosted in
// Android WebView assumes that one URL with a non-standard scheme will be
// same-origin to another URL with the same non-standard scheme.
URL_EXPORT void EnableNonStandardSchemesForAndroidWebView();

// Whether or not SchemeHostPort and Origin allow non-standard schemes.
URL_EXPORT bool AllowNonStandardSchemesForAndroidWebView();

// A pair for representing a standard scheme name and the SchemeType for it.
struct URL_EXPORT SchemeWithType {
  const char* scheme;
  SchemeType type;
};

// The following Add*Scheme method are not threadsafe and can not be called
// concurrently with any other url_util function. They will assert if the lists
// of schemes have been locked (see LockSchemeRegistries).

// Adds an application-defined scheme to the internal list of "standard-format"
// URL schemes. A standard-format scheme adheres to what RFC 3986 calls "generic
// URI syntax" (https://tools.ietf.org/html/rfc3986#section-3).

URL_EXPORT void AddStandardScheme(const char* new_scheme,
                                  SchemeType scheme_type);

// Adds an application-defined scheme to the internal list of schemes allowed
// for referrers.
URL_EXPORT void AddReferrerScheme(const char* new_scheme,
                                  SchemeType scheme_type);

// Adds an application-defined scheme to the list of schemes that do not trigger
// mixed content warnings.
URL_EXPORT void AddSecureScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetSecureSchemes();

// Adds an application-defined scheme to the list of schemes that normal pages
// cannot link to or access (i.e., with the same security rules as those applied
// to "file" URLs).
URL_EXPORT void AddLocalScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetLocalSchemes();

// Adds an application-defined scheme to the list of schemes that cause pages
// loaded with them to not have access to pages loaded with any other URL
// scheme.
URL_EXPORT void AddNoAccessScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetNoAccessSchemes();

// Adds an application-defined scheme to the list of schemes that can be sent
// CORS requests.
URL_EXPORT void AddCorsEnabledScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetCorsEnabledSchemes();

// Adds an application-defined scheme to the list of web schemes that can be
// used by web to store data (e.g. cookies, local storage, ...). This is
// to differentiate them from schemes that can store data but are not used on
// web (e.g. application's internal schemes) or schemes that are used on web but
// cannot store data.
URL_EXPORT void AddWebStorageScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetWebStorageSchemes();

// Adds an application-defined scheme to the list of schemes that can bypass the
// Content-Security-Policy(CSP) checks.
URL_EXPORT void AddCSPBypassingScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetCSPBypassingSchemes();

// Adds an application-defined scheme to the list of schemes that are strictly
// empty documents, allowing them to commit synchronously.
URL_EXPORT void AddEmptyDocumentScheme(const char* new_scheme);
URL_EXPORT const std::vector<std::string>& GetEmptyDocumentSchemes();

// Sets a flag to prevent future calls to Add*Scheme from succeeding.
//
// This is designed to help prevent errors for multithreaded applications.
// Normal usage would be to call Add*Scheme for your custom schemes at
// the beginning of program initialization, and then LockSchemeRegistries. This
// prevents future callers from mistakenly calling Add*Scheme when the
// program is running with multiple threads, where such usage would be
// dangerous.
//
// We could have had Add*Scheme use a lock instead, but that would add
// some platform-specific dependencies we don't otherwise have now, and is
// overkill considering the normal usage is so simple.
URL_EXPORT void LockSchemeRegistries();

// Locates the scheme in the given string and places it into |found_scheme|,
// which may be NULL to indicate the caller does not care about the range.
//
// Returns whether the given |compare| scheme matches the scheme found in the
// input (if any). The |compare| scheme must be a valid canonical scheme or
// the result of the comparison is undefined.
URL_EXPORT bool FindAndCompareScheme(const char* str,
                                     int str_len,
                                     const char* compare,
                                     Component* found_scheme);
URL_EXPORT bool FindAndCompareScheme(const base::char16* str,
                                     int str_len,
                                     const char* compare,
                                     Component* found_scheme);
inline bool FindAndCompareScheme(const std::string& str,
                                 const char* compare,
                                 Component* found_scheme) {
  return FindAndCompareScheme(str.data(), static_cast<int>(str.size()),
                              compare, found_scheme);
}
inline bool FindAndCompareScheme(const base::string16& str,
                                 const char* compare,
                                 Component* found_scheme) {
  return FindAndCompareScheme(str.data(), static_cast<int>(str.size()),
                              compare, found_scheme);
}

// Returns true if the given scheme identified by |scheme| within |spec| is in
// the list of known standard-format schemes (see AddStandardScheme).
URL_EXPORT bool IsStandard(const char* spec, const Component& scheme);
URL_EXPORT bool IsStandard(const base::char16* spec, const Component& scheme);

// Returns true if the given scheme identified by |scheme| within |spec| is in
// the list of allowed schemes for referrers (see AddReferrerScheme).
URL_EXPORT bool IsReferrerScheme(const char* spec, const Component& scheme);

// Returns true and sets |type| to the SchemeType of the given scheme
// identified by |scheme| within |spec| if the scheme is in the list of known
// standard-format schemes (see AddStandardScheme).
URL_EXPORT bool GetStandardSchemeType(const char* spec,
                                      const Component& scheme,
                                      SchemeType* type);
URL_EXPORT bool GetStandardSchemeType(const base::char16* spec,
                                      const Component& scheme,
                                      SchemeType* type);

// Hosts  ----------------------------------------------------------------------

// Returns true if the |canonical_host| matches or is in the same domain as the
// given |canonical_domain| string. For example, if the canonicalized hostname
// is "www.google.com", this will return true for "com", "google.com", and
// "www.google.com" domains.
//
// If either of the input StringPieces is empty, the return value is false. The
// input domain should match host canonicalization rules. i.e. it should be
// lowercase except for escape chars.
URL_EXPORT bool DomainIs(base::StringPiece canonical_host,
                         base::StringPiece canonical_domain);

// Returns true if the hostname is an IP address. Note: this function isn't very
// cheap, as it must re-parse the host to verify.
URL_EXPORT bool HostIsIPAddress(base::StringPiece host);

// URL library wrappers --------------------------------------------------------

// Parses the given spec according to the extracted scheme type. Normal users
// should use the URL object, although this may be useful if performance is
// critical and you don't want to do the heap allocation for the std::string.
//
// As with the Canonicalize* functions, the charset converter can
// be NULL to use UTF-8 (it will be faster in this case).
//
// Returns true if a valid URL was produced, false if not. On failure, the
// output and parsed structures will still be filled and will be consistent,
// but they will not represent a loadable URL.
URL_EXPORT bool Canonicalize(const char* spec,
                             int spec_len,
                             bool trim_path_end,
                             CharsetConverter* charset_converter,
                             CanonOutput* output,
                             Parsed* output_parsed);
URL_EXPORT bool Canonicalize(const base::char16* spec,
                             int spec_len,
                             bool trim_path_end,
                             CharsetConverter* charset_converter,
                             CanonOutput* output,
                             Parsed* output_parsed);

// Resolves a potentially relative URL relative to the given parsed base URL.
// The base MUST be valid. The resulting canonical URL and parsed information
// will be placed in to the given out variables.
//
// The relative need not be relative. If we discover that it's absolute, this
// will produce a canonical version of that URL. See Canonicalize() for more
// about the charset_converter.
//
// Returns true if the output is valid, false if the input could not produce
// a valid URL.
URL_EXPORT bool ResolveRelative(const char* base_spec,
                                int base_spec_len,
                                const Parsed& base_parsed,
                                const char* relative,
                                int relative_length,
                                CharsetConverter* charset_converter,
                                CanonOutput* output,
                                Parsed* output_parsed);
URL_EXPORT bool ResolveRelative(const char* base_spec,
                                int base_spec_len,
                                const Parsed& base_parsed,
                                const base::char16* relative,
                                int relative_length,
                                CharsetConverter* charset_converter,
                                CanonOutput* output,
                                Parsed* output_parsed);

// Replaces components in the given VALID input URL. The new canonical URL info
// is written to output and out_parsed.
//
// Returns true if the resulting URL is valid.
URL_EXPORT bool ReplaceComponents(const char* spec,
                                  int spec_len,
                                  const Parsed& parsed,
                                  const Replacements<char>& replacements,
                                  CharsetConverter* charset_converter,
                                  CanonOutput* output,
                                  Parsed* out_parsed);
URL_EXPORT bool ReplaceComponents(
    const char* spec,
    int spec_len,
    const Parsed& parsed,
    const Replacements<base::char16>& replacements,
    CharsetConverter* charset_converter,
    CanonOutput* output,
    Parsed* out_parsed);

// String helper functions -----------------------------------------------------

enum class DecodeURLResult {
  // Did not contain code points greater than 0x7F.
  kAsciiOnly,
  // Did UTF-8 decode only.
  kUTF8,
  // Did byte to Unicode mapping only.
  // https://infra.spec.whatwg.org/#isomorphic-decode
  kIsomorphic,
};

// Unescapes the given string using URL escaping rules.
// This function tries to decode non-ASCII characters in UTF-8 first,
// then in isomorphic encoding if UTF-8 decoding failed.
URL_EXPORT DecodeURLResult DecodeURLEscapeSequences(const char* input,
                                                    int length,
                                                    CanonOutputW* output);

// Escapes the given string as defined by the JS method encodeURIComponent. See
// https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/encodeURIComponent
URL_EXPORT void EncodeURIComponent(const char* input,
                                   int length,
                                   CanonOutput* output);

}  // namespace url

#endif  // URL_URL_UTIL_H_
