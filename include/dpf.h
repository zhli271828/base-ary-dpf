#ifndef _DPF
#define _DPF

#include <stdio.h>
#include <string.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "prf.h"

typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;

struct DPFKey
{
    struct PRFKeys *prf_keys;
    unsigned char *k;
    size_t msg_len;
    size_t size;
};

void DPFGen(
    struct PRFKeys *prf_keys,
    size_t domain_size,
    size_t index,
    uint128_t *msg_blocks,
    size_t msg_block_len,
    struct DPFKey *k0,
    struct DPFKey *k1);

void DPFFullDomainEval(
    struct DPFKey *k,
    uint128_t *cache,
    uint128_t *output);

void HalfDPFGen(
    struct PRFKeys *prf_keys,
    size_t domain_size,
    size_t index,
    uint128_t *msg_blocks,
    size_t msg_block_len,
    struct DPFKey *k0,
    struct DPFKey *k1);

void HalfDPFFullDomainEval(
    struct DPFKey *k,
    uint128_t *cache,
    uint128_t *output);

int ExtendOutput(
    struct PRFKeys *prf_keys,
    uint128_t *output,
    uint128_t *cache,
    const size_t output_size,
    const size_t new_output_size);

int ExtendOutputZ(
    struct PRFKeysZ *prf_keys_z,
    uint128_t *output,
    uint128_t *cache,
    const size_t output_size,
    const size_t new_output_size);


struct DPFKeyZ {
    struct PRFKeysZ *prf_keys_z;
    unsigned char *k;
    size_t msg_len;
    size_t size;
};
// DPFGenZ for Z^n_{base}
void DPFGenZ(
    const size_t base,
    struct PRFKeysZ *prf_keys_z,
    size_t domain_size,
    size_t index,
    uint128_t *msg_blocks,
    size_t msg_block_len,
    struct DPFKeyZ *k0,
    struct DPFKeyZ *k1);

// DPFFullDomainEvalZ for Z^n_{base}
void DPFFullDomainEvalZ(
    const size_t base,
    struct DPFKeyZ *k,
    uint128_t *cache,
    uint128_t *output);
#endif
