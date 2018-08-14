# Life of a URLRequest

This document is intended as an overview of the core layers of the network
stack, their basic responsibilities, how they fit together, and where some of
the pain points are, without going into too much detail. Though it touches a
bit on child processes and the content/loader stack, the focus is on net/
itself.

It's particularly targeted at people new to the Chrome network stack, but
should also be useful for team members who may be experts at some parts of the
stack, but are largely unfamiliar with other components. It starts by walking
through how a basic request issued by another process works its way through the
network stack, and then moves on to discuss how various components plug in.

If you notice any inaccuracies in this document, or feel that things could be
better explained, please do not hesitate to submit patches.

# Anatomy of the Network Stack

The top-level network stack object is the URLRequestContext. The context has
non-owning pointers to everything needed to create and issue a URLRequest. The
context must outlive all requests that use it. Creating a context is a rather
complicated process, and it's recommended that most consumers use
URLRequestContextBuilder to do this.

Chrome has a number of different URLRequestContexts, as there is often a need to
keep cookies, caches, and socket pools separate for different types of requests.
Here are the ones that the network team owns:

* The proxy URLRequestContext, owned by the IOThread and used to get PAC
scripts while avoiding re-entrancy.
* The system URLRequestContext, also owned by the IOThread, used for requests
that aren't associated with a profile.
* Each profile, including incognito profiles, has a number of URLRequestContexts
that are created as needed:
    * The main URLRequestContext is mostly created in ProfileIOData, though it
    has a couple components that are passed in from content's StoragePartition
    code. Several other components are shared with the system URLRequestContext,
    like the HostResolver.
    * Each non-incognito profile also has a media request context, which uses a
    different on-disk cache than the main request context. This prevents a
    single huge media file from evicting everything else in the cache.
    * On desktop platforms, each profile has a request context for extensions.
    * Each profile has two contexts for each isolated app (One for media, one
    for everything else).

The primary use of the URLRequestContext is to create URLRequest objects using
URLRequestContext::CreateRequest(). The URLRequest is the main interface used
by consumers of the network stack.  It is used to make the actual requests to a
server. Each URLRequest tracks a single request across all redirects until an
error occurs, it's canceled, or a final response is received, with a (possibly
empty) body.

The HttpNetworkSession is another major network stack object. It owns the
HttpStreamFactory, the socket pools, and the HTTP/2 and QUIC session pools. It
also has non-owning pointers to the network stack objects that more directly
deal with sockets.

This document does not mention either of these objects much, but at layers
above the HttpStreamFactory, objects often grab their dependencies from the
URLRequestContext, while the HttpStreamFactory and layers below it generally
get their dependencies from the HttpNetworkSession.


# How many "Delegates"?

The network stack informs the embedder of important events for a request using
two main interfaces: the URLRequest::Delegate interface and the NetworkDelegate
interface.

The URLRequest::Delegate interface consists of a small set of callbacks needed
to let the embedder drive a request forward. URLRequest::Delegates generally own
the URLRequest.

The NetworkDelegate is an object pointed to by the URLRequestContext and shared
by all requests, and includes callbacks corresponding to most of the
URLRequest::Delegate's callbacks, as well as an assortment of other methods. The
NetworkDelegate is optional, while the URLRequest::Delegate is not.


# Life of a Simple URLRequest

A request for data is normally dispatched from a child to the browser process.
There a URLRequest is created to drive the request. A protocol-specific job
(e.g. HTTP, data, file) is attached to the request. That job first checks the
cache, and then creates a network connection object, if necessary, to actually
fetch the data. That connection object interacts with network socket pools to
potentially re-use sockets; the socket pools create and connect a socket if
there is no appropriate existing socket. Once that socket exists, the HTTP
request is dispatched, the response read and parsed, and the result returned
back up the stack and sent over to the child process.

Of course, it's not quite that simple :-}.

Consider a simple request issued by a child process. Suppose it's an HTTP
request, the response is uncompressed, no matching entry in the cache, and there
are no idle sockets connected to the server in the socket pool.

Continuing with a "simple" URLRequest, here's a bit more detail on how things
work.

### Request starts in a child process

Summary:

* A user (e.g. the WebURLLoaderImpl for Blink) asks ResourceDispatcher to start
the request.
* ResourceDispatcher sends an IPC to the ResourceDispatcherHost in the
browser process.

Chrome has a single browser process, which handles network requests and tab
management, among other things, and multiple child processes, which are
generally sandboxed so can't send out network requests directly. There are
multiple types of child processes (renderer, GPU, plugin, etc). The renderer
processes are the ones that layout webpages and run HTML.

Each child process has at most one ResourceDispatcher, which is responsible for
all URL request-related communication with the browser process. When something
in another process needs to issue a resource request, it calls into the
ResourceDispatcher to start a request. A RequestPeer is passed in to receive
messages related to the request. When started, the
ResourceDispatcher assigns the request a per-renderer ID, and then sends the
ID, along with all information needed to issue the request, to the
ResourceDispatcherHost in the browser process.

### ResourceDispatcherHost sets up the request in the browser process

Summary:

* ResourceDispatcherHost uses the URLRequestContext to create the URLRequest.
* ResourceDispatcherHost creates a ResourceLoader and a chain of
ResourceHandlers to manage the URLRequest.
* ResourceLoader starts the URLRequest.

The ResourceDispatcherHost (RDH), along with most of the network stack, lives
on the browser process's IO thread. The browser process only has one RDH,
which is responsible for handling all network requests initiated by
ResourceDispatchers in all child processes, not just renderer processes.
Requests initiated in the browser process don't go through the RDH, with some
exceptions.

When the RDH sees the request, it calls into a URLRequestContext to create the
URLRequest. The URLRequestContext has pointers to all the network stack
objects needed to issue the request over the network, such as the cache, cookie
store, and host resolver. The RDH then creates a chain of ResourceHandlers
each of which can monitor/modify/delay/cancel the URLRequest and the
information it returns. The only one of these I'll talk about here is the
AsyncResourceHandler, which is the last ResourceHandler in the chain. The RDH
then creates a ResourceLoader (which is the URLRequest::Delegate), passes
ownership of the URLRequest and the ResourceHandler chain to it, and then starts
the ResourceLoader.

The ResourceLoader checks that none of the ResourceHandlers want to cancel,
modify, or delay the request, and then finally starts the URLRequest.

### Check the cache, request an HttpStream

Summary:

* The URLRequest asks the URLRequestJobFactory to create a URLRequestJob, in
this case, a URLRequestHttpJob.
* The URLRequestHttpJob asks the HttpCache to create an HttpTransaction
(always an HttpCache::Transaction).
* The HttpCache::Transaction sees there's no cache entry for the request,
and creates an HttpNetworkTransaction.
* The HttpNetworkTransaction calls into the HttpStreamFactory to request an
HttpStream.

The URLRequest then calls into the URLRequestJobFactory to create a
URLRequestJob and then starts it. In the case of an HTTP or HTTPS request, this
will be a URLRequestHttpJob. The URLRequestHttpJob attaches cookies to the
request, if needed.

The URLRequestHttpJob calls into the HttpCache to create an
HttpCache::Transaction. If there's no matching entry in the cache, the
HttpCache::Transaction will just call into the HttpNetworkLayer to create an
HttpNetworkTransaction, and transparently wrap it. The HttpNetworkTransaction
then calls into the HttpStreamFactory to request an HttpStream to the server.

### Create an HttpStream

Summary:

* HttpStreamFactory creates an HttpStreamFactoryImpl::Job.
* HttpStreamFactoryImpl::Job calls into the TransportClientSocketPool to
populate an ClientSocketHandle.
* TransportClientSocketPool has no idle sockets, so it creates a
TransportConnectJob and starts it.
* TransportConnectJob creates a StreamSocket and establishes a connection.
* TransportClientSocketPool puts the StreamSocket in the ClientSocketHandle,
and calls into HttpStreamFactoryImpl::Job.
* HttpStreamFactoryImpl::Job creates an HttpBasicStream, which takes
ownership of the ClientSocketHandle.
* It returns the HttpBasicStream to the HttpNetworkTransaction.

The HttpStreamFactoryImpl::Job creates a ClientSocketHandle to hold a socket,
once connected, and passes it into the ClientSocketPoolManager. The
ClientSocketPoolManager assembles the TransportSocketParams needed to
establish the connection and creates a group name ("host:port") used to
identify sockets that can be used interchangeably.

The ClientSocketPoolManager directs the request to the
TransportClientSocketPool, since there's no proxy and it's an HTTP request. The
request is forwarded to the pool's ClientSocketPoolBase<TransportSocketParams>'s
ClientSocketPoolBaseHelper. If there isn't already an idle connection, and there
are available socket slots, the ClientSocketPoolBaseHelper will create a new
TransportConnectJob using the aforementioned params object. This Job will do the
actual DNS lookup by calling into the HostResolverImpl, if needed, and then
finally establishes a connection.

Once the socket is connected, ownership of the socket is passed to the
ClientSocketHandle. The HttpStreamFactoryImpl::Job is then informed the
connection attempt succeeded, and it then creates an HttpBasicStream, which
takes ownership of the ClientSocketHandle. It then passes ownership of the
HttpBasicStream back to the HttpNetworkTransaction.

### Send request and read the response headers

Summary:

* HttpNetworkTransaction gives the request headers to the HttpBasicStream,
and tells it to start the request.
* HttpBasicStream sends the request, and waits for the response.
* The HttpBasicStream sends the response headers back to the
HttpNetworkTransaction.
* The response headers are sent up to the URLRequest, to the ResourceLoader,
and down through the ResourceHandler chain.
* They're then sent by the the last ResourceHandler in the chain (the
AsyncResourceHandler) to the ResourceDispatcher, with an IPC.

The HttpNetworkTransaction passes the request headers to the HttpBasicStream,
which uses an HttpStreamParser to (finally) format the request headers and body
(if present) and send them to the server.

The HttpStreamParser waits to receive the response and then parses the HTTP/1.x
response headers, and then passes them up through both the
HttpNetworkTransaction and HttpCache::Transaction to the URLRequestHttpJob. The
URLRequestHttpJob saves any cookies, if needed, and then passes the headers up
to the URLRequest and on to the ResourceLoader.

The ResourceLoader passes them through the chain of ResourceHandlers, and then
they make their way to the AsyncResourceHandler. The AsyncResourceHandler uses
the renderer process ID ("child ID") to figure out which process the request
was associated with, and then sends the headers along with the request ID to
that process's ResourceDispatcher. The ResourceDispatcher uses the ID to
figure out which RequestPeer the headers should be sent to, which
sends them on to the RequestPeer.

### Response body is read

Summary:

* AsyncResourceHandler allocates a 512k ring buffer of shared memory to read
the body of the request.
* AsyncResourceHandler tells the ResourceLoader to read the response body to
the buffer, 32kB at a time.
* AsyncResourceHandler informs the ResourceDispatcher of each read using
cross-process IPCs.
* ResourceDispatcher tells the AsyncResourceHandler when it's done with the
data with each read, so it knows when parts of the buffer can be reused.

Without waiting to hear back from the ResourceDispatcher, the ResourceLoader
tells its ResourceHandler chain to allocate memory to receive the response
body. The AsyncResourceHandler creates a 512KB ring buffer of shared memory,
and then passes the first 32KB of it to the ResourceLoader for the first read.
The ResourceLoader then passes a 32KB body read request down through the
URLRequest all the way down to the HttpStreamParser. Once some data is read,
possibly less than 32KB, the number of bytes read makes its way back to the
AsyncResourceHandler, which passes the shared memory buffer and the offset and
amount of data read to the renderer process.

The AsyncResourceHandler relies on ACKs from the renderer to prevent it from
overwriting data that the renderer has yet to consume. This process repeats
until the response body is completely read.

### URLRequest is destroyed

Summary:

* When complete, the RDH deletes the ResourceLoader, which deletes the
URLRequest and the ResourceHandler chain.
* During destruction, the HttpNetworkTransaction determines if the socket is
reusable, and if so, tells the HttpBasicStream to return it to the socket pool.

When the URLRequest informs the ResourceLoader it's complete, the
ResourceLoader tells the ResourceHandlers, and the AsyncResourceHandler tells
the ResourceDispatcher the request is complete. The RDH then deletes
ResourceLoader, which deletes the URLRequest and ResourceHandler chain.

When the HttpNetworkTransaction is being torn down, it figures out if the
socket is reusable. If not, it tells the HttpBasicStream to close the socket.
Either way, the ClientSocketHandle returns the socket is then returned to the
socket pool, either for reuse or so the socket pool knows it has another free
socket slot.

### Object Relationships and Ownership

A sample of the object relationships involved in the above process is
diagramed here: 

![Object Relationship Diagram for URLRequest lifetime](url_request.svg)

There are a couple of points in the above diagram that do not come
clear visually:

* The method that generates the filter chain that is hung off the
  URLRequestJob is declared on URLRequestJob, but the only current
  implementation of it is on URLRequestHttpJob, so the generation is
  shown as happening from that class.
* HttpTransactions of different types are layered; i.e. a
  HttpCache::Transaction contains a pointer to an HttpTransaction, but
  that pointed-to HttpTransaction generally is an
  HttpNetworkTransaction. 

# Additional Topics

## HTTP Cache

The HttpCache::Transaction sits between the URLRequestHttpJob and the
HttpNetworkTransaction, and implements the HttpTransaction interface, just like
the HttpNetworkTransaction. The HttpCache::Transaction checks if a request can
be served out of the cache. If a request needs to be revalidated, it handles
sending a 204 revalidation request over the network. It may also break a range
request into multiple cached and non-cached contiguous chunks, and may issue
multiple network requests for a single range URLRequest.

The HttpCache::Transaction uses one of three disk_cache::Backends to actually
store the cache's index and files: The in memory backend, the blockfile cache
backend, and the simple cache backend. The first is used in incognito. The
latter two are both stored on disk, and are used on different platforms.

One important detail is that it has a read/write lock for each URL. The lock
technically allows multiple reads at once, but since an HttpCache::Transaction
always grabs the lock for writing and reading before downgrading it to a read
only lock, all requests for the same URL are effectively done serially. The
renderer process merges requests for the same URL in many cases, which mitigates
this problem to some extent.

It's also worth noting that each renderer process also has its own in-memory
cache, which has no relation to the cache implemented in net/, which lives in
the browser process.

## Cancellation

A request can be cancelled by the child process, by any of the
ResourceHandlers in the chain, or by the ResourceDispatcherHost itself. When the
cancellation message reaches the URLRequest, it passes on the fact it's been
cancelled back to the ResourceLoader, which then sends the message down the
ResourceHandler chain.

When an HttpNetworkTransaction for a cancelled request is being torn down, it
figures out if the socket the HttpStream owns can potentially be reused, based
on the protocol (HTTP / HTTP/2 / QUIC) and any received headers. If the socket
potentially can be reused, an HttpResponseBodyDrainer is created to try and
read any remaining body bytes of the HttpStream, if any, before returning the
socket to the SocketPool. If this takes too long, or there's an error, the
socket is closed instead. Since this all happens at the layer below the cache,
any drained bytes are not written to the cache, and as far as the cache layer is
concerned, it only has a partial response.

## Redirects

The URLRequestHttpJob checks if headers indicate a redirect when it receives
them from the next layer down (Typically the HttpCache::Transaction). If they
indicate a redirect, it tells the cache the response is complete, ignoring the
body, so the cache only has the headers. The cache then treats it as a complete
entry, even if the headers indicated there will be a body.

The URLRequestHttpJob then checks with the URLRequest if the redirect should be
followed. The URLRequest then informs the ResourceLoader about the redirect, to
give it a chance to cancel the request. The information makes its way down
through the AsyncResourceHandler into the other process, via the
ResourceDispatcher. Whatever issued the original request then checks if the
redirect should be followed.

The ResourceDispatcher then asynchronously sends a message back to either
follow the redirect or cancel the request. In either case, the old
HttpTransaction is destroyed, and the HttpNetworkTransaction attempts to drain
the socket for reuse, just as in the cancellation case. If the redirect is
followed, the URLRequest calls into the URLRequestJobFactory to create a new
URLRequestJob, and then starts it.

## Filters (gzip, deflate, brotli, etc)

When the URLRequestHttpJob receives headers, it sends a list of all
Content-Encoding values to Filter::Factory, which creates a (possibly empty)
chain of filters. As body bytes are received, they're passed through the
filters at the URLRequestJob layer and the decoded bytes are passed back to the
URLRequest::Delegate.

Since this is done above the cache layer, the cache stores the responses prior
to decompression. As a result, if files aren't compressed over the wire, they
aren't compressed in the cache, either.

## Socket Pools

The ClientSocketPoolManager is responsible for assembling the parameters needed
to connect a socket, and then sending the request to the right socket pool.
Each socket request sent to a socket pool comes with a socket params object, a
ClientSocketHandle, and a "group name". The params object contains all the
information a ConnectJob needs to create a connection of a given type, and
different types of socket pools take different params types. The
ClientSocketHandle will take temporary ownership of a connected socket and
return it to the socket pool when done. All connections with the same group name
in the same pool can be used to service the same connection requests, so it
consists of host, port, protocol, and whether "privacy mode" is enabled for
sockets in the goup.

All socket pool classes derive from the ClientSocketPoolBase<SocketParamType>.
The ClientSocketPoolBase handles managing sockets - which requests to create
sockets for, which requests get connected sockets first, which sockets belong
to which groups, connection limits per group, keeping track of and closing idle
sockets, etc. Each ClientSocketPoolBase subclass has its own ConnectJob type,
which establishes a connection using the socket params, before the pool hands
out the connected socket.

### Socket Pool Layering

Some socket pools are layered on top other socket pools. This is done when a
"socket" in a higher layer needs to establish a connection in a lower level
pool and then take ownership of it as part of its connection process. For
example, each socket in the SSLClientSocketPool is layered on top of a socket
in the TransportClientSocketPool. There are a couple additional complexities
here.

From the perspective of the lower layer pool, all of its sockets that a higher
layer pools owns are actively in use, even when the higher layer pool considers
them idle. As a result, when a lower layer pool is at its connection limit and
needs to make a new connection, it will ask any higher layer pools to close an
idle connection if they have one, so it can make a new connection.

Since sockets in the higher layer pool are also in a group in the lower layer
pool, they must have their own distinct group name. This is needed so that, for
instance, SSL and HTTP connections won't be grouped together in the
TcpClientSocketPool, which the SSLClientSocketPool sits on top of.

### Socket Pool Class Relationships

The relationships between the important classes in the socket pools is
shown diagrammatically for the lowest layer socket pool
(TransportSocketPool) below. 

![Object Relationship Diagram for Socket Pools](pools.svg)

The ClientSocketPoolBase is a template class templatized on the class
containing the parameters for the appropriate type of socket (in this
case TransportSocketParams).  It contains a pointer to the
ClientSocketPoolBaseHelper, which contains all the type-independent
machinery of the socket pool.  

When socket pools are initialized, they in turn initialize their
templatized ClientSocketPoolBase member with an object with which it
should create connect jobs.  That object must derive from
ClientSocketPoolBase::ConnectJobFactory templatized by the same type
as the ClientSocketPoolBase.  (In the case of the diagram above, that
object is a TransportConnectJobFactory, which derives from
ClientSocketPoolBase::ConnectJobFactory&lt;TransportSocketParams&gt;.)
Internally, that object is wrapped in a type-unsafe wrapper
(ClientSocketPoolBase::ConnectJobFactoryAdaptor) so that it can be
passed to the initialization of the ClientSocketPoolBaseHelper.  This
allows the helper to create connect jobs while preserving a type-safe
API to the initialization of the socket pool.

### SSL

When an SSL connection is needed, the ClientSocketPoolManager assembles the
parameters needed both to connect the TCP socket and establish an SSL
connection. It then passes them to the SSLClientSocketPool, which creates
an SSLConnectJob using them. The SSLConnectJob's first step is to call into the
TransportSocketPool to establish a TCP connection.

Once a connection is established by the lower layered pool, the SSLConnectJob
then starts SSL negotiation. Once that's done, the SSL socket is passed back to
the HttpStreamFactoryImpl::Job that initiated the request, and things proceed
just as with HTTP. When complete, the socket is returned to the
SSLClientSocketPool.

## Proxies

Each proxy has its own completely independent set of socket pools. They have
their own exclusive TransportSocketPool, their own protocol-specific pool above
it, and their own SSLSocketPool above that. HTTPS proxies also have a second
SSLSocketPool between the the HttpProxyClientSocketPool and the
TransportSocketPool, since they can talk SSL to both the proxy and the
destination server, layered on top of each other.

The first step the HttpStreamFactoryImpl::Job performs, just before calling
into the ClientSocketPoolManager to create a socket, is to pass the URL to the
Proxy service to get an ordered list of proxies (if any) that should be tried
for that URL.  Then when the ClientSocketPoolManager tries to get a socket for
the Job, it uses that list of proxies to direct the request to the right socket
pool.

## Alternate Protocols

### HTTP/2 (Formerly SPDY)

HTTP/2 negotation is performed as part of the SSL handshake, so when
HttpStreamFactoryImpl::Job gets a socket, it may have HTTP/2 negotiated over it
as well. When it gets a socket with HTTP/2 negotiated as well, the Job creates a
SpdySession using the socket and a SpdyHttpStream on top of the SpdySession.
The SpdyHttpStream will be passed to the HttpNetworkTransaction, which drives
the stream as usual.

The SpdySession will be shared with other Jobs connecting to the same server,
and future Jobs will find the SpdySession before they try to create a
connection. HttpServerProperties also tracks which servers supported HTTP/2 when
we last talked to them. We only try to establish a single connection to servers
we think speak HTTP/2 when multiple HttpStreamFactoryImpl::Jobs are trying to
connect to them, to avoid wasting resources.

### QUIC

QUIC works quite a bit differently from HTTP/2. Servers advertise QUIC support
with an "Alternate-Protocol" HTTP header in their responses.
HttpServerProperties then tracks servers that have advertised QUIC support.

When a new request comes in to HttpStreamFactoryImpl for a connection to a
server that has advertised QUIC support in the past, it will create a second
HttpStreamFactoryImpl::Job for QUIC, which returns an QuicHttpStream on success.
The two Jobs (One for QUIC, one for all versions of HTTP) will be raced against
each other, and whichever successfully creates an HttpStream first will be used.

As with HTTP/2, once a QUIC connection is established, it will be shared with
other Jobs connecting to the same server, and future Jobs will just reuse the
existing QUIC session.

## Prioritization

URLRequests are assigned a priority on creation. It only comes into play in
a couple places:

* The ResourceScheduler lives outside net/, and in some cases, delays starting
low priority requests on a per-tab basis.
* DNS lookups are initiated based on the highest priority request for a lookup.
* Socket pools hand out and create sockets based on prioritization. However,
when a socket becomes idle, it will be assigned to the highest priority request
for the server its connected to, even if there's a higher priority request to
another server that's waiting on a free socket slot.
* HTTP/2 and QUIC both support sending priorities over-the-wire.

At the socket pool layer, sockets are only assigned to socket requests once the
socket is connected and SSL is negotiated, if needed. This is done so that if
a higher priority request for a group reaches the socket pool before a
connection is established, the first usable connection goes to the highest
priority socket request.

## Non-HTTP Schemes

The URLRequestJobFactory has a ProtocolHander for each supported scheme.
Non-HTTP URLRequests have their own ProtocolHandlers. Some are implemented in
net/, (like FTP, file, and data, though the renderer handles some data URLs
internally), and others are implemented in content/ or chrome (like blob,
chrome, and chrome-extension).
