#include "socket.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static errno_t gai_error_to_errno(int gai_error) {
  switch (gai_error) {
#if __USE_GNU
  case EAI_ADDRFAMILY:
    return EAFNOSUPPORT;
#endif
  case EAI_AGAIN:
    return EAGAIN;
  case EAI_BADFLAGS:
    return EINVAL;
  case EAI_FAIL:
    return EFAULT;
  case EAI_FAMILY:
    return EAFNOSUPPORT;
  case EAI_MEMORY:
    return ENOMEM;
#if __USE_GNU
  case EAI_NODATA:
    return ENODATA;
#endif
  case EAI_NONAME:
    return ENONET;
  case EAI_SERVICE:
    return ENOMEDIUM;
  case EAI_SOCKTYPE:
    return ENOPROTOOPT;
  case EAI_SYSTEM:
    return errno;
  default:
    fprintf(stderr, "unexpected getaddrinfo error: %d\n", gai_error);
    abort();
  }
}

static size_or_error_t
socket_send(const socket_t *self, size_t size, const uint8_t src[size]) {
  assert(self);
  auto const result = send(self->fd, src, size, 0);
  if (-1 == result) {
    return (size_or_error_t){.has_value = false, .error = errno};
  }

  return (size_or_error_t){.has_value = true, .value = (size_t)result};
}

static size_or_error_t
socket_recv(const socket_t *self, size_t size, uint8_t dst[size]) {
  assert(self);
  auto const result = recv(self->fd, dst, size, 0);
  if (-1 == result) {
    return (size_or_error_t){.has_value = false, .error = errno};
  }

  return (size_or_error_t){.has_value = true, .value = (size_t)result};
}

static void_or_error_t socket_close(socket_t *self) {
  assert(self);
  if (-1 == close(self->fd)) {
    return (void_or_error_t){.has_value = false, .error = errno};
  }

  *self = (socket_t){.fd = -1,
                     .local = {},
                     .remote = {},
                     .send = socket_send,
                     .recv = socket_recv,
                     .close = socket_close};

  return (void_or_error_t){.has_value = true};
}

static socket_or_error_t socket_create(int fd) {
  struct sockaddr_storage addr = {0};
  socklen_t addrlen = sizeof(addr);

  if (-1 == getsockname(fd, (struct sockaddr *)&addr, &addrlen)) {
    return (socket_or_error_t){.has_value = false, .error = errno};
  }

  if (!(addr.ss_family == AF_INET || addr.ss_family == AF_INET6)) {
    return (socket_or_error_t){.has_value = false, .error = EAFNOSUPPORT};
  }

  const socket_address_t local =
      (addr.ss_family == AF_INET)
          ? socket_address_from_sockaddr_in((const struct sockaddr_in *)&addr)
          : socket_address_from_sockaddr_in6(
                (const struct sockaddr_in6 *)&addr);

  memset(&addr, 0, sizeof(addr));
  addrlen = sizeof(addr);

  if (-1 == getpeername(fd, (struct sockaddr *)&addr, &addrlen)) {
    return (socket_or_error_t){.has_value = false, .error = errno};
  }

  if (!(addr.ss_family == AF_INET || addr.ss_family == AF_INET6)) {
    return (socket_or_error_t){.has_value = false, .error = EAFNOSUPPORT};
  }

  const socket_address_t remote =
      (addr.ss_family == AF_INET)
          ? socket_address_from_sockaddr_in((const struct sockaddr_in *)&addr)
          : socket_address_from_sockaddr_in6(
                (const struct sockaddr_in6 *)&addr);

  return (socket_or_error_t){.has_value = true,
                             .value = {.fd = fd,
                                       .local = local,
                                       .remote = remote,
                                       .send = socket_send,
                                       .recv = socket_recv,
                                       .close = socket_close}};
}

socket_or_error_t socket_connect(const char *node, const char *service) {
  const int fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (-1 == fd) {
    return (socket_or_error_t){.has_value = false, .error = errno};
  }

  const int v6only = false;
  if (-1 == setsockopt(fd, IPPROTO_IP, IPV6_V6ONLY, &v6only, sizeof(v6only))) {
    return (socket_or_error_t){.has_value = false, .error = errno};
  }

  struct addrinfo hints = {0};
  hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL | AI_NUMERICHOST;
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  struct addrinfo *ai = nullptr;
  const int gai_result = getaddrinfo(node, service, &hints, &ai);
  if (0 != gai_result) {
    return (socket_or_error_t){.has_value = false,
                               .error = gai_error_to_errno(gai_result)};
  }

  for (auto iter = ai; iter; iter = iter->ai_next) {
    if (0 == connect(fd, iter->ai_addr, iter->ai_addrlen)) {
      freeaddrinfo(ai);
      return socket_create(fd);
    }
  }

  freeaddrinfo(ai);
  return (socket_or_error_t){.has_value = false, .error = EHOSTUNREACH};
}
