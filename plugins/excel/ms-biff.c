/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-biff.c: MS Excel Biff support...
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2002 Michael Meeks
 *          2002 Jody Goldberg
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include <libole2/ms-ole.h>
#include <gsf/gsf-input.h>

#include "ms-biff.h"
#include "biff-types.h"

#include <stdio.h>

#define BIFF_DEBUG 0

/**
 * The only complicated bits in this code are:
 *  a) Speed optimisation of passing a raw pointer to the mapped stream back
 *  b) Handling decryption
 **/

/*******************************************************************************/
/*                             Helper Functions                                */
/*******************************************************************************/

void
dump_biff (BiffQuery *q)
{
	printf ("Opcode 0x%x length %d malloced? %d\nData:\n", q->opcode, q->length, q->data_malloced);
	if (q->length > 0)
		ms_ole_dump (q->data, q->length);
/*	dump_stream (q->pos); */
}

/*******************************************************************************/
/*                                 Read Side                                   */
/*******************************************************************************/


/* 
this is just cut out of wvMD5Final to get the byte order correct
under MSB systems, the previous code was woefully tied to intel
x86

C.
*/
static void
wvMD5StoreDigest (MD5_CTX *mdContext)
{
	unsigned int i, ii;
	/* store buffer in digest */
	for (i = 0, ii = 0; i < 4; i++, ii += 4) {
		mdContext->digest[ii] = (unsigned char) (mdContext->buf[i] & 0xFF);
		mdContext->digest[ii + 1] =
			(unsigned char) ((mdContext->buf[i] >> 8) & 0xFF);
		mdContext->digest[ii + 2] =
			(unsigned char) ((mdContext->buf[i] >> 16) & 0xFF);
		mdContext->digest[ii + 3] =
			(unsigned char) ((mdContext->buf[i] >> 24) & 0xFF);
	}
}

static void
makekey (guint32 block, RC4_KEY *key, MD5_CTX *valContext)
{
	MD5_CTX mdContext;
	guint8 pwarray[64];

	memset (pwarray, 0, 64);

	/* 40 bit of hashed password, set by verify_password () */
	memcpy (pwarray, valContext->digest, 5);

	/* put block number in byte 6...9 */
	pwarray[5] = (guint8) (block & 0xFF);
	pwarray[6] = (guint8) ((block >> 8) & 0xFF);
	pwarray[7] = (guint8) ((block >> 16) & 0xFF);
	pwarray[8] = (guint8) ((block >> 24) & 0xFF);

	pwarray[9] = 0x80;
	pwarray[56] = 0x48;

	wvMD5Init (&mdContext);
	wvMD5Update (&mdContext, pwarray, 64);
	wvMD5StoreDigest (&mdContext);
	prepare_key (mdContext.digest, 16, key);
}

/**
 * verify_password :
 *
 * convert utf8-password into utf16
 */
static gboolean
verify_password (char const *password, guint8 const *docid,
		 guint8 const *salt_data, guint8 const *hashedsalt_data,
		 MD5_CTX *valContext)
{
	guint8 pwarray [64], salt [64], hashedsalt [16];
	MD5_CTX mdContext1, mdContext2;
	RC4_KEY key;
	int offset, keyoffset, i;
	unsigned int tocopy;
	glong items_read, items_written;
	gunichar2 *utf16 = g_utf8_to_utf16 (password, -1,
		 &items_read, &items_written, NULL);

	/* we had better recieve valid utf8 */
	g_return_val_if_fail (utf16 != NULL, FALSE);

	/* Be careful about endianness */
	memset (pwarray, 0, sizeof (pwarray));
	for (i = 0 ; utf16[i] ; i++) {
		pwarray[(2 * i) + 0] = ((utf16 [i] >> 0) & 0xff);
		pwarray[(2 * i) + 1] = ((utf16 [i] >> 8) & 0xff);
	}
	g_free (utf16);

	pwarray[2 * i] = 0x80;
	pwarray[56] = (i << 4);

	wvMD5Init (&mdContext1);
	wvMD5Update (&mdContext1, pwarray, 64);
	wvMD5StoreDigest (&mdContext1);

	offset = 0;
	keyoffset = 0;
	tocopy = 5;

	wvMD5Init (valContext);
	while (offset != 16) {
		if ((64 - offset) < 5)
			tocopy = 64 - offset;

		memcpy (pwarray + offset, mdContext1.digest + keyoffset, tocopy);
		offset += tocopy;

		if (offset == 64) {
			wvMD5Update (valContext, pwarray, 64);
			keyoffset = tocopy;
			tocopy = 5 - tocopy;
			offset = 0;
			continue;
		}

		keyoffset = 0;
		tocopy = 5;
		memcpy (pwarray + offset, docid, 16);
		offset += 16;
	}

	/* Fix (zero) all but first 16 bytes */
	pwarray[16] = 0x80;
	memset (pwarray + 17, 0, 47);
	pwarray[56] = 0x80;
	pwarray[57] = 0x0A;

	wvMD5Update (valContext, pwarray, 64);
	wvMD5StoreDigest (valContext);

	/* Generate 40-bit RC4 key from 128-bit hashed password */
	makekey (0, &key, valContext);

	memcpy (salt, salt_data, 16);
	rc4 (salt, 16, &key);
	memcpy (hashedsalt, hashedsalt_data, 16);
	rc4 (hashedsalt, 16, &key);

	salt[16] = 0x80;
	memset (salt + 17, 0, 47);
	salt[56] = 0x80;

	wvMD5Init (&mdContext2);
	wvMD5Update (&mdContext2, salt, 64);
	wvMD5StoreDigest (&mdContext2);

	return 0 == memcmp (mdContext2.digest, hashedsalt, 16);
}

#define REKEY_BLOCK 0x400
static void
skip_bytes (BiffQuery *q, int start, int count)
{
	char scratch [REKEY_BLOCK];
	int block;

	block = (start + count) / REKEY_BLOCK;

	if (block != q->block) {
		makekey (q->block = block, &q->rc4_key, &q->md5_ctxt);
		count = (start + count) % REKEY_BLOCK;
	}
	rc4 (scratch, count, &q->rc4_key);
}

#define sizeof_BIFF_FILEPASS	(6 + 3*16)
/**
 * ms_biff_query_set_decrypt :
 * @q :
 * @password : password in utf8 encoding.
 */
gboolean
ms_biff_query_set_decrypt (BiffQuery *q, char const *password)
{
	g_return_val_if_fail (q->opcode == BIFF_FILEPASS, FALSE);
	g_return_val_if_fail (q->length == sizeof_BIFF_FILEPASS, FALSE);

	if (password == NULL)
		return FALSE;

	if (!verify_password (password, q->data + 6,
			      q->data + 22, q->data + 38, &q->md5_ctxt))
		return FALSE;

	q->is_encrypted = TRUE;
	q->block = -1;

	/* For some reaons the 1st record after FILEPASS seems to be unencrypted */
	q->dont_decrypt_next_record = TRUE;

	/* pretend to decrypt the entire stream up till this point, it was not
	 * encrypted, but do it anyway to keep the rc4 state in sync
	 */
	skip_bytes (q, 0, gsf_input_tell (q->input));

	return TRUE;
}

BiffQuery *
ms_biff_query_new (GsfInput *input)
{
	BiffQuery *q;

	g_return_val_if_fail (input != NULL, NULL);

	q = g_new0 (BiffQuery, 1);
	q->opcode        = 0;
	q->length        = 0;
	q->data_malloced = q->non_decrypted_data_malloced = FALSE;
	q->data 	 = q->non_decrypted_data = NULL;
	q->input         = input;
	q->is_encrypted  = FALSE;

#if BIFF_DEBUG > 0
	dump_biff (q);
#endif
	return q;
}

gboolean
ms_biff_query_peek_next (BiffQuery *q, guint16 *opcode)
{
	guint8 const *data;

	g_return_val_if_fail (opcode != NULL, 0);
	g_return_val_if_fail (q != NULL, 0);

	data = gsf_input_read (q->input, 2, NULL);
	if (data == NULL)
		return FALSE;
	*opcode = MS_OLE_GET_GUINT16 (data);

	gsf_input_seek (q->input, -2, GSF_SEEK_CUR);

	return TRUE;
}

/**
 * Returns 0 if has hit end
 **/
int
ms_biff_query_next (BiffQuery *q)
{
	guint8 const *data;
	int ans = 1;

	g_return_val_if_fail (q != NULL, 0);

	if (gsf_input_eof (q->input))
		return 0;

	if (q->data_malloced) {
		g_free (q->data);
		q->data = 0;
		q->data_malloced = FALSE;
	}
	if (q->non_decrypted_data_malloced) {
		g_free (q->non_decrypted_data);
		q->non_decrypted_data = 0;
		q->non_decrypted_data_malloced = FALSE;
	}

	q->streamPos = gsf_input_tell (q->input);
	data = gsf_input_read (q->input, 4, NULL);
	if (data == NULL)
		return FALSE;
	q->opcode = MS_OLE_GET_GUINT16 (data);
	q->length = MS_OLE_GET_GUINT16 (data + 2);
	q->ms_op  = (q->opcode>>8);
	q->ls_op  = (q->opcode&0xff);

	/* no biff record should be larger than around 20,000 */
	g_return_val_if_fail (q->length < 20000, 0);

	if (q->length > 0) 
		q->data = (guint8 *)gsf_input_read (q->input, q->length, NULL);
	else
		q->data = NULL;

	if (q->is_encrypted) {
		q->non_decrypted_data_malloced = q->data_malloced;
		q->non_decrypted_data = q->data;

		q->data_malloced = TRUE;
		q->data = g_new (guint8, q->length);
		memcpy (q->data, q->non_decrypted_data, q->length);

		if (q->dont_decrypt_next_record) {
			skip_bytes (q, q->streamPos, 4 + q->length);
			q->dont_decrypt_next_record = FALSE;
		} else {
			int pos = q->streamPos;
			char *data = q->data;
			int len = q->length;

			/* pretend to decrypt header */
			skip_bytes (q, pos, 4);
			pos += 4;

			while (q->block != (pos + len) / REKEY_BLOCK) {
				int step = REKEY_BLOCK - (pos % REKEY_BLOCK);
				rc4 (data, step, &q->rc4_key);
				data += step;
				pos += step;
				len -= step;
				makekey (++q->block, &q->rc4_key, &q->md5_ctxt);
			}

			rc4 (data, len, &q->rc4_key);
		}
	} else
		q->non_decrypted_data = q->data;

#if BIFF_DEBUG > 2
	printf ("Biff read code 0x%x, length %d\n", q->opcode, q->length);
	dump_biff (q);
#endif
	if (!q->length) {
		q->data = 0;
		return 1;
	}

	return ans;
}

void
ms_biff_query_destroy (BiffQuery *q)
{
	if (q) {
		if (q->data_malloced) {
			g_free (q->data);
			q->data = 0;
			q->data_malloced = FALSE;
		}
		if (q->non_decrypted_data_malloced) {
			g_free (q->non_decrypted_data);
			q->non_decrypted_data = 0;
			q->non_decrypted_data_malloced = FALSE;
		}
		g_free (q);
	}
}

/*******************************************************************************/
/*                                 Write Side                                  */
/*******************************************************************************/

#define MAX_LIKED_BIFF_LEN 0x2000

/* Sets up a record on a stream */
BiffPut *
ms_biff_put_new (MsOleStream *s)
{
	BiffPut *bp;
	g_return_val_if_fail (s != NULL, 0);

	bp = g_new (BiffPut, 1);

	bp->ms_op         = bp->ls_op = 0;
	bp->length        = 0;
	bp->length        = 0;
	bp->streamPos     = s->tell (s);
	bp->data_malloced = FALSE;
	bp->data          = 0;
	bp->len_fixed     = 0;
	bp->pos           = s;

	return bp;
}

void
ms_biff_put_destroy (BiffPut *bp)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->pos != NULL);

	g_free (bp);
}

guint8 *
ms_biff_put_len_next (BiffPut *bp, guint16 opcode, guint32 len)
{
	g_return_val_if_fail (bp, 0);
	g_return_val_if_fail (bp->pos, 0);
	g_return_val_if_fail (bp->data == NULL, 0);
	g_return_val_if_fail (len < MAX_LIKED_BIFF_LEN, 0);

#if BIFF_DEBUG > 0
	printf ("Biff put len 0x%x\n", opcode);
#endif

	bp->len_fixed  = 1;
	bp->ms_op      = (opcode >>   8);
	bp->ls_op      = (opcode & 0xff);
	bp->length     = len;
	bp->streamPos  = bp->pos->tell (bp->pos);
	if (len > 0) {
		bp->data = g_new (guint8, len);
		bp->data_malloced = TRUE;
	}

	return bp->data;
}
void
ms_biff_put_var_next (BiffPut *bp, guint16 opcode)
{
	guint8 data[4];
	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->pos != NULL);

#if BIFF_DEBUG > 0
	printf ("Biff put var 0x%x\n", opcode);
#endif

	bp->len_fixed  = 0;
	bp->ms_op      = (opcode >>   8);
	bp->ls_op      = (opcode & 0xff);
	bp->curpos     = 0;
	bp->length     = 0;
	bp->data       = 0;
	bp->streamPos  = bp->pos->tell (bp->pos);

	MS_OLE_SET_GUINT16 (data,    opcode);
	MS_OLE_SET_GUINT16 (data + 2,0xfaff); /* To be corrected later */
	bp->pos->write (bp->pos, data, 4);
}

void
ms_biff_put_var_write  (BiffPut *bp, guint8 *data, guint32 len)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (data != NULL);
	g_return_if_fail (bp->pos != NULL);

	g_return_if_fail (!bp->data);
	g_return_if_fail (!bp->len_fixed);

	/* Temporary */
	g_return_if_fail (bp->length + len < 0xf000);

	bp->pos->write (bp->pos, data, len);
	bp->curpos+= len;
	if (bp->curpos > bp->length)
		bp->length = bp->curpos;
}

void
ms_biff_put_var_seekto (BiffPut *bp, MsOlePos pos)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->pos != NULL);

	g_return_if_fail (!bp->len_fixed);
	g_return_if_fail (!bp->data);

	bp->curpos = pos;
	bp->pos->lseek (bp->pos, bp->streamPos + bp->curpos + 4, MsOleSeekSet);
}

static void
ms_biff_put_var_commit (BiffPut *bp)
{
	guint8   tmp [4];
	MsOlePos endpos;

	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->pos != NULL);

	g_return_if_fail (!bp->len_fixed);
	g_return_if_fail (!bp->data);

	endpos = bp->streamPos + bp->length + 4;
	bp->pos->lseek (bp->pos, bp->streamPos, MsOleSeekSet);

	MS_OLE_SET_GUINT16 (tmp, (bp->ms_op<<8) + bp->ls_op);
	MS_OLE_SET_GUINT16 (tmp+2, bp->length);
	bp->pos->write (bp->pos, tmp, 4);

	bp->pos->lseek (bp->pos, endpos, MsOleSeekSet);
	bp->streamPos  = endpos;
	bp->curpos     = 0;
}
static void
ms_biff_put_len_commit (BiffPut *bp)
{
	guint8  tmp[4];

	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->pos != NULL);
	g_return_if_fail (bp->len_fixed);
	g_return_if_fail (bp->length == 0 || bp->data);
	g_return_if_fail (bp->length < MAX_LIKED_BIFF_LEN);

/*	if (!bp->data_malloced) Unimplemented optimisation
		bp->pos->lseek (bp->pos, bp->length, MsOleSeekCur);
		else */
	MS_OLE_SET_GUINT16 (tmp, (bp->ms_op<<8) + bp->ls_op);
	MS_OLE_SET_GUINT16 (tmp + 2, bp->length);
	bp->pos->write (bp->pos, tmp, 4);
	bp->pos->write (bp->pos, bp->data, bp->length);

	g_free (bp->data);
	bp->data      = 0 ;
	bp->data_malloced = FALSE;
	bp->streamPos = bp->pos->tell (bp->pos);
	bp->curpos    = 0;
}

void
ms_biff_put_commit (BiffPut *bp)
{
	if (bp->len_fixed)
		ms_biff_put_len_commit (bp);
	else
		ms_biff_put_var_commit (bp);
}
