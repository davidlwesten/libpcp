#pragma once

#include <pcp/ip.h>
#include <pcp/pcp.h>
#include <pcp/transport.h>

#include <stdint.h>

typedef struct void_or_erorr {
  bool has_value;
  errno_t error;
} void_or_error_t;

typedef struct socket {
  int fd;
  socket_address_t local;
  socket_address_t remote;
  pcp_transport_size_or_error_t (*send)(const struct socket *self,
                                         pcp_transport_const_view_t packet);
  pcp_transport_size_or_error_t (*recv)(const struct socket *self,
                                         pcp_transport_view_t buffer);
  void_or_error_t (*shutdown)(struct socket *self);
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
