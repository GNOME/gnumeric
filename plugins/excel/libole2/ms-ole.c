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

#define PPS_ROOT_INDEX    0
#define PPS_BLOCK_SIZE 0x80
#define PPS_END_OF_CHAIN 0xffffffff

typedef struct _PPS PPS;

struct _PPS {
	char    *name;
	GList   *children;
	PPS     *parent;
	guint32  size;
	BLP      start;
	PPS_TYPE type;
	PPS_IDX  idx; /* Only used on write */
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
	printf ("Num BBD Blocks : %d Root %%d, SB blocks %d\n",
		f->bb?f->bb->len:-1,
/*		f->pps?f->pps->len:-1, */
		f->sb?f->sb->len:-1);
	printf ("-------------------------------------------------------------\n");
}

static void
characterise_block (MS_OLE *f, BLP blk, char **ans)
{
	int lp, nblk;

	nblk = g_array_index (f->bb, BLP, blk);
	if (nblk == UNUSED_BLOCK) {
		*ans = "unused";
		return;
	} else if (nblk == SPECIAL_BLOCK) {
		*ans = "special";
		return;
	}
	*ans = "unknown";
	g_return_if_fail (f);
	g_return_if_fail (f->bb);
	g_return_if_fail (f->pps);

/*	for (lp=0;lp<f->pps->len;lp++) {
		PPS *p = g_ptr_array_index (f->pps, lp);
		BLP cur = p->start;
		while (cur != END_OF_CHAIN) {
			if (cur == SPECIAL_BLOCK ||
			    cur == UNUSED_BLOCK) {
				*ans = "serious block error";
				return;
			}
			if (cur == blk) {
				*ans = p->name;
				return;
			}
			cur = NEXT_BB(f, cur);
		}
		}*/
}

static void
dump_tree (GList *list, int indent)
{
	PPS *p;
	int lp;
	char indentstr[64];
	g_return_if_fail (indent<60);

	for (lp=0;lp<indent;lp++)
		indentstr[lp]= '-';
	indentstr[lp]=0;

	while (list) {
		p = list->data;
		if (p) {
			printf ("%s '%s' - %d\n",
				indentstr, p->name, p->size);
			if (p->children)
				dump_tree (p->children, indent+1);
		} else
			printf ("%s NULL!\n", indentstr);
		list = g_list_next (list);
	}
}

static void
dump_allocation (MS_OLE *f)
{
	int lp;
	char *blktype;

	for (lp=0;lp<f->bb->len;lp++) {
		characterise_block (f, lp, &blktype);
		printf ("Block %d -> block %d ( '%s' )\n", lp,
			g_array_index (f->bb, BLP, lp),
			blktype);
	}
	
	if (f->pps) {
		printf ("Root blocks : %d\n", f->num_pps); 
		dump_tree (f->pps, 0);
	} else
		printf ("No root yet\n");
/*	
	printf ("sbd blocks : %d\n", h->sbd_list->len);
	for (lp=0;lp<h->sbd_list->len;lp++)
	printf ("sbd_list[%d] = %d\n", lp, (int)ms_array_index (h->sbd_list, SBPtr, lp));*/
	printf ("-------------------------------------------------------------\n");
}

/**
 * Dump some useful facts.
 **/
void
ms_ole_debug (MS_OLE *f, int magic)
{
	dump_header (f);
	dump_allocation (f);
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

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (f->mem, 0);

	f->bb   = g_array_new (FALSE, FALSE, sizeof(BLP));
	numbbd  = GET_NUM_BBD_BLOCKS  (f);

        /* Sanity checks */
	if (numbbd < ((f->length - BB_BLOCK_SIZE + ((BB_BLOCK_SIZE*BB_BLOCK_SIZE)/4) - 1) /
		      ((BB_BLOCK_SIZE*BB_BLOCK_SIZE)/4))) {
		printf ("Duff block descriptors\n");
		return 0;
	}
	
	for (lp=0;lp<(f->length/BB_BLOCK_SIZE)-1;lp++) {
		BLP tmp = get_next_block (f, lp);
		g_array_append_val (f->bb, tmp);
	}

	/* Free up those blocks for a bit. */
	for (lp=0;lp<numbbd;lp++)
		g_array_index (f->bb, BLP, GET_BBD_LIST(f,lp)) = UNUSED_BLOCK;

	g_assert (f->bb->len < f->length/BB_BLOCK_SIZE);

	/* More sanity checks */
/*	for (lp=0;lp<numbbd;lp++) {
		BLP bbdblk = GET_BBD_LIST(f, lp);
		if (g_array_index(f->bb, BLP, bbdblk) != SPECIAL_BLOCK) {
			printf ("Error - BBD blocks not marked correctly\n");
			g_array_free (f->bb, TRUE);
			return 0;
		}
		}*/

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

#endif
}

static BLP
next_free_bb (MS_OLE *f)
{
	BLP blk, tblk;
	guint32 idx, lp;
  
	g_assert (f);

	blk = 0;
	g_assert (f->bb->len < f->length/BB_BLOCK_SIZE);
	while (blk < f->bb->len)
		if (g_array_index (f->bb, BLP, blk) == UNUSED_BLOCK)
			return blk;
	        else 
			blk++;

	extend_file (f, 1);
	tblk = UNUSED_BLOCK;
	g_array_append_val (f->bb, tblk);
#ifndef OLE_MMAP
#       error Need to extend bbptr as well.
#endif
	g_assert ((g_array_index (f->bb, BLP, blk) == UNUSED_BLOCK));
	g_assert (f->bb->len < f->length/BB_BLOCK_SIZE);
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
		if (f->sbf->len > 0)
			g_array_index (f->bb, BLP, 
				       g_array_index (f->sbf, BLP, f->sbf->len-1)) = new_sbf;
		g_array_append_val (f->sbf, new_sbf);
		g_array_index (f->bb, BLP, new_sbf) = END_OF_CHAIN;
	}

	g_assert ((f->sb->len + (BB_BLOCK_SIZE/SB_BLOCK_SIZE) - 1) / (BB_BLOCK_SIZE/SB_BLOCK_SIZE) <= f->sbf->len);

	return blk;
}

static guint8 *
get_pps_ptr (MS_OLE *f, PPS_IDX i)
{
	int lp;
	BLP blk = GET_ROOT_STARTBLOCK (f);

	lp = i/(BB_BLOCK_SIZE/PPS_BLOCK_SIZE);
	while (lp && blk != END_OF_CHAIN) {
		if (blk == SPECIAL_BLOCK ||
		    blk == UNUSED_BLOCK) {
			printf ("Duff block in root chain\n");
			return 0;
		}
		lp--;
		blk = NEXT_BB(f, blk);
	}
	if (blk == END_OF_CHAIN) {
		printf ("Serious error finding pps %d\n", i);
		return 0;
	}
	return BBPTR(f, blk) + (i%(BB_BLOCK_SIZE/PPS_BLOCK_SIZE))*PPS_BLOCK_SIZE;
}

static gint
pps_compare_func (PPS *a, PPS *b)
{
	g_return_val_if_fail (a, 0);
	g_return_val_if_fail (b, 0);
	g_return_val_if_fail (a->name, 0);
	g_return_val_if_fail (b->name, 0);
	
	return g_strcasecmp (b->name, a->name);
}

static void
pps_decode_tree (MS_OLE *f, PPS_IDX p, PPS *parent)
{
	PPS    *pps, *tpps;
	guint8 *mem;
	GList  *tmp;
       
	if (p == PPS_END_OF_CHAIN)
		return;

	pps           = g_new (PPS, 1);
	mem           = get_pps_ptr (f, p);
	pps->name     = pps_get_text  (mem, PPS_GET_NAME_LEN(mem));
	pps->type     = PPS_GET_TYPE  (mem);
	pps->size     = PPS_GET_SIZE  (mem);
	pps->children = NULL;
	pps->parent   = parent;
	pps->idx      = 0;
	if (!pps->name) { /* Make safe */
		printf ("how odd: blank named file in directory\n");
		g_free (pps);
		return;
	}

	f->num_pps++;
	
	if (parent) {
#if OLE_DEBUG > 0
		printf ("Inserting '%s' into '%s'\n", pps->name, parent->name);
#endif
		parent->children = g_list_insert_sorted (parent->children, pps,
							 (GCompareFunc)pps_compare_func);
	}
	else {
#if OLE_DEBUG > 0
		printf ("Setting root to '%s'\n", pps->name);
#endif
		f->pps = g_list_append (0, pps);
	}

	if (PPS_GET_NEXT(mem) != PPS_END_OF_CHAIN)
		pps_decode_tree (f, PPS_GET_NEXT(mem), parent);
		
	if (PPS_GET_PREV(mem) != PPS_END_OF_CHAIN)
		pps_decode_tree (f, PPS_GET_PREV(mem), parent);

	if (PPS_GET_DIR (mem) != PPS_END_OF_CHAIN)
		pps_decode_tree (f, PPS_GET_DIR(mem), pps);

	pps->start   = PPS_GET_STARTBLOCK (mem);
	
#if OLE_DEBUG > 1
	printf ("PPS decode : '%s'\n", pps->name?pps->name:"Null");
	dump (mem, PPS_BLOCK_SIZE);
#endif
	return;
}

static int
read_pps (MS_OLE *f)
{
	GPtrArray *ans = g_ptr_array_new ();

	g_return_val_if_fail (f, 0);

	f->num_pps = 0;
	pps_decode_tree (f, PPS_ROOT_INDEX, NULL);

	if (g_list_length (f->pps) < 1 ||
	    g_list_length (f->pps) > 1) {
		printf ("Invalid root chain\n");
		return 0;
	} else if (!f->pps->data) {
		printf ("No root entry\n");
		return 0;
	}

	{ /* Free up the root chain */
		BLP blk, last;
		last = blk = GET_ROOT_STARTBLOCK (f);
		while (blk != END_OF_CHAIN) {
			last = blk;
			blk = NEXT_BB(f, blk);
			g_array_index (f->bb, BLP, last) = UNUSED_BLOCK;
		}
	}
	
	if (!f->pps) {
		printf ("Root directory too small\n");
		return 0;
	}
	return 1;
}

/**
 * Write the blocks main data recursively.
 **/
static void
pps_encode_tree_initial (MS_OLE *f, GList *list, PPS_IDX *p)
{
	int lp, max;
	guint8 *mem;
	PPS    *pps;

	g_return_if_fail (list);
	g_return_if_fail (list->data);
	
	pps = list->data;
	pps->idx = *p;
	(*p)++;
	mem = get_pps_ptr (f, pps->idx);

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
		printf ("No name %d\n", *p);
		max = -1;
	}
	PPS_SET_NAME_LEN(mem, (max+1)*2);
	
	/* Magic numbers */
	SET_GUINT8   (mem + 0x43, 0x01); /* Or zero ? */
	SET_GUINT32  (mem + 0x50, 0x00020900);
	if (pps->idx == PPS_ROOT_INDEX) { /* Only Root */
		SET_GUINT32  (mem + 0x58, 0x000000c0);
		SET_GUINT32  (mem + 0x5c, 0x46000000);
	}

	PPS_SET_TYPE (mem, pps->type);
	PPS_SET_SIZE (mem, pps->size);
        PPS_SET_STARTBLOCK(mem, pps->start);
	PPS_SET_NEXT (mem, PPS_END_OF_CHAIN);
	PPS_SET_PREV (mem, PPS_END_OF_CHAIN);
	PPS_SET_DIR  (mem, PPS_END_OF_CHAIN);

#if OLE_DEBUG > 0
	printf ("encoding '%s' as %d\n", pps->name, pps->idx);
#endif

	if (pps->children)
		pps_encode_tree_initial (f, pps->children, p);
	if (g_list_next (list))
		pps_encode_tree_initial (f, g_list_next(list), p);
}

/**
 * Chain the blocks together afterwards
 * FIXME: Leaks like a sieve
 **/
static void
pps_encode_tree_chain (MS_OLE *f, GList *list)
{
	PPS     *pps, *p;
	GList   *l;
	int      lp, len;
	PPS     *next, *prev;
	guint8  *mem, *parmem;

	g_return_if_fail (list);
	g_return_if_fail (list->data);
	
	pps      = list->data;
	parmem   = get_pps_ptr (f, pps->idx);
	g_return_if_fail (pps->children);
	len      = g_list_length (pps->children);
	l        = pps->children;

	if (len==0) {
#if OLE_DEBUG > 0
		printf ("Empty directory '%s'\n", pps->name);
		return;
#endif		
	} else if (len==1) {
		p = l->data;
		PPS_SET_DIR  (parmem, p->idx);
		return;
	}

	g_assert (l);
	next = prev = p = l->data;

#if OLE_DEBUG > 0
	printf ("No. of entries is %d\n", len);
#endif
	if (len/2==1)
		l = g_list_next (l);

	for (lp=1;lp<len/2;lp++) {
		p    = l->data;
		prev = g_list_previous(l)->data;

#if OLE_DEBUG > 0
		printf ("Chaining previous for '%s'\n", p->name);
#endif
		if (p->type == MS_OLE_PPS_STORAGE)
			pps_encode_tree_chain (f, l);

		mem  = get_pps_ptr (f, p->idx);
		PPS_SET_NEXT (mem, PPS_END_OF_CHAIN);
		PPS_SET_PREV (mem, prev->idx);
		l    = g_list_next (l);
	}

	g_assert (l);
	prev   = p;
	p      = l->data;

	/* The base node of the directory */
	PPS_SET_DIR  (parmem, p->idx);

#if OLE_DEBUG > 0
	printf ("Base node is '%s'\n", p->name);
#endif

	/* Points potentialy both ways */
	mem    = get_pps_ptr (f, p->idx);
	PPS_SET_PREV (mem, prev->idx);
	l      = g_list_next (l);
	if (l)
		PPS_SET_NEXT (mem, ((PPS *)l->data)->idx);
	else
		PPS_SET_NEXT (mem, PPS_END_OF_CHAIN);

	while (l && g_list_next(l)) {
	        p    = l->data;
		next = g_list_next (l)->data;

#if OLE_DEBUG > 0
		printf ("Chaining next for '%s'\n", p->name);
#endif
		if (p->type == MS_OLE_PPS_STORAGE)
			pps_encode_tree_chain (f, l);

		mem  = get_pps_ptr (f, p->idx);
		PPS_SET_NEXT (mem, next->idx);
		PPS_SET_PREV (mem, PPS_END_OF_CHAIN);
		l = g_list_next (l);
	}
}

static int
write_pps (MS_OLE *f)
{
	int lp;
	PPS_IDX idx;
	BLP blk  = END_OF_CHAIN;
	BLP last = END_OF_CHAIN;
	guint8 *mem;

	/* Build the root chain */
	for (lp=0;lp<(f->num_pps+(BB_BLOCK_SIZE/PPS_BLOCK_SIZE)-1)/(BB_BLOCK_SIZE/PPS_BLOCK_SIZE);lp++) {
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

	g_assert (GET_ROOT_STARTBLOCK(f) != END_OF_CHAIN);

	idx    = PPS_ROOT_INDEX;
	pps_encode_tree_initial (f, f->pps, &idx);
	pps_encode_tree_chain   (f, f->pps);

	f->pps = 0;
	f->num_pps = 0;
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

	root = f->pps->data;
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

	root = f->pps->data;

	if (f->sbf->len * BB_BLOCK_SIZE < f->sb->len*SB_BLOCK_SIZE) {
		printf ("Not enough descriptor / blocks being written %d %d\n",
			f->sbf->len, f->sb->len);
	}
	if (f->sbf->len>0)
		sbf_start = g_array_index (f->sbf, BLP, 0);

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

	g_assert (f->bb->len < f->length/BB_BLOCK_SIZE);

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
		p           = g_new(PPS, 1);
		p->name     = g_strdup ("Root Entry");
		p->start    = END_OF_CHAIN;
		p->type     = MS_OLE_PPS_ROOT;
		p->size     = 0;
		p->children = 0;
		f->pps = g_list_append (0, p);
		f->num_pps = 1;
	}
	g_assert (f->bb->len < f->length/BB_BLOCK_SIZE);
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
	p = s->pps;

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
			PPS *p = s->pps;
#if OLE_DEBUG > 0
			printf ("Set first Small block to %d\n", block);
#endif
			p->start = block;
		}

		g_array_index (s->file->sb, BLP, block) = eoc;
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
#if OLE_DEBUG > 1
			printf ("Chained Big block %d to block %d\n", block, lastblk);
#endif
		} else { /* First block in a file */
			PPS *p = s->pps;
#if OLE_DEBUG > 0
			printf ("Set first Big block to %d\n", block);
#endif
			p->start = block;
		}

		g_array_index (s->file->bb, BLP, block) = eoc;
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

#if OLE_DEBUG > 1
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
			PPS         *p = s->pps;
			ms_ole_pos_t oldlen;
			guint8      *buffer;

			buffer       = g_new (guint8, s->size);
			s->lseek     (s, 0, MS_OLE_SEEK_SET);
			oldlen       = s->size;
			s->read_copy (s, buffer, oldlen);

			free_allocation (s->file, p->start, 0);
			p->start    = END_OF_CHAIN;
#if OLE_DEBUG > 1
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
#if OLE_DEBUG > 1
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

	p           = d->pps->data;

	s           = g_new0 (MS_OLE_STREAM, 1);
	s->file     = f;
	s->pps      = p;
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
		if (s->file && s->file->mode == 'w')
			((PPS *)s->pps)->size = s->size;

		if (s->blocks)
			g_array_free (s->blocks, TRUE);
		g_free (s);
	}
}

static MS_OLE_DIRECTORY *
pps_to_dir (MS_OLE *f, GList *l, MS_OLE_DIRECTORY *d)
{
	PPS *p;
	MS_OLE_DIRECTORY *dir;

	g_return_val_if_fail (f, 0);
	g_return_val_if_fail (l, 0);
	g_return_val_if_fail (f->pps, 0);
	g_return_val_if_fail (l->data, 0);
	
	p = l->data;
	if (d)
		dir = d;
	else
		dir = g_new (MS_OLE_DIRECTORY, 1);
	dir->name   = p->name;
	dir->type   = p->type;
	dir->pps    = l;
	dir->length = p->size;
	dir->file   = f;
	dir->first  = 0;
	return dir;
}

MS_OLE_DIRECTORY *
ms_ole_get_root (MS_OLE *f)
{
	return pps_to_dir (f, f->pps, NULL);
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
	if (!d || !d->file || !d->pps)
		return 0;

	if (d->first) /* Hack for now */
		d->first = 0;
	else
		d->pps = g_list_next (d->pps);

	if (!d->pps || !d->pps->data ||
	    !((PPS *)d->pps->data)->name)
		return ms_ole_directory_next (d);
	pps_to_dir (d->file, d->pps, d);
	
#if OLE_DEBUG > 0
	printf ("Forward next '%s' %d %d\n", d->name, d->type, d->length);
#endif
	return 1;
}

void
ms_ole_directory_enter (MS_OLE_DIRECTORY *d)
{
	MS_OLE *f;
	PPS    *p;
	if (!d || !d->file || !d->pps)
		return;

	f = d->file;
	p = d->pps->data;

	d->first = 1;

	if (d->type != MS_OLE_PPS_STORAGE &&
	    d->type != MS_OLE_PPS_ROOT) {
		printf ("Bad type %d %d\n", d->type, MS_OLE_PPS_ROOT);
		return;
	}

	if (p->children)
		d->pps = p->children;
        else
		printf ("Can't enter '%s'\n", p->name);
	return;
}

void
ms_ole_directory_unlink (MS_OLE_DIRECTORY *d)
{
	MS_OLE *f;
	if (!d || !d->file || !d->pps)
		return;
	
	f = d->file;

	d->file->dirty = 1;

/* Only problem is: have to find its parent in the tree. */
/*	if (d->pps != d->primary_entry) {
		PPS *p = g_ptr_array_index (f->pps, d->pps);
		if (p->next == PPS_END_OF_CHAIN &&
		    p->prev == PPS_END_OF_CHAIN) {  Little, lost & loosely attached
			g_free (p->name);
			p->name = 0;
		}
	}
	else */
	printf ("FIXME: Unlink unimplemented\n");
}

/**
 * This is passed the handle of a directory in which to create the
 * new stream / directory.
 **/
MS_OLE_DIRECTORY *
ms_ole_directory_create (MS_OLE_DIRECTORY *d, char *name, PPS_TYPE type)
{
	/* Find a free PPS */
	PPS *p;
	PPS *dp;
	MS_OLE *f = d->file;
	BLP  startblock;
	guint8 *mem;
	int lp=0;

	if (!d || !d->pps || !d->pps->data ||
	    !d->file || d->file->mode != 'w') {
		printf ("Trying to write to readonly file\n");
		return NULL;
	}

	if (!name) {
		printf ("No name!\n");
		return NULL;
	}

	d->file->dirty = 1;

	dp = d->pps->data;
	p  = g_new (PPS, 1);
	p->name = g_strdup (name);
	p->type  = type;
	p->size  = 0;
	p->start = END_OF_CHAIN;
	
	dp->children = g_list_insert_sorted (dp->children, p,
					     (GCompareFunc)pps_compare_func);

	printf ("Created file with name '%s'\n", name);
	return pps_to_dir (d->file, g_list_find (dp->children, p), 0);
}

void
ms_ole_directory_destroy (MS_OLE_DIRECTORY *d)
{
	if (d)
		g_free (d);
}

