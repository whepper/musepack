/*
 * Musepack audio compression
 * Copyright (c) 2005-2009, The Musepack Development Team
 * Copyright (C) 1999-2004 Buschmann/Klemm/Piecha/Wolf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef _WIN32
#include <windows.h>
#define sleep(t) Sleep((t) * 1000)
#else
#include <unistd.h>
#endif

#include "libmpcenc.h"
#include "stdio.h"
#include "../common/cnk_tables.h"
#include "../common/fileio.h"

unsigned long mpc_crc32(unsigned char *buf, int len);

#define MAX_ENUM 32

// Private static copies of the shared combinatorial tables (see
// common/cnk_tables.h). The encoder must not link against libmpcdec.
static const mpc_uint32_t Cnk[MAX_ENUM / 2][MAX_ENUM] = MPC_CNK_TABLE;
static const mpc_uint8_t Cnk_len[MAX_ENUM / 2][MAX_ENUM] = MPC_CNK_LEN_TABLE;
static const mpc_uint32_t Cnk_lost[MAX_ENUM / 2][MAX_ENUM] = MPC_CNK_LOST_TABLE;

static const mpc_uint8_t mpc_log2[32] =
{ 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6};

static const mpc_uint8_t mpc_log2_lost[32] =
{ 0, 1, 0, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 31};

void emptyBits(mpc_encoder_t * e)
{
	while( e->bitsCount >= 8 ){
		e->bitsCount -= 8;
		e->buffer[e->pos] = (mpc_uint8_t) (e->bitsBuff >> e->bitsCount);
		e->pos++;
	}
}

unsigned int encodeSize(mpc_uint64_t size, char * buff, mpc_bool_t addCodeSize)
{
	unsigned int i = 1;
	int j;

	if (addCodeSize) {
		while ((1ull << (7 * i)) - i <= size) i++;
		size += i;
	} else
		while ((1ull << (7 * i)) <= size) i++;

	for( j = i - 1; j >= 0; j--){
		buff[j] = (char) (size | 0x80);
		size >>= 7;
	}
	buff[i - 1] &= 0x7F;

	return i;
}

static void encodeGolomb(mpc_encoder_t * e, mpc_uint32_t nb, mpc_uint_t k)
{
	unsigned int l = (nb >> k) + 1;
	nb &= (1 << k) - 1;

	while( l > 31 ){
		writeBits(e, 0, 31);
		l -= 31;
	}
	writeBits(e, 1, l);
	writeBits(e, nb, k);
}

void encodeEnum(mpc_encoder_t * e, const mpc_uint32_t bits, const mpc_uint_t N)
{
	mpc_uint32_t code = 0;
	const mpc_uint32_t * C = Cnk[0];
	unsigned int n = 0, k = 0;

	for( ; n < N; n++){
		if ((bits >> n) & 1) {
			code += C[n];
			C += MAX_ENUM;
			k++;
		}
	}

	if (k == 0) return;

	if (code < Cnk_lost[k-1][n-1])
		writeBits(e, code, Cnk_len[k-1][n-1] - 1);
	else
		writeBits(e, code + Cnk_lost[k-1][n-1], Cnk_len[k-1][n-1]);
}

void encodeLog(mpc_encoder_t * e, mpc_uint32_t value, mpc_uint32_t max)
{
	if (value < mpc_log2_lost[max - 1])
		writeBits(e, value, mpc_log2[max - 1] - 1);
	else
		writeBits(e, value + mpc_log2_lost[max - 1], mpc_log2[max - 1]);
}

void writeMagic(mpc_encoder_t * e)
{
	fwrite("MPCK", sizeof(char), 4, e->outputFile);
	e->outputBits += 32;
	e->framesInBlock = 0;
}

mpc_uint32_t writeBlock ( mpc_encoder_t * e, const char * key, const mpc_bool_t addCRC, mpc_uint32_t min_size)
{
	FILE * fp = e->outputFile;
	mpc_uint32_t written = 0;
	mpc_uint8_t * datas = e->buffer;
	char blockSize[10];
	mpc_uint32_t len;

	writeBits(e, 0, (8 - e->bitsCount) % 8);
	emptyBits(e);

	// write block header (key / length)
	len = e->pos + (addCRC > 0) * 4;
	if (min_size <= len)
		min_size = len;
	else {
		mpc_uint32_t pad = min_size - len, i;
		for(i = 0; i < pad; i++)
			writeBits(e, 0, 8);
		emptyBits(e);
	}
	len = encodeSize(min_size + 2, blockSize, MPC_TRUE);
	fwrite(key, sizeof(char), 2, fp);
	fwrite(blockSize, sizeof(char), len, fp);
	e->outputBits += (len + 2) * 8;

	if (addCRC) {
		char tmp[4];
		unsigned long CRC32 = mpc_crc32((unsigned char *) e->buffer, e->pos);
		tmp[0] = (char) (CRC32 >> 24);
		tmp[1] = (char) (CRC32 >> 16);
		tmp[2] = (char) (CRC32 >> 8);
		tmp[3] = (char) CRC32;
		fwrite(tmp, sizeof(char), 4, fp);
		e->outputBits += 32;
	}

	// write datas
	while ( e->pos != 0 ) {
		written = fwrite ( datas, sizeof(*e->buffer), e->pos, fp );
		if ( written == 0 ) {
			fprintf(stderr, "\b\n WARNING: Disk full?, retry after 10 sec ...\a");
            sleep (10);
        }
		if ( written > 0 ) {
			datas += written;
			e->pos -= written;
        }
	}
	e->framesInBlock = 0;

	return min_size;
}

void writeSeekTable (mpc_encoder_t * e)
{
	mpc_uint32_t len;
	mpc_seek_t i;
	mpc_seek_t * table = e->seek_table;
	mpc_uint8_t tmp[10];

	// write the position to header
	i = mpc_file_tell(e->outputFile); // get the seek table position
	len = encodeSize(i - e->seek_ptr, (char*)tmp, MPC_FALSE);
	mpc_file_seek(e->outputFile, e->seek_ptr + 3, SEEK_SET);
	fwrite(tmp, sizeof(mpc_uint8_t), len, e->outputFile);
	mpc_file_seek(e->outputFile, i, SEEK_SET);

	// write the seek table datas
	len = encodeSize(e->seek_pos, (char*)tmp, MPC_FALSE);
	for( i = 0; i < len; i++)
		writeBits ( e, tmp[i], 8 );
	writeBits ( e, e->seek_pwr, 4 );

	len = encodeSize(table[0] - e->seek_ref, (char*)tmp, MPC_FALSE);
	for( i = 0; i < len; i++)
		writeBits ( e, tmp[i], 8 );
	if (e->seek_pos > 1) {
		len = encodeSize(table[1] - e->seek_ref, (char*)tmp, MPC_FALSE);
		for( i = 0; i < len; i++)
			writeBits ( e, tmp[i], 8 );
	}

	for( i = 2; i < e->seek_pos; i++){
		mpc_int64_t code = (mpc_int64_t)(table[i] - 2 * table[i-1] + table[i-2]) << 1;
		if (code < 0)
			code = -code | 1;
		encodeGolomb(e, (mpc_uint32_t) code, 12);
	}
}

/* end of bitstream.c */
