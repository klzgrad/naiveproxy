// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_handler_negotiate.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "net/base/address_family.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_util.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_auth_filter.h"
#include "net/http/http_auth_preferences.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_with_source.h"
#include "net/ssl/ssl_info.h"

namespace net {

namespace {

std::unique_ptr<base::Value> NetLogParameterChannelBindings(
    const std::string& channel_binding_token,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict;
  if (!capture_mode.include_socket_bytes())
    return std::move(dict);

  dict.reset(new base::DictionaryValue());
  dict->SetString("token", base::HexEncode(channel_binding_token.data(),
                                           channel_binding_token.size()));
  return std::move(dict);
}

}  // namespace

HttpAuthHandlerNegotiate::Factory::Factory()
    : resolver_(nullptr),
#if defined(OS_WIN)
      max_token_length_(0),
#endif
      is_unsupported_(false) {
}

HttpAuthHandlerNegotiate::Factory::~Factory() {
}

void HttpAuthHandlerNegotiate::Factory::set_host_resolver(
    HostResolver* resolver) {
  resolver_ = resolver;
}

int HttpAuthHandlerNegotiate::Factory::CreateAuthHandler(
    HttpAuthChallengeTokenizer* challenge,
    HttpAuth::Target target,
    const SSLInfo& ssl_info,
    const GURL& origin,
    CreateReason reason,
    int digest_nonce_count,
    const NetLogWithSource& net_log,
    std::unique_ptr<HttpAuthHandler>* handler) {
#if defined(OS_WIN)
  if (is_unsupported_ || reason == CREATE_PREEMPTIVE)
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  if (max_token_length_ == 0) {
    int rv = DetermineMaxTokenLength(auth_library_.get(), NEGOSSP_NAME,
                                     &max_token_length_);
    if (rv == ERR_UNSUPPORTED_AUTH_SCHEME)
      is_unsupported_ = true;
    if (rv != OK)
      return rv;
  }
  // TODO(cbentzel): Move towards model of parsing in the factory
  //                 method and only constructing when valid.
  std::unique_ptr<HttpAuthHandler> tmp_handler(
      new HttpAuthHandlerNegotiate(auth_library_.get(), max_token_length_,
                                   http_auth_preferences(), resolver_));
#elif defined(OS_ANDROID)
  if (is_unsupported_ || !http_auth_preferences() ||
      http_auth_preferences()->AuthAndroidNegotiateAccountType().empty() ||
      reason == CREATE_PREEMPTIVE)
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  // TODO(cbentzel): Move towards model of parsing in the factory
  //                 method and only constructing when valid.
  std::unique_ptr<HttpAuthHandler> tmp_handler(
      new HttpAuthHandlerNegotiate(http_auth_preferences(), resolver_));
#elif defined(OS_POSIX)
  bool allow_gssapi_library_load = true;
#if defined(OS_CHROMEOS)
  allow_gssapi_library_load = http_auth_preferences() &&
                              http_auth_preferences()->AllowGssapiLibraryLoad();
#endif
  if (is_unsupported_ || !allow_gssapi_library_load)
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  if (!auth_library_->Init()) {
    is_unsupported_ = true;
    return ERR_UNSUPPORTED_AUTH_SCHEME;
  }
  // TODO(ahendrickson): Move towards model of parsing in the factory
  //                     method and only constructing when valid.
  std::unique_ptr<HttpAuthHandler> tmp_handler(new HttpAuthHandlerNegotiate(
      auth_library_.get(), http_auth_preferences(), resolver_));
#endif
  if (!tmp_handler->InitFromChallenge(challenge, target, ssl_info, origin,
                                      net_log))
    return ERR_INVALID_RESPONSE;
  handler->swap(tmp_handler);
  return OK;
}

HttpAuthHandlerNegotiate::HttpAuthHandlerNegotiate(
#if !defined(OS_ANDROID)
    AuthLibrary* auth_library,
#endif
#if defined(OS_WIN)
    ULONG max_token_length,
#endif
    const HttpAuthPreferences* prefs,
    HostResolver* resolver)
#if defined(OS_ANDROID)
    : auth_system_(prefs),
#elif defined(OS_WIN)
    : auth_system_(auth_library, "Negotiate", NEGOSSP_NAME, max_token_length),
#elif defined(OS_POSIX)
    : auth_system_(auth_library, "Negotiate", CHROME_GSS_SPNEGO_MECH_OID_DESC),
#endif
      resolver_(resolver),
      already_called_(false),
      has_credentials_(false),
      auth_token_(NULL),
      next_state_(STATE_NONE),
      http_auth_preferences_(prefs) {
}

HttpAuthHandlerNegotiate::~HttpAuthHandlerNegotiate() {
}

std::string HttpAuthHandlerNegotiate::CreateSPN(const AddressList& address_list,
                                                const GURL& origin) {
  // Kerberos Web Server SPNs are in the form HTTP/<host>:<port> through SSPI,
  // and in the form HTTP@<host>:<port> through GSSAPI
  //   http://msdn.microsoft.com/en-us/library/ms677601%28VS.85%29.aspx
  //
  // However, reality differs from the specification. A good description of
  // the problems can be found here:
  //   http://blog.michelbarneveld.nl/michel/archive/2009/11/14/the-reason-why-kb911149-and-kb908209-are-not-the-soluton.aspx
  //
  // Typically the <host> portion should be the canonical FQDN for the service.
  // If this could not be resolved, the original hostname in the URL will be
  // attempted instead. However, some intranets register SPNs using aliases
  // for the same canonical DNS name to allow multiple web services to reside
  // on the same host machine without requiring different ports. IE6 and IE7
  // have hotpatches that allow the default behavior to be overridden.
  //   http://support.microsoft.com/kb/911149
  //   http://support.microsoft.com/kb/938305
  //
  // According to the spec, the <port> option should be included if it is a
  // non-standard port (i.e. not 80 or 443 in the HTTP case). However,
  // historically browsers have not included the port, even on non-standard
  // ports. IE6 required a hotpatch and a registry setting to enable
  // including non-standard ports, and IE7 and IE8 also require the same
  // registry setting, but no hotpatch. Firefox does not appear to have an
  // option to include non-standard ports as of 3.6.
  //   http://support.microsoft.com/kb/908209
  //
  // Without any command-line flags, Chrome matches the behavior of Firefox
  // and IE. Users can override the behavior so aliases are allowed and
  // non-standard ports are included.
  int port = origin.EffectiveIntPort();
  std::string server = address_list.canonical_name();
  if (server.empty())
    server = origin.host();
#if defined(OS_WIN)
  static const char kSpnSeparator = '/';
#elif defined(OS_POSIX)
  static const char kSpnSeparator = '@';
#endif
  if (port != 80 && port != 443 &&
      (http_auth_preferences_ &&
       http_auth_preferences_->NegotiateEnablePort())) {
    return base::StringPrintf("HTTP%c%s:%d", kSpnSeparator, server.c_str(),
                              port);
  } else {
    return base::StringPrintf("HTTP%c%s", kSpnSeparator, server.c_str());
  }
}

HttpAuth::AuthorizationResult HttpAuthHandlerNegotiate::HandleAnotherChallenge(
    HttpAuthChallengeTokenizer* challenge) {
  return auth_system_.ParseChallenge(challenge);
}

// Require identity on first pass instead of second.
bool HttpAuthHandlerNegotiate::NeedsIdentity() {
  return auth_system_.NeedsIdentity();
}

bool HttpAuthHandlerNegotiate::AllowsDefaultCredentials() {
  if (target_ == HttpAuth::AUTH_PROXY)
    return true;
  if (!http_auth_preferences_)
    return false;
  return http_auth_preferences_->CanUseDefaultCredentials(origin_);
}

bool HttpAuthHandlerNegotiate::AllowsExplicitCredentials() {
  return auth_system_.AllowsExplicitCredentials();
}

// The Negotiate challenge header looks like:
//   WWW-Authenticate: NEGOTIATE auth-data
bool HttpAuthHandlerNegotiate::Init(HttpAuthChallengeTokenizer* challenge,
                                    const SSLInfo& ssl_info) {
#if defined(OS_POSIX)
  if (!auth_system_.Init()) {
    VLOG(1) << "can't initialize GSSAPI library";
    return false;
  }
  // GSSAPI does not provide a way to enter username/password to
  // obtain a TGT. If the default credentials are not allowed for
  // a particular site (based on whitelist), fall back to a
  // different scheme.
  if (!AllowsDefaultCredentials())
    return false;
#endif
  if (CanDelegate())
    auth_system_.Delegate();
  auth_scheme_ = HttpAuth::AUTH_SCHEME_NEGOTIATE;
  score_ = 4;
  properties_ = ENCRYPTS_IDENTITY | IS_CONNECTION_BASED;

  HttpAuth::AuthorizationResult auth_result =
      auth_system_.ParseChallenge(challenge);
  if (auth_result != HttpAuth::AUTHORIZATION_RESULT_ACCEPT)
    return false;

  // Try to extract channel bindings.
  if (ssl_info.is_valid())
    x509_util::GetTLSServerEndPointChannelBinding(*ssl_info.cert,
                                                  &channel_bindings_);
  if (!channel_bindings_.empty())
    net_log_.AddEvent(
        NetLogEventType::AUTH_CHANNEL_BINDINGS,
        base::Bind(&NetLogParameterChannelBindings, channel_bindings_));
  return true;
}

int HttpAuthHandlerNegotiate::GenerateAuthTokenImpl(
    const AuthCredentials* credentials, const HttpRequestInfo* request,
    const CompletionCallback& callback, std::string* auth_token) {
  DCHECK(callback_.is_null());
  DCHECK(auth_token_ == NULL);
  auth_token_ = auth_token;
  if (already_called_) {
    DCHECK((!has_credentials_ && credentials == NULL) ||
           (has_credentials_ && credentials->Equals(credentials_)));
    next_state_ = STATE_GENERATE_AUTH_TOKEN;
  } else {
    already_called_ = true;
    if (credentials) {
      has_credentials_ = true;
      credentials_ = *credentials;
    }
    next_state_ = STATE_RESOLVE_CANONICAL_NAME;
  }
  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    callback_ = callback;
  return rv;
}

void HttpAuthHandlerNegotiate::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    DoCallback(rv);
}

void HttpAuthHandlerNegotiate::DoCallback(int rv) {
  DCHECK(rv != ERR_IO_PENDING);
  DCHECK(!callback_.is_null());
  CompletionCallback callback = callback_;
  callback_.Reset();
  callback.Run(rv);
}

int HttpAuthHandlerNegotiate::DoLoop(int result) {
  DCHECK(next_state_ != STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_RESOLVE_CANONICAL_NAME:
        DCHECK_EQ(OK, rv);
        rv = DoResolveCanonicalName();
        break;
      case STATE_RESOLVE_CANONICAL_NAME_COMPLETE:
        rv = DoResolveCanonicalNameComplete(rv);
        break;
      case STATE_GENERATE_AUTH_TOKEN:
        DCHECK_EQ(OK, rv);
        rv = DoGenerateAuthToken();
        break;
      case STATE_GENERATE_AUTH_TOKEN_COMPLETE:
        rv = DoGenerateAuthTokenComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);

  return rv;
}

int HttpAuthHandlerNegotiate::DoResolveCanonicalName() {
  next_state_ = STATE_RESOLVE_CANONICAL_NAME_COMPLETE;
  if ((http_auth_preferences_ &&
       http_auth_preferences_->NegotiateDisableCnameLookup()) ||
      !resolver_)
    return OK;

  // TODO(cbentzel): Add reverse DNS lookup for numeric addresses.
  HostResolver::RequestInfo info(HostPortPair(origin_.host(), 0));
  info.set_host_resolver_flags(HOST_RESOLVER_CANONNAME);
  return resolver_->Resolve(info, DEFAULT_PRIORITY, &address_list_,
                            base::Bind(&HttpAuthHandlerNegotiate::OnIOComplete,
                                       base::Unretained(this)),
                            &request_, net_log_);
}

int HttpAuthHandlerNegotiate::DoResolveCanonicalNameComplete(int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  if (rv != OK) {
    // Even in the error case, try to use origin_.host instead of
    // passing the failure on to the caller.
    VLOG(1) << "Problem finding canonical name for SPN for host "
            << origin_.host() << ": " << ErrorToString(rv);
    rv = OK;
  }

  next_state_ = STATE_GENERATE_AUTH_TOKEN;
  spn_ = CreateSPN(address_list_, origin_);
  address_list_ = AddressList();
  return rv;
}

int HttpAuthHandlerNegotiate::DoGenerateAuthToken() {
  next_state_ = STATE_GENERATE_AUTH_TOKEN_COMPLETE;
  AuthCredentials* credentials = has_credentials_ ? &credentials_ : NULL;
  return auth_system_.GenerateAuthToken(
      credentials, spn_, channel_bindings_, auth_token_,
      base::Bind(&HttpAuthHandlerNegotiate::OnIOComplete,
                 base::Unretained(this)));
}

int HttpAuthHandlerNegotiate::DoGenerateAuthTokenComplete(int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  auth_token_ = NULL;
  return rv;
}

bool HttpAuthHandlerNegotiate::CanDelegate() const {
  // TODO(cbentzel): Should delegation be allowed on proxies?
  if (target_ == HttpAuth::AUTH_PROXY)
    return false;
  if (!http_auth_preferences_)
    return false;
  return http_auth_preferences_->CanDelegate(origin_);
}

}  // namespace net
