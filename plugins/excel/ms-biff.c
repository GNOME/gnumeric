/**
 * ms-biff.c: MS Excel Biff support...
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 * (C) 2016 Morten Welinder
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>

#include "ms-biff.h"
#include "biff-types.h"
#include "ms-excel-util.h"
#include "md5.h"

#include <gsf/gsf-input.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>
#include <goffice/goffice.h>

#define BIFF_DEBUG 0
#define sizeof_BIFF_8_FILEPASS	 (6 + 3*16)
#define sizeof_BIFF_2_7_FILEPASS  4

/**
 * The only complicated bits in this code are:
 *  a) Speed optimisation of passing a raw pointer to the mapped stream back
 *  b) Handling decryption
 **/

/*******************************************************************************/
/*                             Helper Functions                                */
/*******************************************************************************/

static void
destroy_sensitive (void *p, size_t len)
{
	if (len > 0) {
		memset (p, 0, len);
		memset (p, 0xaa, len - 1);
		go_destroy_password (p);
	}
}

void
ms_biff_query_dump (BiffQuery *q)
{
	const char *opname = biff_opcode_name (q->opcode);
	g_print ("Opcode 0x%x (%s) length %d malloced? %d\nData:\n",
		 q->opcode,
		 opname ? opname : "?",
		 q->length,
		 q->data_malloced);
	if (q->length > 0)
		gsf_mem_dump (q->data, q->length);
/*	dump_stream (q->output); */
}

guint32
ms_biff_query_bound_check (BiffQuery *q, guint32 offset, unsigned len)
{
	if (offset >= q->length) {
		guint16 opcode;

		offset -= q->length;
		if (!ms_biff_query_peek_next (q, &opcode) ||
		    opcode != BIFF_CONTINUE ||
		    !ms_biff_query_next (q)) {
			g_warning ("missing CONTINUE");
			return (guint32)-1;
		}
	}

	if ((offset + len) > q->length) {
		g_warning ("supposedly atomic item of len %u sst spans CONTINUEs, we are screwed", len);
		return (guint32)-1;
	}
	return offset;
}

/*****************************************************************************/
/*                                Read Side                                  */
/*****************************************************************************/

/**
 *  ms_biff_password_hash and ms_biff_crypt_seq
 * based on pseudo-code in the OpenOffice.org XL documentation
 **/
static guint16
ms_biff_password_hash  (guint8 const *password)
{
	int tmp, index = 0, len = strlen ((char const *)password);
	guint16 chr, hash= 0;

	do {
		chr = password[index];
		index++;
		tmp = (chr << index);
		hash ^= (tmp & 0x7fff) | (tmp >> 15);
	} while (index < len);
	hash = hash ^ len ^ 0xce4b;

	return hash;
}

static void
ms_biff_crypt_seq (BiffQuery *q, guint16 key, guint8 const *password)
{
	static guint8 const preset [] = {
		0xbb, 0xff, 0xff, 0xba, 0xff, 0xff, 0xb9, 0x80,
		0x00, 0xbe, 0x0f, 0x00, 0xbf, 0x0f, 0x00, 0x00
	};
	guint8 const low  =        key & 0xff;
	guint8 const high = (key >> 8) & 0xff;
	unsigned i, len = strlen ((char const*)password);
	guint8 *seq = q->xor_key;

	strncpy (seq, password, 16);
	for (i = 0; (len + i) < 16; i++)
		seq[len + i] = preset[i];

	for (i = 0; i < 16; i += 2) {
		seq[i]   ^= low;
		seq[i+1] ^= high;
	}

	for (i = 0; i < 16; i++)
		seq[i] = (seq[i] << 2) | (seq[i] >> 6);
}

static gboolean
ms_biff_pre_biff8_query_set_decrypt  (BiffQuery *q, guint8 const *password)
{
	guint16 hash, key;
	guint16 pw_hash = ms_biff_password_hash (password);


	if (q->length == 4) {
		key = GSF_LE_GET_GUINT16(q->data + 0);
		hash = GSF_LE_GET_GUINT16(q->data + 2);
	} else if (q->length == 6) {
		/* BIFF8 record with pre-biff8 crypto, these do exist */
		key = GSF_LE_GET_GUINT16(q->data + 2);
		hash = GSF_LE_GET_GUINT16(q->data + 4);
	} else {
		return FALSE;
	}

	if (hash != pw_hash)
		return FALSE;

	ms_biff_crypt_seq (q, key, password);

	q->encryption = MS_BIFF_CRYPTO_XOR;
	return TRUE;
}

static void
makekey (guint32 block, RC4_KEY *key, const unsigned char *valDigest)
{
	struct md5_ctx ctx;
	unsigned char digest[16];
	guint8 pwarray[64];

	memset (pwarray, 0, 64);

	/* 40 bit of hashed password, set by verify_password () */
	memcpy (pwarray, valDigest, 5);

	GSF_LE_SET_GUINT32 (pwarray + 5, block);
	pwarray[9] = 0x80;
	pwarray[56] = 0x48;

	md5_init_ctx (&ctx);
	md5_process_block (pwarray, 64, &ctx);
	md5_read_ctx (&ctx, digest);
	prepare_key (digest, 16, key);

	destroy_sensitive (&ctx, sizeof (ctx));
	destroy_sensitive (digest, sizeof (digest));
	destroy_sensitive (pwarray, sizeof (pwarray));
}

/**
 * verify_password :
 *
 * convert UTF-8-password into UTF-16
 */
static gboolean
verify_password (guint8 const *password, guint8 const *docid,
		 guint8 const *salt_data, guint8 const *hashedsalt_data,
		 unsigned char *valDigest)
{
	guint8 pwarray [64], salt [64], hashedsalt [16];
	struct md5_ctx mdContext;
	unsigned char digest[16];
	RC4_KEY key;
	int offset, keyoffset, i;
	unsigned int tocopy;
	gboolean res;
	gunichar2 *utf16 = g_utf8_to_utf16 (password, -1, NULL, NULL, NULL);

	/* we had better receive valid UTF-8 */
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

	md5_init_ctx (&mdContext);
	md5_process_block (pwarray, 64, &mdContext);
	md5_read_ctx (&mdContext, digest);

	offset = 0;
	keyoffset = 0;
	tocopy = 5;

	md5_init_ctx (&mdContext);
	while (offset != 16) {
		if ((64 - offset) < 5)
			tocopy = 64 - offset;

		memcpy (pwarray + offset, digest + keyoffset, tocopy);
		offset += tocopy;

		if (offset == 64) {
			md5_process_block (pwarray, 64, &mdContext);
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

	md5_process_block (pwarray, 64, &mdContext);
	md5_read_ctx (&mdContext, valDigest);

	/* Generate 40-bit RC4 key from 128-bit hashed password */
	makekey (0, &key, valDigest);

	memcpy (salt, salt_data, 16);
	rc4 (salt, 16, &key);
	memcpy (hashedsalt, hashedsalt_data, 16);
	rc4 (hashedsalt, 16, &key);

	salt[16] = 0x80;
	memset (salt + 17, 0, 47);
	salt[56] = 0x80;

	md5_init_ctx (&mdContext);
	md5_process_block (salt, 64, &mdContext);
	md5_read_ctx (&mdContext, digest);

	res = memcmp (digest, hashedsalt, 16) == 0;

	destroy_sensitive (pwarray, sizeof (pwarray));
	destroy_sensitive (salt, sizeof (salt));
	destroy_sensitive (hashedsalt, sizeof (hashedsalt));
	destroy_sensitive (&mdContext, sizeof (mdContext));
	destroy_sensitive (digest, sizeof (digest));
	destroy_sensitive (&key, sizeof (key));

	return res;
}

#define REKEY_BLOCK 0x400
static void
skip_bytes (BiffQuery *q, int start, int count)
{
	static guint8 scratch[REKEY_BLOCK];
	int block;

	block = (start + count) / REKEY_BLOCK;

	if (block != q->block) {
		makekey (q->block = block, &q->rc4_key, q->md5_digest);
		count = (start + count) % REKEY_BLOCK;
	}

	g_assert (count <= REKEY_BLOCK);
	rc4 (scratch, count, &q->rc4_key);
}

/**
 * ms_biff_query_set_decrypt :
 * @q:
 * @password: password in UTF-8 encoding.
 **/
gboolean
ms_biff_query_set_decrypt (BiffQuery *q, MsBiffVersion version,
			   guint8 const *password)
{
	g_return_val_if_fail (q->opcode == BIFF_FILEPASS, FALSE);

	if (password == NULL)
		return FALSE;

	if (version < MS_BIFF_V8 || q->length == 0 || q->data[0] == 0)
		return ms_biff_pre_biff8_query_set_decrypt (q, password);

	XL_CHECK_CONDITION_VAL (q->length == sizeof_BIFF_8_FILEPASS, FALSE);

	if (!verify_password (password, q->data + 6,
			      q->data + 22, q->data + 38, q->md5_digest))
		return FALSE;

	q->encryption = MS_BIFF_CRYPTO_RC4;
	q->block = -1;

	/* For some reaons the 1st record after FILEPASS seems to be unencrypted */
	q->dont_decrypt_next_record = TRUE;

	/* pretend to decrypt the entire stream up till this point, it was not
	 * encrypted, but do it anyway to keep the rc4 state in sync
	 */
	skip_bytes (q, 0, gsf_input_tell (q->input));

	return TRUE;
}
void
ms_biff_query_copy_decrypt (BiffQuery *dst, BiffQuery const *src)
{
	g_return_if_fail (dst != NULL);
	g_return_if_fail (src != NULL);

#warning FINISH this
	switch (src->encryption) {
	default :
	case MS_BIFF_CRYPTO_NONE:
		XL_CHECK_CONDITION (dst->encryption == MS_BIFF_CRYPTO_NONE);
		break;
	case MS_BIFF_CRYPTO_XOR :
		break;
	case MS_BIFF_CRYPTO_RC4 :
		break;
	}
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
	q->data		 = q->non_decrypted_data = NULL;
	q->input         = input;
	q->encryption    = MS_BIFF_CRYPTO_NONE;

#if BIFF_DEBUG > 0
	ms_biff_query_dump (q);
#endif
	return q;
}

gboolean
ms_biff_query_peek_next (BiffQuery *q, guint16 *opcode)
{
	guint8 const *data;
	guint16 len;

	g_return_val_if_fail (opcode != NULL, FALSE);
	g_return_val_if_fail (q != NULL, FALSE);

	data = gsf_input_read (q->input, 4, NULL);
	if (data == NULL)
		return FALSE;
	*opcode = GSF_LE_GET_GUINT16 (data);
	len = GSF_LE_GET_GUINT16 (data + 2);
	gsf_input_seek (q->input, -4, G_SEEK_CUR);

	return gsf_input_remaining (q->input) >= 4 + len;
}

/**
 * Returns FALSE if has hit end
 **/
gboolean
ms_biff_query_next (BiffQuery *q)
{
	guint8 const *data;
	guint16 len;
	gboolean auto_continue;

	g_return_val_if_fail (q != NULL, FALSE);

	if (gsf_input_eof (q->input))
		return FALSE;

	if (q->data_malloced) {
		g_free (q->data);
		q->data = NULL;
		q->data_malloced = FALSE;
	}
	if (q->non_decrypted_data_malloced) {
		g_free (q->non_decrypted_data);
		q->non_decrypted_data = NULL;
		q->non_decrypted_data_malloced = FALSE;
	}

	q->streamPos = gsf_input_tell (q->input);
	data = gsf_input_read (q->input, 4, NULL);
	if (data == NULL)
		return FALSE;
	q->opcode = GSF_LE_GET_GUINT16 (data);
	len = GSF_LE_GET_GUINT16 (data + 2);

	q->data   = NULL;
	q->length = 0;

	/* no biff record should be larger than around 20,000 */
	XL_CHECK_CONDITION_VAL (len < 20000, FALSE);

	if (len > 0) {
		q->data = (guint8 *)gsf_input_read (q->input, len, NULL);
		if (q->data == NULL)
			return FALSE;
	}
	q->length = len;

	if (q->encryption == MS_BIFF_CRYPTO_RC4) {
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
			guint8 *data = q->data;
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
				makekey (++q->block, &q->rc4_key, q->md5_digest);
			}

			rc4 (data, len, &q->rc4_key);
		}
	} else if (q->encryption == MS_BIFF_CRYPTO_XOR) {
		unsigned int offset, k;

		q->non_decrypted_data_malloced = q->data_malloced;
		q->non_decrypted_data = q->data;
		q->data_malloced = TRUE;
		q->data = g_new (guint8, q->length);
		memcpy (q->data, q->non_decrypted_data, q->length);

		offset = (q->streamPos + q->length + 4) % 16;
		for (k= 0; k < q->length; ++k) {
			guint8 tmp = (q->data[k] << 3) | (q->data[k] >> 5);
			q->data[k] = tmp ^ q->xor_key[offset];
			offset = (offset + 1) % 16;
		}
	} else {
		q->non_decrypted_data = q->data;
#if 0
		// Turn this on to debug memory access beyond record
		// end.
		q->non_decrypted_data_malloced = q->data_malloced;
		q->data = go_memdup (q->data, q->length);
		q->data_malloced = TRUE;
#endif
	}

#if BIFF_DEBUG > 2
	ms_biff_query_dump (q);
#endif

	/*
	 * I am guessing that we should always handle BIFF_CONTINUE here,
	 * except for the few record types exempt from encryption.  For
	 * now, however, do the bare minimum.
	 */
	switch (q->opcode) {
	case BIFF_BG_PIC:
	case BIFF_CF:
	case BIFF_CODENAME:
	case BIFF_CONDFMT:
	case BIFF_DV:
	case BIFF_DVAL:
	case BIFF_EXTERNNAME_v0:
	case BIFF_EXTERNNAME_v2:
	case BIFF_EXTERNSHEET:
	case BIFF_FONT_v0:
	case BIFF_FONT_v2:
	case BIFF_FOOTER:
	case BIFF_FORMAT_v0:
	case BIFF_FORMAT_v4:
	case BIFF_FORMULA_v0:
	case BIFF_FORMULA_v2:
	case BIFF_FORMULA_v4:
	case BIFF_HEADER:
	case BIFF_HLINK:
	case BIFF_IMDATA:
	case BIFF_LABEL_v0:
	case BIFF_LABEL_v2:
	case BIFF_MERGECELLS:
	case BIFF_NAME_v0:
	case BIFF_NAME_v2:
	case BIFF_NOTE:
	case BIFF_STRING_v0:
	case BIFF_STRING_v2:
	case BIFF_SUPBOOK:
		auto_continue = TRUE;
		break;

	case BIFF_BOUNDSHEET:
		// Needs access to non_decrypted_data
	case BIFF_CONTINUE:
		// Eh?
	case BIFF_TXO:
		/*
		 * The continuation of the string part seems to repeat the
		 * unicode marker so we clearly cannot just concatenate.
		 */
	default:
		auto_continue = FALSE;
	}
	if (auto_continue) {
		guint16 opcode;

		while (ms_biff_query_peek_next (q, &opcode) &&
		       opcode == BIFF_CONTINUE) {
			GString *data = g_string_new_len (q->data, q->length);
			opcode = q->opcode;
			if (!ms_biff_query_next (q)) {
				g_string_free (data, TRUE);
				return FALSE; /* Probably shouldn't happen */
			}
			q->opcode = opcode;
			g_string_append_len (data, q->data, q->length);
			if (q->data_malloced)
				g_free (q->data);
			q->length = data->len;
			q->data = g_string_free (data, FALSE);
			q->data_malloced = TRUE;
		}
	}

	return TRUE;
}

void
ms_biff_query_destroy (BiffQuery *q)
{
	if (q) {
		if (q->data_malloced) {
			g_free (q->data);
			q->data = NULL;
			q->data_malloced = FALSE;
		}
		if (q->non_decrypted_data_malloced) {
			g_free (q->non_decrypted_data);
			q->non_decrypted_data = NULL;
			q->non_decrypted_data_malloced = FALSE;
		}

		/* Paranoia: */
		destroy_sensitive (q, sizeof (*q));

		g_free (q);
	}
}

/*****************************************************************************/
/*                                Write Side                                 */
/*****************************************************************************/

#define MAX_BIFF7_RECORD_SIZE 0x820
#define MAX_BIFF8_RECORD_SIZE 0x2020

/**
 * ms_biff_put_new :
 * @output: (transfer full): the output storage
 * @version: file format version
 * @codepage: Codepage to use for strings.  Only used pre-BIff8 and ignored
 *   unless positive and for
 *
 **/
BiffPut *
ms_biff_put_new (GsfOutput *output, MsBiffVersion version, int codepage)
{
	BiffPut *bp;

	g_return_val_if_fail (output != NULL, NULL);

	bp = g_new (BiffPut, 1);

	bp->opcode        = 0;
	bp->streamPos     = gsf_output_tell (output);
	bp->len_fixed     = -1;
	bp->output        = output;
	bp->version       = version;

	bp->record = g_string_new (NULL);

	if (version >= MS_BIFF_V8) {
		bp->convert = g_iconv_open ("UTF-16LE", "UTF-8");
		bp->codepage = 1200;
	} else {
		bp->codepage = (codepage > 0)
			? codepage
			: gsf_msole_iconv_win_codepage ();
		bp->convert = gsf_msole_iconv_open_codepage_for_export (bp->codepage);
	}

	return bp;
}

void
ms_biff_put_destroy (BiffPut *bp)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->output != NULL);

	if (bp->output != NULL) {
		gsf_output_close (bp->output);
		g_object_unref (bp->output);
	}

	g_string_free (bp->record, TRUE);
	gsf_iconv_close (bp->convert);

	g_free (bp);
}

guint8 *
ms_biff_put_len_next (BiffPut *bp, guint16 opcode, guint32 len)
{
	g_return_val_if_fail (bp, NULL);
	g_return_val_if_fail (bp->output, NULL);
	g_return_val_if_fail (bp->len_fixed == -1, NULL);

#if BIFF_DEBUG > 0
	{
		const char *opname = biff_opcode_name (opcode);
		g_printerr ("Biff put len 0x%x (%s)\n",
			    opcode, opname ? opname : "?");
	}
#endif

	bp->len_fixed  = +1;
	bp->opcode     = opcode;
	bp->streamPos  = gsf_output_tell (bp->output);

	g_string_set_size (bp->record, len);

	return bp->record->str;
}

void
ms_biff_put_var_next (BiffPut *bp, guint16 opcode)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->output != NULL);
	g_return_if_fail (bp->len_fixed == -1);

#if BIFF_DEBUG > 0
	{
		const char *opname = biff_opcode_name (opcode);
		g_printerr ("Biff put var 0x%x (%s)\n",
			    opcode, opname ? opname : "?");
	}
#endif

	bp->len_fixed  = 0;
	bp->opcode     = opcode;
	bp->curpos     = 0;
	bp->streamPos  = gsf_output_tell (bp->output);

	g_string_set_size (bp->record, 0);
}

inline unsigned
ms_biff_max_record_len (BiffPut const *bp)
{
	return (bp->version >= MS_BIFF_V8) ? MAX_BIFF8_RECORD_SIZE : MAX_BIFF7_RECORD_SIZE;
}

void
ms_biff_put_var_write (BiffPut *bp, guint8 const *data, guint32 len)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (data != NULL);
	g_return_if_fail (bp->output != NULL);
	g_return_if_fail ((gint32)len >= 0);

	g_return_if_fail (bp->len_fixed == 0);

	/* Make room */
	if (bp->curpos + len > bp->record->len)
		g_string_set_size (bp->record, bp->curpos + len);

	/* Copy data */
	memcpy (bp->record->str + bp->curpos, data, len);

	bp->curpos += len;
}

void
ms_biff_put_var_seekto (BiffPut *bp, int pos)
{
	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->output != NULL);
	g_return_if_fail (bp->len_fixed == 0);
	g_return_if_fail (pos >= 0);

	bp->curpos = pos;
}

void
ms_biff_put_commit (BiffPut *bp)
{
	guint16 opcode;
	size_t len, maxlen;
	const char *data;

	g_return_if_fail (bp != NULL);
	g_return_if_fail (bp->output != NULL);

	maxlen = ms_biff_max_record_len (bp);

	opcode = bp->opcode;
	len = bp->record->len;
	data = bp->record->str;
	do {
		guint8 tmp[4];
		size_t thislen = MIN (len, maxlen);

		GSF_LE_SET_GUINT16 (tmp, opcode);
		GSF_LE_SET_GUINT16 (tmp + 2, thislen);
		gsf_output_write (bp->output, 4, tmp);
		gsf_output_write (bp->output, thislen, data);

		opcode = BIFF_CONTINUE;
		data += thislen;
		len -= thislen;
	} while (len > 0);

	bp->streamPos = gsf_output_tell (bp->output);
	bp->curpos    = 0;
	bp->len_fixed = -1;

	if (0) {
		/*
		 * This is useful to figure out who writes uninitialized
		 * memory to files.  It causes layers below to flush.
		 */
		gsf_output_seek (bp->output, -1, G_SEEK_CUR);
		gsf_output_seek (bp->output, +1, G_SEEK_CUR);
	}
}

void
ms_biff_put_empty (BiffPut *bp, guint16 opcode)
{
	ms_biff_put_len_next (bp, opcode, 0);
	ms_biff_put_commit (bp);
}

void
ms_biff_put_2byte (BiffPut *bp, guint16 opcode, guint16 content)
{
	guint8 *data = ms_biff_put_len_next (bp, opcode, 2);
	GSF_LE_SET_GUINT16 (data, content);
	ms_biff_put_commit (bp);
}

void
ms_biff_put_abs_write (BiffPut *bp, gsf_off_t pos, gconstpointer buf, gsize size)
{
	gsf_off_t old = gsf_output_tell (bp->output);
	gsf_output_seek (bp->output, pos, G_SEEK_SET);
	gsf_output_write (bp->output, size, buf);
	gsf_output_seek (bp->output, old, G_SEEK_SET);
}
