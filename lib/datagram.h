#pragma once

#include "internal.h"

#include <pcp/ip.h>

#include <stddef.h>
#include <time.h>

constexpr size_t PCP_DATAGRAM_MAX_SIZE = 1100;
constexpr size_t PCP_HEADER_SIZE = 24;

typedef struct pcp_datagram {
  uint8_t data[PCP_DATAGRAM_MAX_SIZE];
} pcp_datagram_t;

void pcp_datagram_write_request_header(pcp_datagram_t *dst,
                                       pcp_opcode_t opcode,
                                       time_t ttl,
                                       ip_address_t local);

void pcp_datagram_write_response_header(pcp_datagram_t *dst,
                                        pcp_opcode_t opcode,
                                        pcp_result_t code,
                                        time_t ttl,
                                        time_t epoch);

void pcp_datagram_body_write_bytes(pcp_datagram_t *dst,
                                   size_t offset,
                                   size_t size,
                                   const uint8_t data[size]);

void pcp_datagram_body_write_uint8_t(pcp_datagram_t *dst,
                                     size_t offset,
                                     uint8_t value);

void pcp_datagram_body_write_uint16_t(pcp_datagram_t *dst,
                                      size_t offset,
                                      uint16_t value);

void pcp_datagram_body_write_ip_address(pcp_datagram_t *dst,
                                        size_t offset,
                                        ip_address_t ip);

void pcp_datagram_read_request_header(pcp_opcode_t *opcode,
                                      time_t *ttl,
                                      ip_address_t *local,
                                      const pcp_datagram_t *src);

void pcp_datagram_read_response_header(pcp_opcode_t *opcode,
                                       pcp_result_t *code,
                                       time_t *ttl,
                                       time_t *epoch,
                                       const pcp_datagram_t *src);

void pcp_datagram_body_read_bytes(size_t size,
                                  uint8_t dst[size],
                                  const pcp_datagram_t *src,
                                  size_t offset);

uint8_t pcp_datagram_body_read_uint8_t(const pcp_datagram_t *src,
                                       size_t offset);

uint16_t pcp_datagram_body_read_uint16_t(const pcp_datagram_t *src,
                                         size_t offset);

ip_address_t pcp_datagram_body_read_ip_address(const pcp_datagram_t *src,
                                               size_t offset);
