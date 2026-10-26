#include "epoch.h"

#include <assert.h>

static bool pcp_epoch_valid(pcp_epoch_t const *epoch,
                            pcp_time_ms_t now,
                            pcp_time_s_t server) {
  if (!epoch->has_previous) {
    return true;
  }

  // the server's clock went backwards
  if ((int64_t)server + 2 < (int64_t)epoch->server) {
    return false;
  }

  // both clocks should have moved by roughly the same amount, give or take 2s
  // and 1/16 of drift
  constexpr int64_t slack = 2000;
  auto const client_delta = (int64_t)(now - epoch->client);
  auto const server_delta = ((int64_t)server - (int64_t)epoch->server) * 1000;

  return client_delta + slack >= server_delta - server_delta / 16 &&
         server_delta + slack >= client_delta - client_delta / 16;
}

bool pcp_epoch_update(pcp_epoch_t *epoch,
                      pcp_time_ms_t now,
                      pcp_time_s_t server) {
  assert(epoch);
  assert(!epoch->has_previous || now >= epoch->client);

  auto const valid = pcp_epoch_valid(epoch, now, server);
  *epoch = (pcp_epoch_t){
      .has_previous = true,
      .client = now,
      .server = server,
  };
  return valid;
}
