/**
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H
#include "ms-ole.h"

#define BIFF_GET_GUINT8(p) (*((const guint8 *)(p)+0))
#define BIFF_GET_GUINT16(p) (guint16)(*((const guint8 *)(p)+0) | (*((const guint8 *)(p)+1)<<8))
#define BIFF_GET_GUINT32(p) (guint32)(*((const guint8 *)(p)+0) | \
                        (*((const guint8 *)(p)+1)<<8) | \
                        (*((const guint8 *)(p)+2)<<16) | \
                        (*((const guint8 *)(p)+3)<<24))
#define BIFF_GET_GUINT64(p) (BIFF_GET_GUINT32(p) | (((Dguint32)BIFF_GET_GUINT32((const guint8 *)(p)+4))<<32))

#define BIFF_SET_GUINT8(p,n)  (*((guint8 *)(p)+0)=n)
#define BIFF_SET_GUINT16(p,n) ((*((guint8 *)(p)+0)=((n)&0xff)), \
                               (*((guint8 *)(p)+1)=((n)>>8)&0xff))
#define BIFF_SET_GUINT32(p,n) ((*((guint8 *)(p)+0)=((n))&0xff), \
                               (*((guint8 *)(p)+1)=((n)>>8)&0xff), \
                               (*((guint8 *)(p)+2)=((n)>>16)&0xff), \
                               (*((guint8 *)(p)+3)=((n)>>24)&0xff))

extern double biff_getdouble (guint8 *p);
extern void   biff_setdouble (guint8 *p, double d);
	
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#     define BIFF_GETDOUBLE(p)   (*((double*)(p)))
#     define BIFF_SETDOUBLE(p,q) (*((double*)(p))=(q))
#else
#     define BIFF_GETDOUBLE(p)   (biff_getdouble(p))
#     define BIFF_SETDOUBLE(p,q) (biff_setdouble(p,q))
#endif

/*******************************************************************************/
/*                                 Read Side                                   */
/*******************************************************************************/

/**
 * Returns query data, it is imperative that copies of
 * 'data *' should _not_ be kept.
 **/
typedef struct _BiffQuery
{
	guint8  ms_op;
	guint8  ls_op;
	guint16 opcode;
	guint32 length;        /* NB. can be extended by a continue opcode */
	guint8  *data;
	guint32 streamPos;
	guint16 num_merges;
	gint16  padding;
	int     data_malloced; /* is *data a copy ? */
	MsOleStream *pos;
} BiffQuery;
 
/* Sets up a query on a stream */
extern BiffQuery   *ms_biff_query_new        (MsOleStream *);
/* Duplicates this query, so chaining can re-commence here */
extern BiffQuery   *ms_biff_query_copy       (const BiffQuery *p);
/* Updates the BiffQuery structure with the next BIFF record
 * returns: 1 for succes, and 0 for EOS(tream) */
extern int           ms_biff_query_next_merge (BiffQuery *, gboolean do_merge);
#define       ms_biff_query_next(q)    ms_biff_query_next_merge ((q), TRUE)
/* Converts a merged query to the un-merged equivalent */
extern void          ms_biff_query_unmerge    (BiffQuery *);
extern void          ms_biff_query_destroy    (BiffQuery *);

/*******************************************************************************/
/*                                 Write Side                                  */
/*******************************************************************************/

typedef struct _BiffPut
{
	guint8         ms_op;
	guint8         ls_op;
	guint32        length; /* NB. can be extended by a continue opcode */
	guint8        *data;
	ms_ole_pos_t   streamPos;
	ms_ole_pos_t   curpos; /* Curpos is offset from beggining of header */
	guint16        num_merges;      
	gint16         padding;
	int            data_malloced;
	int            len_fixed;
	MsOleStream *pos;
} BiffPut;
 
/* Sets up a record on a stream */
extern BiffPut      *ms_biff_put_new        (MsOleStream *);
extern void           ms_biff_put_destroy    (BiffPut *);
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
extern void           ms_biff_put_var_seekto (BiffPut *, ms_ole_pos_t pos);
/* Must commit after each record */
extern void           ms_biff_put_commit     (BiffPut *);

void dump_biff (BiffQuery *bq);
#endif
