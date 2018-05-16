/**
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#ifndef GNM_BIFF_H
#define GNM_BIFF_H

#include "rc4.h"

typedef enum { MS_BIFF_CRYPTO_NONE = 0,
	       MS_BIFF_CRYPTO_XOR,
	       MS_BIFF_CRYPTO_RC4,
	       MS_BIFF_CRYPTO_UNKNOWN } MsBiffCrypto ;

typedef enum {
	MS_BIFF_V_UNKNOWN = 0,
	MS_BIFF_V2 = 2,
	MS_BIFF_V3 = 3,
	MS_BIFF_V4 = 4,
	MS_BIFF_V5 = 5, /* Excel 5.0 */
	MS_BIFF_V7 = 7, /* Excel 95 */
	MS_BIFF_V8 = 8  /* Excel 97, 2000, XP, 2003 */
} MsBiffVersion;

/*****************************************************************************/
/*                                Read Side                                  */
/*****************************************************************************/

/**
 * Returns query data, it is imperative that copies of
 * 'data *' should _not_ be kept.
 **/
typedef struct {
	guint16	  opcode;
	guint32	  length;
	gboolean  data_malloced, non_decrypted_data_malloced;
	guint8	 *data, *non_decrypted_data;

	guint32 streamPos;
	GsfInput *input;

	MsBiffCrypto encryption;
	guint8   xor_key[16];
	RC4_KEY	 rc4_key;
	unsigned char md5_digest[16];
	int	 block;
	gboolean dont_decrypt_next_record;
} BiffQuery;

/* Sets up a query on a stream */
BiffQuery  *ms_biff_query_new          (GsfInput *);
gboolean    ms_biff_query_set_decrypt  (BiffQuery *q, MsBiffVersion version,
					guint8 const *password);
void	    ms_biff_query_copy_decrypt (BiffQuery *dst, BiffQuery const *src);

/* Updates the BiffQuery structure with the next BIFF record
 * returns: TRUE for succes, and FALSE for EOS(tream) */
gboolean    ms_biff_query_next        (BiffQuery *);
gboolean    ms_biff_query_peek_next   (BiffQuery *, guint16 *opcode);
void        ms_biff_query_destroy     (BiffQuery *);
void        ms_biff_query_dump	      (BiffQuery *);
guint32     ms_biff_query_bound_check (BiffQuery *q,
				       guint32 offset, unsigned len);

/*****************************************************************************/
/*                                Write Side                                 */
/*****************************************************************************/

typedef struct _BiffPut {
	guint16		 opcode;
	gsf_off_t	 streamPos;
	unsigned	 curpos; /* Curpos is offset from beginning of header */
	int		 len_fixed;
	GsfOutput	*output;
	MsBiffVersion	 version;

	/*
	 * Records are stored here until committed at which time they may
	 * by split using BIFF_CONTINUE records.
	 */
	GString         *record;

	int	 codepage;
	GIConv   convert;
} BiffPut;

/* Sets up a record on a stream */
BiffPut *ms_biff_put_new     (GsfOutput *, MsBiffVersion version, int codepage);
void     ms_biff_put_destroy (BiffPut *bp);

/**
 * If between the 'next' and 'commit' ls / ms_op are changed they will be
 * written correctly.
 **/
/* For known length records shorter than 0x2000 bytes. */
guint8  *ms_biff_put_len_next   (BiffPut *bp, guint16 opcode, guint32 len);
/* For unknown length records */
void     ms_biff_put_var_next   (BiffPut *bp, guint16 opcode);
void     ms_biff_put_var_write  (BiffPut *bp, guint8 const *dat, guint32 len);
/* Seeks to pos bytes after the beggining of the record */
void     ms_biff_put_var_seekto (BiffPut *bp, int pos);
/* Must commit after each record */
void     ms_biff_put_commit     (BiffPut *bp);

/* convenience routines for simple records */
void     ms_biff_put_empty   (BiffPut *bp, guint16 opcode);
void     ms_biff_put_2byte   (BiffPut *bp, guint16 opcode, guint16 data);

unsigned ms_biff_max_record_len (BiffPut const *bp);

void ms_biff_put_abs_write (BiffPut *bp, gsf_off_t pos, gconstpointer buf, gsize size);

#endif /* GNM_BIFF_H */
