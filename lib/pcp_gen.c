/* Generated from pcp.pkt -- do not edit. */
#include "pcp_gen.h"

#include <string.h>

/* ---- wire primitives ---------------------------------------------------- */

/* Read cursor over [p, end). Every read checks bounds first. */
struct pcp_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct pcp_wr {
    uint8_t *p;
    uint8_t *end;
};

static size_t pcp_rd_left(const struct pcp_rd *r) {
    return (size_t)(r->end - r->p);
}

static bool pcp_rd_u8(struct pcp_rd *r, uint8_t *v) {
    if (pcp_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}

static bool pcp_rd_u16(struct pcp_rd *r, uint16_t *v) {
    if (pcp_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}

static bool pcp_rd_u32(struct pcp_rd *r, uint32_t *v) {
    if (pcp_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}

static bool pcp_rd_skip(struct pcp_rd *r, size_t n) {
    if (pcp_rd_left(r) < n)
        return false;
    r->p += n;
    return true;
}

static bool pcp_rd_raw(struct pcp_rd *r, uint8_t *dst, size_t n) {
    if (pcp_rd_left(r) < n)
        return false;
    memcpy(dst, r->p, n);
    r->p += n;
    return true;
}

static bool pcp_rd_rest(struct pcp_rd *r, struct pcp_bytes *v) {
    v->ptr = r->p;
    v->len = pcp_rd_left(r);
    r->p = r->end;
    return true;
}

static size_t pcp_wr_left(const struct pcp_wr *w) {
    return (size_t)(w->end - w->p);
}

static pcp_status pcp_wr_u8(struct pcp_wr *w, uint8_t v) {
    if (pcp_wr_left(w) < 1)
        return PCP_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return PCP_OK;
}

static pcp_status pcp_wr_u16(struct pcp_wr *w, uint16_t v) {
    if (pcp_wr_left(w) < 2)
        return PCP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return PCP_OK;
}

static pcp_status pcp_wr_u32(struct pcp_wr *w, uint32_t v) {
    if (pcp_wr_left(w) < 4)
        return PCP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 24);
    w->p[1] = (uint8_t)(v >> 16);
    w->p[2] = (uint8_t)(v >> 8);
    w->p[3] = (uint8_t)v;
    w->p += 4;
    return PCP_OK;
}

static pcp_status pcp_wr_raw(struct pcp_wr *w, const void *src, size_t n) {
    if (pcp_wr_left(w) < n)
        return PCP_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return PCP_OK;
}

static pcp_status pcp_wr_zero(struct pcp_wr *w, size_t n) {
    if (pcp_wr_left(w) < n)
        return PCP_NOSPACE;
    if (n != 0)
        memset(w->p, 0, n);
    w->p += n;
    return PCP_OK;
}

#define PCP_CHECK(cond) \
    do { \
        if (!(cond)) \
            return false; \
    } while (0)

#define PCP_TRY(expr) \
    do { \
        pcp_status st_ = (expr); \
        if (st_ != PCP_OK) \
            return st_; \
    } while (0)

/* ---- struct MapBody ----------------------------------------------------- */

static bool pcp_map_body_read(struct pcp_rd *r, struct pcp_map_body *out) {
    PCP_CHECK(pcp_rd_raw(r, out->nonce, 12));
    PCP_CHECK(pcp_rd_u8(r, &out->protocol));
    PCP_CHECK(pcp_rd_skip(r, 3));
    PCP_CHECK(pcp_rd_u16(r, &out->internal_port));
    PCP_CHECK(pcp_rd_u16(r, &out->external_port));
    return pcp_rd_raw(r, out->external_ip, 16);
}

static pcp_status pcp_map_body_write(struct pcp_wr *w,
                                     const struct pcp_map_body *in) {
    PCP_TRY(pcp_wr_raw(w, in->nonce, 12));
    PCP_TRY(pcp_wr_u8(w, in->protocol));
    PCP_TRY(pcp_wr_zero(w, 3));
    PCP_TRY(pcp_wr_u16(w, in->internal_port));
    PCP_TRY(pcp_wr_u16(w, in->external_port));
    return pcp_wr_raw(w, in->external_ip, 16);
}

static size_t pcp_map_body_size(const struct pcp_map_body *in) {
    (void)in;
    return 36;
}

/* ---- struct PeerBody ---------------------------------------------------- */

static bool pcp_peer_body_read(struct pcp_rd *r, struct pcp_peer_body *out) {
    PCP_CHECK(pcp_rd_raw(r, out->nonce, 12));
    PCP_CHECK(pcp_rd_u8(r, &out->protocol));
    PCP_CHECK(pcp_rd_skip(r, 3));
    PCP_CHECK(pcp_rd_u16(r, &out->internal_port));
    PCP_CHECK(pcp_rd_u16(r, &out->external_port));
    PCP_CHECK(pcp_rd_raw(r, out->external_ip, 16));
    PCP_CHECK(pcp_rd_u16(r, &out->peer_port));
    PCP_CHECK(pcp_rd_skip(r, 2));
    return pcp_rd_raw(r, out->peer_ip, 16);
}

static pcp_status pcp_peer_body_write(struct pcp_wr *w,
                                      const struct pcp_peer_body *in) {
    PCP_TRY(pcp_wr_raw(w, in->nonce, 12));
    PCP_TRY(pcp_wr_u8(w, in->protocol));
    PCP_TRY(pcp_wr_zero(w, 3));
    PCP_TRY(pcp_wr_u16(w, in->internal_port));
    PCP_TRY(pcp_wr_u16(w, in->external_port));
    PCP_TRY(pcp_wr_raw(w, in->external_ip, 16));
    PCP_TRY(pcp_wr_u16(w, in->peer_port));
    PCP_TRY(pcp_wr_zero(w, 2));
    return pcp_wr_raw(w, in->peer_ip, 16);
}

static size_t pcp_peer_body_size(const struct pcp_peer_body *in) {
    (void)in;
    return 56;
}

/* ---- message Request ---------------------------------------------------- */

static bool pcp_request_body_read(struct pcp_rd *r, struct pcp_request *out) {
    switch (out->opcode) {
    case PCP_OPCODE_MAP:
        return pcp_map_body_read(r, &out->body.map_body);
    case PCP_OPCODE_PEER:
        return pcp_peer_body_read(r, &out->body.peer_body);
    default:
        return false; /* unknown tag */
    }
}

static pcp_status pcp_request_body_write(struct pcp_wr *w,
                                         const struct pcp_request *in) {
    switch (in->opcode) {
    case PCP_OPCODE_MAP:
        return pcp_map_body_write(w, &in->body.map_body);
    case PCP_OPCODE_PEER:
        return pcp_peer_body_write(w, &in->body.peer_body);
    default:
        return PCP_INVALID; /* unknown tag */
    }
}

static size_t pcp_request_body_size(const struct pcp_request *in) {
    switch (in->opcode) {
    case PCP_OPCODE_MAP:
        return pcp_map_body_size(&in->body.map_body);
    case PCP_OPCODE_PEER:
        return pcp_peer_body_size(&in->body.peer_body);
    default:
        return 0; /* unknown tag: encode fails */
    }
}

static bool pcp_request_read(struct pcp_rd *r, struct pcp_request *out) {
    uint8_t bits0;
    PCP_CHECK(pcp_rd_u8(r, &bits0));
    PCP_CHECK(bits0 == 2);
    uint8_t bits1;
    PCP_CHECK(pcp_rd_u8(r, &bits1));
    PCP_CHECK((bits1 >> 7) == 0);
    out->opcode = (enum pcp_opcode)(bits1 & 0x7f);
    PCP_CHECK(pcp_rd_skip(r, 2));
    PCP_CHECK(pcp_rd_u32(r, &out->lifetime));
    PCP_CHECK(pcp_rd_raw(r, out->client_ip, 16));
    return pcp_request_body_read(r, out);
}

static pcp_status pcp_request_write(struct pcp_wr *w,
                                    const struct pcp_request *in) {
    PCP_TRY(pcp_wr_u8(w, (uint8_t)(0x2)));
    if (in->opcode > 0x7f)
        return PCP_INVALID;
    PCP_TRY(pcp_wr_u8(w, (uint8_t)(in->opcode)));
    PCP_TRY(pcp_wr_zero(w, 2));
    PCP_TRY(pcp_wr_u32(w, in->lifetime));
    PCP_TRY(pcp_wr_raw(w, in->client_ip, 16));
    return pcp_request_body_write(w, in);
}

static size_t pcp_request_size(const struct pcp_request *in) {
    return 24 + pcp_request_body_size(in);
}

pcp_status pcp_request_decode(struct pcp_request *out, const uint8_t *buf,
                              size_t len) {
    struct pcp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!pcp_request_read(&r, out) || r.p != r.end)
        return PCP_INVALID;
    return PCP_OK;
}

pcp_status pcp_request_encode(const struct pcp_request *in, uint8_t *buf,
                              size_t cap, size_t *written) {
    struct pcp_wr w = { buf, cap != 0 ? buf + cap : buf };
    PCP_TRY(pcp_request_write(&w, in));
    *written = (size_t)(w.p - buf);
    return PCP_OK;
}

size_t pcp_request_encoded_size(const struct pcp_request *in) {
    return pcp_request_size(in);
}

/* ---- struct ThirdParty -------------------------------------------------- */

static bool pcp_third_party_read(struct pcp_rd *r,
                                 struct pcp_third_party *out) {
    uint16_t bits0;
    PCP_CHECK(pcp_rd_u16(r, &bits0));
    PCP_CHECK(bits0 == 16);
    return pcp_rd_raw(r, out->internal_ip, 16);
}

static pcp_status pcp_third_party_write(struct pcp_wr *w,
                                        const struct pcp_third_party *in) {
    PCP_TRY(pcp_wr_u16(w, (uint16_t)(0x10)));
    return pcp_wr_raw(w, in->internal_ip, 16);
}

static size_t pcp_third_party_size(const struct pcp_third_party *in) {
    (void)in;
    return 18;
}

/* ---- struct Filter ------------------------------------------------------ */

static bool pcp_filter_read(struct pcp_rd *r, struct pcp_filter *out) {
    uint16_t bits0;
    PCP_CHECK(pcp_rd_u16(r, &bits0));
    PCP_CHECK(bits0 == 20);
    PCP_CHECK(pcp_rd_skip(r, 1));
    PCP_CHECK(pcp_rd_u8(r, &out->prefix));
    PCP_CHECK(pcp_rd_u16(r, &out->peer_port));
    return pcp_rd_raw(r, out->peer_ip, 16);
}

static pcp_status pcp_filter_write(struct pcp_wr *w,
                                   const struct pcp_filter *in) {
    PCP_TRY(pcp_wr_u16(w, (uint16_t)(0x14)));
    PCP_TRY(pcp_wr_zero(w, 1));
    PCP_TRY(pcp_wr_u8(w, in->prefix));
    PCP_TRY(pcp_wr_u16(w, in->peer_port));
    return pcp_wr_raw(w, in->peer_ip, 16);
}

static size_t pcp_filter_size(const struct pcp_filter *in) {
    (void)in;
    return 22;
}

/* ---- message RequestOption ---------------------------------------------- */

static bool pcp_request_option_body_read(struct pcp_rd *r,
                                         struct pcp_request_option *out) {
    switch (out->code) {
    case PCP_OPTION_CODE_THIRD_PARTY:
        return pcp_third_party_read(r, &out->body.third_party);
    case PCP_OPTION_CODE_FILTER:
        return pcp_filter_read(r, &out->body.filter);
    default:
        return false; /* unknown tag */
    }
}

static pcp_status pcp_request_option_body_write(
    struct pcp_wr *w, const struct pcp_request_option *in) {
    switch (in->code) {
    case PCP_OPTION_CODE_THIRD_PARTY:
        return pcp_third_party_write(w, &in->body.third_party);
    case PCP_OPTION_CODE_FILTER:
        return pcp_filter_write(w, &in->body.filter);
    default:
        return PCP_INVALID; /* unknown tag */
    }
}

static size_t pcp_request_option_body_size(
    const struct pcp_request_option *in) {
    switch (in->code) {
    case PCP_OPTION_CODE_THIRD_PARTY:
        return pcp_third_party_size(&in->body.third_party);
    case PCP_OPTION_CODE_FILTER:
        return pcp_filter_size(&in->body.filter);
    default:
        return 0; /* unknown tag: encode fails */
    }
}

static bool pcp_request_option_read(struct pcp_rd *r,
                                    struct pcp_request_option *out) {
    PCP_CHECK(pcp_rd_u8(r, &out->code));
    PCP_CHECK(pcp_rd_skip(r, 1));
    return pcp_request_option_body_read(r, out);
}

static pcp_status pcp_request_option_write(
    struct pcp_wr *w, const struct pcp_request_option *in) {
    PCP_TRY(pcp_wr_u8(w, in->code));
    PCP_TRY(pcp_wr_zero(w, 1));
    return pcp_request_option_body_write(w, in);
}

static size_t pcp_request_option_size(const struct pcp_request_option *in) {
    return 2 + pcp_request_option_body_size(in);
}

pcp_status pcp_request_option_decode(struct pcp_request_option *out,
                                     const uint8_t *buf, size_t len) {
    struct pcp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!pcp_request_option_read(&r, out) || r.p != r.end)
        return PCP_INVALID;
    return PCP_OK;
}

pcp_status pcp_request_option_encode(const struct pcp_request_option *in,
                                     uint8_t *buf, size_t cap,
                                     size_t *written) {
    struct pcp_wr w = { buf, cap != 0 ? buf + cap : buf };
    PCP_TRY(pcp_request_option_write(&w, in));
    *written = (size_t)(w.p - buf);
    return PCP_OK;
}

size_t pcp_request_option_encoded_size(const struct pcp_request_option *in) {
    return pcp_request_option_size(in);
}

/* ---- struct MapResponse ------------------------------------------------- */

static bool pcp_map_response_read(struct pcp_rd *r,
                                  struct pcp_map_response *out) {
    PCP_CHECK(pcp_rd_raw(r, out->nonce, 12));
    PCP_CHECK(pcp_rd_u8(r, &out->protocol));
    PCP_CHECK(pcp_rd_skip(r, 3));
    PCP_CHECK(pcp_rd_u16(r, &out->internal_port));
    PCP_CHECK(pcp_rd_u16(r, &out->external_port));
    PCP_CHECK(pcp_rd_raw(r, out->external_ip, 16));
    return pcp_rd_rest(r, &out->options);
}

static pcp_status pcp_map_response_write(struct pcp_wr *w,
                                         const struct pcp_map_response *in) {
    PCP_TRY(pcp_wr_raw(w, in->nonce, 12));
    PCP_TRY(pcp_wr_u8(w, in->protocol));
    PCP_TRY(pcp_wr_zero(w, 3));
    PCP_TRY(pcp_wr_u16(w, in->internal_port));
    PCP_TRY(pcp_wr_u16(w, in->external_port));
    PCP_TRY(pcp_wr_raw(w, in->external_ip, 16));
    return pcp_wr_raw(w, in->options.ptr, in->options.len);
}

static size_t pcp_map_response_size(const struct pcp_map_response *in) {
    return 36 + in->options.len;
}

/* ---- struct PeerResponse ------------------------------------------------ */

static bool pcp_peer_response_read(struct pcp_rd *r,
                                   struct pcp_peer_response *out) {
    PCP_CHECK(pcp_rd_raw(r, out->nonce, 12));
    PCP_CHECK(pcp_rd_u8(r, &out->protocol));
    PCP_CHECK(pcp_rd_skip(r, 3));
    PCP_CHECK(pcp_rd_u16(r, &out->internal_port));
    PCP_CHECK(pcp_rd_u16(r, &out->external_port));
    PCP_CHECK(pcp_rd_raw(r, out->external_ip, 16));
    PCP_CHECK(pcp_rd_u16(r, &out->peer_port));
    PCP_CHECK(pcp_rd_skip(r, 2));
    PCP_CHECK(pcp_rd_raw(r, out->peer_ip, 16));
    return pcp_rd_rest(r, &out->options);
}

static pcp_status pcp_peer_response_write(struct pcp_wr *w,
                                          const struct pcp_peer_response *in) {
    PCP_TRY(pcp_wr_raw(w, in->nonce, 12));
    PCP_TRY(pcp_wr_u8(w, in->protocol));
    PCP_TRY(pcp_wr_zero(w, 3));
    PCP_TRY(pcp_wr_u16(w, in->internal_port));
    PCP_TRY(pcp_wr_u16(w, in->external_port));
    PCP_TRY(pcp_wr_raw(w, in->external_ip, 16));
    PCP_TRY(pcp_wr_u16(w, in->peer_port));
    PCP_TRY(pcp_wr_zero(w, 2));
    PCP_TRY(pcp_wr_raw(w, in->peer_ip, 16));
    return pcp_wr_raw(w, in->options.ptr, in->options.len);
}

static size_t pcp_peer_response_size(const struct pcp_peer_response *in) {
    return 56 + in->options.len;
}

/* ---- message Response --------------------------------------------------- */

static bool pcp_response_body_read(struct pcp_rd *r,
                                   struct pcp_response *out) {
    switch (out->opcode) {
    case PCP_OPCODE_MAP:
        return pcp_map_response_read(r, &out->body.map_response);
    case PCP_OPCODE_PEER:
        return pcp_peer_response_read(r, &out->body.peer_response);
    default:
        return pcp_rd_rest(r, &out->body.unknown);
    }
}

static pcp_status pcp_response_body_write(struct pcp_wr *w,
                                          const struct pcp_response *in) {
    switch (in->opcode) {
    case PCP_OPCODE_MAP:
        return pcp_map_response_write(w, &in->body.map_response);
    case PCP_OPCODE_PEER:
        return pcp_peer_response_write(w, &in->body.peer_response);
    default:
        return pcp_wr_raw(w, in->body.unknown.ptr, in->body.unknown.len);
    }
}

static size_t pcp_response_body_size(const struct pcp_response *in) {
    switch (in->opcode) {
    case PCP_OPCODE_MAP:
        return pcp_map_response_size(&in->body.map_response);
    case PCP_OPCODE_PEER:
        return pcp_peer_response_size(&in->body.peer_response);
    default:
        return in->body.unknown.len;
    }
}

static bool pcp_response_read(struct pcp_rd *r, struct pcp_response *out) {
    uint8_t bits0;
    PCP_CHECK(pcp_rd_u8(r, &bits0));
    PCP_CHECK(bits0 == 2);
    uint8_t bits1;
    PCP_CHECK(pcp_rd_u8(r, &bits1));
    PCP_CHECK((bits1 >> 7) == 1);
    out->opcode = (enum pcp_opcode)(bits1 & 0x7f);
    PCP_CHECK(pcp_rd_skip(r, 1));
    PCP_CHECK(pcp_rd_u8(r, &out->result));
    PCP_CHECK(pcp_rd_u32(r, &out->lifetime));
    PCP_CHECK(pcp_rd_u32(r, &out->epoch));
    PCP_CHECK(pcp_rd_skip(r, 12));
    return pcp_response_body_read(r, out);
}

static pcp_status pcp_response_write(struct pcp_wr *w,
                                     const struct pcp_response *in) {
    PCP_TRY(pcp_wr_u8(w, (uint8_t)(0x2)));
    if (in->opcode > 0x7f)
        return PCP_INVALID;
    PCP_TRY(pcp_wr_u8(w, (uint8_t)(0x80 | in->opcode)));
    PCP_TRY(pcp_wr_zero(w, 1));
    PCP_TRY(pcp_wr_u8(w, in->result));
    PCP_TRY(pcp_wr_u32(w, in->lifetime));
    PCP_TRY(pcp_wr_u32(w, in->epoch));
    PCP_TRY(pcp_wr_zero(w, 12));
    return pcp_response_body_write(w, in);
}

static size_t pcp_response_size(const struct pcp_response *in) {
    return 24 + pcp_response_body_size(in);
}

pcp_status pcp_response_decode(struct pcp_response *out, const uint8_t *buf,
                               size_t len) {
    if (len > PCP_RESPONSE_MAX_SIZE)
        return PCP_INVALID;
    struct pcp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!pcp_response_read(&r, out) || r.p != r.end)
        return PCP_INVALID;
    return PCP_OK;
}

pcp_status pcp_response_encode(const struct pcp_response *in, uint8_t *buf,
                               size_t cap, size_t *written) {
    if (pcp_response_size(in) > PCP_RESPONSE_MAX_SIZE)
        return PCP_INVALID;
    struct pcp_wr w = { buf, cap != 0 ? buf + cap : buf };
    PCP_TRY(pcp_response_write(&w, in));
    *written = (size_t)(w.p - buf);
    return PCP_OK;
}

size_t pcp_response_encoded_size(const struct pcp_response *in) {
    return pcp_response_size(in);
}
