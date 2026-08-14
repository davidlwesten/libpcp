#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>

#include <stdint.h>

#include <errno.h>

typedef typeof(errno) errno_t;

typedef struct size_or_error {
  bool has_value;
  union {
    size_t value;
    errno_t error;
  };
} size_or_error_t;

typedef struct void_or_erorr {
  bool has_value;
  errno_t error;
} void_or_error_t;

typedef struct socket {
  int fd;
  socket_address_t local;
  socket_address_t remote;
  size_or_error_t (*send)(const struct socket *self,
                          size_t size,
                          const uint8_t src[size]);
  size_or_error_t (*recv)(const struct socket *self,
                          size_t size,
                          uint8_t dst[size]);
  void_or_error_t (*close)(struct socket *self);
} socket_t;

typedef struct socket_or_error {
  bool has_value;
  union {
    socket_t value;
    errno_t error;
  };
} socket_or_error_t;

socket_or_error_t socket_connect(const char *node, const char *service);
