/**
 * ms-biff.c: MS Excel Biff support...
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1998, 1999 Michael Meeks
 **/

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <malloc.h>
#include <assert.h>
#include <ctype.h>

#include <config.h>
#include <glib.h>
#include "ms-ole.h"
#include "ms-biff.h"
#include "biff-types.h"

#define BIFF_DEBUG 0

/**
 * The only complicated bits in this code are:
 *  a) Speed optimisation of passing a raw pointer to the mapped stream back
 *  b) Merging continues, if a record is too large for a single BIFF record
 *     ie. 0xffff bytes ( or more usu. 0x2000 ) it is split into continues.
 **/

/*******************************************************************************/
/*                             Helper Functions                                */
/*******************************************************************************/

#if G_BYTE_ORDER == G_BIG_ENDIAN
double biff_getdouble (const guint8 *p)
{
    double d;
    int i;
    guint8 *t = (guint8 *)&d;
    int sd = sizeof (d);

    for (i = 0; i < sd; i++)
      t[i] = p[sd - 1 - i];

    return d;
}

void biff_setdouble (guint8 *p, double d)
{
    int i;
    guint8 *t = (guint8 *)&d;
    int sd = sizeof (d);

    for (i = 0; i < sd; i++)
	    p[sd - 1 - i] = t[i];
}
#endif

void
dump_biff (BiffQuery *bq)
{
	printf ("Opcode 0x%x length %d malloced? %d\nData:\n", bq->opcode, bq->length, bq->data_malloced);
	if (bq->length>0)
		dump (bq->data, bq->length);
/*	dump_stream (bq->pos); */
}

/*******************************************************************************/
/*                                 Read Side                                   */
/*******************************************************************************/

BiffQuery *
ms_biff_query_new (MsOleStream *ptr)
{
	BiffQuery *bq   ;
	if (!ptr)
		return 0;
	bq = g_new0 (BiffQuery, 1);
	bq->opcode        = 0;
	bq->length        = 0;
	bq->data_malloced = 0;
/*	bq->padding       = 0; */
	bq->num_merges    = 0;
	bq->pos           = ptr;
#if BIFF_DEBUG > 0
	dump_biff(bq);
#endif
	return bq;
}

/**
 * I know this is ugly but its not so much my fault !
 **/
static int
ms_biff_merge_continues (BiffQuery *bq, guint32 len)
{
	GArray *contin;
	guint8  tmp[4];
	guint32 lp, total_len;
	guint8 *d;
	typedef struct {
		guint8 *data;
		guint32 length;
	} chunk_t;
	chunk_t chunk;

	contin = g_array_new (0,1,sizeof(chunk_t));

	/* First block: already got */
	chunk.length = bq->length;
	if (bq->data_malloced)
		chunk.data = bq->data;
	else {
		chunk.data = g_new (guint8, bq->length);
		memcpy (chunk.data, bq->data, bq->length);
	}
	total_len = chunk.length;
	g_array_append_val (contin, chunk);

	/* Subsequent continue blocks */
	chunk.length = len;
	do {
		if (bq->pos->position >= bq->pos->size)
			return 0;
		chunk.data = g_new (guint8, chunk.length);
		if (!bq->pos->read_copy (bq->pos, chunk.data, chunk.length))
			return 0;
#if BIFF_DEBUG > 8
		printf ("Read raw : 0x%x -> 0x%x\n", chunk.data[0],
			chunk.data[chunk.length-1]);
#endif
		tmp[0] = 0; tmp[1] = 0; tmp[2] = 0; tmp[3] = 0;
		bq->pos->read_copy (bq->pos, tmp, 4);	
		total_len   += chunk.length/* - bq->padding*/;
		g_array_append_val (contin, chunk);

		chunk.length = MS_OLE_GET_GUINT16 (tmp+2);
		bq->num_merges++;
	} while ((MS_OLE_GET_GUINT16(tmp) & 0xff) == BIFF_CONTINUE);
	bq->pos->lseek (bq->pos, -4, MsOleSeekCur); /* back back off */

	bq->data = g_malloc (total_len);
	if (!bq->data)
		return 0;
	bq->length = total_len;
	d = bq->data;
	bq->data_malloced = 1;
	for (lp=0;lp<contin->len;lp++) {
		chunk = g_array_index (contin, chunk_t, lp);
#if BIFF_DEBUG > 8
		printf ("Copying block stats with 0x%x ends with 0x%x len 0x%x\n",
			chunk.data[0], chunk.data[chunk.length-1], chunk.length);
		g_assert ((d-bq->data)+chunk.length<=total_len);
#endif
		if (lp) {
			memcpy (d, chunk.data/*+bq->padding*/, chunk.length/*-bq->padding*/);
			d+=chunk.length/*-bq->padding*/;
		} else {
			memcpy (d, chunk.data, chunk.length);
			d+=chunk.length;
		}
		g_free (chunk.data);
	}
	g_array_free (contin, 1);
#if BIFF_DEBUG > 2
	printf ("MERGE %d CONTINUES... len 0x%x\n", contin->len, len);
	printf ("Biff read code 0x%x, length %d\n", bq->opcode, bq->length);
	dump_biff (bq);
#endif
	return 1;
}

int
ms_biff_query_peek_next (BiffQuery *bq, guint16 *opcode)
{
	guint8 data[4];
	g_return_val_if_fail (opcode != NULL, 0);

	if (!bq || (bq->pos->position + 4 > bq->pos->size))
		return 0;

	if (!bq->pos->read_copy (bq->pos, data, 4))
		return 0;

	bq->pos->lseek (bq->pos, -4, MsOleSeekCur); /* back back off */

	*opcode = MS_OLE_GET_GUINT16 (data);
	return 1;
}

/**
 * Returns 0 if has hit end
 **/
int
ms_biff_query_next_merge (BiffQuery *bq, gboolean do_merge)
{
	guint8  tmp[4];
	int ans=1;

	if (!bq || bq->pos->position >= bq->pos->size)
		return 0;
	if (bq->data_malloced) { /* always true for merged records*/
		g_free (bq->data);
		bq->num_merges    = 0;
/*		bq->padding       = 0;*/
		bq->data_malloced = 0;
	}
	g_assert (bq->num_merges == 0);

	bq->streamPos = bq->pos->position;
	if (!bq->pos->read_copy (bq->pos, tmp, 4))
		return 0;
	bq->opcode = MS_OLE_GET_GUINT16 (tmp);
	bq->length = MS_OLE_GET_GUINT16 (tmp+2);
	bq->ms_op  = (bq->opcode>>8);
	bq->ls_op  = (bq->opcode&0xff);

	if (bq->length > 0 &&
	    !(bq->data = bq->pos->read_ptr(bq->pos, bq->length))) {
		bq->data = g_new0 (guint8, bq->length);
		if (!bq->pos->read_copy(bq->pos, bq->data, bq->length)) {
			ans = 0;
			g_free(bq->data);
			bq->length = 0;
		} else
			bq->data_malloced = 1;
	}
	if (ans && do_merge &&
	    bq->pos->read_copy (bq->pos, tmp, 4)) {
		if ((MS_OLE_GET_GUINT16(tmp) & 0xff) == BIFF_CONTINUE)
			return ms_biff_merge_continues (bq, MS_OLE_GET_GUINT16(tmp+2));
		bq->pos->lseek (bq->pos, -4, MsOleSeekCur); /* back back off */
#if BIFF_DEBUG > 4
		printf ("Backed off\n");
#endif
	}

#if BIFF_DEBUG > 2
	printf ("Biff read code 0x%x, length %d\n", bq->opcode, bq->length);
	dump_biff (bq);
#endif
	if (!bq->length) {
		bq->data = 0;
		return 1;
	}

	return (ans);
}

void
ms_biff_query_unmerge (BiffQuery *bq)
{
	if (!bq || !bq->num_merges)
		return;
	bq->pos->lseek (bq->pos, -(4*(bq->num_merges+1)/* - (bq->num_merges*bq->padding)*/
				   + bq->length), MsOleSeekCur);
	ms_biff_query_next_merge (bq, FALSE);
}

void
ms_biff_query_destroy (BiffQuery *bq)
{
	if (bq)
	{
		if (bq->data_malloced)
			g_free (bq->data);
		g_free (bq);
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
	bp->num_merges    = 0;
/*	bp->padding       = 0;*/
	bp->data_malloced = 0;
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
ms_biff_put_len_next   (BiffPut *bp, guint16 opcode, guint32 len)
{
	g_return_val_if_fail (bp, 0);
	g_return_val_if_fail (bp->pos, 0);
	g_return_val_if_fail (len < MAX_LIKED_BIFF_LEN, 0);

	bp->len_fixed  = 1;
	bp->ms_op      = (opcode >>   8);
	bp->ls_op      = (opcode & 0xff);
	bp->length     = len;
/*	bp->padding    = 0;*/
	bp->num_merges = 0;
	bp->streamPos  = bp->pos->tell (bp->pos);
	if (len > 0)
		bp->data = g_new (guint8, len);
	else
		bp->data = 0;

	return bp->data;
}
void
ms_biff_put_var_next   (BiffPut *bp, guint16 opcode)
{
	guint8 data[4];
	g_return_if_fail (bp);
	g_return_if_fail (bp->pos);

	bp->len_fixed  = 0;
	bp->ms_op      = (opcode >>   8);
	bp->ls_op      = (opcode & 0xff);
/*	bp->padding    = 0;*/
	bp->num_merges = 0;
	bp->curpos     = 0;
	bp->length     = 0;
	bp->data       = 0;
	bp->streamPos  = bp->pos->tell (bp->pos);

	MS_OLE_SET_GUINT16(data, opcode);
	MS_OLE_SET_GUINT16(data+2,0xfaff); /* To be corrected later */
	bp->pos->write (bp->pos, data, 4);
}
void
ms_biff_put_var_write  (BiffPut *bp, guint8 *data, guint32 len)
{
	g_return_if_fail (bp);
	g_return_if_fail (data);
	g_return_if_fail (bp->pos);
	g_return_if_fail (!bp->len_fixed);
	g_return_if_fail (!bp->data);

	/* Temporary */
	g_return_if_fail (bp->length+len < 0xf000);

	bp->pos->write (bp->pos, data, len);
	bp->curpos+= len;
	if (bp->curpos > bp->length)
		bp->length = bp->curpos;
}
void
ms_biff_put_var_seekto (BiffPut *bp, MsOlePos pos)
{
	g_return_if_fail (bp);
	g_return_if_fail (!bp->len_fixed);
	g_return_if_fail (!bp->data);

	bp->curpos = pos;
	bp->pos->lseek (bp->pos, bp->streamPos + bp->curpos + 4, MsOleSeekSet);
}

static void
ms_biff_put_var_commit (BiffPut *bp)
{
	guint8       tmp[4];
	MsOlePos endpos;

	g_return_if_fail (bp);
	g_return_if_fail (bp->pos);
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

	g_return_if_fail (bp);
	g_return_if_fail (bp->pos);
	g_return_if_fail (bp->len_fixed);
	g_return_if_fail (bp->length == 0 || bp->data);
	g_return_if_fail (bp->length < MAX_LIKED_BIFF_LEN);

/*	if (!bp->data_malloced) Unimplemented optimisation
		bp->pos->lseek (bp->pos, bp->length, MsOleSeekCur);
		else */
	MS_OLE_SET_GUINT16 (tmp, (bp->ms_op<<8) + bp->ls_op);
	MS_OLE_SET_GUINT16 (tmp+2, bp->length);
	bp->pos->write (bp->pos, tmp, 4);
	bp->pos->write (bp->pos, bp->data, bp->length);

	g_free (bp->data);
	bp->data      = 0 ;
	bp->streamPos = bp->pos->tell (bp->pos);
	bp->curpos    = 0;
}
void ms_biff_put_commit (BiffPut *bp)
{
	if (bp->len_fixed)
		ms_biff_put_len_commit (bp);
	else
		ms_biff_put_var_commit (bp);
}


