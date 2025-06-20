#include "dpf.h"
#include "prf.h"
#include "utils.h"

#include <openssl/rand.h>

#define LOG_BATCH_SIZE 6 // operate in smallish batches to maximize cache hits

// Naming conventions:
// - A,B refer to shares given to parties A and B
// - 0,1,2 refer to the branch index in the ternary tree

void DPFGen(
	struct PRFKeys *prf_keys,
	size_t domain_size, // length of the log Domain
	size_t index, // secret position
	uint128_t *msg_blocks, // 
	size_t msg_block_len,
	struct DPFKey *k0,
	struct DPFKey *k1)
{
	// starting seeds given to each party
	uint128_t seedA;
	uint128_t seedB;

	// correction word provided to both parties
	// (one correction word per level)
	uint128_t sCW0[domain_size];
	uint128_t sCW1[domain_size];
	uint128_t sCW2[domain_size];

	// starting seeds are random
	RAND_bytes((uint8_t *)&seedA, 16);
	RAND_bytes((uint8_t *)&seedB, 16);

	// variables for the intermediate values
	uint128_t parent, parentA, parentB, sA0, sA1, sA2, sB0, sB1, sB2;

	// current parent value (xor of the two seeds)
	parent = seedA ^ seedB;

	// control bit of the parent on the special path must always be set to 1
	// so as to apply the corresponding correction word
	if (get_lsb(parent) == 0)
		seedA = flip_lsb(seedA);

	parentA = seedA;
	parentB = seedB;

	uint8_t prev_control_bit_A, prev_control_bit_B;

	for (size_t i = 0; i < domain_size; i++)
	{
		prev_control_bit_A = get_lsb(parentA);
		prev_control_bit_B = get_lsb(parentB);

		// expand the starting seeds of each party
		PRFEval(prf_keys->prf_key0, &parentA, &sA0);
		PRFEval(prf_keys->prf_key1, &parentA, &sA1);
		PRFEval(prf_keys->prf_key2, &parentA, &sA2);

		PRFEval(prf_keys->prf_key0, &parentB, &sB0);
		PRFEval(prf_keys->prf_key1, &parentB, &sB1);
		PRFEval(prf_keys->prf_key2, &parentB, &sB2);

		// on-path correction word is set to random
		// so as to be indistinguishable from the real correction words
		uint128_t r;
		RAND_bytes((uint8_t *)&r, sizeof(uint128_t));

		// get the current trit (ternary bit) of the special index
		uint8_t trit = get_trit(index, domain_size, i);

		switch (trit) {
		case 0:
			parent = sA0 ^ sB0 ^ r;
			if (get_lsb(parent) == 0)
				r = flip_lsb(r);
			sCW0[i] = r;
			sCW1[i] = sA1 ^ sB1;
			sCW2[i] = sA2 ^ sB2;
			if (get_lsb(parentA) == 1) {
				parentA = sA0 ^ r;
				parentB = sB0;
			} else {
				parentA = sA0;
				parentB = sB0 ^ r;
			}
			break;
		case 1:
			parent = sA1 ^ sB1 ^ r;
			if (get_lsb(parent) == 0)
				r = flip_lsb(r);

			sCW0[i] = sA0 ^ sB0;
			sCW1[i] = r;
			sCW2[i] = sA2 ^ sB2;

			if (get_lsb(parentA) == 1) {
				parentA = sA1 ^ r;
				parentB = sB1;
			} else {
				parentA = sA1;
				parentB = sB1 ^ r;
			}

			break;

		case 2:
			parent = sA2 ^ sB2 ^ r;
			if (get_lsb(parent) == 0)
				r = flip_lsb(r);

			sCW0[i] = sA0 ^ sB0;
			sCW1[i] = sA1 ^ sB1;
			sCW2[i] = r;

			if (get_lsb(parentA) == 1) {
				parentA = sA2 ^ r;
				parentB = sB2;
			} else {
				parentA = sA2;
				parentB = sB2 ^ r;
			}
			break;
		default:
			printf("error: not a ternary digit!\n");
			exit(0);
		}
	}

	// set the last correction word to correct the output to msg
	uint128_t leaf_seedA, leaf_seedB;
	uint8_t last_trit = get_trit(index, domain_size, domain_size - 1);
	if (last_trit == 0)
	{
		leaf_seedA = sA0 ^ (prev_control_bit_A * sCW0[domain_size - 1]);
		leaf_seedB = sB0 ^ (prev_control_bit_B * sCW0[domain_size - 1]);
	}
	else if (last_trit == 1)
	{
		leaf_seedA = sA1 ^ (prev_control_bit_A * sCW1[domain_size - 1]);
		leaf_seedB = sB1 ^ (prev_control_bit_B * sCW1[domain_size - 1]);
	}

	else if (last_trit == 2)
	{
		leaf_seedA = sA2 ^ (prev_control_bit_A * sCW2[domain_size - 1]);
		leaf_seedB = sB2 ^ (prev_control_bit_B * sCW2[domain_size - 1]);
	}

	uint128_t *outputA = malloc(sizeof(uint128_t) * msg_block_len);
	uint128_t *outputB = malloc(sizeof(uint128_t) * msg_block_len);
	uint128_t *cache = malloc(sizeof(uint128_t) * msg_block_len);
	uint128_t *outputCW = malloc(sizeof(uint128_t) * msg_block_len);

	outputA[0] = leaf_seedA;
	outputB[0] = leaf_seedB;

	ExtendOutput(prf_keys, outputA, cache, 1, msg_block_len);
	ExtendOutput(prf_keys, outputB, cache, 1, msg_block_len);

	for (size_t i = 0; i < msg_block_len; i++)
		outputCW[i] = outputA[i] ^ outputB[i] ^ msg_blocks[i];

	// memcpy all the generated values into two keys
	// 16 = sizeof(uint128_t)
	size_t key_size = sizeof(uint128_t);			 // initial seed size;
	key_size += 3 * domain_size * sizeof(uint128_t); // correction words
	key_size += sizeof(uint128_t) * msg_block_len;	 // output correction word

	k0->prf_keys = prf_keys;
	k0->k = malloc(key_size);
	k0->size = domain_size;
	k0->msg_len = msg_block_len;
	memcpy(&k0->k[0], &seedA, 16);
	memcpy(&k0->k[16], &sCW0[0], domain_size * 16);
	memcpy(&k0->k[16 * domain_size + 16], &sCW1[0], domain_size * 16);
	memcpy(&k0->k[16 * 2 * domain_size + 16], &sCW2[0], domain_size * 16);
	memcpy(&k0->k[16 * 3 * domain_size + 16], &outputCW[0], msg_block_len * 16);

	k1->prf_keys = prf_keys;
	k1->k = malloc(key_size);
	k1->size = domain_size;
	k1->msg_len = msg_block_len;
	memcpy(&k1->k[0], &seedB, 16);
	memcpy(&k1->k[16], &sCW0[0], domain_size * 16);
	memcpy(&k1->k[16 * domain_size + 16], &sCW1[0], domain_size * 16);
	memcpy(&k1->k[16 * 2 * domain_size + 16], &sCW2[0], domain_size * 16);
	memcpy(&k1->k[16 * 3 * domain_size + 16], &outputCW[0], msg_block_len * 16);

	free(outputA);
	free(outputB);
	free(cache);
	free(outputCW);
}

// evaluates the full DPF domain; much faster than
// batching the evaluation points since each level of the DPF tree
// is only expanded once.
void DPFFullDomainEval(
	struct DPFKey *key,
	uint128_t *cache,
	uint128_t *output)
{
	size_t size = key->size;
	const uint8_t *k = key->k;
	struct PRFKeys *prf_keys = key->prf_keys;

	if (size % 2 == 1)
	{
		uint128_t *tmp = cache;
		cache = output;
		output = tmp;
	}
	// full_eval_size = pow(3, size);
	const size_t num_leaves = ipow(3, size);

	memcpy(&output[0], &k[0], 16); // output[0] is the start seed
	const uint128_t *sCW0 = (uint128_t *)&k[16];
	const uint128_t *sCW1 = (uint128_t *)&k[16 * size + 16];
	const uint128_t *sCW2 = (uint128_t *)&k[16 * 2 * size + 16];

	// inner loop variables related to node expansion
	// and correction word application
	uint128_t *tmp;
	size_t idx0, idx1, idx2;
	uint8_t cb = 0;

	// batching variables related to chunking of inner loop processing
	// for the purpose of maximizing cache hits
	size_t max_batch_size = ipow(3, LOG_BATCH_SIZE);
	size_t batch, num_batches, batch_size, offset;

	size_t num_nodes = 1;
	for (uint8_t i = 0; i < size; i++)
	{
		if (i < LOG_BATCH_SIZE)
		{
			batch_size = num_nodes;
			num_batches = 1;
		}
		else
		{
			batch_size = max_batch_size;
			num_batches = num_nodes / max_batch_size;
		}

		offset = 0;
		for (batch = 0; batch < num_batches; batch++)
		{
			PRFBatchEval(prf_keys->prf_key0, &output[offset], &cache[offset], batch_size);
			PRFBatchEval(prf_keys->prf_key1, &output[offset], &cache[num_nodes + offset], batch_size);
			PRFBatchEval(prf_keys->prf_key2, &output[offset], &cache[(num_nodes * 2) + offset], batch_size);
			idx0 = offset;
			idx1 = num_nodes + offset;
			idx2 = (num_nodes * 2) + offset;

			while (idx0 < offset + batch_size)
			{
				cb = output[idx0] & 1; // gets the LSB of the parent
				cache[idx0] ^= (cb * sCW0[i]);
				cache[idx1] ^= (cb * sCW1[i]);
				cache[idx2] ^= (cb * sCW2[i]);

				idx0++;
				idx1++;
				idx2++;
			}

			offset += batch_size;
		}

		tmp = output;
		output = cache;
		cache = tmp;

		num_nodes *= 3;
	}

	const size_t output_length = key->msg_len * num_leaves;
	const size_t msg_len = key->msg_len;
	uint128_t *outputCW = (uint128_t *)&k[16 * 3 * size + 16];
	ExtendOutput(prf_keys, output, cache, num_leaves, output_length);

	size_t j = 0;
	for (size_t i = 0; i < num_leaves; i++)
	{
		// TODO: a bit hacky, assumes that cache[i*msg_len] = old_output[i]
		// which is the case internally in ExtendOutput. It would be good
		// to remove this assumption however using memcpy is costly...

		if (cache[i * msg_len] & 1) // parent control bit
		{
			for (j = 0; j < msg_len; j++)
				output[i * msg_len + j] ^= outputCW[j];
		}
	}
}

void DPFGenZ(
    const size_t base,
    struct PRFKeysZ *prf_keys_z,
    size_t domain_size,
    size_t index,
    uint128_t *msg_blocks,
    size_t msg_block_len,
    struct DPFKeyZ *k0,
    struct DPFKeyZ *k1)
{
    // starting seeds given to each party
    uint128_t seedA;
    uint128_t seedB;

    // correction word provided to both parties
    // (one correction word per level)
    uint128_t sCW[base][domain_size];

    // starting seeds are random
    RAND_bytes((uint8_t *)&seedA, 16);
    RAND_bytes((uint8_t *)&seedB, 16);

    // variables for the intermediate values
    uint128_t parent, parentA, parentB, sA[base], sB[base];

    // current parent value (xor of the two seeds)
    parent = seedA ^ seedB;

    // control bit of the parent on the special path must always be set to 1
    // so as to apply the corresponding correction word
    if (get_lsb(parent) == 0)
        seedA = flip_lsb(seedA);

    parentA = seedA;
    parentB = seedB;

    uint8_t prev_control_bit_A, prev_control_bit_B;

    for (size_t i = 0; i < domain_size; i++)
    {
        prev_control_bit_A = get_lsb(parentA);
        prev_control_bit_B = get_lsb(parentB);

        // expand the starting seeds of each party
        for (int j = 0; j < base; j++)
        {
            PRFEval(prf_keys_z->prf_key[j], &parentA, &sA[j]);
            PRFEval(prf_keys_z->prf_key[j], &parentB, &sB[j]);
        }

        // on-path correction word is set to random
        // so as to be indistinguishable from the real correction words
        uint128_t r;
        RAND_bytes((uint8_t *)&r, sizeof(uint128_t));

        // get the current septit (septary bit) of the special index
        uint8_t septit = get_septit(base, index, domain_size, i);

        for (int j = 0; j < base; j++)
        {
            if (j == septit)
            {
                parent = sA[j] ^ sB[j] ^ r;
                if (get_lsb(parent) == 0)
                    r = flip_lsb(r);
                sCW[j][i] = r;
            }
            else
            {
                sCW[j][i] = sA[j] ^ sB[j];
            }
        }

        if (get_lsb(parentA) == 1)
        {
            parentA = sA[septit] ^ r;
            parentB = sB[septit];
        }
        else
        {
            parentA = sA[septit];
            parentB = sB[septit] ^ r;
        }
    }

    // set the last correction word to correct the output to msg
    uint128_t leaf_seedA, leaf_seedB;
    uint8_t last_septit = get_septit(base, index, domain_size, domain_size - 1);
    leaf_seedA = sA[last_septit] ^ (prev_control_bit_A * sCW[last_septit][domain_size - 1]);
    leaf_seedB = sB[last_septit] ^ (prev_control_bit_B * sCW[last_septit][domain_size - 1]);

    uint128_t *outputA = malloc(sizeof(uint128_t) * msg_block_len);
    uint128_t *outputB = malloc(sizeof(uint128_t) * msg_block_len);
    uint128_t *cache = malloc(sizeof(uint128_t) * msg_block_len);
    uint128_t *outputCW = malloc(sizeof(uint128_t) * msg_block_len);

    outputA[0] = leaf_seedA;
    outputB[0] = leaf_seedB;

    ExtendOutputZ(prf_keys_z, outputA, cache, 1, msg_block_len);
    ExtendOutputZ(prf_keys_z, outputB, cache, 1, msg_block_len);

    for (size_t i = 0; i < msg_block_len; i++)
        outputCW[i] = outputA[i] ^ outputB[i] ^ msg_blocks[i];

    // memcpy all the generated values into two keys
    // 16 = sizeof(uint128_t)
    size_t key_size = sizeof(uint128_t);             // initial seed size;
    key_size += base * domain_size * sizeof(uint128_t); // correction words
    key_size += sizeof(uint128_t) * msg_block_len;   // output correction word

    k0->prf_keys_z = prf_keys_z;
    k0->k = malloc(key_size);
    k0->size = domain_size;
    k0->msg_len = msg_block_len;
    memcpy(&k0->k[0], &seedA, 16);
    for (int j = 0; j < base; j++)
    {
        memcpy(&k0->k[16 + j * domain_size * 16], &sCW[j][0], domain_size * 16);
    }
    memcpy(&k0->k[16 + base * domain_size * 16], &outputCW[0], msg_block_len * 16);

    k1->prf_keys_z = prf_keys_z;
    k1->k = malloc(key_size);
    k1->size = domain_size;
    k1->msg_len = msg_block_len;
    memcpy(&k1->k[0], &seedB, 16);
    for (int j = 0; j < base; j++)
    {
        memcpy(&k1->k[16 + j * domain_size * 16], &sCW[j][0], domain_size * 16);
    }
    memcpy(&k1->k[16 + base * domain_size * 16], &outputCW[0], msg_block_len * 16);

    free(outputA);
    free(outputB);
    free(cache);
    free(outputCW);
}

// evaluates the full DPF domain; much faster than
// batching the evaluation points since each level of the DPF tree
// is only expanded once.
void DPFFullDomainEvalZ(
    const size_t base,
    struct DPFKeyZ *key,
    uint128_t *cache,
    uint128_t *output)
{
    size_t size = key->size;
    const uint8_t *k = key->k;
    struct PRFKeysZ *prf_keys_z = key->prf_keys_z;

    if (size % 2 == 1)
    {
        uint128_t *tmp = cache;
        cache = output;
        output = tmp;
    }

    const size_t num_leaves = ipow(base, size);

    memcpy(&output[0], &k[0], 16); // output[0] is the start seed
    const uint128_t *sCW[base];
    for (int j = 0; j < base; j++)
    {
        sCW[j] = (uint128_t *)&k[16 + j * size * 16];
    }

    // inner loop variables related to node expansion
    // and correction word application
    uint128_t *tmp;
    size_t idx[base];
    uint8_t cb = 0;

    // batching variables related to chunking of inner loop processing
    // for the purpose of maximizing cache hits
    size_t max_batch_size = ipow(base, LOG_BATCH_SIZE);
    size_t batch, num_batches, batch_size, offset;

    size_t num_nodes = 1;
    for (uint8_t i = 0; i < size; i++)
    {
        if (i < LOG_BATCH_SIZE)
        {
            batch_size = num_nodes;
            num_batches = 1;
        }
        else
        {
            batch_size = max_batch_size;
            num_batches = num_nodes / max_batch_size;
        }

        offset = 0;
        for (batch = 0; batch < num_batches; batch++)
        {
            for (int j = 0; j < base; j++)
            {
                PRFBatchEval(prf_keys_z->prf_key[j], &output[offset], &cache[num_nodes * j + offset], batch_size);
            }

            for (int j = 0; j < base; j++)
            {
                idx[j] = num_nodes * j + offset;
            }

            while (idx[0] < offset + batch_size)
            {
                cb = output[idx[0]] & 1; // gets the LSB of the parent
                for (int j = 0; j < base; j++)
                {
                    cache[idx[j]] ^= (cb * sCW[j][i]);
                }

                for (int j = 0; j < base; j++)
                {
                    idx[j]++;
                }
            }

            offset += batch_size;
        }

        tmp = output;
        output = cache;
        cache = tmp;

        num_nodes *= base;
    }

    const size_t output_length = key->msg_len * num_leaves;
    const size_t msg_len = key->msg_len;
    uint128_t *outputCW = (uint128_t *)&k[16 + base * size * 16];
    ExtendOutputZ(prf_keys_z, output, cache, num_leaves, output_length);

    size_t j = 0;
    for (size_t i = 0; i < num_leaves; i++)
    {
        // TODO: a bit hacky, assumes that cache[i*msg_len] = old_output[i]
        // which is the case internally in ExtendOutput. It would be good
        // to remove this assumption however using memcpy is costly...

        if (cache[i * msg_len] & 1) // parent control bit
        {
            for (j = 0; j < msg_len; j++)
                output[i * msg_len + j] ^= outputCW[j];
        }
    }
}