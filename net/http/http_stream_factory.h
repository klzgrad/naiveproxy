// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_FACTORY_H_
#define NET_HTTP_HTTP_STREAM_FACTORY_H_

#include <list>
#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "net/base/completion_callback.h"
#include "net/base/load_states.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/http/http_server_properties.h"
#include "net/socket/connection_attempts.h"
// This file can be included from net/http even though
// it is in net/websockets because it doesn't
// introduce any link dependency to net/websockets.
#include "net/websockets/websocket_handshake_stream_base.h"

namespace base {
namespace trace_event {
class ProcessMemoryDump;
}
}

namespace net {

class BidirectionalStreamImpl;
class HostMappingRules;
class HttpAuthController;
class HttpNetworkSession;
class HttpResponseHeaders;
class HttpResponseInfo;
class HttpStream;
class NetLogWithSource;
class ProxyInfo;
class SSLCertRequestInfo;
class SSLInfo;
struct HttpRequestInfo;
struct SSLConfig;

// The HttpStreamRequest is the client's handle to the worker object which
// handles the creation of an HttpStream.  While the HttpStream is being
// created, this object is the creator's handle for interacting with the
// HttpStream creation process.  The request is cancelled by deleting it, after
// which no callbacks will be invoked.
class NET_EXPORT_PRIVATE HttpStreamRequest {
 public:
  // Indicates which type of stream is requested.
  enum StreamType {
    BIDIRECTIONAL_STREAM,
    HTTP_STREAM,
  };

  // The HttpStreamRequest::Delegate is a set of callback methods for a
  // HttpStreamRequestJob.  Generally, only one of these methods will be
  // called as a result of a stream request.
  class NET_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

    // This is the success case for RequestStream.
    // |stream| is now owned by the delegate.
    // |used_ssl_config| indicates the actual SSL configuration used for this
    // stream, since the HttpStreamRequest may have modified the configuration
    // during stream processing.
    // |used_proxy_info| indicates the actual ProxyInfo used for this stream,
    // since the HttpStreamRequest performs the proxy resolution.
    virtual void OnStreamReady(const SSLConfig& used_ssl_config,
                               const ProxyInfo& used_proxy_info,
                               std::unique_ptr<HttpStream> stream) = 0;

    // This is the success case for RequestWebSocketHandshakeStream.
    // |stream| is now owned by the delegate.
    // |used_ssl_config| indicates the actual SSL configuration used for this
    // stream, since the HttpStreamRequest may have modified the configuration
    // during stream processing.
    // |used_proxy_info| indicates the actual ProxyInfo used for this stream,
    // since the HttpStreamRequest performs the proxy resolution.
    virtual void OnWebSocketHandshakeStreamReady(
        const SSLConfig& used_ssl_config,
        const ProxyInfo& used_proxy_info,
        std::unique_ptr<WebSocketHandshakeStreamBase> stream) = 0;

    virtual void OnBidirectionalStreamImplReady(
        const SSLConfig& used_ssl_config,
        const ProxyInfo& used_proxy_info,
        std::unique_ptr<BidirectionalStreamImpl> stream) = 0;

    // This is the failure to create a stream case.
    // |used_ssl_config| indicates the actual SSL configuration used for this
    // stream, since the HttpStreamRequest may have modified the configuration
    // during stream processing.
    virtual void OnStreamFailed(int status,
                                const NetErrorDetails& net_error_details,
                                const SSLConfig& used_ssl_config) = 0;

    // Called when we have a certificate error for the request.
    // |used_ssl_config| indicates the actual SSL configuration used for this
    // stream, since the HttpStreamRequest may have modified the configuration
    // during stream processing.
    virtual void OnCertificateError(int status,
                                    const SSLConfig& used_ssl_config,
                                    const SSLInfo& ssl_info) = 0;

    // This is the failure case where we need proxy authentication during
    // proxy tunnel establishment.  For the tunnel case, we were unable to
    // create the HttpStream, so the caller provides the auth and then resumes
    // the HttpStreamRequest.
    //
    // For the non-tunnel case, the caller will discover the authentication
    // failure when reading response headers. At that point, it will handle the
    // authentication failure and restart the HttpStreamRequest entirely.
    //
    // Ownership of |auth_controller| and |proxy_response| are owned
    // by the HttpStreamRequest. |proxy_response| is not guaranteed to be usable
    // after the lifetime of this callback.  The delegate may take a reference
    // to |auth_controller| if it is needed beyond the lifetime of this
    // callback.
    //
    // |used_ssl_config| indicates the actual SSL configuration used for this
    // stream, since the HttpStreamRequest may have modified the configuration
    // during stream processing.
    virtual void OnNeedsProxyAuth(const HttpResponseInfo& proxy_response,
                                  const SSLConfig& used_ssl_config,
                                  const ProxyInfo& used_proxy_info,
                                  HttpAuthController* auth_controller) = 0;

    // This is the failure for SSL Client Auth
    // Ownership of |cert_info| is retained by the HttpStreamRequest.  The
    // delegate may take a reference if it needs the cert_info beyond the
    // lifetime of this callback.
    virtual void OnNeedsClientAuth(const SSLConfig& used_ssl_config,
                                   SSLCertRequestInfo* cert_info) = 0;

    // This is the failure of the CONNECT request through an HTTPS proxy.
    // Headers can be read from |response_info|, while the body can be read
    // from |stream|.
    //
    // |used_ssl_config| indicates the actual SSL configuration used for this
    // stream, since the HttpStreamRequest may have modified the configuration
    // during stream processing.
    //
    // |used_proxy_info| indicates the actual ProxyInfo used for this stream,
    // since the HttpStreamRequest performs the proxy resolution.
    //
    // Ownership of |stream| is transferred to the delegate.
    virtual void OnHttpsProxyTunnelResponse(
        const HttpResponseInfo& response_info,
        const SSLConfig& used_ssl_config,
        const ProxyInfo& used_proxy_info,
        std::unique_ptr<HttpStream> stream) = 0;

    // Called when finding all QUIC alternative services are marked broken for
    // the origin in this request which advertises supporting QUIC.
    virtual void OnQuicBroken() = 0;
  };

  virtual ~HttpStreamRequest() {}

  // When a HttpStream creation process is stalled due to necessity
  // of Proxy authentication credentials, the delegate OnNeedsProxyAuth
  // will have been called.  It now becomes the delegate's responsibility
  // to collect the necessary credentials, and then call this method to
  // resume the HttpStream creation process.
  virtual int RestartTunnelWithProxyAuth() = 0;

  // Called when the priority of the parent transaction changes.
  virtual void SetPriority(RequestPriority priority) = 0;

  // Returns the LoadState for the request.
  virtual LoadState GetLoadState() const = 0;

  // Returns true if TLS/ALPN was negotiated for this stream.
  virtual bool was_alpn_negotiated() const = 0;

  // Protocol negotiated with the server.
  virtual NextProto negotiated_protocol() const = 0;

  // Returns true if this stream is being fetched over SPDY.
  virtual bool using_spdy() const = 0;

  // Returns socket-layer connection attempts made for this stream request.
  virtual const ConnectionAttempts& connection_attempts() const = 0;
};

// The HttpStreamFactory defines an interface for creating usable HttpStreams.
class NET_EXPORT HttpStreamFactory {
 public:
  virtual ~HttpStreamFactory();

  void ProcessAlternativeServices(HttpNetworkSession* session,
                                  const HttpResponseHeaders* headers,
                                  const url::SchemeHostPort& http_server);

  // Virtual interface methods.

  // Request a stream.
  // Will call delegate->OnStreamReady on successful completion.
  virtual std::unique_ptr<HttpStreamRequest> RequestStream(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log) = 0;

  // Request a WebSocket handshake stream.
  // Will call delegate->OnWebSocketHandshakeStreamReady on successful
  // completion.
  virtual std::unique_ptr<HttpStreamRequest> RequestWebSocketHandshakeStream(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      WebSocketHandshakeStreamBase::CreateHelper* create_helper,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log) = 0;

  // Request a BidirectionalStreamImpl.
  // Will call delegate->OnBidirectionalStreamImplReady on successful
  // completion.
  virtual std::unique_ptr<HttpStreamRequest> RequestBidirectionalStreamImpl(
      const HttpRequestInfo& info,
      RequestPriority priority,
      const SSLConfig& server_ssl_config,
      const SSLConfig& proxy_ssl_config,
      HttpStreamRequest::Delegate* delegate,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      const NetLogWithSource& net_log) = 0;

  // Requests that enough connections for |num_streams| be opened.
  virtual void PreconnectStreams(int num_streams,
                                 const HttpRequestInfo& info) = 0;

  virtual const HostMappingRules* GetHostMappingRules() const = 0;

  // Dumps memory allocation stats. |parent_dump_absolute_name| is the name
  // used by the parent MemoryAllocatorDump in the memory dump hierarchy.
  virtual void DumpMemoryStats(
      base::trace_event::ProcessMemoryDump* pmd,
      const std::string& parent_absolute_name) const = 0;

 protected:
  HttpStreamFactory();

 private:
  url::SchemeHostPort RewriteHost(const url::SchemeHostPort& server);

  DISALLOW_COPY_AND_ASSIGN(HttpStreamFactory);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_FACTORY_H_
