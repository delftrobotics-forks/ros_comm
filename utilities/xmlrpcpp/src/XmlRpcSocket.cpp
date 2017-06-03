// this file modified by Morgan Quigley on 22 Apr 2008.
// added features: server can be opened on port 0 and you can read back
// what port the OS gave you

#include "xmlrpcpp/XmlRpcSocket.h"
#include "xmlrpcpp/XmlRpcUtil.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef MAKEDEPEND

#if defined(_WINDOWS)
# include <winsock2.h>
# include <ws2tcpip.h>
//# pragma lib(WS2_32.lib)

// MS VS10 actually has these definitions (as opposed to earlier versions),
// so if present, temporarily disable them and reset to WSA codes for this file only.
#ifdef EAGAIN
  #undef EAGAIN
#endif
#ifdef EINTR
  #undef EINTR
#endif
#ifdef EINPROGRESS
  #undef EINPROGRESS
#endif
#ifdef  EWOULDBLOCK
  #undef EWOULDBLOCK
#endif
#ifdef ETIMEDOUT
  #undef ETIMEDOUT
#endif
# define EAGAIN		WSATRY_AGAIN
# define EINTR			WSAEINTR
# define EINPROGRESS	WSAEINPROGRESS
# define EWOULDBLOCK	WSAEWOULDBLOCK
# define ETIMEDOUT	    WSAETIMEDOUT
#else
extern "C" {
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
# include <fcntl.h>
# include <arpa/inet.h>
}
#endif  // _WINDOWS

#endif // MAKEDEPEND

// MSG_NOSIGNAL does not exists on OS X
#if defined(__APPLE__) || defined(__MACH__)
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif


using namespace XmlRpc;


#if defined(_WINDOWS)

static void initWinSock()
{
  static bool wsInit = false;
  if (! wsInit)
  {
    WORD wVersionRequested = MAKEWORD( 2, 0 );
    WSADATA wsaData;
    WSAStartup(wVersionRequested, &wsaData);
    wsInit = true;
  }
}

#else

#define initWinSock()

#endif // _WINDOWS


// These errors are not considered fatal for an IO operation; the operation will be re-tried.
static inline bool
nonFatalError()
{
  int err = XmlRpcSocket::getError();
  return (err == EINPROGRESS || err == EAGAIN || err == EWOULDBLOCK || err == EINTR);
}

int
XmlRpcSocket::socket(bool ipv6)
{
  initWinSock();
  return (int) ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
}


void
XmlRpcSocket::close(int fd)
{
  XmlRpcUtil::log(4, "XmlRpcSocket::close: fd %d.", fd);
#if defined(_WINDOWS)
  closesocket(fd);
#else
  ::close(fd);
#endif // _WINDOWS
}




bool
XmlRpcSocket::setNonBlocking(int fd)
{
#if defined(_WINDOWS)
  unsigned long flag = 1;
  return (ioctlsocket((SOCKET)fd, FIONBIO, &flag) == 0);
#else
  return (fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
#endif // _WINDOWS
}


bool
XmlRpcSocket::setReuseAddr(int fd)
{
  // Allow this port to be re-bound immediately so server re-starts are not delayed
  int sflag = 1;
  return (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&sflag, sizeof(sflag)) == 0);
}


// Bind to a specified port
bool
XmlRpcSocket::bind(int fd, int port, bool ipv6)
{
  sockaddr_storage ss = {};

  if (ipv6)
  {
    sockaddr_in6 *address = (sockaddr_in6 *)&ss;
    address->sin6_family = AF_INET6;
    address->sin6_addr = in6addr_any;
    address->sin6_port = htons((u_short) port);
  }
  else
  {
    sockaddr_in *address = (sockaddr_in *)&ss;
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_ANY);
    address->sin_port = htons((u_short) port);
  }

  return ::bind(fd, (sockaddr*)&ss, sizeof(sockaddr_storage)) == 0;
}


// Set socket in listen mode
bool
XmlRpcSocket::listen(int fd, int backlog)
{
  return (::listen(fd, backlog) == 0);
}


int
XmlRpcSocket::accept(int fd)
{
  struct sockaddr_in addr;
#if defined(_WINDOWS)
  int
#else
  socklen_t
#endif
    addrlen = sizeof(addr);
  // accept will truncate the address if the buffer is too small.
  // As we are not using it, no special case for IPv6
  // has to be made.
  return (int) ::accept(fd, (struct sockaddr*)&addr, &addrlen);
}

namespace {
  sa_family_t resolveTypeToFamilyHint(XmlRpcSocket::ResolveType type)
  {
    switch (type) {
      case XmlRpcSocket::resolve_both: return AF_UNSPEC;
      case XmlRpcSocket::resolve_ipv4: return AF_INET;
      case XmlRpcSocket::resolve_ipv6: return AF_INET6;
    }
    return AF_UNSPEC;
  }

  std::vector<sockaddr_storage>
  resolve(const std::string & host, XmlRpcSocket::ResolveType type)
  {
    std::vector<sockaddr_storage> result;

    addrinfo hints = {};
    hints.ai_family = resolveTypeToFamilyHint(type);

    addrinfo* addr;
    if (getaddrinfo(host.c_str(), NULL, &hints, &addr) != 0 || !addr) {
      fprintf(stderr, "Couldn't find an address for [%s]\n", host.c_str());
      return result;
    }

    for (addrinfo* it = addr; it; it = it->ai_next) {
      char buf[128];
      // TODO IPV6: check if this also works under Windows
      XmlRpcUtil::log(5, "found host as %s\n", inet_ntop(it->ai_family, (void*)it->ai_addr, buf, sizeof(buf)));

      sockaddr_storage storage = {};
      memcpy(&storage, it->ai_addr, it->ai_addrlen);
      result.push_back(storage);
    }

    freeaddrinfo(addr);

    return result;
  }
}

// Connect a socket to a server (from a client)
bool
XmlRpcSocket::connect(int fd, const std::string& host, int port, bool ipv6)
{
  std::vector<sockaddr_storage> resolved = resolve(
    host,
    ipv6 ? resolve_ipv6 : resolve_ipv4
  );

  // For asynch operation, this will return EWOULDBLOCK (windows) or
  // EINPROGRESS (linux) and we just need to wait for the socket to be writable...
  for (std::size_t i = 0; i < resolved.size(); ++i) {
    int result = ::connect(fd, (sockaddr*)&resolved[i], sizeof(sockaddr_storage));
    if (result == 0)
      return true;

    char buf[128];
    inet_ntop(resolved[i].ss_family, (void*)&resolved[i], buf, sizeof(buf));
    XmlRpcUtil::log(5, "failed to connect to %s:%d (%s)\n", buf, port, getErrorMsg().c_str());
  }

  return false;
}

int
XmlRpcSocket::connect(const std::string& host, int port, ResolveType resolve_type)
{
  std::vector<sockaddr_storage> resolved = resolve(host, resolve_type);

  for (std::size_t i = 0; i < resolved.size(); ++i) {
    int fd = socket(resolved[i].ss_family == AF_INET6);
    if (fd < 0) {
      XmlRpcUtil::log(5, "Could not create socket (%s).", XmlRpcSocket::getErrorMsg().c_str());
      continue;
    }

    int result = ::connect(fd, (sockaddr*)&resolved[i], sizeof(sockaddr_storage));
    if (result == 0)
      return fd;

    char buf[128];
    inet_ntop(resolved[i].ss_family, (void*)&resolved[i], buf, sizeof(buf));
    XmlRpcUtil::log(5, "failed to connect to %s:%d (%s)\n", buf, port, getErrorMsg().c_str());
    close(fd);
  }

  return -1;
}


// Read available text from the specified socket. Returns false on error.
bool
XmlRpcSocket::nbRead(int fd, std::string& s, bool *eof)
{
  const int READ_SIZE = 4096;   // Number of bytes to attempt to read at a time
  char readBuf[READ_SIZE];

  bool wouldBlock = false;
  *eof = false;

  while ( ! wouldBlock && ! *eof) {
#if defined(_WINDOWS)
    int n = recv(fd, readBuf, READ_SIZE-1, 0);
#else
    int n = read(fd, readBuf, READ_SIZE-1);
#endif
    XmlRpcUtil::log(5, "XmlRpcSocket::nbRead: read/recv returned %d.", n);

    if (n > 0) {
      readBuf[n] = 0;
      s.append(readBuf, n);
    } else if (n == 0) {
      *eof = true;
    } else if (nonFatalError()) {
      wouldBlock = true;
    } else {
      return false;   // Error
    }
  }
  return true;
}


// Write text to the specified socket. Returns false on error.
bool
XmlRpcSocket::nbWrite(int fd, std::string& s, int *bytesSoFar)
{
  int nToWrite = int(s.length()) - *bytesSoFar;
  char *sp = const_cast<char*>(s.c_str()) + *bytesSoFar;
  bool wouldBlock = false;

  while ( nToWrite > 0 && ! wouldBlock ) {
#if defined(_WINDOWS)
    int n = send(fd, sp, nToWrite, 0);
#else
    int n = write(fd, sp, nToWrite);
#endif
    XmlRpcUtil::log(5, "XmlRpcSocket::nbWrite: send/write returned %d.", n);

    if (n > 0) {
      sp += n;
      *bytesSoFar += n;
      nToWrite -= n;
    } else if (nonFatalError()) {
      wouldBlock = true;
    } else {
      return false;   // Error
    }
  }
  return true;
}


// Returns last errno
int
XmlRpcSocket::getError()
{
#if defined(_WINDOWS)
  return WSAGetLastError();
#else
  return errno;
#endif
}


// Returns message corresponding to last errno
std::string
XmlRpcSocket::getErrorMsg()
{
  return getErrorMsg(getError());
}

// Returns message corresponding to errno... well, it should anyway
std::string
XmlRpcSocket::getErrorMsg(int error)
{
  char err[60];
#ifdef _MSC_VER
  strerror_s(err,60,error);
#else
  snprintf(err,sizeof(err),"%s",strerror(error));
#endif
  return std::string(err);
}

int XmlRpcSocket::get_port(int socket)
{
  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  getsockname(socket, (sockaddr *)&ss, &ss_len);

  sockaddr_in *sin = (sockaddr_in *)&ss;
  sockaddr_in6 *sin6 = (sockaddr_in6 *)&ss;
  
  switch (ss.ss_family)
  {
    case AF_INET:
      return ntohs(sin->sin_port);
    case AF_INET6:
      return ntohs(sin6->sin6_port);
  }  
  return 0;
}

