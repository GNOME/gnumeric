/**
 * ms-ole.c: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
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
#include <glib.h>
#include "ms-ole.h"

/* Implementational detail - not for global header */

#define OLE_DEBUG 0

/* These take a _guint8_ pointer */
#define GET_GUINT8(p)  (*((const guint8 *)(p)+0))
#define GET_GUINT16(p) (*((const guint8 *)(p)+0)+(*((const guint8 *)(p)+1)<<8))
#define GET_GUINT32(p) (*((const guint8 *)(p)+0)+ \
		    (*((const guint8 *)(p)+1)<<8)+ \
		    (*((const guint8 *)(p)+2)<<16)+ \
		    (*((const guint8 *)(p)+3)<<24))

#define SET_GUINT8(p,n)  (*((guint8 *)(p)+0)=n)
#define SET_GUINT16(p,n) ((*((guint8 *)(p)+0)=((n)&0xff)), \
                          (*((guint8 *)(p)+1)=((n)>>8)&0xff))
#define SET_GUINT32(p,n) ((*((guint8 *)(p)+0)=((n))&0xff), \
                          (*((guint8 *)(p)+1)=((n)>>8)&0xff), \
                          (*((guint8 *)(p)+2)=((n)>>16)&0xff), \
                          (*((guint8 *)(p)+3)=((n)>>24)&0xff))


#define SPECIAL_BLOCK  0xfffffffd
#define END_OF_CHAIN   0xfffffffe
#define UNUSED_BLOCK   0xffffffff

#define BB_BLOCK_SIZE     512
#define SB_BLOCK_SIZE      64

#if OLE_DEBUG > 0
/* Very grim, but quite necessary */
#       define ms_array_index(a,b,c) (b)my_array_hack ((a), sizeof(b), (c))

static guint32
my_array_hack (GArray *a, guint s, guint32 idx)
{
	g_assert (a);
	g_assert (idx>=0);
	g_assert (idx<a->len);
	g_assert (s==4);
	return ((guint32 *)a->data)[idx];
}
#else
/* Far far faster... */
#       define ms_array_index(a,b,c) g_array_index (a, b, c)
#endif

#define BB_THRESHOLD   0x1000

#define PPS_ROOT_BLOCK    0
#define PPS_BLOCK_SIZE 0x80
#define PPS_END_OF_CHAIN 0xffffffff

typedef struct _PPS PPS;

struct _PPS {
	char    *name;
	PPS_IDX  next, prev, dir, pps;
	guint32  size;
	BLP      start;
	PPS_TYPE type;
};

#if OLE_MMAP
#       define BBPTR(f,b)  ((f)->mem + (b+1)*BB_BLOCK_SIZE)
#       define GET_SB_START_PTR(f,b) (BBPTR(f, g_array_index ((f)->sbf, BLP, (b)/(BB_BLOCK_SIZE/SB_BLOCK_SIZE))) \
				      + (((b)%(BB_BLOCK_SIZE/SB_BLOCK_SIZE))*SB_BLOCK_SIZE))
#else
#       define BBPTR(f,b)  (get_block_ptr (f, b))
#endif


static guint8 *
get_block_ptr (MS_OLE *f, BLP b)
{
	/* Reads it in if neccessary */
	return NULL;
}

/* This is a list of big blocks which contain a flat description of all blocks in the file.
   Effectively inside these blocks is a FAT of chains of other BBs, so the theoretical max
   size = 128 BB Fat blocks, thus = 128*512*512/4 blocks ~= 8.4MBytes */
/* The number of Big Block Descriptor (fat) Blocks */
#define GET_NUM_BBD_BLOCKS(f)   (GET_GUINT32((f)->mem + 0x2c))
#define SET_NUM_BBD_BLOCKS(f,n) (SET_GUINT32((f)->mem + 0x2c, (n)))
/* The block locations of the Big Block Descriptor Blocks */
#define GET_BBD_LIST(f,i)           (GET_GUINT32((f)->mem + 0x4c + (i)*4))
#define SET_BBD_LIST(f,i,n)         (SET_GUINT32((f)->mem + 0x4c + (i)*4, (n)))
#define NEXT_BB(f,n)                (g_array_index ((f)->bb, BLP, n))
#define NEXT_SB(f,n)                (g_array_index ((f)->sb, BLP, n))
/* Get the start block of the root directory ( PPS ) chain */
#define GET_ROOT_STARTBLOCK(f)   (GET_GUINT32((f)->mem + 0x30))
#define SET_ROOT_STARTBLOCK(f,i) (SET_GUINT32((f)->mem + 0x30, i))
/* Get the start block of the SBD chain */
#define GET_SBD_STARTBLOCK(f)    (GET_GUINT32((f)->mem + 0x3c))
#define SET_SBD_STARTBLOCK(f,i)  (SET_GUINT32((f)->mem + 0x3c, i))


/* NB it is misleading to assume that Microsofts linked lists link correctly.
   It is not the case that pps_next(f, pps_prev(f, n)) = n ! For the final list
   item there are no valid links. Cretins. */
#define PPS_GET_NAME_LEN(p)   (GET_GUINT16(p + 0x40))
#define PPS_SET_NAME_LEN(p,i) (SET_GUINT16(p + 0x40, (i)))
#define PPS_NAME(f,n)     (pps_get_text (p, PPS_GET_NAME_LEN(f,n)))
#define PPS_GET_PREV(p)   ((PPS_IDX) GET_GUINT32(p + 0x44))
#define PPS_GET_NEXT(p)   ((PPS_IDX) GET_GUINT32(p + 0x48))
#define PPS_GET_DIR(p)    ((PPS_IDX) GET_GUINT32(p + 0x4c))
#define PPS_SET_PREV(p,i) ((PPS_IDX) SET_GUINT32(p + 0x44, i))
#define PPS_SET_NEXT(p,i) ((PPS_IDX) SET_GUINT32(p + 0x48, i))
#define PPS_SET_DIR(p,i)  ((PPS_IDX) SET_GUINT32(p + 0x4c, i))
/* These get other interesting stuff from the PPS record */
#define PPS_GET_STARTBLOCK(p)      ( GET_GUINT32(p + 0x74))
#define PPS_GET_SIZE(p)            ( GET_GUINT32(p + 0x78))
#define PPS_GET_TYPE(p) ((PPS_TYPE)( GET_GUINT8(p + 0x42)))
#define PPS_SET_STARTBLOCK(p,i)    ( SET_GUINT32(p + 0x74, i))
#define PPS_SET_SIZE(p,i)          ( SET_GUINT32(p + 0x78, i))
#define PPS_SET_TYPE(p,i)          ( SET_GUINT8 (p + 0x42, i))

/* FIXME: This needs proper unicode support ! current support is a guess */
/* Length is in bytes == 1/2 the final text length */
/* NB. Different from biff_get_text, looks like a bug ! */
static char *
pps_get_text (guint8 *ptr, int length)
{
	int lp, skip;
	char *ans;
	guint16 c;
	guint8 *inb;
	
	length = (length+1)/2;

	if (length <= 0 ||
	    length > (PPS_BLOCK_SIZE/4)) {
#if OLE_DEBUG > 0
		printf ("Nulled name of length %d\n", length);
#endif
		return 0;
	}
	
	ans = (char *)g_malloc (sizeof(char) * length + 1);
	
	c = GET_GUINT16(ptr);
	if (c<0x30) /* Magic unicode number I made up */
		inb = ptr + 2;
	else
		inb = ptr;
	for (lp=0;lp<length;lp++) {
		c = GET_GUINT16(inb);
		ans[lp] = (char)c;
		inb+=2;
	}
	ans[lp] = 0;
	return ans;
}

static void
dump_header (MS_OLE *f)
{
	int lp;
	printf ("--------------------------MS_OLE HEADER-------------------------\n");
	printf ("Num BBD Blocks : %d Root %d, SB blocks %d\n",
		f->bb?f->bb->len:-1,
		f->pps?f->pps->len:-1,
		f->sb?f->sb->len:-1);

	for (lp=0;lp<f->bb->len;lp++)
		printf ("Block %d -> block %d\n", lp,
			g_array_index (f->bb, BLP, lp));
	
	if (f->pps) {
		printf ("Root blocks : %d\n", f->pps->len);
		for (lp=0;lp<f->pps->len;lp++) {
			PPS *p = g_ptr_array_index (f->pps, lp);
			printf ("root_list[%d] = '%s' ( <-%d, V %d, %d->)\n", lp, p->name?p->name:"Null",
				p->prev, p->dir, p->next);
		}
	} else
		printf ("No root yet\n");
/*	
	printf ("sbd blocks : %d\n", h->sbd_list->len);
	for (lp=0;lp<h->sbd_list->len;lp++)
	printf ("sbd_list[%d] = %d\n", lp, (int)ms_array_index (h->sbd_list, SBPtr, lp));*/
	printf ("-------------------------------------------------------------\n");
}

static BLP
get_next_block (MS_OLE *f, BLP blk)
{
	BLP bbd     = GET_BBD_LIST (f, blk/(BB_BLOCK_SIZE/4));
	return        GET_GUINT32 (BBPTR(f,bbd) + 4*(blk%(BB_BLOCK_SIZE/4)));
}

static int
read_bb (MS_OLE *f)
{
	guint32 numbbd;
	BLP     lp;
	GArray *ans;

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (f->mem, 0);

	ans     = g_array_new (FALSE, FALSE, sizeof(BLP));
	numbbd  = GET_NUM_BBD_BLOCKS  (f);

        /* Sanity checks */
	if (numbbd < ((f->length - BB_BLOCK_SIZE + ((BB_BLOCK_SIZE*BB_BLOCK_SIZE)/4) - 1) /
		      ((BB_BLOCK_SIZE*BB_BLOCK_SIZE)/4))) {
		printf ("Duff block descriptors\n");
		return 0;
	}
	
	for (lp=0;lp<(f->length+BB_BLOCK_SIZE-1)/BB_BLOCK_SIZE;lp++) {
		BLP tmp = get_next_block (f, lp);
		g_array_append_val (ans, tmp);
	}

	g_assert ((f->length+BB_BLOCK_SIZE-1)/BB_BLOCK_SIZE <= ans->len);

	/* More sanity checks */
/*	for (lp=0;lp<numbbd;lp++) {
		BLP bbdblk = GET_BBD_LIST(f, lp);
		if (g_array_index(ans, BLP, bbdblk) != SPECIAL_BLOCK) {
			printf ("Error - BBD blocks not marked correctly\n");
			g_array_free (ans, TRUE);
			return 0;
		}
		}*/

	f->bb = ans;
#if OLE_DEBUG > 1
	dump_header (f);
#endif
	return 1;
}

static void
extend_file (MS_OLE *f, guint blocks)
{
#ifndef OLE_MMAP
#       error Simply add more blocks at the end in memory
#else
	struct stat st;
	int file;
	guint8 *newptr, zero = 0;
	guint32 oldlen;
	guint32 blk, lp;

	g_assert (f);
	file = f->file_descriptor;

#if OLE_DEBUG > 5
	printf ("Before extend\n");
	dump_allocation(f);
#endif

	g_assert (munmap(f->mem, f->length) != -1);
	/* Extend that file by blocks */

	if ((fstat(file, &st)==-1) ||
	    (lseek (file, st.st_size + BB_BLOCK_SIZE*blocks - 1, SEEK_SET)==(off_t)-1) ||
	    (write (file, &zero, 1)==-1))
	{
		printf ("Serious error extending file\n");
		f->mem = 0;
		return;
	}

	oldlen = st.st_size;
	fstat(file, &st);
	f->length = st.st_size;
	g_assert (f->length == BB_BLOCK_SIZE*blocks + oldlen);
	if (f->length%BB_BLOCK_SIZE)
		printf ("Warning file %d non-integer number of blocks\n", f->length);
	newptr = mmap (f->mem, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0);
#if OLE_DEBUG > 0
	if (newptr != f->mem)
		printf ("Memory map moved from %p to %p\n",
			f->mem, newptr);
#endif
	f->mem = newptr;

#if OLE_DEBUG > 5
	printf ("After extend\n");
	dump_allocation(f);
#endif
#endif
}

static BLP
next_free_bb (MS_OLE *f)
{
	BLP blk, tblk;
	guint32 idx, lp;
  
	g_assert (f);

	blk = 0;
	while (blk < f->bb->len)
		if (g_array_index (f->bb, BLP, blk) == UNUSED_BLOCK)
			return blk;
	        else 
			blk++;

	extend_file (f, 2);
	tblk = UNUSED_BLOCK;
	g_array_append_val (f->bb, tblk);
	g_array_append_val (f->bb, tblk);
#ifndef OLE_MMAP
#       error Need to extend bbptr as well.
#endif
	g_assert ((g_array_index (f->bb, BLP, blk) == UNUSED_BLOCK));
	return blk;
}

static int
write_bb (MS_OLE *f)
{
	guint32 numbbd;
	BLP     ptr, lp, lpblk;
	GArray *ans;

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (f->mem, 0);
	g_return_val_if_fail (f->bb,  0);

	numbbd  = (f->bb->len + (BB_BLOCK_SIZE*BB_BLOCK_SIZE/4) - 1) /
		((BB_BLOCK_SIZE*BB_BLOCK_SIZE/4) - 1); /* Think carefully ! */
	SET_NUM_BBD_BLOCKS (f, numbbd);

	for (lp=0;lp<numbbd;lp++) {
		BLP blk = next_free_bb(f);
		SET_BBD_LIST (f, lp, blk);
		g_array_index (f->bb, BLP, blk) = SPECIAL_BLOCK;
	}

	lpblk = 0;
	while (lpblk<f->bb->len) { /* Described blocks */
		guint8 *mem = BBPTR(f, GET_BBD_LIST(f, lpblk/(BB_BLOCK_SIZE/4)));
		SET_GUINT32 (mem + (lpblk%(BB_BLOCK_SIZE/4))*4,
			     g_array_index (f->bb, BLP, lpblk));
		lpblk++;
	}
	while (lpblk%(BB_BLOCK_SIZE/4) != 0) { /* Undescribed blocks */
		guint8 *mem = BBPTR(f, GET_BBD_LIST(f, lpblk/(BB_BLOCK_SIZE/4)));
		SET_GUINT32 (mem + (lpblk%(BB_BLOCK_SIZE/4))*4,
			     UNUSED_BLOCK);
		lpblk++;
	}
	g_array_free (f->bb, TRUE);
	f->bb = 0;
	return 1;
}

static BLP
next_free_sb (MS_OLE *f)
{
	BLP blk, tblk;
	guint32 idx, lp;
  
	g_assert (f);

	blk = 0;
	while (blk < f->sb->len)
		if (g_array_index (f->sb, BLP, blk) == UNUSED_BLOCK)
			return blk;
	        else 
			blk++;
	
	tblk = UNUSED_BLOCK;
	g_array_append_val (f->sb, tblk);
	g_assert ((g_array_index (f->sb, BLP, blk) == UNUSED_BLOCK));
	g_assert (blk < f->sb->len);

	if ((f->sb->len + (BB_BLOCK_SIZE/SB_BLOCK_SIZE) - 1) / (BB_BLOCK_SIZE/SB_BLOCK_SIZE) >= f->sbf->len) {
	/* Create an extra big block on the small block stream */
		BLP new_sbf = next_free_bb(f);
		g_array_append_val (f->sbf, new_sbf);
        /* We don't need to chain it in as we have this info in f->sbf */
		g_array_index (f->bb, BLP, new_sbf) = END_OF_CHAIN;
	}

	g_assert ((f->sb->len + (BB_BLOCK_SIZE/SB_BLOCK_SIZE) - 1) / (BB_BLOCK_SIZE/SB_BLOCK_SIZE) <= f->sbf->len);

	return blk;
}

static PPS *
pps_decode (guint8 *mem)
{
	PPS *pps     = g_new (PPS, 1);
	pps->name    = pps_get_text  (mem, PPS_GET_NAME_LEN(mem));
	pps->type    = PPS_GET_TYPE  (mem);
	pps->size    = PPS_GET_SIZE  (mem);
	if (pps->name) {
		pps->next    = PPS_GET_NEXT  (mem);
		pps->prev    = PPS_GET_PREV  (mem);
		pps->dir     = PPS_GET_DIR   (mem);
		pps->start   = PPS_GET_STARTBLOCK (mem);
	} else { /* Make safe */
		pps->next    = PPS_END_OF_CHAIN;
		pps->prev    = PPS_END_OF_CHAIN;
		pps->dir     = PPS_END_OF_CHAIN;
		pps->start   = PPS_END_OF_CHAIN;
	}
#if OLE_DEBUG > 1
	printf ("PPS decode : '%s'\n", pps->name?pps->name:"Null");
	dump (mem, PPS_BLOCK_SIZE);
#endif
	return pps;
}

static void
pps_encode (guint8 *mem, PPS *pps)
{
	int lp, max;

	g_return_if_fail (pps);
	
	/* Blank stuff I don't understand */
	for (lp=0;lp<PPS_BLOCK_SIZE;lp++)
		SET_GUINT8(mem+lp, 0);

	if (pps->name) {
		max = strlen (pps->name);
		if (max >= (PPS_BLOCK_SIZE/4))
			max = (PPS_BLOCK_SIZE/4);
		for (lp=0;lp<max;lp++)
			SET_GUINT16(mem + lp*2, pps->name[lp]);
	} else {
		printf ("No name %d\n", pps->pps);
		max = -1;
	}
	
	PPS_SET_NAME_LEN(mem, (max+1)*2);
	
	/* Magic numbers */
	SET_GUINT8   (mem + 0x43, 0x01); /* Or zero ? */
	SET_GUINT32  (mem + 0x50, 0x00020900);
	SET_GUINT32  (mem + 0x58, 0x000000c0);
	SET_GUINT32  (mem + 0x5c, 0x46000000);

	PPS_SET_TYPE (mem, pps->type);
	PPS_SET_SIZE (mem, pps->size);
	PPS_SET_NEXT (mem, pps->next);
	PPS_SET_PREV (mem, pps->prev);
	PPS_SET_DIR  (mem, pps->dir);
        PPS_SET_STARTBLOCK(mem, pps->start);
}

static int
read_pps (MS_OLE *f)
{
	BLP blk;
	GPtrArray *ans = g_ptr_array_new ();

	g_return_val_if_fail (f, 0);

	blk = GET_ROOT_STARTBLOCK (f);
#if OLE_DEBUG > 0
	printf ("Root start block %d\n", blk);
#endif
	while (blk != END_OF_CHAIN) {
		int lp;
		BLP last;

		if (blk == SPECIAL_BLOCK ||
		    blk == UNUSED_BLOCK) {
			printf ("Duff block in root chain\n");
			return 0;
		}

		for (lp=0;lp<BB_BLOCK_SIZE/PPS_BLOCK_SIZE;lp++) {
			PPS *p  = pps_decode(BBPTR(f,blk) + lp*PPS_BLOCK_SIZE);
			p->pps  = lp;
			g_ptr_array_add (ans, p);
		}
		last = blk;
		blk = NEXT_BB(f, blk);
		g_array_index (f->bb, BLP, last) = UNUSED_BLOCK;
	}
	
	f->pps = ans;
	if (f->pps->len < 1) {
		printf ("Root directory too small\n");
		return 0;
	}
	return 1;
}

static int
write_pps (MS_OLE *f)
{
	int ppslp;
	BLP blk  = END_OF_CHAIN;
	BLP last = END_OF_CHAIN;

	for (ppslp=0;ppslp<f->pps->len;ppslp++) {
		PPS *cur;
		if (ppslp%(BB_BLOCK_SIZE/PPS_BLOCK_SIZE)==0) {
			last  = blk;
			blk   = next_free_bb (f);
			g_assert (g_array_index (f->bb, BLP, blk) == UNUSED_BLOCK);
			if (last != END_OF_CHAIN)
				g_array_index (f->bb, BLP, last) = blk;
		        else {
#if OLE_DEBUG > 0
				printf ("Set root block to %d\n", blk);
#endif
				SET_ROOT_STARTBLOCK (f, blk);
			}

			g_array_index (f->bb, BLP, blk) = END_OF_CHAIN;
		}
		cur = g_ptr_array_index (f->pps, ppslp);

		pps_encode (BBPTR(f,blk) + (ppslp%(BB_BLOCK_SIZE/PPS_BLOCK_SIZE))*PPS_BLOCK_SIZE,
			    cur);
		if (cur->name)
			g_free (cur->name);
		cur->name = 0;
	}
	g_ptr_array_free (f->pps, TRUE);
	f->pps = 0;
	return 1;
}

static int
read_sb (MS_OLE *f)
{
	BLP ptr;
	int lp, lastidx, idx;
	PPS *root;

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (f->pps, 0);

	root = g_ptr_array_index (f->pps, 0);
	g_return_val_if_fail (root, 0);

	f->sbf = g_array_new (FALSE, FALSE, sizeof(BLP));
	f->sb  = g_array_new (FALSE, FALSE, sizeof(BLP));
	
	/* List of big blocks in SB file */
	ptr = root->start;
#if OLE_DEBUG > 0
	printf ("Starting Small block file at %d\n", root->start);
#endif
	while (ptr != END_OF_CHAIN) {
		if (ptr == UNUSED_BLOCK ||
		    ptr == SPECIAL_BLOCK) {
			printf ("Corrupt small block file: serious error, "
				"invalid block in chain\n");
			g_array_free (f->sbf, TRUE);
			f->sbf = 0;
			return 0;
		}
		g_array_append_val (f->sbf, ptr);
		ptr = NEXT_BB (f, ptr);
	}

	/* Description of small blocks */
	lastidx = -1;
	idx     = 0;
	ptr = GET_SBD_STARTBLOCK (f);
	while (ptr != END_OF_CHAIN) {
		guint32 lp;
		if (ptr == UNUSED_BLOCK ||
		    ptr == SPECIAL_BLOCK) {
			printf ("Corrupt file descriptor: serious error, "
				"invalid block in chain\n");
			g_array_free (f->sb, TRUE);
			f->sb = 0;
			return 0;
		}
		for (lp=0;lp<BB_BLOCK_SIZE/4;lp++) {
			BLP p = GET_GUINT32 (BBPTR(f, ptr) + lp*4);
			g_array_append_val (f->sb, p);
			
			if (p != UNUSED_BLOCK)
				lastidx = idx;
			idx++;
		}
		ptr = NEXT_BB (f, ptr);
	}
	if (lastidx>0)
		g_array_set_size (f->sb, lastidx+1);
	
	if (f->sbf->len * BB_BLOCK_SIZE < f->sb->len*SB_BLOCK_SIZE) {
		printf ("Not enough small block file for descriptors\n"
			"sbf->len == %d, sb->len == %d\n", f->sbf->len,
			f->sb->len);
		return 0;
	}

	return 1;
}

static int
write_sb (MS_OLE *f)
{
	guint32 lp, lastused;
	PPS *root;
	BLP sbd_start  = END_OF_CHAIN;
	BLP sbf_start  = END_OF_CHAIN;

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (f->pps, 0);

	root        = g_ptr_array_index (f->pps, PPS_ROOT_BLOCK);

	if (f->sbf->len * BB_BLOCK_SIZE < f->sb->len*SB_BLOCK_SIZE) {
		printf ("Not enough descriptor / blocks being written %d %d\n",
			f->sbf->len, f->sb->len);
	}
	if (f->sbf->len>0)
		sbf_start = g_array_index (f->sbf, BLP, 0);
	/* Chain up the sbf blocks */
	for (lp=0;lp<f->sbf->len-1;lp++) {
		BLP blk, next ;
		blk  = g_array_index (f->sbf, BLP, lp);
		next = g_array_index (f->sbf, BLP, lp+1);
		/* this assert is not really important, its just how we left it */
		g_assert (g_array_index (f->bb, BLP, blk) == END_OF_CHAIN);
		g_array_index (f->bb, BLP, blk) = next;
	}

	lastused = END_OF_CHAIN;
	for (lp=0;lp<f->sb->len;lp++) {
		if (g_array_index (f->sb, BLP, lp) != UNUSED_BLOCK)
			lastused = lp;
	}

	if (lastused != END_OF_CHAIN) { /* Bother writing stuff */
		guint8 *mem = 0;
		guint32 num_sbdf = (lastused + (BB_BLOCK_SIZE/4)-1) /
			(BB_BLOCK_SIZE/4);
		BLP blk = END_OF_CHAIN, last;

#if OLE_DEBUG > 0
		printf ("Num SB descriptor blocks : %d\n", num_sbdf);
#endif
		for (lp=0;lp<num_sbdf*(BB_BLOCK_SIZE/4);lp++) {
			BLP set;
			if (lp%(BB_BLOCK_SIZE/4) == 0) {
				last = blk;
				blk = next_free_bb(f);
				if (!lp)
					sbd_start = blk;
				if (last != END_OF_CHAIN)
					g_array_index (f->bb, BLP, last) = blk;
				g_array_index (f->bb, BLP, blk) = END_OF_CHAIN;
				mem = BBPTR (f, blk);
			}
			if (lp<f->sb->len)
				set = g_array_index (f->sb, BLP, lp);
			else
				set = UNUSED_BLOCK;
			SET_GUINT32 (mem + (lp%(BB_BLOCK_SIZE/4))*4, set);
		}
	} else {
#if OLE_DEBUG > 0
		printf ("Blank SB allocation\n");
#endif
		sbf_start = END_OF_CHAIN;
	}

	root->start = sbf_start;
	SET_SBD_STARTBLOCK (f, sbd_start);
	g_array_free (f->sb,  TRUE);
	g_array_free (f->sbf, TRUE);
	f->sb       = 0;
	f->sbf      = 0;
	return 1;
}

static int
ms_ole_setup (MS_OLE *f)
{
	if (read_bb  (f) &&
	    read_pps (f) &&
	    read_sb  (f)) {
#if OLE_DEBUG > 1
		printf ("Just read header of\n");
		dump_header (f);
#endif		
		return 1;
	}
	return 0;
}

static int
ms_ole_cleanup (MS_OLE *f)
{
	if (f->mode != 'w') /* Nothing to write */
		return 1;
#if OLE_DEBUG > 1
	printf ("About to write header of: \n");
	dump_header (f);
#endif
	if (write_sb  (f) &&
	    write_pps (f) &&
	    write_bb  (f))
		return 1;
	return 0;
}

static MS_OLE *
new_null_msole ()
{
	MS_OLE *f = g_new0 (MS_OLE, 1);

	f->mem    = (guint8 *)0xdeadbeef;
	f->length = 0;
	f->mode   = 'r';
	f->bb     = 0;
#ifndef OLE_MMAP
	f->bbptr  = 0;
#endif
	f->sb     = 0;
	f->sbf    = 0;
	f->pps    = 0;
	f->dirty  = 0;

	return f;
}

MS_OLE *
ms_ole_open (const char *name)
{
	struct stat st;
	int prot = PROT_READ | PROT_WRITE;
	int file;
	char mode;
	MS_OLE *f;

#if OLE_DEBUG > 0
	printf ("New OLE file '%s'\n", name);
#endif

	f = new_null_msole();
#if OLE_MMAP
	f->file_descriptor = file = open (name, O_RDWR);
	f->mode = 'w';
	if (file == -1) {
		f->file_descriptor = file = open (name, O_RDONLY);
		f->mode = 'r';
		prot &= ~PROT_WRITE;
	}
	if (file == -1 || fstat(file, &st))
	{
		printf ("No such file '%s'\n", name);
		g_free (f) ;
		return 0;
	}
	f->length = st.st_size;
	if (f->length<=0x4c)  /* Bad show */
	{
		printf ("File '%s' too short\n", name);
		close (file) ;
		g_free (f) ;
		return 0;
	}

	f->mem = mmap (0, f->length, prot, MAP_SHARED, file, 0);
#else
	f->mem = read (dfjlsdfj, first block only);
#endif

	if (GET_GUINT32(f->mem    ) != 0xe011cfd0 ||
	    GET_GUINT32(f->mem + 4) != 0xe11ab1a1)
	{
#if OLE_DEBUG > 0
		printf ("Failed OLE2 magic number %x %x\n",
			GET_GUINT32(f->mem), GET_GUINT32(f->mem+4));
#endif
		ms_ole_destroy (f);
		return 0;
	}
	if (f->length%BB_BLOCK_SIZE)
		printf ("Warning file '%s':%d non-integer number of blocks\n", name, f->length);

	if (!ms_ole_setup(f)) {
		printf ("'%s' : duff file !\n", name);
		ms_ole_destroy (f);
		return 0;
	}

#if OLE_DEBUG > 0
	printf ("New OLE file '%s'\n", name);
#endif
	/* If writing then when destroy commit it */
	return f;
}

MS_OLE *
ms_ole_create (const char *name)
{
	struct stat st;
	int file, zero=0;
	MS_OLE *f;
	int init_blocks = 1, lp;
	guint8 *mem;

	if ((file = open (name, O_RDWR|O_CREAT|O_TRUNC|O_NONBLOCK,
			  S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)) == -1)
	{
		printf ("Can't create file '%s'\n", name);
		return 0;
	}

	if ((lseek (file, BB_BLOCK_SIZE*init_blocks - 1, SEEK_SET)==(off_t)-1) ||
	    (write (file, &zero, 1)==-1))
	{
		printf ("Serious error extending file to %d bytes\n", BB_BLOCK_SIZE*init_blocks);
		return 0;
	}

	f = new_null_msole ();

	f->file_descriptor  = file;
	f->mode             = 'w';
	fstat(file, &st);
	f->length = st.st_size;
	if (f->length%BB_BLOCK_SIZE)
		printf ("Warning file %d non-integer number of blocks\n", f->length);

#ifdef OLE_MMAP
	f->mem  = mmap (0, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0);
	if (!f->mem)
	{
		printf ("Serious error mapping file to %d bytes\n", BB_BLOCK_SIZE*init_blocks);
		close (file);
		g_free (f);
		return 0;
	}
#else
#       error Not implemented yet
#endif
	/* The header block */
	for (lp=0;lp<BB_BLOCK_SIZE/4;lp++)
		SET_GUINT32(f->mem + lp*4, (lp<(0x52/4))?0:UNUSED_BLOCK);

	SET_GUINT32(f->mem, 0xe011cfd0); /* Magic number */
	SET_GUINT32(f->mem + 4, 0xe11ab1a1);

	/* More magic numbers */
	SET_GUINT32(f->mem + 0x18, 0x0003003e);
	SET_GUINT32(f->mem + 0x1c, 0x0009fffe);
	SET_GUINT32(f->mem + 0x20, 0x6); 
	SET_GUINT32(f->mem + 0x38, 0x00001000); 
/*	SET_GUINT32(f->mem + 0x40, 0x1);  */
	SET_GUINT32(f->mem + 0x44, 0xfffffffe); 

	SET_NUM_BBD_BLOCKS  (f, 0);
	SET_ROOT_STARTBLOCK (f, END_OF_CHAIN);
	SET_SBD_STARTBLOCK  (f, END_OF_CHAIN);

	{
		PPS *p;

		f->bb  = g_array_new (FALSE, FALSE, sizeof(BLP));
		f->sb  = g_array_new (FALSE, FALSE, sizeof(BLP));
		f->sbf = g_array_new (FALSE, FALSE, sizeof(BLP));
		f->pps = g_ptr_array_new ();
		p = g_new(PPS, 1);
		p->name  = g_strdup ("Root Entry");
		p->prev  = p->dir = p->next = PPS_END_OF_CHAIN;
		p->pps   = PPS_ROOT_BLOCK;
		p->start = END_OF_CHAIN;
		p->type  = MS_OLE_PPS_ROOT;
		p->size  = 0;
		g_ptr_array_add (f->pps, p);
	}
	return f;
}

/**
 * This closes the file and truncates any free blocks
 **/
void
ms_ole_destroy (MS_OLE *f)
{
#if OLE_DEBUG > 0
	printf ("FIXME: should truncate to remove unused blocks\n");
#endif
	if (f) {
		if (f->dirty)
			ms_ole_cleanup (f);

#ifdef OLE_MMAP
		munmap (f->mem, f->length);
		close (f->file_descriptor);
#else
#               error No destroy code yet	       
#endif
		g_free (f);

#if OLE_DEBUG > 0
		printf ("Closing OLE file\n");
#endif
	}
}

void
dump (guint8 *ptr, guint32 len)
{
	guint32 lp,lp2;
	guint32 off;

	for (lp = 0;lp<(len+15)/16;lp++)
	{
		printf ("%8x  |  ", lp*16);
		for (lp2=0;lp2<16;lp2++) {
			off = lp2 + (lp<<4);
			off<len?printf("%2x ", ptr[off]):printf("XX ");
		}
		printf ("  |  ");
		for (lp2=0;lp2<16;lp2++) {
			off = lp2 + (lp<<4);
			printf ("%c", off<len?(ptr[off]>'!'&&ptr[off]<127?ptr[off]:'.'):'*');
		}
		printf ("\n");
	}
}

static void
dump_stream (MS_OLE_STREAM *s)
{
	g_return_if_fail (s);

	if (s->size>=BB_THRESHOLD)
		printf ("Big block : ");
	else
		printf ("Small block : ");
	printf ("position %d\n", s->position);
}

static void
check_stream (MS_OLE_STREAM *s)
{
	BLP blk;
	guint32 idx;
	PPS *p;
	MS_OLE *f;

	g_return_if_fail (s);
	g_return_if_fail (s->file);

	f = s->file;
	p = g_ptr_array_index (f->pps, s->pps);

	g_return_if_fail (p);
	blk = p->start;
	idx = 0;
	if (s->strtype == MS_OLE_SMALL_BLOCK) {
		while (blk != END_OF_CHAIN) {
			guint8 *ptr;
			g_assert (g_array_index (s->blocks, BLP, idx) ==
				  blk);
#if OLE_DEBUG > 2
			ptr = GET_SB_START_PTR(f, blk);
			dump (ptr, SB_BLOCK_SIZE);
#endif
			blk = NEXT_SB(f, blk);
			idx++;
		}
	} else {
		while (blk != END_OF_CHAIN) {
			guint8 *ptr;
			g_assert (g_array_index (s->blocks, BLP, idx) ==
				  blk);
#if OLE_DEBUG > 2
			ptr = BBPTR(f, blk);
			dump (ptr, BB_BLOCK_SIZE);
#endif
			blk = NEXT_BB(f, blk);
			idx++;
		}
	}
}

static ms_ole_pos_t
tell_pos (MS_OLE_STREAM *s)
{
	return s->position;
}

/**
 * Free the allocation chains, and free up the blocks.
 * "It was for freedom that Christ has set us free."
 *   Galatians 5:11
 **/
static void
free_allocation (MS_OLE *f, guint32 startblock, gboolean is_big_block_stream)
{
	g_return_if_fail (f);

#if OLE_DEBUG > 0
	printf ("Free allocation %d : (%d)\n", startblock,
		is_big_block_stream);
#endif
       
	if (is_big_block_stream)
	{
		BLP p = startblock;
		printf ("FIXME: this should also free up blocks\n");
		while (p != END_OF_CHAIN) {
			BLP next = NEXT_BB(f,p);
			if (next == p) {
				printf ("Serious bug: cyclic ring in BB allocation\n");
				return;
			} else if (p == SPECIAL_BLOCK ||
				   p == UNUSED_BLOCK) {
				printf ("Serious bug: Special / Unused block "
					"in BB allocation\n");
				return;
			}
			g_array_index (f->bb, BLP, p) = UNUSED_BLOCK;
			p = next;
		}
	}
	else
	{
		BLP p = startblock;
		while (p != END_OF_CHAIN) {
			BLP next = NEXT_SB(f,p);
			if (next == p) {
				printf ("Serious bug: cyclic ring in SB allocation\n");
				return;
			} else if (p == SPECIAL_BLOCK ||
				   p == UNUSED_BLOCK) {
				printf ("Serious bug: Special / Unused block "
					"in SB allocation\n");
				return;
			}
			g_array_index (f->sb, BLP, p) = UNUSED_BLOCK;
			p = next;
		}
		/* Seek forwards to find blank sbf blocks */
		{
			guint32 lp;
			BLP     lastused = END_OF_CHAIN;
			for (lp=0;lp<f->sb->len;lp++) {
				if (g_array_index (f->sb, BLP, lp) != UNUSED_BLOCK)
					lastused = lp;
			}
			if (lastused == END_OF_CHAIN) {
				for (lp=0;lp<f->sbf->len;lp++) {
					BLP sbfd = g_array_index (f->sbf, BLP, lp);
					g_array_index (f->bb, BLP, sbfd) = UNUSED_BLOCK;
				}
				g_array_set_size (f->sbf, 0);
				g_array_set_size (f->sb, 0);
			} else {
				guint32 sbf_needed = (lastused+(BB_BLOCK_SIZE/SB_BLOCK_SIZE)-1) /
					             (BB_BLOCK_SIZE/SB_BLOCK_SIZE);

				if (sbf_needed == f->sbf->len)
					return;
				
				for (lp=sbf_needed;lp<f->sbf->len;lp++) {
					BLP sbfd = g_array_index (f->sbf, BLP, lp);
					g_array_index (f->bb, BLP, sbfd) = UNUSED_BLOCK;
				}
				g_array_set_size (f->sbf, sbf_needed);
				g_array_set_size (f->sb, lastused+1);
			}
		}
	}
}

static void
ms_ole_lseek (MS_OLE_STREAM *s, gint32 bytes, ms_ole_seek_t type)
{
	g_return_if_fail (s);

	if (type == MS_OLE_SEEK_SET)
		s->position = bytes;
	else
		s->position+= bytes;
	if (s->position>s->size) {
		s->position = s->size;
		printf ("Truncated seek\n");
	}
}

static guint8*
ms_ole_read_ptr_bb (MS_OLE_STREAM *s, guint32 length)
{
	int blockidx = s->position/BB_BLOCK_SIZE;
	int blklen;
	guint32 len=length;
	guint8 *ans;

	g_return_val_if_fail (s, 0);

	if (!s->blocks || blockidx>=s->blocks->len) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	blklen = BB_BLOCK_SIZE - s->position%BB_BLOCK_SIZE;
	while (len>blklen) {
		len-=blklen;
		blklen = BB_BLOCK_SIZE;
		if (blockidx >= (s->blocks->len - 1) ||
		    (ms_array_index (s->blocks, BLP, blockidx)+1
		     != ms_array_index (s->blocks, BLP, blockidx+1)))
			return 0;
		blockidx++;
	}
	/* Straight map, simply return a pointer */
	ans = BBPTR(s->file, ms_array_index (s->blocks, BLP, s->position/BB_BLOCK_SIZE))
	      + s->position%BB_BLOCK_SIZE;
	ms_ole_lseek (s, length, MS_OLE_SEEK_CUR);
	check_stream (s);
	return ans;
}

static guint8*
ms_ole_read_ptr_sb (MS_OLE_STREAM *s, guint32 length)
{
	int blockidx = s->position/SB_BLOCK_SIZE;
	int blklen;
	guint32 len=length;
	guint8 *ans;

	g_return_val_if_fail (s, 0);

	if (!s->blocks || blockidx>=s->blocks->len) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	blklen = SB_BLOCK_SIZE - s->position%SB_BLOCK_SIZE;
	while (len>blklen) {
		len-=blklen;
		blklen = SB_BLOCK_SIZE;
		if (blockidx >= (s->blocks->len - 1) ||
		    (ms_array_index (s->blocks, BLP, blockidx)+1
		     != ms_array_index (s->blocks, BLP, blockidx+1)))
			return 0;
		blockidx++;
	}
	/* Straight map, simply return a pointer */
	ans = GET_SB_START_PTR(s->file, ms_array_index (s->blocks, BLP, s->position/SB_BLOCK_SIZE))
		+ s->position%SB_BLOCK_SIZE;
	ms_ole_lseek (s, length, MS_OLE_SEEK_CUR);
	check_stream (s);
	return ans;
}

/**
 *  Returns:
 *  0 - on error
 *  1 - on success
 **/
static gboolean
ms_ole_read_copy_bb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int offset = s->position%BB_BLOCK_SIZE;
	int blkidx = s->position/BB_BLOCK_SIZE;
	guint8 *src;

	g_return_val_if_fail (s, 0);

	if (!s->blocks) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	while (length>0)
	{
		BLP block;
		int cpylen = BB_BLOCK_SIZE - offset;
		if (cpylen>length)
			cpylen = length;

		if (s->position + cpylen > s->size ||
		    blkidx == s->blocks->len) {
			printf ("Trying 2 to read beyond end of stream %d+%d %d\n",
				s->position, cpylen, s->size);
			return 0;
		}
		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, BLP, blkidx);
		src = BBPTR(s->file, block) + offset;
		
		memcpy (ptr, src, cpylen);
		ptr   += cpylen;
		length -= cpylen;
		
		offset = 0;
		
		blkidx++;
		s->position+=cpylen;
	}
	check_stream (s);
	return 1;
}

static gboolean
ms_ole_read_copy_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int offset = s->position%SB_BLOCK_SIZE;
	int blkidx = s->position/SB_BLOCK_SIZE;
	guint8 *src;

	g_return_val_if_fail (s, 0);

	if (!s->blocks) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	while (length>0)
	{
		int cpylen = SB_BLOCK_SIZE - offset;
		BLP block;
		if (cpylen>length)
			cpylen = length;
		if (s->position + cpylen > s->size ||
		    blkidx == s->blocks->len) {
			printf ("Trying 3 to read beyond end of stream %d+%d %d\n",
				s->position, cpylen, s->size);
			return 0;
		}
		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, BLP, blkidx);
		src = GET_SB_START_PTR(s->file, block) + offset;
				
		memcpy (ptr, src, cpylen);
		ptr   += cpylen;
		length -= cpylen;
		
		offset = 0;

		blkidx++;
		s->position+=cpylen;
	}
	check_stream (s);
	return 1;
}

static void
ms_ole_append_block (MS_OLE_STREAM *s)
{
	BLP block;
	BLP lastblk = END_OF_CHAIN;
	BLP eoc     = END_OF_CHAIN;

	if (s->strtype==MS_OLE_SMALL_BLOCK) {
		if (!s->blocks)
			s->blocks = g_array_new (FALSE, FALSE, sizeof(BLP));

		else if (s->blocks->len>0)
			lastblk = ms_array_index (s->blocks, BLP, s->blocks->len-1);

		block = next_free_sb (s->file);
		g_array_append_val (s->blocks, block);

		if (lastblk != END_OF_CHAIN) { /* Link onwards */
			g_array_index (s->file->sb, BLP, lastblk) = block;
#if OLE_DEBUG > 1
			printf ("Chained Small block %d to previous block %d\n", block, lastblk);
#endif
		} else { /* First block in a file */
			PPS *p = g_ptr_array_index (s->file->pps, s->pps);
#if OLE_DEBUG > 0
			printf ("Set first Small block to %d\n", block);
#endif
			p->start = block;
		}

		g_array_index (s->file->sb, BLP, block) = eoc;
#if OLE_DEBUG > 3
		printf ("Linked stuff\n");
		dump_allocation(s->file);
#endif
	} else {
		if (!s->blocks)
			s->blocks = g_array_new (FALSE, FALSE, sizeof(BLP));
		else if (s->blocks->len>0)
			lastblk = ms_array_index (s->blocks, BLP, s->blocks->len-1);

		block = next_free_bb (s->file);
#if OLE_DEBUG > 0
		{
			int lp;
			g_assert (g_array_index (s->file->bb, BLP, block) == UNUSED_BLOCK);
			for (lp=0;lp<s->blocks->len;lp++)
				g_assert (g_array_index (s->blocks, BLP, lp) != block);
		}
#endif
		g_array_append_val (s->blocks, block);

		if (lastblk != END_OF_CHAIN) { /* Link onwards */
			g_array_index (s->file->bb, BLP, lastblk) = block;
#if OLE_DEBUG > 0
			printf ("Chained Big block %d to block %d\n", block, lastblk);
#endif
		} else { /* First block in a file */
			PPS *p = g_ptr_array_index (s->file->pps, s->pps);
#if OLE_DEBUG > 0
			printf ("Set first Big block to %d\n", block);
#endif
			p->start = block;
		}

		g_array_index (s->file->bb, BLP, block) = eoc;
#if OLE_DEBUG > 3
		printf ("Linked stuff\n");
		dump_allocation(s->file);
#endif
	}
}

static void
ms_ole_write_bb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	guint8 *dest;
	int     cpylen;
	int     offset  = s->position%BB_BLOCK_SIZE;
	guint32 blkidx  = s->position/BB_BLOCK_SIZE;
	guint32 bytes   = length;
	gint32  lengthen;
	
	s->file->dirty = 1;
	while (bytes>0)
	{
		BLP block;
		int cpylen = BB_BLOCK_SIZE - offset;

		if (cpylen>bytes)
			cpylen = bytes;
		
		if (!s->blocks || blkidx>=s->blocks->len)
			ms_ole_append_block (s);

		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, BLP, blkidx);
		
		dest = BBPTR(s->file, block) + offset;

#if OLE_DEBUG > 0
		printf ("Copy %d bytes to block %d\n", cpylen, block);
#endif
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;
		
		offset = 0;
		blkidx++;
	}

	lengthen = s->position - s->size + length;
	if (lengthen > 0)
		s->size+=lengthen;

	s->lseek (s, length, MS_OLE_SEEK_CUR);
	check_stream (s);
	return;
}

static void
ms_ole_write_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	guint8 *dest;
	int     cpylen;
	int     offset  = s->position%SB_BLOCK_SIZE;
	guint32 blkidx  = s->position/SB_BLOCK_SIZE;
	guint32 bytes   = length;
	gint32  lengthen;
	
	s->file->dirty = 1;
	while (bytes>0)
	{
		BLP block;
		int cpylen = SB_BLOCK_SIZE - offset;

		if (cpylen>bytes)
			cpylen = bytes;
		
		if (!s->blocks || blkidx >= s->blocks->len)
			ms_ole_append_block (s);
		g_assert (s->blocks);

		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, BLP, blkidx);
		
		dest = GET_SB_START_PTR(s->file, block) + offset;
		
		g_assert (cpylen>=0);

		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;

		lengthen = s->position + length - bytes - s->size;
		if (lengthen > 0)
			s->size+=lengthen;
		
		/* Must be exactly filling the block */
		if (s->size >= BB_THRESHOLD)
		{
			PPS         *p = g_ptr_array_index (s->file->pps, s->pps);
			ms_ole_pos_t oldlen;
			guint8      *buffer;

			buffer       = g_new (guint8, s->size);
			s->lseek     (s, 0, MS_OLE_SEEK_SET);
			oldlen       = s->size;
			s->read_copy (s, buffer, oldlen);

			free_allocation (s->file, p->start, 0);
			p->start    = END_OF_CHAIN;
#if OLE_DEBUG > 0
			printf ("\n\n--- Converting ---\n\n\n");
#endif
			s->read_copy = ms_ole_read_copy_bb;
			s->read_ptr  = ms_ole_read_ptr_bb;
			s->lseek     = ms_ole_lseek;
			s->tell      = tell_pos;
			s->write     = ms_ole_write_bb;

			g_assert (s->size%SB_BLOCK_SIZE == 0);

			/* Convert the file to BBlocks */
			s->size     = 0;
			s->position = 0;
			s->strtype  = MS_OLE_LARGE_BLOCK;
			g_array_free (s->blocks, TRUE);
			s->blocks   = 0;

			s->write (s, buffer, oldlen);

			/* Continue the interrupted write */
			ms_ole_write_bb(s, ptr, bytes);
			bytes = 0;
#if OLE_DEBUG > 0
			printf ("\n\n--- Done ---\n\n\n");
#endif
			g_free (buffer);
			return;
		}
		
		offset = 0;
		blkidx++;
		check_stream (s);
	}
	s->lseek (s, length, MS_OLE_SEEK_CUR);
	return;
}

MS_OLE_STREAM *
ms_ole_stream_open (MS_OLE_DIRECTORY *d, char mode)
{
	PPS    *p;
	MS_OLE *f=d->file;
	MS_OLE_STREAM *s;
	int lp;

	if (!d || !f)
		return 0;

	if (mode == 'w' && f->mode != 'w') {
		printf ("Opening stream '%c' when file is '%c' only\n",
			mode, f->mode);
		return NULL;
	}

	p           = g_ptr_array_index (f->pps, d->pps);
	s           = g_new0 (MS_OLE_STREAM, 1);
	s->file     = f;
	s->pps      = d->pps;
	s->position = 0;
	s->size     = p->size;
	s->blocks   = NULL;

#if OLE_DEBUG > 0
	printf ("Parsing blocks\n");
#endif
	if (s->size>=BB_THRESHOLD)
	{
		BLP b = p->start;

		s->read_copy = ms_ole_read_copy_bb;
		s->read_ptr  = ms_ole_read_ptr_bb;
		s->lseek     = ms_ole_lseek;
		s->tell      = tell_pos;
		s->write     = ms_ole_write_bb;

		s->blocks    = g_array_new (FALSE, FALSE, sizeof(BLP));
		s->strtype   = MS_OLE_LARGE_BLOCK;
		for (lp=0;lp<(s->size+BB_BLOCK_SIZE-1)/BB_BLOCK_SIZE;lp++)
		{
			g_array_append_val (s->blocks, b);
#if OLE_DEBUG > 1
			printf ("Block %d\n", b);
#endif
			if (b == END_OF_CHAIN)
				printf ("Warning: bad file length in '%s'\n", p->name);
			else if (b == SPECIAL_BLOCK)
				printf ("Warning: special block in '%s'\n", p->name);
			else if (b == UNUSED_BLOCK)
				printf ("Warning: unused block in '%s'\n", p->name);
			else
				b = NEXT_BB(f, b);
		}
		if (b != END_OF_CHAIN && NEXT_BB(f, b) != END_OF_CHAIN)
			printf ("FIXME: Extra useless blocks on end of '%s'\n", p->name);
	}
	else
	{
		BLP b = p->start;

		s->read_copy = ms_ole_read_copy_sb;
		s->read_ptr  = ms_ole_read_ptr_sb;
		s->lseek     = ms_ole_lseek;
		s->tell      = tell_pos;
		s->write     = ms_ole_write_sb;

		if (s->size>0)
			s->blocks = g_array_new (FALSE, FALSE, sizeof(BLP));
		else
			s->blocks = NULL;

		s->strtype   = MS_OLE_SMALL_BLOCK;

		for (lp=0;lp<(s->size+SB_BLOCK_SIZE-1)/SB_BLOCK_SIZE;lp++)
		{
			g_array_append_val (s->blocks, b);
#if OLE_DEBUG > 0
			printf ("Block %d\n", b);
#endif
			if (b == END_OF_CHAIN)
				printf ("Warning: bad file length in '%s'\n", p->name);
			else if (b == SPECIAL_BLOCK)
				printf ("Warning: special block in '%s'\n", p->name);
			else if (b == UNUSED_BLOCK)
				printf ("Warning: unused block in '%s'\n", p->name);
			else
				b = NEXT_SB(f, b);
		}
		if (b != END_OF_CHAIN && NEXT_SB(f, b) != END_OF_CHAIN)
			printf ("FIXME: Extra useless blocks on end of '%s'\n", p->name);
	}
	return s;
}

MS_OLE_STREAM *
ms_ole_stream_copy (MS_OLE_STREAM *s)
{
	MS_OLE_STREAM *ans = g_new (MS_OLE_STREAM, 1);
	memcpy (ans, s, sizeof(MS_OLE_STREAM));
	return ans;
}

void
ms_ole_stream_close (MS_OLE_STREAM *s)
{
	if (s) {
		if (s->file && s->file->mode == 'w') {
			PPS *p  = g_ptr_array_index (s->file->pps, s->pps);
			p->size = s->size;
		}

		if (s->blocks)
			g_array_free (s->blocks, TRUE);
		g_free (s);
	}
}

static MS_OLE_DIRECTORY *
pps_to_dir (MS_OLE *f, PPS_IDX i, MS_OLE_DIRECTORY *d)
{
	PPS *p;
	MS_OLE_DIRECTORY *dir;

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (f->pps, 0);
	
	p = g_ptr_array_index (f->pps, i);
	if (d)
		dir = d;
	else
		dir = g_new (MS_OLE_DIRECTORY, 1);
	dir->name   = p->name;
	dir->type   = p->type;
	dir->pps    = i;
	dir->length = p->size;
	dir->file   = f;
	return dir;
}

MS_OLE_DIRECTORY *
ms_ole_get_root (MS_OLE *f)
{
	return pps_to_dir (f, PPS_ROOT_BLOCK, NULL);
}

/* You probably arn't too interested in the root directory anyway
   but this is first */
MS_OLE_DIRECTORY *
ms_ole_directory_new (MS_OLE *f)
{
	MS_OLE_DIRECTORY *d = ms_ole_get_root (f);
	ms_ole_directory_enter (d);
	return d;
}

/**
 * This navigates by offsets from the primary_entry
 **/
int
ms_ole_directory_next (MS_OLE_DIRECTORY *d)
{
	int offset;
	PPS_IDX tmp;
	MS_OLE *f;
	PPS *p;

	if (!d || !d->file)
		return 0;

	f = d->file;
	
	/* If its primary just go ahead */
	if (d->pps != d->primary_entry)
	{
		/* Checking back up the chain */
		offset = 0;
		tmp = d->primary_entry;
		while (tmp != PPS_END_OF_CHAIN &&
		       tmp != d->pps) {
			p = g_ptr_array_index (f->pps, tmp);
			tmp = p->prev;
			offset++;
		}
		if (d->pps == PPS_END_OF_CHAIN ||
		    tmp != PPS_END_OF_CHAIN) {
			offset--;
#if OLE_DEBUG > 0
			printf ("Back trace by %d\n", offset);
#endif
			tmp = d->primary_entry;
			while (offset > 0) {
				p = g_ptr_array_index (f->pps, tmp);
				tmp = p->prev;
				offset--;
			}
			pps_to_dir (d->file, tmp, d);
			if (!d->name) /* Recurse */
				return ms_ole_directory_next (d);
			return 1;
		}
	}

	/* Go down the chain, ignoring the primary entry */
	p = g_ptr_array_index (f->pps, d->pps);
	tmp = p->next;
	if (tmp == PPS_END_OF_CHAIN)
		return 0;
	
	pps_to_dir (d->file, tmp, d);
	
#if OLE_DEBUG > 0
	printf ("Forward next '%s' %d %d\n", d->name, d->type, d->length);
#endif
	if (!d->name) /* Recurse */
		return ms_ole_directory_next (d);

	return 1;
}

void
ms_ole_directory_enter (MS_OLE_DIRECTORY *d)
{
	MS_OLE *f;
	PPS    *p;
	if (!d || !d->file || d->pps==PPS_END_OF_CHAIN)
		return;

	f = d->file;
	p = g_ptr_array_index (f->pps, d->pps);

	if (d->type != MS_OLE_PPS_STORAGE &&
	    d->type != MS_OLE_PPS_ROOT) {
		printf ("Bad type %d %d\n", d->type, MS_OLE_PPS_ROOT);
		return;
	}

	if (p->dir != PPS_END_OF_CHAIN) {
		d->primary_entry = p->dir;
		/* So it will wind in from the start on 'next' */
		d->pps = PPS_END_OF_CHAIN;
	}
	return;
}

void
ms_ole_directory_unlink (MS_OLE_DIRECTORY *d)
{
	MS_OLE *f;
	if (!d || !d->file || d->pps==PPS_END_OF_CHAIN)
		return;
	
	f = d->file;

	d->file->dirty = 1;
	if (d->pps != d->primary_entry) {
		PPS *p = g_ptr_array_index (f->pps, d->pps);
		if (p->next == PPS_END_OF_CHAIN &&
		    p->prev == PPS_END_OF_CHAIN) { /* Little, lost & loosely attached */
			g_free (p->name);
			p->name = 0;
		}
	}
	else
		printf ("Unlink failed\n");
}

static PPS *
next_free_pps (MS_OLE *f)
{
	PPS_IDX pps = PPS_ROOT_BLOCK;
	PPS_IDX max_pps = f->pps->len;
	guint8 mem[PPS_BLOCK_SIZE];
	int lp;
	PPS *ans;

	while (pps<max_pps) {
		PPS *p = g_ptr_array_index (f->pps, pps);
		if (!p->name || strlen (p->name)==0) {
			printf ("Blank PPS at %d\n", p->pps);
			return p;
		}
		pps++;
	}

	for (lp=0;lp<PPS_BLOCK_SIZE;lp++)
		mem[lp] = 0;
	
	ans      = pps_decode (mem);
	ans->pps = max_pps;
	g_ptr_array_add (f->pps, ans);
	g_assert (g_ptr_array_index (f->pps, max_pps) == ans);

	return ans;
}

/**
 * This is passed the handle of a directory in which to create the
 * new stream / directory.
 **/
MS_OLE_DIRECTORY *
ms_ole_directory_create (MS_OLE_DIRECTORY *d, char *name, PPS_TYPE type)
{
	/* Find a free PPS */
	PPS *p = next_free_pps (d->file);
	PPS *dp, *prim;
	MS_OLE *f = d->file;
	MS_OLE_DIRECTORY *nd = g_new0 (MS_OLE_DIRECTORY, 1);
	BLP  startblock;
	guint8 *mem;
	int lp=0;

	if (!d || !d->file || d->file->mode != 'w') {
		printf ("Trying to write to readonly file\n");
		g_free (nd);
		return NULL;
	}

	if (!name) {
		printf ("No name!\n");
		g_free (nd);
		return NULL;
	}

	d->file->dirty = 1;
	dp = g_ptr_array_index (f->pps, d->pps);
	
	p->name = g_strdup (name);

	/* Chain into the directory */
	if (dp->dir == PPS_END_OF_CHAIN) {
#if OLE_DEBUG > 0
		printf ("First directory entry\n");
#endif
		prim    = p;
		dp->dir = p->pps;
		p->dir  = PPS_END_OF_CHAIN;
		p->next = PPS_END_OF_CHAIN;
		p->prev = PPS_END_OF_CHAIN;
	} else { /* FIXME: this should insert in alphabetic order */
		PPS_IDX oldnext;
		prim       = g_ptr_array_index (f->pps, dp->dir);
		oldnext    = prim->next;
		prim->next = p->pps;
		p->next    = oldnext;
		p->prev    = PPS_END_OF_CHAIN;
		p->dir     = PPS_END_OF_CHAIN;
#if OLE_DEBUG > 0
		printf ("New directory entry after %d\n", dp->dir);
#endif
	}

	p->type  = type;
	p->size  = 0;
	p->start = END_OF_CHAIN;

	printf ("Created file with name '%s'\n", name);
	d->primary_entry = p->pps;

	return pps_to_dir (d->file, p->pps, d);
}

void
ms_ole_directory_destroy (MS_OLE_DIRECTORY *d)
{
	if (d)
		g_free (d);
}

