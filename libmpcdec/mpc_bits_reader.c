/*
  Copyright (c) 2007-2009, The Musepack Development Team
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided
  with the distribution.

  * Neither the name of the The Musepack Development Team nor the
  names of its contributors may be used to endorse or promote
  products derived from this software without specific prior
  written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <mpc/mpcdec.h>
#include "internal.h"
#include "huffman.h"
#include "mpc_bits_reader.h"
#include "../common/cnk_tables.h"

// These tables are exposed as external symbols (declared extern in
// mpc_bits_reader.h) because the inline bit readers reference them across
// translation units. The numeric data lives in common/cnk_tables.h.
const mpc_uint32_t Cnk[MAX_ENUM / 2][MAX_ENUM] = MPC_CNK_TABLE;
const mpc_uint8_t Cnk_len[MAX_ENUM / 2][MAX_ENUM] = MPC_CNK_LEN_TABLE;
const mpc_uint32_t Cnk_lost[MAX_ENUM / 2][MAX_ENUM] = MPC_CNK_LOST_TABLE;

static const mpc_uint8_t mpc_log2[32] =
{ 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6};

static const mpc_uint8_t mpc_log2_lost[32] =
{ 0, 1, 0, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 31};

mpc_int32_t mpc_bits_golomb_dec(mpc_bits_reader * r, const mpc_uint_t k)
{
	unsigned int l = 0;
	unsigned int code = r->buff[0] & ((1 << r->count) - 1);

	while( code == 0 ) {
		l += r->count;
		r->buff++;
		code = r->buff[0];
		r->count = 8;
	}

	while( ((1 << (r->count - 1)) & code) == 0 ) {
		l++;
		r->count--;
	}
	r->count--;

	while( r->count < k ) {
		r->buff++;
		r->count += 8;
		code = (code << 8) | r->buff[0];
	}

	r->count -= k;

	return (l << k) | ((code >> r->count) & ((1 << k) - 1));
}

mpc_uint32_t mpc_bits_log_dec(mpc_bits_reader * r, mpc_uint_t max)
{
	mpc_uint32_t value = 0;
	if (max == 0)
		return 0;
	if (mpc_log2[max - 1] > 1)
		value = mpc_bits_read(r, mpc_log2[max - 1] - 1);
	if (value >= mpc_log2_lost[max - 1])
		value = ((value << 1) | mpc_bits_read(r, 1)) - mpc_log2_lost[max - 1];
	return value;
}

unsigned int mpc_bits_get_size(mpc_bits_reader * r, mpc_uint64_t * p_size)
{
	unsigned char tmp;
	mpc_uint64_t size = 0;
	unsigned int ret = 0;

	do {
		tmp = mpc_bits_read(r, 8);
		size = (size << 7) | (tmp & 0x7F);
		ret++;
	} while((tmp & 0x80));

	*p_size = size;
	return ret;
}

int mpc_bits_get_block(mpc_bits_reader * r, mpc_block * p_block)
{
	int size = 2;

	p_block->size = 0;
	p_block->key[0] = mpc_bits_read(r, 8);
	p_block->key[1] = mpc_bits_read(r, 8);

	size += mpc_bits_get_size(r, &(p_block->size));

	if ((mpc_uint64_t) size <= p_block->size) // check if the block size doesn't conflict with the header size
		p_block->size -= size;

	return size;
}



