/**
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H
#include "ms-ole.h"

typedef guint8  BYTE;
typedef guint16 WORD;
typedef guint32 LONG;
typedef guint64 DLONG;

#define BIFF_GETBYTE(p) (*((const BYTE *)(p)+0))
#define BIFF_GETWORD(p) (guint16)(*((const BYTE *)(p)+0) | (*((const BYTE *)(p)+1)<<8))
#define BIFF_GETLONG(p) (guint32)(*((const BYTE *)(p)+0) | \
                        (*((const BYTE *)(p)+1)<<8) | \
                        (*((const BYTE *)(p)+2)<<16) | \
                        (*((const BYTE *)(p)+3)<<24))
#define BIFF_GETDLONG(p) (BIFF_GETLONG(p) | (((DLONG)BIFF_GETLONG((const BYTE *)(p)+4))<<32))

#define BIFF_SET_GUINT8(p,n)  (*((guint8 *)(p)+0)=n)
#define BIFF_SET_GUINT16(p,n) ((*((guint8 *)(p)+0)=((n)&0xff)), \
                               (*((guint8 *)(p)+1)=((n)>>8)&0xff))
#define BIFF_SET_GUINT32(p,n) ((*((guint8 *)(p)+0)=((n))&0xff), \
                               (*((guint8 *)(p)+1)=((n)>>8)&0xff), \
                               (*((guint8 *)(p)+2)=((n)>>16)&0xff), \
                               (*((guint8 *)(p)+3)=((n)>>24)&0xff))

extern double biff_getdouble(guint8 *p);
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
typedef struct _BIFF_QUERY
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
	MS_OLE_STREAM *pos;
} BIFF_QUERY;
 
/* Sets up a query on a stream */
extern BIFF_QUERY   *ms_biff_query_new        (MS_OLE_STREAM *);
/* Duplicates this query, so chaining can re-commence here */
extern BIFF_QUERY   *ms_biff_query_copy       (const BIFF_QUERY *p);
/* Updates the BIFF_QUERY structure with the next BIFF record
 * returns: 1 for succes, and 0 for EOS(tream) */
extern int           ms_biff_query_next_merge (BIFF_QUERY *, gboolean do_merge);
#define       ms_biff_query_next(q)    ms_biff_query_next_merge ((q), TRUE)
/* Converts a merged query to the un-merged equivalent */
extern void          ms_biff_query_unmerge    (BIFF_QUERY *);
extern void          ms_biff_query_destroy    (BIFF_QUERY *);

/*******************************************************************************/
/*                                 Write Side                                  */
/*******************************************************************************/

typedef struct _BIFF_PUT
{
	guint8         ms_op;
	guint8         ls_op;
	guint32        length; /* NB. can be extended by a continue opcode */
	guint8        *data;
	ms_ole_pos_t   streamPos;
	guint16        num_merges;      
	gint16         padding;
	int            data_malloced;
	int            len_fixed;
	MS_OLE_STREAM *pos;
} BIFF_PUT;
 
/* Sets up a record on a stream */
extern BIFF_PUT      *ms_biff_put_new        (MS_OLE_STREAM *);
extern void           ms_biff_put_destroy    (BIFF_PUT *);
/**
 * If between the 'next' and 'commit' ls / ms_op are changed they will be
 * written correctly.
 **/
/* For known length records shorter than 0x2000 bytes. */
extern guint8        *ms_biff_put_len_next   (BIFF_PUT *, guint16 opcode, guint32 len);
/* For unknown length records */
extern void           ms_biff_put_var_next   (BIFF_PUT *, guint16 opcode);
extern void           ms_biff_put_var_write  (BIFF_PUT *, guint8 *, guint32 len);
/* Must commit after each record */
extern void           ms_biff_put_commit     (BIFF_PUT *);

void dump_biff (BIFF_QUERY *bq);
#endif
