#include "socket.h"

#include "array.h"
#include "packet.h"
#include "transport.h"

#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <math.h>

#include <sys/select.h>

typedef struct transport_base {
  transport_t public;
  transport_on_recv_t recv;
  void *userdata;
} transport_base_t;

typedef struct transport_socket_timer {
  uint32_t id;
  double expiration;
  transport_on_timer_t action;
} transport_socket_timer_t;

typedef struct transport_socket {
  transport_base_t base;
  socket_t socket;
  transport_socket_timer_t *timers;
} transport_socket_t;

static void send_(transport_socket_t const *self,
                  const_view_t src,
                  transport_on_send_t closure) {
  auto const result = CALL(&self->socket, send, src);
  if (closure.fn) {
    closure.fn(self->base.userdata, closure.userdata, result);
  }
}

static int min_timer_(transport_socket_timer_t const *lhs,
                      transport_socket_timer_t const *rhs) {
  return (int)(lhs->expiration - rhs->expiration);
}

static void transport_remove_timer(transport_socket_t *transport,
                                   transport_timer_t id) {
  assert(transport);

  auto const begin = transport->timers;
  auto const end = begin + array_size(transport->timers);
  assert(begin != end);
  for (auto iter = begin; iter != end; ++iter) {
    if (iter->id == id) {
      transport_socket_timer_t timer;
      array_swap(transport->timers, iter, end - 1);
      array_pop_back(transport->timers, &timer);
      array_heapify(transport->timers, (heap_compare_t)min_timer_);
      return;
    }
  }
}

static transport_timer_t transport_add_timer(transport_socket_t *self,
                                             transport_timeout_t ttl,
                                             transport_on_timer_t action) {
  assert(self);
  assert(ttl.seconds > 0);

  static uint32_t NEXT_ID = 0;
  transport_socket_timer_t const timer = {.id = NEXT_ID++,
                                          .expiration = (double)time(nullptr) +
                                                        ttl.seconds,
                                          .action = action};
  array_heap_push(self->timers, timer, (heap_compare_t)min_timer_);
  return timer.id;
}

static void transport_run(transport_socket_t *self) {
  uint8_t data[PCP_PACKET_MAX_SIZE] = {0};

  for (;;) {
    fd_set readfds = {0};
    FD_SET(self->socket.fd, &readfds);

    double now = (double)time(nullptr);

    struct timeval timeval = {0};
    struct timeval *timeout = nullptr;
    if (array_size(self->timers)) {
      double const delta = fmax(0.0, self->timers->expiration - now);
      timeval.tv_sec = (time_t)delta;
      timeval.tv_usec =
          (suseconds_t)((delta - (double)timeval.tv_sec) * 1000000.0);
      timeout = &timeval;
    }

    int const result =
        select(self->socket.fd + 1, &readfds, nullptr, nullptr, timeout);

    switch (result) {
    case -1:
      assert(errno == EINTR);
    case 0: // timeout
      now = (double)time(nullptr);

      while (array_size(self->timers) &&
             isless(self->timers->expiration, now)) {
        transport_socket_timer_t timer;
        array_heap_pop(self->timers, &timer, (heap_compare_t)min_timer_);
        timer.action.fn(self->base.userdata, timer.action.userdata);
      }
      break;
    case 1: {
      auto const size = CALL(&self->socket, recv,
                             (view_t){.size = sizeof(data), .data = data});
      if (!size.has_value) {
        break;
      }

      if (size.value == 0) { // socket was closed
        return;
      }

      if (self->base.recv.fn) {
        self->base.recv.fn(self->base.userdata, self->base.recv.userdata,
                           (const_view_t){.size = size.value, .data = data});
      }
    } break;
    }
  }
}

static void transport_stop(transport_socket_t *self) {
  CALL(&self->socket, shutdown);
}

static void transport_close(transport_socket_t *self) {
  CALL(&self->socket, close);
  array_destroy(self->timers);
}

transport_or_error_t transport_connect(const char *server,
                                       transport_on_recv_t recv,
                                       void *userdata) {

  auto socket = socket_connect(server, "pcp");
  if (!socket.has_value) {
    return (transport_or_error_t){.has_value = false, .error = socket.error};
  }

  auto const transport =
      (transport_socket_t *)malloc(sizeof(transport_socket_t));

  if (!transport) {
    CALL(&socket.value, close);
    return (transport_or_error_t){.has_value = false, .error = ENOMEM};
  }

  (*transport) = (transport_socket_t){
      .base = {.public = {.local = socket.value.local,
                          .remote = socket.value.remote,
                          .send = (transport_send_t)send_,
                          .add_timer =
                              (transport_add_timer_t)transport_add_timer,
                          .remove_timer =
                              (transport_remove_timer_t)transport_remove_timer,
                          .run = (transport_run_t)transport_run,
                          .stop = (transport_stop_t)transport_stop,
                          .close = (transport_close_t)transport_close},
               .recv = recv,
               .userdata = userdata},
      .socket = socket.value,
      .timers = array_create(transport_socket_timer_t)};

  return (transport_or_error_t){.has_value = true,
                                .value = (transport_t *)transport};
}
