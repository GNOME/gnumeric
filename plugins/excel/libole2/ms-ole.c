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

#define OLE_DEBUG 1

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
#define ms_array_index(a,b,c) (b)my_array_hack ((a), sizeof(b), (c))

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
#define ms_array_index(a,b,c) g_array_index (a, b, c)

#endif

/* Bound checks guint8 pointer p inside f */
#define CHECK_VALID(f,ptr) (g_assert ((ptr) >= (f)->mem && \
				      (ptr) <  (f)->mem + (f)->length))

/**
 * These look _ugly_ but reduce to a few shifts, bit masks and adds
 * Under optimisation these are _really_ quick !
 * NB. Parameters are always: 'MS_OLE *', followed by a guint32 block or index
 **/

/* Return total number of Big Blocks in file, NB. first block = root */
#define NUM_BB(f)               (((f)->length/BB_BLOCK_SIZE) - 1)

/* This is a list of big blocks which contain a flat description of all blocks in the file.
   Effectively inside these blocks is a FAT of chains of other BBs, so the theoretical max
   size = 128 BB Fat blocks, thus = 128*512*512/4 blocks ~= 8.4MBytes */
/* The number of Big Block Descriptor (fat) Blocks */
#define GET_NUM_BBD_BLOCKS(f)   (GET_GUINT32((f)->mem + 0x2c))
#define SET_NUM_BBD_BLOCKS(f,n) (SET_GUINT32((f)->mem + 0x2c, (n)))
/* The block locations of the Big Block Descriptor Blocks */
#define GET_BBD_LIST(f,i)           (GET_GUINT32((f)->mem + 0x4c + (i)*4))
#define SET_BBD_LIST(f,i,n)         (SET_GUINT32((f)->mem + 0x4c + (i)*4, (n)))

/* Find the position of the start in memory of a big block   */
#define GET_BB_START_PTR(f,n) ((guint8 *)((f)->mem+((n)+1)*BB_BLOCK_SIZE))
/* Find the position of the start in memory of a small block */
#define GET_SB_START_PTR(f,n) ( GET_BB_START_PTR((f), ms_array_index ((f)->header.sbf_list, SBPtr, ((SB_BLOCK_SIZE*(n))/BB_BLOCK_SIZE))) + \
			       (SB_BLOCK_SIZE*(n)%BB_BLOCK_SIZE) )

/* Get the start block of the root directory ( PPS ) chain */
#define GET_ROOT_STARTBLOCK(f)   (GET_GUINT32((f)->mem + 0x30))
#define SET_ROOT_STARTBLOCK(f,i) (SET_GUINT32((f)->mem + 0x30, i))
/* Get the start block of the SBD chain */
#define GET_SBD_STARTBLOCK(f)    (GET_GUINT32((f)->mem + 0x3c))
#define SET_SBD_STARTBLOCK(f,i)  (SET_GUINT32((f)->mem + 0x3c, i))

/* Gives the position in memory, as a GUINT8 *, of the BB entry ( in a chain ) */
#define GET_BB_CHAIN_PTR(f,n) (GET_BB_START_PTR((f), (GET_BBD_LIST((f), ( ((n)*sizeof(BBPtr)) / BB_BLOCK_SIZE)))) + \
                              (((n)*sizeof(BBPtr))%BB_BLOCK_SIZE))

/* Gives the position in memory, as a GUINT8 *, of the SB entry ( in a chain ) of a small block index */
#define GET_SB_CHAIN_PTR(f,n) (GET_BB_START_PTR(f, ms_array_index ((f)->header.sbd_list, SBPtr, (((n)*sizeof(SBPtr))/BB_BLOCK_SIZE))) + \
                              (((n)*sizeof(SBPtr))%BB_BLOCK_SIZE))

/* Gives the position in memory, as a GUINT8 *, of the SBD entry of a small block index */
#define GET_SBD_CHAIN_PTR(f,n) (GET_BB_CHAIN_PTR((f), ms_array_index ((f)->header.sbd_list, SBPtr, ((sizeof(SBPtr)*(n))/BB_BLOCK_SIZE))))

/* Gives the position in memory, as a GUINT8 *, of the SBF entry of a small block index */
#define GET_SBF_CHAIN_PTR(f,n) (GET_BB_CHAIN_PTR((f), ms_array_index ((f)->header.sbf_list, SBPtr, ((SB_BLOCK_SIZE*(n))/BB_BLOCK_SIZE))))


#define BB_THRESHOLD   0x1000

#define PPS_ROOT_BLOCK    0
#define PPS_BLOCK_SIZE 0x80
#define PPS_END_OF_CHAIN 0xffffffff

/* This takes a PPS_IDX and returns a guint8 * to its data */
#define PPS_PTR(f,n) (GET_BB_START_PTR((f),ms_array_index ((f)->header.root_list, BBPtr, (((n)*PPS_BLOCK_SIZE)/BB_BLOCK_SIZE))) + \
                     (((n)*PPS_BLOCK_SIZE)%BB_BLOCK_SIZE))
#define PPS_GET_NAME_LEN(f,n) (GET_GUINT16(PPS_PTR(f,n) + 0x40))
#define PPS_SET_NAME_LEN(f,n,i) (SET_GUINT16(PPS_PTR(f,n) + 0x40, i))
/* This takes a PPS_IDX and returns a char * to its rendered name */
#define PPS_NAME(f,n) (pps_get_text (PPS_PTR(f,n), PPS_GET_NAME_LEN(f,n)))
/* These takes a PPS_IDX and returns the corresponding functions PPS_IDX */
/* NB it is misleading to assume that Microsofts linked lists link correctly.
   It is not the case that pps_next(f, pps_prev(f, n)) = n ! For the final list
   item there are no valid links. Cretins. */
#define PPS_GET_PREV(f,n) ((PPS_IDX) GET_GUINT32(PPS_PTR(f,n) + 0x44))
#define PPS_GET_NEXT(f,n) ((PPS_IDX) GET_GUINT32(PPS_PTR(f,n) + 0x48))
#define PPS_GET_DIR(f,n)  ((PPS_IDX) GET_GUINT32(PPS_PTR(f,n) + 0x4c))
#define PPS_SET_PREV(f,n,i) ((PPS_IDX) SET_GUINT32(PPS_PTR(f,n) + 0x44, i))
#define PPS_SET_NEXT(f,n,i) ((PPS_IDX) SET_GUINT32(PPS_PTR(f,n) + 0x48, i))
#define PPS_SET_DIR(f,n,i)  ((PPS_IDX) SET_GUINT32(PPS_PTR(f,n) + 0x4c, i))
/* These get other interesting stuff from the PPS record */
#define PPS_GET_STARTBLOCK(f,n)    ( GET_GUINT32(PPS_PTR(f,n) + 0x74))
#define PPS_GET_SIZE(f,n)          ( GET_GUINT32(PPS_PTR(f,n) + 0x78))
#define PPS_GET_TYPE(f,n) ((PPS_TYPE)(GET_GUINT8(PPS_PTR(f,n) + 0x42)))
#define PPS_SET_STARTBLOCK(f,n,i)    ( SET_GUINT32(PPS_PTR(f,n) + 0x74, i))
#define PPS_SET_SIZE(f,n,i)          ( SET_GUINT32(PPS_PTR(f,n) + 0x78, i))
#define PPS_SET_TYPE(f,n,i)          (  SET_GUINT8(PPS_PTR(f,n) + 0x42, i))

/* Returns the next BB after this one in the chain */
#define NEXT_BB(f,n) (GET_GUINT32(GET_BB_CHAIN_PTR(f,n)))
/* Returns the next SB after this one in the chain */
#define NEXT_SB(f,n) (GET_GUINT32(GET_SB_CHAIN_PTR(f,n)))

#define GETRECDATA(a,n)   ((a>>n)&0x1)
#define SETRECDATA(a,n)   (a|=(&0x1<<n))
#define CLEARRECDATA(a,n) (a&=(0xf7f>>(n-7))

static BBPtr
get_block (MS_OLE_STREAM *s)
{
	guint32 bl;
	if (!s || !s->blocks) return END_OF_CHAIN;
	if (s->strtype==MS_OLE_SMALL_BLOCK)
		bl = s->position/SB_BLOCK_SIZE;
	else
		bl = s->position/BB_BLOCK_SIZE;
	
	if (bl<s->blocks->len)
		return ms_array_index (s->blocks, BBPtr, bl);
	else
		return END_OF_CHAIN;
}

static guint32
get_offset (MS_OLE_STREAM *s)
{
	if (s->strtype==MS_OLE_SMALL_BLOCK)
		return s->position%SB_BLOCK_SIZE;
	else
		return s->position%BB_BLOCK_SIZE;	
}

static void
set_offset (MS_OLE_STREAM *s, guint32 offset)
{
	guint lp;
	guint32 block=get_block(s);
	g_assert (0);
	if (!s || !s->blocks) return;
	for (lp=0;lp<s->blocks->len;lp++)
		if (ms_array_index (s->blocks, BBPtr, lp) == block) {
			s->position = lp * (s->strtype==MS_OLE_SMALL_BLOCK?SB_BLOCK_SIZE:BB_BLOCK_SIZE) +
				offset;
			return;
		}
	printf ("Set block failed\n");
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
#undef OFF
#undef OK
}
	
/* FIXME: This needs proper unicode support ! current support is a guess */
/* NB. Different from biff_get_text, looks like a bug ! */
static char *
pps_get_text (guint8 *ptr, int length)
{
	int lp, skip;
	char *ans;
	guint16 c;
	guint8 *inb;
	
	if (!length) 
		return 0;
	
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
	MS_OLE_HEADER *h = &f->header;
	printf ("--------------------------MS_OLE HEADER-------------------------\n");
	printf ("Num BBD Blocks : %d Root %d, SBD %d\n",
		GET_NUM_BBD_BLOCKS(f),
		(int)h->root_startblock,
		(int)h->sbd_startblock);
	for (lp=0;lp<GET_NUM_BBD_BLOCKS(f);lp++)
		printf ("GET_BBD_LIST[%d] = %d\n", lp, GET_BBD_LIST(f,lp));
	
	printf ("Root blocks : %d\n", h->root_list->len);
	for (lp=0;lp<h->root_list->len;lp++)
		printf ("root_list[%d] = %d\n", lp, (int)ms_array_index (h->root_list, BBPtr, lp));
	
	printf ("sbd blocks : %d\n", h->sbd_list->len);
	for (lp=0;lp<h->sbd_list->len;lp++)
		printf ("sbd_list[%d] = %d\n", lp, (int)ms_array_index (h->sbd_list, SBPtr, lp));
	printf ("-------------------------------------------------------------\n");
}

static int
read_header (MS_OLE *f)
{
	MS_OLE_HEADER *header = &f->header;
	
	header->root_startblock      = GET_ROOT_STARTBLOCK(f);
	header->sbd_startblock       = GET_SBD_STARTBLOCK(f);
	return 1;
}

static void
dump_allocation (MS_OLE *f)
{
	int blk, dep, lp;
	printf ("Big block allocation %d blocks, length %d\n", NUM_BB(f), f->length);

	dep = 0;
	blk = 0;
	while (dep<GET_NUM_BBD_BLOCKS(f))
	{
		printf ("FAT block %d\n", dep);
		for (lp=0;lp<BB_BLOCK_SIZE/sizeof(BBPtr);lp++)
		{
			guint32 type;
			type = GET_GUINT32(GET_BB_CHAIN_PTR(f, blk));
			if ((blk + dep*(BB_BLOCK_SIZE/sizeof(BBPtr))) == NUM_BB(f))
				printf ("|");
			else if (type == SPECIAL_BLOCK)
				printf ("S");
			else if (type == UNUSED_BLOCK)
				printf (".");
			else if (type == END_OF_CHAIN)
				printf ("X");
			else
				printf ("O");
			blk++;
			if (!(blk%16))
				printf (" - %d\n", blk-1);
		}
		dep++;
	}

	printf ("Small block allocation\n");
	dep = 0;
	blk = 0;
	while (dep<f->header.sbd_list->len)
	{
		printf ("SB block %d ( = BB block %d )\n", dep, ms_array_index (f->header.sbd_list, SBPtr, dep));
		for (lp=0;lp<BB_BLOCK_SIZE/sizeof(SBPtr);lp++)
		{
			guint32 type;
			type = GET_GUINT32(GET_SB_CHAIN_PTR(f, blk));
			if (type == SPECIAL_BLOCK)
				printf ("S");
			else if (type == UNUSED_BLOCK)
				printf (".");
			else if (type == END_OF_CHAIN)
				printf ("X");
			else
				printf ("O");
			blk++;
			if (!(blk%16))
				printf (" - %d\n", blk-1);
		}
		dep++;
	}
}

/* Create a nice linear array and return count of the number in the array */
static GArray *
read_link_array (MS_OLE *f, BBPtr first)
{
	BBPtr ptr = first;
	int lp, num=0;
	GArray *ans = g_array_new (0, 0, sizeof(BBPtr));

	ptr = first;
	while (ptr != END_OF_CHAIN) {
		if (ptr == UNUSED_BLOCK ||
		    ptr == SPECIAL_BLOCK) {
			printf ("Corrupt file: serious error, invalid block in chain\n");
			g_array_free (ans, TRUE);
			return 0;
		}
			
		g_array_append_val (ans, ptr);
		ptr = NEXT_BB (f, ptr);
	}
	return ans;
}

static int
read_root_list (MS_OLE *f)
{
	/* Find blocks belonging to root ... */
	f->header.root_list = read_link_array(f, f->header.root_startblock);
	if (!f->header.root_list)
		return 0;
	return (f->header.root_list->len!=0);
}

static int
read_sbf_list (MS_OLE *f)
{
	/* Find blocks containing all small blocks ... */
	f->header.sbf_list = read_link_array(f, f->header.sbf_startblock);
	return (f->header.sbf_list!=0);
}

static int
read_sbd_list (MS_OLE *f)
{
	/* Find blocks belonging to sbd ... */
	f->header.sbd_list = read_link_array(f, f->header.sbd_startblock);
	return (f->header.sbd_list!=0);
}

static int
ms_ole_analyse (MS_OLE *f)
{
	if (!read_header(f)) return 0;
	if (!read_root_list(f)) return 0;
	if (!read_sbd_list(f)) return 0;
	f->header.sbf_startblock = PPS_GET_STARTBLOCK(f, PPS_ROOT_BLOCK);
	if (!read_sbf_list(f)) return 0;
#if OLE_DEBUG > 2
	dump_header(f);
	dump_allocation (f);

	{
		int lp;
		for (lp=0;lp<BB_BLOCK_SIZE/PPS_BLOCK_SIZE;lp++)
		{
			printf ("PPS %d type %d, prev %d next %d, dir %d\n", lp, PPS_GET_TYPE(f,lp),
				PPS_GET_PREV(f,lp), PPS_GET_NEXT(f,lp), PPS_GET_DIR(f,lp));
			dump (PPS_PTR(f, lp), PPS_BLOCK_SIZE);
		}
	}
#endif
	return 1;
}

static void
ms_ole_deanalyse (MS_OLE *f)
{
	g_return_if_fail (f);
	if (f->header.sbd_list)
		g_array_free (f->header.sbd_list, TRUE);
	f->header.sbd_list = NULL;
	if (f->header.sbf_list)
		g_array_free (f->header.sbf_list, TRUE);
	f->header.sbf_list = NULL;
	if (f->header.root_list)
		g_array_free (f->header.root_list, TRUE);
	f->header.root_list = NULL;
}

MS_OLE *
ms_ole_create (const char *name)
{
	struct stat st;
	int file;
	MS_OLE *f;
	int init_blocks = 5, lp;
	guint8 *ptr;
	guint32 root_startblock = 0;
	guint32 sbd_startblock  = 0, zero = 0;
	char title[] ="Root Entry";

	if ((file = open (name, O_RDONLY|O_CREAT|O_TRUNC|O_NONBLOCK,
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

	f = g_new0 (MS_OLE, 1);
	f->header.sbd_list = 0;
	f->header.sbf_list = 0;
	f->header.root_list = 0;
	f->file_descriptor = file;
	fstat(file, &st);
	f->length = st.st_size;
	if (f->length%BB_BLOCK_SIZE)
		printf ("Warning file %d non-integer number of blocks\n", f->length);

	f->mem  = mmap (0, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0);
	if (!f->mem)
	{
		printf ("Serious error mapping file to %d bytes\n", BB_BLOCK_SIZE*init_blocks);
		close (file);
		g_free (f);
		return 0;
	}
	/**
	 *  Create the following block structure:
	 * Block -1 : the header block: contains the BBD_LIST
	 * Block  0 : The first PPS (root) block
	 * Block  1 : The first BBD block
	 * Block  2 : The first SBD block
	 * Block  3 : The first SBF block
	 **/

	/* The header block */

	for (lp=0;lp<BB_BLOCK_SIZE/4;lp++)
		SET_GUINT32(f->mem + lp*4, END_OF_CHAIN);

	SET_GUINT32(f->mem, 0xe011cfd0); /* Magic number */
	SET_GUINT32(f->mem + 4, 0xe11ab1a1);
	SET_NUM_BBD_BLOCKS(f, 1);
	SET_BBD_LIST(f, 0, 1);

	f->header.root_startblock = 0;
	f->header.sbd_startblock  = 2;
	f->header.sbf_startblock  = 3;
	SET_ROOT_STARTBLOCK(f, f->header.root_startblock);
	SET_SBD_STARTBLOCK (f, f->header.sbd_startblock);

	/* The first PPS block : 0 */

/*	f->header.root_list = g_array_new (0, 0, BBPtr);
	g_array_append_val (f->header.root_list, 0); */

	lp = 0;
	ptr = f->mem + BB_BLOCK_SIZE;
	while (title[lp])
		*ptr++ = title[lp++];

	for (;lp<PPS_BLOCK_SIZE;lp++) /* Blank stuff I don't understand */
		*ptr++ = 0;

	PPS_SET_NAME_LEN(f, PPS_ROOT_BLOCK, lp);

	PPS_SET_STARTBLOCK(f, PPS_ROOT_BLOCK, f->header.sbf_startblock);
	PPS_SET_TYPE(f, PPS_ROOT_BLOCK, MS_OLE_PPS_ROOT);
	PPS_SET_DIR (f, PPS_ROOT_BLOCK, PPS_END_OF_CHAIN);
	PPS_SET_NEXT(f, PPS_ROOT_BLOCK, PPS_END_OF_CHAIN);
	PPS_SET_PREV(f, PPS_ROOT_BLOCK, PPS_END_OF_CHAIN);
	PPS_SET_SIZE(f, PPS_ROOT_BLOCK, 0);

	/* the first BBD block : 1 */

	for (lp=0;lp<BB_BLOCK_SIZE/4;lp++)
		SET_GUINT32(GET_BB_START_PTR(f,1) + lp*4, END_OF_CHAIN);

	SET_GUINT32(GET_BB_CHAIN_PTR(f,1), SPECIAL_BLOCK); /* Itself */
	SET_GUINT32(GET_BB_CHAIN_PTR(f,2), END_OF_CHAIN);  /* SBD chain */
	SET_GUINT32(GET_BB_CHAIN_PTR(f,3), END_OF_CHAIN);  /* SBF stream */

	/* the first SBD block : 2 */
	for (lp=0;lp<(BB_BLOCK_SIZE/4);lp++)
		SET_GUINT32(GET_BB_START_PTR(f,2) + lp*4, UNUSED_BLOCK);

/*	f->header.number_of_sbd_blocks = 1;
	f->header.sbd_list = g_new (BBPtr, 1);
	f->header.sbd_list[0] = 2;*/

	/* the first SBF block : 3 */
	for (lp=0;lp<BB_BLOCK_SIZE/4;lp++) /* Fill with zeros */
		SET_GUINT32(GET_BB_START_PTR(f,3) + lp*4, 0);

/*	f->header.number_of_sbf_blocks = 1;
	f->header.sbf_list = g_new (BBPtr, 1);
	f->header.sbf_list[0] = 3; */

	if (!ms_ole_analyse (f)) {
		printf ("Not even the file I just created works!\n");
		ms_ole_destroy (f);
		return 0;
	}

	return f;
}


MS_OLE *
ms_ole_new (const char *name)
{
	struct stat st;
	int prot = PROT_READ | PROT_WRITE;
	int file;
	MS_OLE *f;

	if (OLE_DEBUG>0)
		printf ("New OLE file '%s'\n", name);
	f = g_new0 (MS_OLE, 1);

	f->header.sbd_list = 0;
	f->header.sbf_list = 0;
	f->header.root_list = 0;

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

	if (GET_GUINT32(f->mem    ) != 0xe011cfd0 ||
	    GET_GUINT32(f->mem + 4) != 0xe11ab1a1)
	{
		printf ("Failed OLE2 magic number %x %x\n",
			GET_GUINT32(f->mem), GET_GUINT32(f->mem+4));
		ms_ole_destroy (f);
		return 0;
	}
	if (f->length%BB_BLOCK_SIZE)
		printf ("Warning file '%s':%d non-integer number of blocks\n", name, f->length);
	if (!ms_ole_analyse (f))
	{
		printf ("Directory error\n");
		ms_ole_destroy(f);
		return 0;
	}
	printf ("New OLE file '%s'\n", name);
	return f;
}

/**
 * This closes the file and truncates any free blocks
 **/
void
ms_ole_destroy (MS_OLE *f)
{
	if (OLE_DEBUG>0)
		printf ("FIXME: should truncate to remove unused blocks\n");
	if (f)
	{
		ms_ole_deanalyse (f);

		munmap (f->mem, f->length);
		close (f->file_descriptor);
		
		g_free (f);

		if (OLE_DEBUG>0)
			printf ("Closing OLE file\n");
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
	printf ("block %d, offset %d\n", get_block(s), get_offset(s));
}

static void
extend_file (MS_OLE *f, int blocks)
{
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
	if (newptr != f->mem)
		printf ("Memory map moved \n");
	f->mem = newptr;

#if OLE_DEBUG > 5
	printf ("After extend\n");
	dump_allocation(f);
#endif
}

static guint32
next_free_bb (MS_OLE *f)
{
	guint32 blk;
	guint32 idx, lp;
  
	g_assert (f);

	blk = 0;
	while (blk < NUM_BB(f))
		if (GET_GUINT32(GET_BB_CHAIN_PTR(f,blk)) == UNUSED_BLOCK)
			return blk;
		else
			blk++;

	if (blk < GET_NUM_BBD_BLOCKS(f)*(BB_BLOCK_SIZE/sizeof(BBPtr))) {
		/* Must not extend beyond our capability to describe the state of each block */
		gint32 extblks = GET_NUM_BBD_BLOCKS(f)*(BB_BLOCK_SIZE/sizeof(BBPtr)) - NUM_BB(f);
		/* Extend and remap file */
//#if OLE_DEBUG > 0
		printf ("Extend & remap file by %d blocks...\n", extblks);
//#endif
		g_assert (extblks>0);
		extend_file (f, extblks);
		g_assert (blk < NUM_BB(f));
		g_assert (GET_GUINT32(GET_BB_CHAIN_PTR(f,blk)) == UNUSED_BLOCK);
		return blk;
	}

#if OLE_DEBUG > 0
	printf ("Out of unused BB space in %d blocks!\n", NUM_BB(f));
#endif
	extend_file (f,10);
	idx = GET_NUM_BBD_BLOCKS(f);
	g_assert (idx<(BB_BLOCK_SIZE - 0x4c)/4);
	SET_BBD_LIST(f, idx, blk);
	SET_NUM_BBD_BLOCKS(f, idx+1);
	/* Setup that block */
	for (lp=0;lp<(BB_BLOCK_SIZE/sizeof(BBPtr))-1;lp++)
		SET_GUINT32(GET_BB_START_PTR(f, blk) + lp*sizeof(BBPtr), UNUSED_BLOCK);
	SET_GUINT32(GET_BB_CHAIN_PTR(f, blk), SPECIAL_BLOCK); /* blk is the new BBD item */
#if OLE_DEBUG > 1
	printf ("Should be 10 more free blocks at least !\n");
	dump_allocation(f);
#endif
	return next_free_bb (f);
}

static guint32
next_free_sb (MS_OLE *f)
{
	guint32 blk;
	guint32 idx, lp;
  
	g_assert (f);

	blk = 0;

	g_assert (f->header.sbf_list->len/SB_BLOCK_SIZE <
		  f->header.sbd_list->len*(BB_BLOCK_SIZE/sizeof(SBPtr)));

	while (blk < (f->header.sbf_list->len*(BB_BLOCK_SIZE/SB_BLOCK_SIZE)))
		if (GET_GUINT32(GET_SB_CHAIN_PTR(f, blk)) == UNUSED_BLOCK) {
			CHECK_VALID(f, GET_SB_CHAIN_PTR(f, blk));
			return blk;
		}
		else
			blk++;
	/* Create an extra block on the Small block stream */
	{
		BBPtr new_sbf = next_free_bb(f);
		int oldnum = f->header.sbf_list->len;
		BBPtr end_sbf = ms_array_index (f->header.sbf_list, BBPtr, oldnum-1);
		printf ("Extend & remap SBF file block %d chains to %d...\n", end_sbf, new_sbf);
		g_assert (new_sbf != END_OF_CHAIN);
		SET_GUINT32(GET_BB_CHAIN_PTR(f, end_sbf), new_sbf);
		SET_GUINT32(GET_BB_CHAIN_PTR(f, new_sbf), END_OF_CHAIN);
		g_assert (NEXT_BB(f, end_sbf) == new_sbf);

		ms_ole_deanalyse (f);
		ms_ole_analyse (f);

		g_assert (oldnum+1 == f->header.sbf_list->len);
		/* Append the new block to the end of the SBF(ile) */
		g_assert (blk < f->header.sbf_list->len*BB_BLOCK_SIZE/SB_BLOCK_SIZE);
		printf ("Unused small block at %d\n", blk);
	}
	/* Extend the SBD(escription) chain if neccessary */
	if (blk >= f->header.sbd_list->len*(BB_BLOCK_SIZE/sizeof(SBPtr)))
	{
		BBPtr new_sbd = next_free_bb(f);
		BBPtr oldnum = f->header.sbd_list->len;
		BBPtr end_sbd = ms_array_index (f->header.sbd_list, BBPtr, oldnum-1);

		SET_GUINT32(GET_BB_CHAIN_PTR(f, end_sbd), new_sbd);
		SET_GUINT32(GET_BB_CHAIN_PTR(f, new_sbd), END_OF_CHAIN);

		printf ("Extended SBD\n");

		ms_ole_deanalyse (f);
		ms_ole_analyse (f);
		g_assert (oldnum+1 == f->header.sbd_list->len);

		/* Setup that block */
		for (lp=0;lp<BB_BLOCK_SIZE/sizeof(SBPtr);lp++)
			SET_GUINT32(GET_BB_START_PTR(f, new_sbd) + lp*sizeof(SBPtr), UNUSED_BLOCK);
	}
	CHECK_VALID(f, GET_SB_CHAIN_PTR(f, blk));
	return blk;
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

	if (is_big_block_stream)
	{
		BBPtr p = startblock;
		printf ("FIXME: this should also free up blocks\n");
		while (p != END_OF_CHAIN)
		{
			BBPtr next = NEXT_BB(f,p);
			SET_GUINT32 (GET_BB_CHAIN_PTR(f,p), UNUSED_BLOCK);
			p = next;
		}
	}
	else
	{
		SBPtr p = startblock;
		while (p != END_OF_CHAIN)
		{
			SBPtr next = NEXT_SB(f,p);
			SET_GUINT32 (GET_SB_CHAIN_PTR(f,p), UNUSED_BLOCK);
			p = next;
		}
		
		{
			SBPtr lastFree = END_OF_CHAIN;
			SBPtr cur = 0, next;
			while (cur < f->header.sbd_list->len*BB_BLOCK_SIZE/sizeof(SBPtr))
			{
				if (GET_GUINT32(GET_SB_CHAIN_PTR(f,cur)) != UNUSED_BLOCK)
					lastFree = END_OF_CHAIN;
				else if (lastFree == END_OF_CHAIN)
					lastFree = cur;
				cur++;
			}

			if (lastFree == END_OF_CHAIN)
				return;

#if OLE_DEBUG > 1
			printf ("Before freeing stuff\n");
			dump_allocation(f);
#endif
			if (lastFree == 0) /* We don't know whether MS can cope with no SB */
				lastFree++;

			cur = lastFree; /* Shrink the Small block bits .... */
			while (cur < f->header.sbd_list->len*BB_BLOCK_SIZE/sizeof(SBPtr))
			{
/*				if (cur%(BB_BLOCK_SIZE/SB_BLOCK_SIZE) == 0)
				        * We can free a SBF block *
					   SET_GUINT32(GET_SBF_CHAIN_PTR(f,cur), UNUSED_BLOCK); */
				if (lastFree%(BB_BLOCK_SIZE/sizeof(SBPtr))==0)
					/* Free a whole SBD block */
					SET_GUINT32(GET_SBD_CHAIN_PTR(f,cur), UNUSED_BLOCK);
				cur++;
		        }
			ms_ole_deanalyse (f);
			ms_ole_analyse (f);
#if OLE_DEBUG > 1
			printf ("After free_alloc\n");
			dump_allocation(f);
#endif
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
		    (ms_array_index (s->blocks, BBPtr, blockidx)+1
		     != ms_array_index (s->blocks, BBPtr, blockidx+1)))
			return 0;
		blockidx++;
	}
	/* Straight map, simply return a pointer */
	ans = GET_BB_START_PTR(s->file, ms_array_index (s->blocks, BBPtr, s->position/BB_BLOCK_SIZE))
		+ s->position%BB_BLOCK_SIZE;
	ms_ole_lseek (s, length, MS_OLE_SEEK_CUR);
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
		    (ms_array_index (s->blocks, BBPtr, blockidx)+1
		     != ms_array_index (s->blocks, BBPtr, blockidx+1)))
			return 0;
		blockidx++;
	}
	/* Straight map, simply return a pointer */
	ans = GET_SB_START_PTR(s->file, ms_array_index (s->blocks, BBPtr, s->position/SB_BLOCK_SIZE))
		+ s->position%SB_BLOCK_SIZE;
	ms_ole_lseek (s, length, MS_OLE_SEEK_CUR);
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
		BBPtr block;
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
		block = ms_array_index (s->blocks, BBPtr, blkidx);
		src = GET_BB_START_PTR(s->file, block) + offset;
		
		memcpy (ptr, src, cpylen);
		ptr   += cpylen;
		length -= cpylen;
		
		offset = 0;
		
		blkidx++;
		s->position+=cpylen;
	}
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
		BBPtr block;
		if (cpylen>length)
			cpylen = length;
		if (s->position + cpylen > s->size ||
		    blkidx == s->blocks->len) {
			printf ("Trying 3 to read beyond end of stream %d+%d %d\n",
				s->position, cpylen, s->size);
			return 0;
		}
		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, BBPtr, blkidx);
		src = GET_SB_START_PTR(s->file, block) + offset;
				
		memcpy (ptr, src, cpylen);
		ptr   += cpylen;
		length -= cpylen;
		
		offset = 0;

		blkidx++;
		s->position+=cpylen;
	}
	return 1;
}

static void
ms_ole_append_block (MS_OLE_STREAM *s)
{
	BBPtr block;
	BBPtr lastblk=END_OF_CHAIN;

	if (s->strtype==MS_OLE_SMALL_BLOCK) {
		if (!s->blocks)
			s->blocks = g_array_new (0,0,sizeof(SBPtr));

		else if (s->blocks->len>0)
			lastblk = ms_array_index (s->blocks, SBPtr, s->blocks->len-1);

		block = next_free_sb (s->file);
		g_array_append_val (s->blocks, block);

		if (lastblk != END_OF_CHAIN) { /* Link onwards */
			SET_GUINT32(GET_SB_CHAIN_PTR(s->file, lastblk), block);
#if OLE_DEBUG > 0
			printf ("Chained block %d to previous block %d\n", block, lastblk);
#endif
		} else { /* First block in a file */
#if OLE_DEBUG > 0
			printf ("Set first block to %d\n", block);
#endif
			PPS_SET_STARTBLOCK(s->file, s->pps, block);
		}

		SET_GUINT32(GET_SB_CHAIN_PTR(s->file, block), END_OF_CHAIN);
#if OLE_DEBUG > 3
		printf ("Linked stuff\n");
		dump_allocation(s->file);
#endif
	} else {
		if (!s->blocks)
			s->blocks = g_array_new (0,0,sizeof(BBPtr));
		else if (s->blocks->len>0)
			lastblk = ms_array_index (s->blocks, BBPtr, s->blocks->len-1);

		block = next_free_bb (s->file);
		g_array_append_val (s->blocks, block);

		if (lastblk != END_OF_CHAIN) { /* Link onwards */
			SET_GUINT32(GET_BB_CHAIN_PTR(s->file, lastblk), block);
#if OLE_DEBUG > 0
			printf ("Chained block %d to block %d\n", block, lastblk);
#endif
		} else { /* First block in a file */
#if OLE_DEBUG > 0
			printf ("Set first block to %d\n", block);
#endif
			PPS_SET_STARTBLOCK(s->file, s->pps, block);
		}

		SET_GUINT32(GET_BB_CHAIN_PTR(s->file, block), END_OF_CHAIN);
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
	
	while (bytes>0)
	{
		BBPtr block;
		int cpylen = BB_BLOCK_SIZE - offset;

		if (cpylen>bytes)
			cpylen = bytes;
		
		if (!s->blocks || blkidx>=s->blocks->len)
			ms_ole_append_block (s);

		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, BBPtr, blkidx);
		
		dest = GET_BB_START_PTR(s->file, block) + offset;

		s->size+=cpylen;
#if OLE_DEBUG > 0
		printf ("Copy %d bytes to block %d\n", cpylen, block);
#endif
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;
		
		offset = 0;
		blkidx++;
	}
	s->lseek (s, length, MS_OLE_SEEK_CUR);
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
	
	while (bytes>0)
	{
		SBPtr block;
		int cpylen = SB_BLOCK_SIZE - offset;

		if (cpylen>bytes)
			cpylen = bytes;
		
		if (!s->blocks || blkidx >= s->blocks->len)
			ms_ole_append_block (s);
		g_assert (s->blocks);

		g_assert (blkidx < s->blocks->len);
		block = ms_array_index (s->blocks, SBPtr, blkidx);
		
		dest = GET_SB_START_PTR(s->file, block) + offset;
		
		g_assert (cpylen>=0);
		s->size+=cpylen;
		
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;

		/* Must be exactly filling the block */
		if (s->size >= BB_THRESHOLD)
		{
			GArray *blocks = s->blocks;
			guint32 sbidx = 0;
			SBPtr   startblock;

			startblock = PPS_GET_STARTBLOCK(s->file, s->pps);
#if OLE_DEBUG > 0
			printf ("\n\n--- Converting ---\n\n\n");
#endif
			s->read_copy = ms_ole_read_copy_bb;
			s->read_ptr  = ms_ole_read_ptr_bb;
			s->lseek     = ms_ole_lseek;
			s->write     = ms_ole_write_bb;

			g_assert (s->size%SB_BLOCK_SIZE == 0);

			/* Convert the file to BBlocks */
			s->size     = 0;
			s->position = 0;
			s->strtype  = MS_OLE_LARGE_BLOCK;
			s->blocks   = 0;

			g_assert (blocks);
			while (sbidx < blocks->len) {
				SBPtr sbblk = ms_array_index (blocks, SBPtr, sbidx);
				ms_ole_write_bb(s, GET_SB_START_PTR(s->file, sbblk), SB_BLOCK_SIZE);
				sbidx++;
			}

			g_array_free (blocks, TRUE);
			free_allocation (s->file, startblock, 0);

			/* Continue the interrupted write */
			ms_ole_write_bb(s, ptr, bytes);
			bytes = 0;

#if OLE_DEBUG > 0
			printf ("\n\n--- Done ---\n\n\n");
#endif
			return;
		}
		
		offset = 0;
		blkidx++;
	}
	s->lseek (s, length, MS_OLE_SEEK_CUR);
	return;
}

MS_OLE_STREAM *
ms_ole_stream_open (MS_OLE_DIRECTORY *d, char mode)
{
	PPS_IDX p=d->pps;
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

	s           = g_new0 (MS_OLE_STREAM, 1);
	s->file     = f;
	s->pps      = p;
	s->position = 0;
	s->size     = PPS_GET_SIZE(f, p);
	s->blocks   = NULL;

	if (s->size>=BB_THRESHOLD)
	{
		BBPtr b = PPS_GET_STARTBLOCK(f,p);

		s->read_copy = ms_ole_read_copy_bb;
		s->read_ptr  = ms_ole_read_ptr_bb;
		s->lseek     = ms_ole_lseek;
		s->write     = ms_ole_write_bb;
		s->blocks    = g_array_new (0,0,sizeof(BBPtr));
		s->strtype   = MS_OLE_LARGE_BLOCK;
		for (lp=0;lp<(s->size+BB_BLOCK_SIZE-1)/BB_BLOCK_SIZE;lp++)
		{
			g_array_append_val (s->blocks, b);
			if (b == END_OF_CHAIN)
				printf ("Warning: bad file length in '%s'\n", PPS_NAME(f,p));
			else if (b == SPECIAL_BLOCK)
				printf ("Warning: special block in '%s'\n", PPS_NAME(f,p));
			else if (b == UNUSED_BLOCK)
				printf ("Warning: unused block in '%s'\n", PPS_NAME(f,p));
			else
				b = NEXT_BB(f, b);
		}
		if (b != END_OF_CHAIN && NEXT_BB(f, b) != END_OF_CHAIN)
			printf ("FIXME: Extra useless blocks on end of '%s'\n", PPS_NAME(f,p));
	}
	else
	{
		SBPtr b = PPS_GET_STARTBLOCK(f,p);

		s->read_copy = ms_ole_read_copy_sb;
		s->read_ptr  = ms_ole_read_ptr_sb;
		s->lseek     = ms_ole_lseek;
		s->write     = ms_ole_write_sb;

		if (s->size>0)
			s->blocks = g_array_new (0,0,sizeof(SBPtr));
		else
			s->blocks = NULL;

		s->strtype   = MS_OLE_SMALL_BLOCK;

		for (lp=0;lp<(s->size+SB_BLOCK_SIZE-1)/SB_BLOCK_SIZE;lp++)
		{
			g_array_append_val (s->blocks, b);
			if (b == END_OF_CHAIN)
				printf ("Warning: bad file length in '%s'\n", PPS_NAME(f,p));
			else if (b == SPECIAL_BLOCK)
				printf ("Warning: special block in '%s'\n", PPS_NAME(f,p));
			else if (b == UNUSED_BLOCK)
				printf ("Warning: unused block in '%s'\n", PPS_NAME(f,p));
			else
				b = NEXT_SB(f, b);
		}
		if (b != END_OF_CHAIN && NEXT_SB(f, b) != END_OF_CHAIN)
			printf ("FIXME: Extra useless blocks on end of '%s'\n", PPS_NAME(f,p));
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
			PPS_SET_SIZE (s->file, s->pps, s->size);

		g_array_free (s->blocks, 0);
		g_free (s);
	}
}

MS_OLE_DIRECTORY *
ms_ole_directory_get_root (MS_OLE *f)
{
	MS_OLE_DIRECTORY *d = g_new0 (MS_OLE_DIRECTORY, 1);
	d->file          = f;
	d->pps           = PPS_ROOT_BLOCK;
	d->primary_entry = PPS_ROOT_BLOCK;
	d->name          = PPS_NAME(f, d->pps);
	return d;
}

/* You probably arn't too interested in the root directory anyway
   but this is first */
MS_OLE_DIRECTORY *
ms_ole_directory_new (MS_OLE *f)
{
	MS_OLE_DIRECTORY *d = ms_ole_directory_get_root (f);
	ms_ole_directory_enter (d);
	return d;
}

/**
 * Fills fields from the pps index
 **/
static void
directory_setup (MS_OLE_DIRECTORY *d)
{
#if OLE_DEBUG > 1
		printf ("Setup pps = %d\n", d->pps);
#endif
	g_free (d->name);
	d->name   = PPS_NAME(d->file, d->pps);
	d->type   = PPS_GET_TYPE(d->file, d->pps);
	d->length = PPS_GET_SIZE(d->file, d->pps);
}

/**
 * This navigates by offsets from the primary_entry
 **/
int
ms_ole_directory_next (MS_OLE_DIRECTORY *d)
{
	int offset;
	PPS_IDX tmp;

	if (!d)
		return 0;

	/* If its primary just go ahead */
	if (d->pps != d->primary_entry)
	{
		/* Checking back up the chain */
		offset = 0;
		tmp = d->primary_entry;
		while (tmp != PPS_END_OF_CHAIN &&
		       tmp != d->pps)
		{
			tmp = PPS_GET_PREV(d->file, tmp);
			offset++;
		}
		if (d->pps == PPS_END_OF_CHAIN ||
		    tmp != PPS_END_OF_CHAIN)
		{
			offset--;
			if (OLE_DEBUG>0)
				printf ("Back trace by %d\n", offset);
			tmp = d->primary_entry;
			while (offset > 0)
			{
				tmp = PPS_GET_PREV(d->file, tmp);
				offset--;
			}
			d->pps = tmp;
			directory_setup(d);
			return 1;
		}
	}

	/* Go down the chain, ignoring the primary entry */
	tmp = PPS_GET_NEXT(d->file, d->pps);
	if (tmp == PPS_END_OF_CHAIN)
		return 0;

#if OLE_DEBUG > 1
		printf ("Forward trace\n");
#endif
	d->pps = tmp;

	directory_setup(d);
#if OLE_DEBUG > 1
		printf ("Next '%s' %d %d\n", d->name, d->type, d->length);
#endif
	return 1;
}

void
ms_ole_directory_enter (MS_OLE_DIRECTORY *d)
{
	if (!d || d->pps==PPS_END_OF_CHAIN)
		return;

	if (!((PPS_GET_TYPE(d->file, d->pps) == MS_OLE_PPS_STORAGE) ||
	      (PPS_GET_TYPE(d->file, d->pps) == MS_OLE_PPS_ROOT)))
	{
		printf ("Bad type %d %d\n", PPS_GET_TYPE(d->file, d->pps), MS_OLE_PPS_ROOT);
		return;
	}

	if (PPS_GET_DIR(d->file, d->pps) != PPS_END_OF_CHAIN)
	{
		d->primary_entry = PPS_GET_DIR(d->file, d->pps);
		/* So it will wind in from the start on 'next' */
		d->pps = PPS_END_OF_CHAIN;
	}
	return;
}

void
ms_ole_directory_unlink (MS_OLE_DIRECTORY *d)
{
	if (d->pps != d->primary_entry &&
	    PPS_GET_NEXT(d->file, d->pps) == PPS_END_OF_CHAIN &&
	    PPS_GET_PREV(d->file, d->pps) == PPS_END_OF_CHAIN)
	{ /* Little, lost & loosely attached */
		PPS_SET_NAME_LEN (d->file, d->pps, 0); /* Zero its name */
		free_allocation (d->file, PPS_GET_STARTBLOCK(d->file, d->pps),
				 (PPS_GET_SIZE(d->file,d->pps) >= BB_THRESHOLD));
	}
	else
		printf ("Unlink failed\n");
}

static PPS_IDX
next_free_pps (MS_OLE *f)
{
	PPS_IDX p = PPS_ROOT_BLOCK;
	PPS_IDX max_pps = f->header.root_list->len*(BB_BLOCK_SIZE/PPS_BLOCK_SIZE);
	guint32 blk, lp;

	while (p<max_pps)
	{
		if (PPS_GET_NAME_LEN(f, p) == 0)
			return p;
		p++;
	}

	/* We need to extend the pps then */
	blk = next_free_bb(f);
	SET_GUINT32(GET_BB_CHAIN_PTR(f,blk), END_OF_CHAIN);

	/* Clear the new PPS */
	for (lp=0;lp<BB_BLOCK_SIZE;lp++)
		SET_GUINT8(GET_BB_START_PTR(f, blk) + lp, 0);
  
	{ /* Append our new pps block to the chain */
		BBPtr ptr = f->header.root_startblock;
		while (NEXT_BB(f, ptr) != END_OF_CHAIN)
			ptr = NEXT_BB (f, ptr);
		SET_GUINT32(GET_BB_CHAIN_PTR (f, ptr), blk);
	}
	ms_ole_deanalyse (f);
	ms_ole_analyse (f);

	return max_pps;
}

/**
 * This is passed the handle of a directory in which to create the
 * new stream / directory.
 **/
MS_OLE_DIRECTORY *
ms_ole_directory_create (MS_OLE_DIRECTORY *d, char *name, PPS_TYPE type)
{
	/* Find a free PPS */
	PPS_IDX p = next_free_pps (d->file);
	PPS_IDX prim;
	MS_OLE *f = d->file;
	MS_OLE_DIRECTORY *nd = g_new0 (MS_OLE_DIRECTORY, 1);
	SBPtr  startblock;
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

	/* Memory Debug */

	{
		int idx = (((p)*PPS_BLOCK_SIZE)/BB_BLOCK_SIZE);
		guint32 ptr = ms_array_index ((f)->header.root_list, BBPtr, idx);
		
		mem = (GET_BB_START_PTR((f), ptr) +
		       (((p)*PPS_BLOCK_SIZE)%BB_BLOCK_SIZE));
	}
	mem = PPS_PTR(f, p);
	SET_GUINT8(mem, 0);
	mem+= PPS_BLOCK_SIZE-1;
	SET_GUINT8(mem, 0);

  /* Blank stuff I don't understand */
	for (lp=0;lp<PPS_BLOCK_SIZE;lp++)
		SET_GUINT8(PPS_PTR(f, p)+lp, 0);

	lp = 0;
	while (name[lp] && lp < (PPS_BLOCK_SIZE/2)-1)
	{
		SET_GUINT8(PPS_PTR(f, p) + lp*2, name[lp]);
		SET_GUINT8(PPS_PTR(f, p) + lp*2 + 1, 0);
		lp++;
	}
	PPS_SET_NAME_LEN(f, p, lp);
	PPS_SET_STARTBLOCK(f, p, END_OF_CHAIN);

	/* Chain into the directory */
	prim = PPS_GET_DIR (f, d->pps);
	if (prim == PPS_END_OF_CHAIN)
	{
		prim = p;
		PPS_SET_DIR (f, d->pps, p);
		PPS_SET_DIR (f, p, PPS_END_OF_CHAIN);
		PPS_SET_NEXT(f, p, PPS_END_OF_CHAIN);
		PPS_SET_PREV(f, p, PPS_END_OF_CHAIN);
	}
	else /* FIXME: this should insert in alphabetic order */
	{
		PPS_IDX oldnext = PPS_GET_NEXT(f, prim);
		PPS_SET_NEXT(f, prim, p);
		PPS_SET_NEXT(f, p, oldnext);
		PPS_SET_PREV(f, p, PPS_END_OF_CHAIN);
	}

	PPS_SET_TYPE(f, p, type);
	PPS_SET_SIZE(f, p, 0);

	nd->file     = d->file;
	nd->pps      = p;
	nd->name     = PPS_NAME(f, p);
	nd->primary_entry = prim;

	printf ("Created file with name '%s'\n", PPS_NAME(f, p));

	return nd;
}

void
ms_ole_directory_destroy (MS_OLE_DIRECTORY *d)
{
	if (d)
		g_free (d);
}

