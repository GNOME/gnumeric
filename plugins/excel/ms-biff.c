/**
 * ms-biff.c: MS Excel Biff support...
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>
#include <libole2/ms-ole.h>

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

void
dump_biff (BiffQuery *bq)
{
	printf ("Opcode 0x%x length %d malloced? %d\nData:\n", bq->opcode, bq->length, bq->data_malloced);
	if (bq->length>0)
		ms_ole_dump (bq->data, bq->length);
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
	bq->pos           = ptr;
#if BIFF_DEBUG > 0
	dump_biff(bq);
#endif
	return bq;
}

gboolean
ms_biff_query_peek_next (BiffQuery *bq, guint16 *opcode)
{
	guint8 data[4];
	g_return_val_if_fail (opcode != NULL, 0);

	if (!bq || (bq->pos->position + 4 > bq->pos->size))
		return FALSE;

	if (!bq->pos->read_copy (bq->pos, data, 4))
		return FALSE;

	bq->pos->lseek (bq->pos, -4, MsOleSeekCur); /* back back off */

	*opcode = MS_OLE_GET_GUINT16 (data);
	return TRUE;
}

/**
 * Returns 0 if has hit end
 **/
int
ms_biff_query_next (BiffQuery *bq)
{
	guint8  tmp[4];
	int ans=1;

	if (!bq || bq->pos->position >= bq->pos->size)
		return 0;

	if (bq->data_malloced) {
		g_free (bq->data);
		bq->data = 0;
		bq->data_malloced = 0;
	}

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
			bq->data = 0;
			bq->length = 0;
		} else
			bq->data_malloced = 1;
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
ms_biff_query_destroy (BiffQuery *bq)
{
	if (bq) {
		if (bq->data_malloced) {
			g_free (bq->data);
			bq->data = 0;
			bq->data_malloced = 0;
		}
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
	bp->data_malloced = 0;
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
		bp->data_malloced = 1;
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
	bp->data_malloced = 0;
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
