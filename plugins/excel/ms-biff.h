/**
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2002 Michael Meeks
 **/
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H

#include <libole2/ms-ole.h>

/*******************************************************************************/
/*                                 Read Side                                   */
/*******************************************************************************/

/**
 * Returns query data, it is imperative that copies of
 * 'data *' should _not_ be kept.
 **/
typedef struct {
	guint8  ms_op;
	guint8  ls_op;
	guint16 opcode;

	guint8  *data;
	int     data_malloced;
	guint32 length;

	guint32 streamPos;
	MsOleStream *pos;
} BiffQuery;

/* Sets up a query on a stream */
BiffQuery  *ms_biff_query_new        (MsOleStream *);
/* Updates the BiffQuery structure with the next BIFF record
 * returns: 1 for succes, and 0 for EOS(tream) */
int         ms_biff_query_next       (BiffQuery *);
gboolean    ms_biff_query_peek_next  (BiffQuery *, guint16 *opcode);
void        ms_biff_query_destroy    (BiffQuery *);

/*******************************************************************************/
/*                                 Write Side                                  */
/*******************************************************************************/

typedef struct _BiffPut
{
	guint8       ms_op;
	guint8       ls_op;
	guint32      length; /* NB. can be extended by a continue opcode */
	guint8      *data;
	MsOlePos     streamPos;
	MsOlePos     curpos; /* Curpos is offset from beggining of header */
	int          data_malloced;
	int          len_fixed;
	MsOleStream *pos;
} BiffPut;

/* Sets up a record on a stream */
BiffPut      *ms_biff_put_new        (MsOleStream *);
void          ms_biff_put_destroy    (BiffPut *);

/**
 * If between the 'next' and 'commit' ls / ms_op are changed they will be
 * written correctly.
 **/
/* For known length records shorter than 0x2000 bytes. */
guint8        *ms_biff_put_len_next   (BiffPut *, guint16 opcode, guint32 len);
/* For unknown length records */
void           ms_biff_put_var_next   (BiffPut *, guint16 opcode);
void           ms_biff_put_var_write  (BiffPut *, guint8 *, guint32 len);
/* Seeks to pos bytes after the beggining of the record */
void           ms_biff_put_var_seekto (BiffPut *, MsOlePos pos);
/* Must commit after each record */
void           ms_biff_put_commit     (BiffPut *);

void dump_biff (BiffQuery *bq);

#endif /* GNUMERIC_BIFF_H */
