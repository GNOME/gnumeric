/**
 * ms-ole-vba.c: MS Office VBA support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * Copyright 2000 Helix Code, Inc.
 **/

#include <config.h>
#include <stdio.h>
#include <ms-ole-vba.h>

#undef VBA_DEBUG

struct _MsOleVba {
	MsOleStream *s;
	GArray      *text;
	int          pos;
};

inline gboolean
ms_ole_vba_eof (MsOleVba *vba)
{
	return !vba || vba->pos >= vba->text->len;
}

char
ms_ole_vba_getc (MsOleVba *vba)
{
	g_assert (!ms_ole_vba_eof (vba));

	return g_array_index (vba->text, guint8, vba->pos++);
}

char
ms_ole_vba_peek (MsOleVba *vba)
{
	g_assert (!ms_ole_vba_eof (vba));

	return g_array_index (vba->text, guint8, vba->pos);
}

#if VBA_DEBUG > 1
static void
print_bin (guint16 dt)
{
	int i;
	
	printf ("|");
	for (i = 15; i >= 0; i--) {
		if (dt & (1 << i))
			printf ("1");
		else
			printf ("0");
		if (i == 8)
			printf ("|");
	}
	printf ("|");
}
#endif

/*
 * lzw, arc like compression.
 */
static void
decompress_vba (MsOleVba *vba, guint8 *data, guint32 len)
{
#define BUF_SIZE    4096 /* a bottleneck */
	guint8   buffer[BUF_SIZE];
	guint8  *ptr;
	guint32  pos;
	GArray  *ans = g_array_new (FALSE, FALSE, 1);

	vba->text = ans;
	vba->pos  = 0;

	for (pos = 0; pos < BUF_SIZE; pos++)
		buffer[pos] = ' ';

#if VBA_DEBUG > 0
	g_warning ("My compressed stream:\n");
	ms_ole_dump (data, len);
#endif

	ptr = data;
	pos = 0;

	while (ptr < data + len) {
		guint8 hdr = *ptr++;
		int    shift;

		for (shift = 0x01; shift < 0x100; shift = shift<<1) {
			if (pos == 4096) {
#if VBA_DEBUG > 0
				printf ("\nSomething extremely odd"
					" happens after 4096 bytes 0x%x\n\n",
					MS_OLE_GET_GUINT16 (ptr));
				ms_ole_dump (ptr, len - (ptr - data));
#endif
				ptr+=2;
				hdr = *ptr++;
				pos = 0;
				shift = 0x01;
			}
			if (hdr & shift) {
				guint16 dt = MS_OLE_GET_GUINT16 (ptr);
				int i, back, len, shft;

				if (pos <= 64)
					shft = 10;
				else if (pos <= 128)
					shft = 9;
				else if (pos <= 256)
					shft = 8;
				else if (pos <= 512)
					shft = 7;
				else if (pos <= 1024)
					shft = 6;
				else if (pos <= 2048)
					shft = 5;
				else
					shft = 4;

				back = (dt >> shft) + 1;
				len = 0;
				for (i = 0; i < shft; i++)
					len |= dt & (0x1 << i);
				len += 3;

#if VBA_DEBUG > 1
				printf ("|match 0x%x (%d,%d) >> %d = %d, %d| pos = %d |\n",
					dt, (dt>>8), (dt&0xff), shft, back, len, pos);
 				/* Perhaps dt & SHIFT = dist. to end of run */
				print_bin (dt);
				printf ("\n");
#endif				
				for (i = 0; i < len; i++) {
					guint8 c;
					guint32 srcpos = (BUF_SIZE + (pos%BUF_SIZE)) - back;
						
					if (srcpos >= BUF_SIZE)
						srcpos-= BUF_SIZE;

					g_assert (srcpos >= 0);
					g_assert (srcpos < BUF_SIZE);
					c = buffer [srcpos];
					buffer [pos++ % BUF_SIZE] = c;
					g_array_append_val (ans, c);
#if VBA_DEBUG > 0
					printf ("%c", c);
#endif
				}
				ptr += 2;
			} else {
				buffer [pos++ % BUF_SIZE] = *ptr;
				g_array_append_val (ans, *ptr);
#if VBA_DEBUG > 0
				printf ("%c", *ptr);
#endif
				ptr++;
			}
		}
	}
	
	{
		char c;

		c = '\n';
		g_array_append_val (ans, c);

		c = '\0';
		g_array_append_val (ans, c);
	}
}

static guint8 *
seek_sig (guint8 *data, int len)
{
	int i;
	guint8 vba_sig[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1, 0x1 };

	for (i = 0; i < len; i++) {
		guint8 *p = data;
		int j;

		for (j = 0; j < sizeof (vba_sig); j++) {
			if (*p++ != vba_sig [j])
				break;
		}
		if (j == sizeof (vba_sig))
			return p;

		data++;
	}

	return NULL;
}

static guint8 *
find_compressed_vba (guint8 *data, MsOlePos len)
{
	guint8  *sig;
	guint32  offset;
	guint32  offpos;
		
	if (!(sig = seek_sig (data, len))) {
		g_warning ("No VBA kludge signature");
		return NULL;
	}

	offpos = MS_OLE_GET_GUINT32 (sig) + 0xd0 - 0x6b - 0x8;

#if VBA_DEBUG > 0
	printf ("Offpos : 0x%x -> \n", offpos);
#endif

	offset = MS_OLE_GET_GUINT32 (sig + offpos);

	if (len < offset + 3) {
		g_warning ("Too small for offset 0x%x\n", offset);
		return NULL;
	}

#if VBA_DEBUG > 0
	printf ("Offset is 0x%x\n", offset);
#endif

	return data + offset;
}


/**
 * ms_ole_vba_open:
 * @s: the stream pointer.
 * 
 * Attempt to open a stream as a VBA stream, and commence
 * decompression of it.
 * 
 * Return value: NULL if not a VBA stream or fails.
 **/
MsOleVba *
ms_ole_vba_open (MsOleStream *s)
{
	MsOleVba    *vba;
	int          i, len;
	guint8      *data, *vba_data;
	guint8       sig [16];
	const guint8 gid [16] = { 0x1,  0x16, 0x1,  0x0,
				  0x6,  0xb6, 0x0,  0xff,
				  0xff, 0x1,  0x1,  0x0,
				  0x0,  0x0,  0x0,  0xff };
	
	g_return_val_if_fail (s != NULL, NULL);

	if (s->size < 16)
		return NULL;

	s->lseek     (s, 0, MsOleSeekSet);
	s->read_copy (s, sig, 16);

	for (i = 0; i < 16; i++)
		if (sig [i] != gid [i])
			return NULL;

	data = g_new (guint8, s->size);

	s->lseek (s, 0, MsOleSeekSet);
	if (!s->read_copy (s, data, s->size)) {
		g_warning ("Strange: failed read");
		g_free (data);		
		return NULL;
	}

	if (!(vba_data = find_compressed_vba (data, s->size))) {
		g_free (data);
		return NULL;
	}

	if (MS_OLE_GET_GUINT8 (vba_data) != 1)
		g_warning ("Digit 0x%x != 1...", MS_OLE_GET_GUINT8 (vba_data));

	vba = g_new0 (MsOleVba, 1);
	vba->s = s;

	len = MS_OLE_GET_GUINT16 (vba_data + 1);

#if VBA_DEBUG > 0
	printf ("Length 0x%x\n", len);
#endif

	len = len & ~0xb000;

	decompress_vba (vba, vba_data + 3, len);
	g_free (data);
	
	return vba;
}

/**
 * me_ols_vba_close:
 * @vba: 
 * 
 *   Free the resources associated with this vba
 * stream.
 **/
void
ms_ole_vba_close (MsOleVba *vba)
{
	if (vba) {
		g_array_free (vba->text, TRUE);
		vba->text = NULL;

		g_free (vba);
	}
}

