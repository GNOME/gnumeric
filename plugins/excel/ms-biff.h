/**
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999 Michael Meeks
 **/
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H
#include "ms-ole.h"

extern double biff_getdouble (const guint8 *p);
extern void   biff_setdouble (guint8 *p, double d);
	
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
/* MW: I have reservations about this.  We are assuming not only little
 * endian, but also arbitrary alignment for doubles as well as a certain
 * layout (IEEE) of doubles.  */
#     define BIFF_GETDOUBLE(p)   (*((const double*)(p)))
#     define BIFF_SETDOUBLE(p,q) (*((double*)(p))=(q))
#else
#     define BIFF_GETDOUBLE(p)   (biff_getdouble(p))
#     define BIFF_SETDOUBLE(p,q) (biff_setdouble(p,q))
#endif

/*******************************************************************************/
/*                                 Read Side                                   */
/*******************************************************************************/

typedef struct _BiffQuery BiffQuery;

/**
 * Returns query data, it is imperative that copies of
 * 'data *' should _not_ be kept.
 **/
struct _BiffQuery {
	guint8  ms_op;
	guint8  ls_op;
	guint16 opcode;
	guint32 length;        /* NB. can be extended by a continue opcode */
	guint8  *data;
	guint32 streamPos;
	guint16 num_merges;
/*	gint16  padding;*/
	int     data_malloced; /* is *data a copy ? */
	MsOleStream *pos;
};
 
/* Sets up a query on a stream */
extern BiffQuery  *ms_biff_query_new         (MsOleStream *);
/* Updates the BiffQuery structure with the next BIFF record
 * returns: 1 for succes, and 0 for EOS(tream) */
extern int         ms_biff_query_next_merge (BiffQuery *, gboolean do_merge);
#define            ms_biff_query_next(q)     ms_biff_query_next_merge ((q), TRUE)
extern int         ms_biff_query_peek_next  (BiffQuery *, guint16 *opcode);
/* Converts a merged query to the un-merged equivalent */
extern void        ms_biff_query_unmerge    (BiffQuery *);
extern void        ms_biff_query_destroy    (BiffQuery *);

/*******************************************************************************/
/*                                 Write Side                                  */
/*******************************************************************************/

typedef struct _BiffPut
{
	guint8         ms_op;
	guint8         ls_op;
	guint32        length; /* NB. can be extended by a continue opcode */
	guint8        *data;
	MsOlePos   streamPos;
	MsOlePos   curpos; /* Curpos is offset from beggining of header */
	guint16        num_merges;      
/*	gint16         padding;*/
	int            data_malloced;
	int            len_fixed;
	MsOleStream *pos;
} BiffPut;
 
/* Sets up a record on a stream */
extern BiffPut      *ms_biff_put_new        (MsOleStream *);
extern void          ms_biff_put_destroy    (BiffPut *);

/**
 * If between the 'next' and 'commit' ls / ms_op are changed they will be
 * written correctly.
 **/
/* For known length records shorter than 0x2000 bytes. */
extern guint8        *ms_biff_put_len_next   (BiffPut *, guint16 opcode, guint32 len);
/* For unknown length records */
extern void           ms_biff_put_var_next   (BiffPut *, guint16 opcode);
extern void           ms_biff_put_var_write  (BiffPut *, guint8 *, guint32 len);
/* Seeks to pos bytes after the beggining of the record */
extern void           ms_biff_put_var_seekto (BiffPut *, MsOlePos pos);
/* Must commit after each record */
extern void           ms_biff_put_commit     (BiffPut *);

void dump_biff (BiffQuery *bq);
#endif
