#include "wire.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Parsing and validation on top of the generated codec (pcp_gen.h, from
// pcp.pkt): the PCP rules the schema can't express

static struct pcp_request_option pcp_packet_option(pcp_option_t const *src) {
  struct pcp_request_option result = {0};
  switch (src->type) {
  case THIRD_PARTY:
    result.code = PCP_OPTION_CODE_THIRD_PARTY;
    memcpy(result.body.third_party.internal_ip, src->third_party.internal.addr,
           sizeof(result.body.third_party.internal_ip));
    break;
  case FILTER:
    result.code = PCP_OPTION_CODE_FILTER;
    result.body.filter.prefix = src->filter.peers.prefix;
    result.body.filter.peer_port = src->filter.port;
    memcpy(result.body.filter.peer_ip, src->filter.peers.address.addr,
           sizeof(result.body.filter.peer_ip));
    break;
  }
  return result;
}

// Deletes (lifetime 0) can't carry FILTER. THIRD_PARTY has to stay though:
// without it the server looks for a mapping on our own address, doesn't find
// one and still answers SUCCESS.
static bool pcp_option_included(struct pcp_request const *request,
                                pcp_option_t const *option) {
  return option->type != FILTER || request->lifetime > 0;
}

size_t pcp_packet_request_size(struct pcp_request const *request,
                               size_t count,
                               pcp_option_t const options[count]) {
  assert(request);
  assert(request->opcode == PCP_OPCODE_MAP ||
         request->opcode == PCP_OPCODE_PEER);
  assert(count == 0 || options);

  size_t size = pcp_request_encoded_size(request);
  auto const begin = options;
  auto const end = options + count;
  for (auto iter = begin; iter != end; ++iter) {
    if (pcp_option_included(request, iter)) {
      auto const option = pcp_packet_option(iter);
      size += pcp_request_option_encoded_size(&option);
    }
  }
  return size;
}

size_t pcp_packet_write_request(view_t dst,
                                struct pcp_request const *request,
                                size_t count,
                                pcp_option_t const options[count]) {
  assert(dst.size >= pcp_packet_request_size(request, count, options));

  size_t cursor = 0;
  [[maybe_unused]] auto status =
      pcp_request_encode(request, dst.data, dst.size, &cursor);
  assert(status == PCP_OK);

  // the options follow the request
  auto const begin = options;
  auto const end = options + count;
  for (auto iter = begin; iter != end; ++iter) {
    if (pcp_option_included(request, iter)) {
      auto const option = pcp_packet_option(iter);
      size_t written = 0;
      status = pcp_request_option_encode(&option, dst.data + cursor,
                                         dst.size - cursor, &written);
      assert(status == PCP_OK);
      cursor += written;
    }
  }

  return cursor;
}

pcp_optional_response_t pcp_packet_read_response(const_view_t src) {
  assert(src.data);

  pcp_optional_response_t result = {.has_value = false};

  // silently drop anything malformed (RFC 6887 section 8.3). The generated
  // decoder checks the size limits, the version and the R bit.
  if (src.size % 4 != 0 ||
      pcp_response_decode(&result.value, src.data, src.size) != PCP_OK) {
    return result;
  }

  // only ANNOUNCE and responses to our own MAP/PEER requests matter. Errors
  // without a body can't be matched to a mapping anyway, and fail to decode.
  switch (result.value.opcode) {
  case PCP_OPCODE_ANNOUNCE:
  case PCP_OPCODE_MAP:
  case PCP_OPCODE_PEER:
    result.has_value = true;
    break;
  }
  return result;
}
