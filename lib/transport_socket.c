#include "socket.h"
#include "transport.h"

#include <asm-generic/errno.h>
#include <malloc.h>

typedef struct transport_socket {
  bool closed;
  socket_t socket;
  on_transport_data_t recv;
} transport_socket_t;

static void transport_socket_send(const transport_t *self,
                                  size_t size,
                                  const uint8_t data[size],
                                  void (*callback)(size_or_error_t result)) {
  auto const ctx = (const transport_socket_t *)self->ctx;
  auto const result = CALL(&ctx->socket, send, size, data);
  if (callback) {
    callback(result);
  }
}

static void transport_socket_run(transport_t const *self) {
  auto const ctx = (transport_socket_t *)self->ctx;

  static constexpr size_t MTU = 1500;
  uint8_t data[MTU] = {0};

  while (!ctx->closed) {
    auto const size = CALL(&ctx->socket, recv, sizeof(data), data);
    if (!size.has_value) {
      if (size.error == EAGAIN || size.error == EWOULDBLOCK ||
          size.error == EINTR) {
        continue;
      }
      break;
    }

    if (ctx->recv.callback) {
      ctx->recv.callback(self, ctx->recv.ctx,
                         (const_view_t){.size = size.value, .data = data});
    }
  }
}

void transport_socket_close(transport_t const *self) {
  auto const ctx = (transport_socket_t *)self->ctx;
  ctx->closed = true;
  CALL(&ctx->socket, close);
}

transport_or_error_t transport_connect(const char *server,
                                       on_transport_data_t callback) {

  auto socket = socket_connect(server, "pcp");
  if (!socket.has_value) {
    return (transport_or_error_t){.has_value = false, .error = socket.error};
  }

  auto const ctx = (transport_socket_t *)malloc(sizeof(transport_socket_t));
  if (!ctx) {
    CALL(&socket.value, close);
    return (transport_or_error_t){.has_value = false, .error = ENOMEM};
  }

  (*ctx) = (transport_socket_t){.closed = false,
                                .socket = socket.value,
                                .recv = callback};

  return (transport_or_error_t){.has_value = true,
                                .value = {.local = socket.value.local,
                                          .remote = socket.value.remote,
                                          .send = transport_socket_send,
                                          .run = transport_socket_run,
                                          .close = transport_socket_close,
                                          .ctx = ctx}};
}
