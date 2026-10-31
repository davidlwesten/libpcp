/* Generated from pcp.pkt -- do not edit. */
#ifndef PCP_GEN_H
#define PCP_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PCP_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    PCP_INVALID,
    /* encode: output buffer too small */
    PCP_NOSPACE,
} pcp_status;

/* enum Opcode : u7 */
enum pcp_opcode : uint8_t {
    PCP_OPCODE_ANNOUNCE = 0,
    PCP_OPCODE_MAP      = 1,
    PCP_OPCODE_PEER     = 2,
};

/* enum OptionCode : u8 */
enum pcp_option_code : uint8_t {
    PCP_OPTION_CODE_THIRD_PARTY = 1,
    PCP_OPTION_CODE_FILTER      = 3,
};

/* u8[..] and u8[len]: points into the buffer. */
struct pcp_bytes {
    const uint8_t *ptr;
    size_t len;
};

/* struct MapBody {
 *     nonce: u8[12];
 *     protocol: u8;
 *     _: u8[3];
 *     internal_port: u16;
 *     external_port: u16;
 *     external_ip: u8[16];
 * } */
struct pcp_map_body {
    uint8_t nonce[12];
    uint8_t protocol;
    uint16_t internal_port;
    uint16_t external_port;
    uint8_t external_ip[16];
};

/* struct PeerBody {
 *     nonce: u8[12];
 *     protocol: u8;
 *     _: u8[3];
 *     internal_port: u16;
 *     external_port: u16;
 *     external_ip: u8[16];
 *     peer_port: u16;
 *     _: u16;
 *     peer_ip: u8[16];
 * } */
struct pcp_peer_body {
    uint8_t nonce[12];
    uint8_t protocol;
    uint16_t internal_port;
    uint16_t external_port;
    uint8_t external_ip[16];
    uint16_t peer_port;
    uint8_t peer_ip[16];
};

/* message Request {
 *     version: u8 = 2;
 *     r: u1 = 0;
 *     opcode: Opcode;
 *     _: u16;
 *     lifetime: u32;
 *     client_ip: u8[16];
 *     body: switch (opcode) { ... };
 * } */
struct pcp_request {
    enum pcp_opcode opcode;  /* selects body */
    uint32_t lifetime;
    uint8_t client_ip[16];
    union {
        struct pcp_map_body  map_body;   /* MAP */
        struct pcp_peer_body peer_body;  /* PEER */
    } body;
};

/* Size limits of an encoded Request, in bytes. */
constexpr size_t PCP_REQUEST_MIN_SIZE = 60;
constexpr size_t PCP_REQUEST_MAX_SIZE = 80;

/* struct ThirdParty { length: u16 = 16; internal_ip: u8[16]; } */
struct pcp_third_party {
    uint8_t internal_ip[16];
};

/* struct Filter {
 *     length: u16 = 20;
 *     _: u8;
 *     prefix: u8;
 *     peer_port: u16;
 *     peer_ip: u8[16];
 * } */
struct pcp_filter {
    uint8_t prefix;
    uint16_t peer_port;
    uint8_t peer_ip[16];
};

/* message RequestOption {
 *     code: OptionCode;
 *     _: u8;
 *     body: switch (code) { ... };
 * } */
struct pcp_request_option {
    enum pcp_option_code code;  /* selects body */
    union {
        struct pcp_third_party third_party;  /* THIRD_PARTY */
        struct pcp_filter      filter;       /* FILTER */
    } body;
};

/* Size limits of an encoded RequestOption, in bytes. */
constexpr size_t PCP_REQUEST_OPTION_MIN_SIZE = 20;
constexpr size_t PCP_REQUEST_OPTION_MAX_SIZE = 24;

/* struct MapResponse {
 *     nonce: u8[12];
 *     protocol: u8;
 *     _: u8[3];
 *     internal_port: u16;
 *     external_port: u16;
 *     external_ip: u8[16];
 *     options: u8[..];
 * } */
struct pcp_map_response {
    uint8_t nonce[12];
    uint8_t protocol;
    uint16_t internal_port;
    uint16_t external_port;
    uint8_t external_ip[16];
    struct pcp_bytes options;
};

/* struct PeerResponse {
 *     nonce: u8[12];
 *     protocol: u8;
 *     _: u8[3];
 *     internal_port: u16;
 *     external_port: u16;
 *     external_ip: u8[16];
 *     peer_port: u16;
 *     _: u16;
 *     peer_ip: u8[16];
 *     options: u8[..];
 * } */
struct pcp_peer_response {
    uint8_t nonce[12];
    uint8_t protocol;
    uint16_t internal_port;
    uint16_t external_port;
    uint8_t external_ip[16];
    uint16_t peer_port;
    uint8_t peer_ip[16];
    struct pcp_bytes options;
};

/* message Response max 1100 {
 *     version: u8 = 2;
 *     r: u1 = 1;
 *     opcode: Opcode;
 *     _: u8;
 *     result: u8;
 *     lifetime: u32;
 *     epoch: u32;
 *     _: u8[12];
 *     body: switch (opcode) { ... };
 * } */
struct pcp_response {
    enum pcp_opcode opcode;  /* selects body */
    uint8_t result;
    uint32_t lifetime;
    uint32_t epoch;
    union {
        struct pcp_map_response  map_response;   /* MAP */
        struct pcp_peer_response peer_response;  /* PEER */
        struct pcp_bytes         unknown;        /* any other value */
    } body;
};

/* Size limits of an encoded Response, in bytes. Decode and encode
 * reject a Response larger than PCP_RESPONSE_MAX_SIZE. */
constexpr size_t PCP_RESPONSE_MIN_SIZE = 24;
constexpr size_t PCP_RESPONSE_MAX_SIZE = 1100;

/* Decode one Request. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
pcp_status pcp_request_decode(struct pcp_request *out, const uint8_t *buf,
                              size_t len);

/* Encode into buf[0..cap). On PCP_OK, *written is the encoded length. */
pcp_status pcp_request_encode(const struct pcp_request *in, uint8_t *buf,
                              size_t cap, size_t *written);

/* Exact number of bytes pcp_request_encode will write, if it succeeds. */
size_t pcp_request_encoded_size(const struct pcp_request *in);

/* Decode one RequestOption. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
pcp_status pcp_request_option_decode(struct pcp_request_option *out,
                                     const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On PCP_OK, *written is the encoded length. */
pcp_status pcp_request_option_encode(const struct pcp_request_option *in,
                                     uint8_t *buf, size_t cap,
                                     size_t *written);

/* Exact number of bytes pcp_request_option_encode will write, if it succeeds. */
size_t pcp_request_option_encoded_size(const struct pcp_request_option *in);

/* Decode one Response. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
pcp_status pcp_response_decode(struct pcp_response *out, const uint8_t *buf,
                               size_t len);

/* Encode into buf[0..cap). On PCP_OK, *written is the encoded length. */
pcp_status pcp_response_encode(const struct pcp_response *in, uint8_t *buf,
                               size_t cap, size_t *written);

/* Exact number of bytes pcp_response_encode will write, if it succeeds. */
size_t pcp_response_encoded_size(const struct pcp_response *in);

#endif
