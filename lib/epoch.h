#pragma once

#include <pcp/pcp.h>

/**
 * @brief Last epoch time we got from the server, and our clock at that point
 *
 * See RFC 6887 section 8.5.
 */
typedef struct pcp_epoch {
  bool has_previous;
  pcp_time_ms_t client;
  pcp_time_s_t server;
} pcp_epoch_t;

/**
 * @brief Check a response's epoch time against the previous one, then store it
 *
 * @param[in] epoch  The previous epoch, updated in either case
 * @param[in] now    The time the response was received
 * @param[in] server The epoch time from the response
 *
 * @returns false if the server lost its state (e.g. it restarted)
 */
bool pcp_epoch_update(pcp_epoch_t *epoch,
                      pcp_time_ms_t now,
                      pcp_time_s_t server);
