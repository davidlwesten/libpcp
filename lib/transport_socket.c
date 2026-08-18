#include "socket.h"
#include "transport.h"

#include <asm-generic/errno.h>
#include <malloc.h>

typedef struct transport_socket {
  socket_t socket;
  transport_on_data_t recv;
} transport_socket_t;

static void transport_socket_send(const transport_t *self,
                                  size_t size,
                                  const uint8_t data[size],
                                  transport_on_send_t callback) {
  auto const ctx = (const transport_socket_t *)self->ctx;
  auto const result = CALL(&ctx->socket, send, size, data);
  if (callback.fn) {
    callback.fn(self, callback.ctx, result);
  }
}

static void transport_socket_run(transport_t const *self) {
  auto const ctx = (transport_socket_t *)self->ctx;

  static constexpr size_t MTU = 1500;
  uint8_t data[MTU] = {0};

  for (;;) {
    auto const size = CALL(&ctx->socket, recv, sizeof(data), data);
    if (!size.has_value) {
      if (size.error == EAGAIN || size.error == EWOULDBLOCK ||
          size.error == EINTR) {
        continue;
      }
      break;
    } else if (size.value == 0) {
      // socket was closed
      break;
    } else if (ctx->recv.fn) {
      ctx->recv.fn(self, ctx->recv.ctx,
                   (const_view_t){.size = size.value, .data = data});
    }
  }
}

static void transport_socket_stop(transport_t const *self) {
  auto const ctx = (transport_socket_t *)self->ctx;
  CALL(&ctx->socket, shutdown);
}

static void transport_socket_close(transport_t const *self) {
  auto const ctx = (transport_socket_t *)self->ctx;
  CALL(&ctx->socket, close);
}

transport_or_error_t transport_connect(const char *server,
                                       transport_on_data_t callback) {

  auto socket = socket_connect(server, "pcp");
  if (!socket.has_value) {
    return (transport_or_error_t){.has_value = false, .error = socket.error};
  }

  auto const ctx = (transport_socket_t *)malloc(sizeof(transport_socket_t));
  if (!ctx) {
    CALL(&socket.value, close);
    return (transport_or_error_t){.has_value = false, .error = ENOMEM};
  }

  (*ctx) = (transport_socket_t){.socket = socket.value, .recv = callback};

  return (transport_or_error_t){.has_value = true,
                                .value = {.local = socket.value.local,
                                          .remote = socket.value.remote,
                                          .ctx = ctx,
                                          .send = transport_socket_send,
                                          .run = transport_socket_run,
                                          .stop = transport_socket_stop,
                                          .close = transport_socket_close}};
}
