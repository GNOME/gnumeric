/**
 * ms-biff.h: MS Excel BIFF support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_BIFF_H
#define GNUMERIC_BIFF_H
#include "ms-ole.h"

typedef guint8  BYTE ;
typedef guint16 WORD ;
typedef guint32 LONG ;
typedef guint64 DLONG ;

#define BIFF_GETBYTE(p) (*((const BYTE *)(p)+0))
#define BIFF_GETWORD(p) (*((const BYTE *)(p)+0) | (*((const BYTE *)(p)+1)<<8))
#define BIFF_GETLONG(p) (*((const BYTE *)(p)+0) | \
                        (*((const BYTE *)(p)+1)<<8) | \
                        (*((const BYTE *)(p)+2)<<16) | \
                        (*((const BYTE *)(p)+3)<<24))
#define BIFF_GETDLONG(p) (BIFF_GETLONG(p) | (((DLONG)BIFF_GETLONG((const BYTE *)(p)+4))<<32))

double biff_getdouble(guint8 *p);
	
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#     define BIFF_GETDOUBLE(p) (*((double*)(p)))
#else
#     define BIFF_GETDOUBLE(p) (biff_getdouble(p))
#endif
/**
 * Returns query data, it is imperative that copies of
 * 'data *' should _not_ be kept.
 **/
typedef struct _BIFF_QUERY
{
	guint8  ms_op;
	guint8  ls_op;
	guint16 opcode;
	gint32  length;        /* NB. can be extended by a continue opcode */
	guint8  *data;
	guint32 streamPos;
	int     data_malloced; /* is *data a copy ? */
	MS_OLE_STREAM *pos;
} BIFF_QUERY ;
 
/* Sets up a query on a stream */
extern BIFF_QUERY *ms_biff_query_new (MS_OLE_STREAM *) ;
/* Duplicates this query, so chaining can re-commence here */
extern BIFF_QUERY *ms_biff_query_copy (const BIFF_QUERY *p) ;
/**
 * Updates the BIFF_QUERY structure with the next BIFF record
 * returns: 1 for succes, and 0 for EOS(tream)
 **/
extern int ms_biff_query_next (BIFF_QUERY *) ;
extern void ms_biff_query_destroy (BIFF_QUERY *) ;
/* Returns a stream which contains the data in the BIFF record. */
extern MS_OLE_STREAM *ms_biff_query_data_to_stream (BIFF_QUERY *);
#endif
