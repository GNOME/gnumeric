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
#include "ms-biff.h"
#include "biff-types.h"

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

/**
 * These look _ugly_ but reduce to a few shifts, bit masks and adds
 * Under optimisation these are _really_ quick !
 * NB. Parameters are always: 'MS_OLE *', followed by a guint32 block or index
 **/

/* Return total number of Big Blocks in file, NB. first block = root */
#define NUM_BB(f)               ((f->length/BB_BLOCK_SIZE) - 1)

/* This is a list of big blocks which contain a flat description of all blocks in the file.
   Effectively inside these blocks is a FAT of chains of other BBs, so the theoretical max
   size = 128 BB Fat blocks, thus = 128*512*512/4 blocks ~= 8.4MBytes */
/* The number of Big Block Descriptor (fat) Blocks */
#define GET_NUM_BBD_BLOCKS(f)   (GET_GUINT32((f)->mem + 0x2c))
#define SET_NUM_BBD_BLOCKS(f,n) (SET_GUINT32((f)->mem + 0x2c, n))
/* The block locations of the Big Block Descriptor Blocks */
#define GET_BBD_LIST(f,i)           (GET_GUINT32((f)->mem + 0x4c + (i)*4))
#define SET_BBD_LIST(f,i,n)         (SET_GUINT32((f)->mem + 0x4c + (i)*4, n))

/* Find the position of the start in memory of a big block   */
#define GET_BB_START_PTR(f,n) ((guint8 *)(f->mem+(n+1)*BB_BLOCK_SIZE))
/* Find the position of the start in memory of a small block */
#define GET_SB_START_PTR(f,n) ( GET_BB_START_PTR((f), (f)->header.sbf_list[((SB_BLOCK_SIZE*(n))/BB_BLOCK_SIZE)]) + \
			       (SB_BLOCK_SIZE*(n)%BB_BLOCK_SIZE) )

/* Get the start block of the root directory ( PPS ) chain */
#define GET_ROOT_STARTBLOCK(f)   (GET_GUINT32(f->mem + 0x30))
#define SET_ROOT_STARTBLOCK(f,i) (SET_GUINT32(f->mem + 0x30, i))
/* Get the start block of the SBD chain */
#define GET_SBD_STARTBLOCK(f)    (GET_GUINT32(f->mem + 0x3c))
#define SET_SBD_STARTBLOCK(f,i)  (SET_GUINT32(f->mem + 0x3c, i))

/* Gives the position in memory, as a GUINT8 *, of the BB entry ( in a chain ) */
#define GET_BB_CHAIN_PTR(f,n) (GET_BB_START_PTR((f), (GET_BBD_LIST(f, ( ((n)*sizeof(BBPtr)) / BB_BLOCK_SIZE)))) + \
                              (((n)*sizeof(BBPtr))%BB_BLOCK_SIZE))

/* Gives the position in memory, as a GUINT8 *, of the SB entry ( in a chain ) of a small block index */
#define GET_SB_CHAIN_PTR(f,n) (GET_BB_START_PTR(f, f->header.sbd_list[(((n)*sizeof(SBPtr))/BB_BLOCK_SIZE)]) + \
                              (((n)*sizeof(SBPtr))%BB_BLOCK_SIZE))

/* Gives the position in memory, as a GUINT8 *, of the SBD entry of a small block index */
#define GET_SBD_CHAIN_PTR(f,n) (GET_BB_CHAIN_PTR(f,f->header.sbd_list[((sizeof(SBPtr)*(n))/BB_BLOCK_SIZE)]))

/* Gives the position in memory, as a GUINT8 *, of the SBF entry of a small block index */
#define GET_SBF_CHAIN_PTR(f,n) (GET_BB_CHAIN_PTR(f,f->header.sbf_list[((SB_BLOCK_SIZE*(n))/BB_BLOCK_SIZE)]))


#define BB_THRESHOLD   0x1000

#define PPS_ROOT_BLOCK    0
#define PPS_BLOCK_SIZE 0x80
#define PPS_END_OF_CHAIN 0xffffffff

/* This takes a PPS_IDX and returns a guint8 * to its data */
#define PPS_PTR(f,n) (GET_BB_START_PTR((f),(f)->header.root_list[(((n)*PPS_BLOCK_SIZE)/BB_BLOCK_SIZE)]) + \
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
		return g_array_index (s->blocks, BBPtr, bl);
	else
		return END_OF_CHAIN;
}

static void
set_block (MS_OLE_STREAM *s, BBPtr block)
{
	guint lp;
	if (!s || !s->blocks) return;
	g_assert (0);
	for (lp=0;lp<s->blocks->len;lp++)
		if (g_array_index (s->blocks, BBPtr, lp) == block) {
			s->position = lp*(s->strtype==MS_OLE_SMALL_BLOCK?SB_BLOCK_SIZE:BB_BLOCK_SIZE);
			return;
		}
	printf ("Set block failed\n");
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
		if (g_array_index (s->blocks, BBPtr, lp) == block) {
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
pps_get_text (BYTE *ptr, int length)
{
	int lp, skip;
	char *ans;
	guint16 c;
	BYTE *inb;
	
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
	
	printf ("Root blocks : %d\n", h->number_of_root_blocks);
	for (lp=0;lp<h->number_of_root_blocks;lp++)
		printf ("root_list[%d] = %d\n", lp, (int)h->root_list[lp]);
	
	printf ("sbd blocks : %d\n", h->number_of_sbd_blocks);
	for (lp=0;lp<h->number_of_sbd_blocks;lp++)
		printf ("sbd_list[%d] = %d\n", lp, (int)h->sbd_list[lp]);
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
	while (dep<f->header.number_of_sbd_blocks)
	{
		printf ("SB block %d ( = BB block %d )\n", dep, f->header.sbd_list[dep]);
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
static int
read_link_array(MS_OLE *f, BBPtr first, BBPtr **array)
{
	BBPtr ptr = first;
	int lp, num=0;

	while (ptr != END_OF_CHAIN)	/* Count the items */
	{
		ptr = NEXT_BB (f, ptr);
		num++;
	}

	if (num == 0)
		*array = 0;
	else
		*array  = g_new (BBPtr, num);

	lp = 0;
	ptr = first;
	while (ptr != END_OF_CHAIN)
	{
		(*array)[lp++] = ptr;
		ptr = NEXT_BB (f, ptr);
	}
	return num;
}

static int
read_root_list (MS_OLE *f)
{
	/* Find blocks belonging to root ... */
	f->header.number_of_root_blocks = read_link_array(f, f->header.root_startblock, &f->header.root_list);
	return (f->header.number_of_root_blocks!=0);
}

static int
read_sbf_list (MS_OLE *f)
{
	/* Find blocks containing all small blocks ... */
	f->header.number_of_sbf_blocks = read_link_array(f, f->header.sbf_startblock, &f->header.sbf_list);
	return 1;
}

static int
read_sbd_list (MS_OLE *f)
{
	/* Find blocks belonging to sbd ... */
	f->header.number_of_sbd_blocks = read_link_array(f, f->header.sbd_startblock, &f->header.sbd_list);
	return 1;
}

static int
ms_ole_analyse (MS_OLE *f)
{
	if (!read_header(f)) return 0;
	if (!read_root_list(f)) return 0;
	if (!read_sbd_list(f)) return 0;
	f->header.sbf_startblock = PPS_GET_STARTBLOCK(f, PPS_ROOT_BLOCK);
	if (!read_sbf_list(f)) return 0;
	if (OLE_DEBUG>0)
	{
		dump_header(f);
		dump_allocation (f);
	}

/*	{
		int lp;
		for (lp=0;lp<BB_BLOCK_SIZE/PPS_BLOCK_SIZE;lp++)
		{
			printf ("PPS %d type %d, prev %d next %d, dir %d\n", lp, PPS_GET_TYPE(f,lp),
				PPS_GET_PREV(f,lp), PPS_GET_NEXT(f,lp), PPS_GET_DIR(f,lp));
			dump (PPS_PTR(f, lp), PPS_BLOCK_SIZE);
		}
		} */
	return 1;
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

	f->header.number_of_root_blocks = 1;
	f->header.root_list = g_new (BBPtr, 1);
	f->header.root_list[0] = 0;

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

	f->header.number_of_sbd_blocks = 1;
	f->header.sbd_list = g_new (BBPtr, 1);
	f->header.sbd_list[0] = 2;

	/* the first SBF block : 3 */
	for (lp=0;lp<BB_BLOCK_SIZE/4;lp++) /* Fill with zeros */
		SET_GUINT32(GET_BB_START_PTR(f,3) + lp*4, 0);

	f->header.number_of_sbf_blocks = 1;
	f->header.sbf_list = g_new (BBPtr, 1);
	f->header.sbf_list[0] = 3;

	if (OLE_DEBUG > 0)
	{
		int lp;
		dump_header(f);
		dump_allocation (f);

		for (lp=0;lp<BB_BLOCK_SIZE/PPS_BLOCK_SIZE;lp++)
		{
			printf ("PPS %d type %d, prev %d next %d, dir %d\n", lp, PPS_GET_TYPE(f,lp),
				PPS_GET_PREV(f,lp), PPS_GET_NEXT(f,lp), PPS_GET_DIR(f,lp));
			dump (PPS_PTR(f, lp), PPS_BLOCK_SIZE);
		}
	}

	/*  printf ("\n\nEntire created file\n\n\n");
	    dump(f->mem, init_blocks*BB_BLOCK_SIZE); */

	return f;
}


MS_OLE *
ms_ole_new (const char *name)
{
	struct stat st;
	int file;
	MS_OLE *f;

	if (OLE_DEBUG>0)
		printf ("New OLE file '%s'\n", name);
	f = g_new0 (MS_OLE, 1);

	f->file_descriptor = file = open (name, O_RDWR);
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

	f->mem = mmap (0, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0);

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
		munmap (f->mem, f->length);
		close (f->file_descriptor);
		
		if (f->header.root_list)
			g_free (f->header.root_list);
		if (f->header.sbd_list)
			g_free (f->header.sbd_list);
		if (f->header.sbf_list)
			g_free (f->header.sbf_list);
		g_free (f);

		if (OLE_DEBUG>0)
			printf ("Closing OLE file\n");
	}
}

static void
dump_stream (MS_OLE_STREAM *s)
{
	if (s->size>=BB_THRESHOLD)
		printf ("Big block : ");
	else
		printf ("Small block : ");
	printf ("block %d, offset %d\n", get_block(s), get_offset(s));
}

static void
dump_biff (BIFF_QUERY *bq)
{
	printf ("Opcode 0x%x length %d malloced? %d\nData:\n", bq->opcode, bq->length, bq->data_malloced);
	if (bq->length>0)
		dump (bq->data, bq->length);
	dump_stream (bq->pos);
}

static void
extend_file (MS_OLE *f, int blocks)
{
	struct stat st;
	int file = f->file_descriptor;
	guint8 *newptr, zero = 0;
	guint32 oldlen;
	guint32 blk, lp;

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

	/* Really let MS know they are unused ! */
	for (lp=0;lp<blocks;lp++)
		SET_GUINT32(GET_BB_CHAIN_PTR(f, oldlen/BB_BLOCK_SIZE + lp), UNUSED_BLOCK);
}

static guint32
next_free_bb (MS_OLE *f)
{
	guint32 blk;
	guint32 idx, lp;
  
	blk = 0;
	while (blk < NUM_BB(f))
		if (GET_GUINT32(GET_BB_CHAIN_PTR(f,blk)) == UNUSED_BLOCK)
			return blk;
		else
			blk++;

	if (blk < GET_NUM_BBD_BLOCKS(f)*BB_BLOCK_SIZE/sizeof(BBPtr))
	{
		/* Extend and remap file */
		printf ("Extend & remap file ...\n");
		extend_file(f, 5);
		g_assert (blk <= NUM_BB(f));
		return blk;
	}

	printf ("Out of unused BB space in %d blocks!\n", NUM_BB(f));
	extend_file (f,10);
	idx = GET_NUM_BBD_BLOCKS(f);
	g_assert (idx<(BB_BLOCK_SIZE - 0x4c)/4);
	SET_GUINT32(GET_BB_CHAIN_PTR(f, blk), SPECIAL_BLOCK); /* blk is the new BBD item */
	SET_BBD_LIST(f, idx, blk);
	SET_NUM_BBD_BLOCKS(f, idx+1);
	/* Setup that block */
	for (lp=0;lp<BB_BLOCK_SIZE/sizeof(BBPtr);lp++)
		SET_GUINT32(GET_BB_START_PTR(f, blk) + lp*sizeof(BBPtr), UNUSED_BLOCK);
	printf ("Should be 10 more free blocks at least !\n");
	dump_allocation(f);
	return blk+1;
}

static guint32
next_free_sb (MS_OLE *f)
{
	guint32 blk;
	guint32 idx, lp;
  
	blk = 0;
	while (blk < (f->header.number_of_sbf_blocks*BB_BLOCK_SIZE/SB_BLOCK_SIZE))
		if (GET_GUINT32(GET_SB_CHAIN_PTR(f, blk)) == UNUSED_BLOCK)
			return blk;
		else
			blk++;
	/* Create an extra block on the Small block stream */
	{
		BBPtr new_sbf = next_free_bb(f);
		int oldnum = f->header.number_of_sbf_blocks;
		BBPtr end_sbf = f->header.sbf_list[oldnum-1];
		printf ("Extend & remap SBF file block %d chains to %d...\n", end_sbf, new_sbf);
		g_assert (new_sbf != END_OF_CHAIN);
		SET_GUINT32(GET_BB_CHAIN_PTR(f, end_sbf), new_sbf);
		SET_GUINT32(GET_BB_CHAIN_PTR(f, new_sbf), END_OF_CHAIN);
		g_assert (NEXT_BB(f, end_sbf) == new_sbf);
		g_free (f->header.sbf_list);
		read_sbf_list(f);
		g_assert (oldnum+1 == f->header.number_of_sbf_blocks);
		/* Append the new block to the end of the SBF(ile) */
		g_assert (blk < f->header.number_of_sbf_blocks*BB_BLOCK_SIZE/SB_BLOCK_SIZE);
		printf ("Unused small block at %d\n", blk);
	}
	/* Extend the SBD(escription) chain if neccessary */
	if (blk > f->header.number_of_sbd_blocks*BB_BLOCK_SIZE/sizeof(SBPtr))
	{
		BBPtr new_sbd = next_free_bb(f);
		BBPtr oldnum = f->header.number_of_sbd_blocks;
		BBPtr end_sbd = f->header.sbd_list[oldnum-1];

		SET_GUINT32(GET_BB_CHAIN_PTR(f, end_sbd), new_sbd);
		SET_GUINT32(GET_BB_CHAIN_PTR(f, new_sbd), END_OF_CHAIN);
		g_free (f->header.sbd_list);
		read_sbd_list(f);
		g_assert (oldnum+1 == f->header.number_of_sbd_blocks);

		/* Setup that block */
		for (lp=0;lp<BB_BLOCK_SIZE/sizeof(SBPtr);lp++)
			SET_GUINT32(GET_BB_START_PTR(f, new_sbd) + lp*sizeof(SBPtr), UNUSED_BLOCK);
	}
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
			SBPtr cur = 0;
			while (cur < f->header.number_of_sbd_blocks*BB_BLOCK_SIZE/sizeof(SBPtr))
			{
				if (GET_GUINT32(GET_SB_CHAIN_PTR(f,cur)) != UNUSED_BLOCK)
					lastFree = END_OF_CHAIN;
				else if (lastFree == END_OF_CHAIN)
					lastFree = cur;
				cur++;
			}

			if (lastFree == END_OF_CHAIN)
				return;

			printf ("Before freeing stuff\n");
			dump_allocation(f);

			if (lastFree == 0) /* We don't know whether MS can cope with no SB */
				lastFree++;

			cur = lastFree;
			SET_GUINT32(GET_SBD_CHAIN_PTR(f, lastFree), END_OF_CHAIN);
			SET_GUINT32(GET_SBF_CHAIN_PTR(f, lastFree), END_OF_CHAIN);
			while (cur < f->header.number_of_sbd_blocks*BB_BLOCK_SIZE/sizeof(SBPtr))
			{
				if (cur%(BB_BLOCK_SIZE/SB_BLOCK_SIZE) == 0)
				        /* We can free a SBF block */
					SET_GUINT32(GET_SBF_CHAIN_PTR(f,cur), UNUSED_BLOCK);
				if (lastFree%(BB_BLOCK_SIZE/sizeof(SBPtr))==0)
					/* Free a whole SBD block */
					SET_GUINT32(GET_SBD_CHAIN_PTR(f,cur), UNUSED_BLOCK);
				cur++;
			}
			g_free (f->header.sbf_list);
			g_free (f->header.sbd_list);
			read_sbf_list(f);
			read_sbd_list(f);
			printf ("After free_alloc\n");
			dump_allocation(f);
		}
	}
}

static void
ms_ole_lseek (MS_OLE_STREAM *s, gint32 bytes, ms_ole_seek_t type)
{
	if (type == MS_OLE_SEEK_SET)
		s->position = bytes;
	else
		s->position+= bytes;
	if (s->position>=s->size) {
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

	if (!s->blocks || blockidx>=s->blocks->len) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	blklen = BB_BLOCK_SIZE - s->position%BB_BLOCK_SIZE;
	while (len>blklen) {
		len-=blklen;
		blklen = BB_BLOCK_SIZE;
		if (g_array_index (s->blocks, BBPtr, blockidx)+1
		    != g_array_index (s->blocks, BBPtr, blockidx+1))
			return 0;
		if (blockidx>=s->blocks->len) {
			printf ("End of chain error\n");
			return 0;
		}
		blockidx++;
	}
	/* Straight map, simply return a pointer */
	ans = GET_BB_START_PTR(s->file, g_array_index (s->blocks, BBPtr, s->position/BB_BLOCK_SIZE))
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

	if (!s->blocks || blockidx>=s->blocks->len) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	blklen = SB_BLOCK_SIZE - s->position%SB_BLOCK_SIZE;
	while (len>blklen) {
		len-=blklen;
		blklen = SB_BLOCK_SIZE;
		if (g_array_index (s->blocks, BBPtr, blockidx)+1
		    != g_array_index (s->blocks, BBPtr, blockidx+1))
			return 0;
		if (blockidx>=s->blocks->len) {
			printf ("End of chain error\n");
			return 0;
		}
		blockidx++;
	}
	/* Straight map, simply return a pointer */
	ans = GET_SB_START_PTR(s->file, g_array_index (s->blocks, BBPtr, s->position/SB_BLOCK_SIZE))
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
	BBPtr block= g_array_index (s->blocks, BBPtr, blkidx);
	guint8 *src;
	if (!s->blocks) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	while (length>0)
	{
		int cpylen = BB_BLOCK_SIZE - offset;
		if (cpylen>length)
			cpylen = length;

		if (s->position + cpylen > s->size ||
		    block == END_OF_CHAIN)
		{
			printf ("Trying 3 to read beyond end of stream\n");
			return 0;
		}
		src = GET_BB_START_PTR(s->file, block) + offset;
		
		memcpy (ptr, src, cpylen);
		ptr   += cpylen;
		length -= cpylen;
		
		offset = 0;
		
		block= g_array_index (s->blocks, BBPtr, ++blkidx);
		s->position+=cpylen;
	}
	return 1;
}

static gboolean
ms_ole_read_copy_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int offset = s->position%SB_BLOCK_SIZE;
	int blkidx = s->position/SB_BLOCK_SIZE;
	BBPtr block= g_array_index (s->blocks, BBPtr, blkidx);
	guint8 *src;
	if (!s->blocks) {
		printf ("Reading from NULL file\n");
		return 0;
	}

	while (length>0)
	{
		int cpylen = SB_BLOCK_SIZE - offset;
		if (cpylen>length)
			cpylen = length;
		if (s->position + cpylen > s->size ||
		    block == END_OF_CHAIN)
		{
			printf ("Trying 3 to read beyond end of stream\n");
			return 0;
		}
		src = GET_SB_START_PTR(s->file, block) + offset;
				
		memcpy (ptr, src, cpylen);
		ptr   += cpylen;
		length -= cpylen;
		
		offset = 0;
		
		block= g_array_index (s->blocks, BBPtr, ++blkidx);
		s->position+=cpylen;
	}
	return 1;
}

static void
ms_ole_write_bb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int cpylen;
	int offset = get_offset(s);
	guint32 block   = get_block(s);
	guint32 lastblk = END_OF_CHAIN;
	guint32 bytes  = length;
	guint8 *dest;
	
	while (bytes>0)
	{
		int cpylen = BB_BLOCK_SIZE - offset;
		if (cpylen>bytes)
			cpylen = bytes;
		
		if (block == END_OF_CHAIN)
		{
			block = next_free_bb(s->file);
			printf ("next free block : %d\n", block);
			if (lastblk != END_OF_CHAIN) /* Link onwards */
				SET_GUINT32(GET_BB_CHAIN_PTR(s->file, lastblk), block);
			else
			{
				PPS_SET_STARTBLOCK(s->file, s->pps, block);
				set_block (s, block);
			}
			SET_GUINT32(GET_BB_CHAIN_PTR(s->file, block), END_OF_CHAIN);
			printf ("Linked stuff\n");
			dump_allocation(s->file);
		}
		
		dest = GET_BB_START_PTR(s->file, block) + offset;

		PPS_SET_SIZE(s->file, s->pps, s->size+cpylen);
		s->size = PPS_GET_SIZE(s->file, s->pps);
		printf ("Copy %d bytes to block %d\n", cpylen, block);
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;
		
		offset = 0;
		lastblk = block;
		block  = NEXT_BB(s->file, block);
	}
	s->lseek (s, length, MS_OLE_SEEK_CUR);
	return;
}

static void
ms_ole_write_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int cpylen;
	int offset = get_offset(s);
	guint32 block   = get_block(s);
	guint32 lastblk = END_OF_CHAIN;
	guint32 bytes  = length;
	guint8 *dest;
	
	while (bytes>0)
	{
		int cpylen = SB_BLOCK_SIZE - offset;
		if (cpylen>bytes)
			cpylen = bytes;
		
		if (block == END_OF_CHAIN)
		{
			block = next_free_sb(s->file);
			if (lastblk != END_OF_CHAIN) /* Link onwards */
				SET_GUINT32(GET_SB_CHAIN_PTR(s->file, lastblk), block);
			else /* block == END_OF_CHAIN only on the 1st block ever */
			{
				PPS_SET_STARTBLOCK(s->file, s->pps, block);
				set_block (s, block);
				printf ("Start of SB file\n");
			}
			SET_GUINT32(GET_SB_CHAIN_PTR(s->file, block), END_OF_CHAIN);
			printf ("Linked stuff\n");
/*			dump_allocation(s->file); */
		}
		
		dest = GET_SB_START_PTR(s->file, block) + offset;
		
		g_assert (cpylen>=0);
		PPS_SET_SIZE(s->file, s->pps, s->size+cpylen);
		s->size+=cpylen;
		
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;

		/* Must be exactly filling the block */
		if (s->size >= BB_THRESHOLD)
		{
			SBPtr sb_start_block = PPS_GET_STARTBLOCK(s->file, s->pps);
			SBPtr sbblk = sb_start_block;
			guint32 size = PPS_GET_SIZE(s->file, s->pps);
			int cnt;

			printf ("\n\n--- Converting ---\n\n\n");

			s->read_copy = ms_ole_read_copy_bb;
			s->read_ptr  = ms_ole_read_ptr_bb;
			s->lseek     = ms_ole_lseek;
			s->write     = ms_ole_write_bb;

			g_assert (size%SB_BLOCK_SIZE == 0);

			/* Convert the file to BBlocks */
			PPS_SET_SIZE(s->file, s->pps, 0);
			set_block(s, END_OF_CHAIN);
			set_offset (s, 0);

			cnt = 0;
			while (sbblk != END_OF_CHAIN)
			{
				ms_ole_write_bb(s, GET_SB_START_PTR(s->file, sbblk), SB_BLOCK_SIZE);
				sbblk = NEXT_SB(s->file, sbblk);
				cnt++;
			}

/*			g_assert (cnt==BB_BLOCK_SIZE/SB_BLOCK_SIZE); */
			free_allocation (s->file, sb_start_block, 0);

			/* Continue the interrupted write */
			ms_ole_write_bb(s, ptr, bytes);

			printf ("\n\n--- Done ---\n\n\n");
			return;
		}
		
		offset = 0;
		lastblk = block;
		block  = NEXT_SB(s->file, block);
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

	s         = g_new0 (MS_OLE_STREAM, 1);
	s->file   = f;
	s->pps    = p;
	s->position = 0;
	s->size = PPS_GET_SIZE(f, p);
	s->blocks = NULL;

	if (s->size>=BB_THRESHOLD)
	{
		BBPtr b = PPS_GET_STARTBLOCK(f,p);

		s->read_copy = ms_ole_read_copy_bb;
		s->read_ptr  = ms_ole_read_ptr_bb;
		s->lseek     = ms_ole_lseek;
		s->write     = ms_ole_write_bb;
		s->blocks    = g_array_new (0,0,sizeof(BBPtr));
		s->strtype   = MS_OLE_LARGE_BLOCK;
		for (lp=0;lp<s->size/BB_BLOCK_SIZE;lp++)
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
		g_array_append_val (s->blocks, b);
	}
	else
	{
		SBPtr b = PPS_GET_STARTBLOCK(f,p);

		s->read_copy = ms_ole_read_copy_sb;
		s->read_ptr  = ms_ole_read_ptr_sb;
		s->lseek     = ms_ole_lseek;
		s->write     = ms_ole_write_sb;
		s->blocks    = g_array_new (0,0,sizeof(SBPtr));
		s->strtype   = MS_OLE_SMALL_BLOCK;

		for (lp=0;lp<s->size/SB_BLOCK_SIZE;lp++)
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
		g_array_append_val (s->blocks, b);
	}
	return s;
}

/* FIXME: This needs to be more cunning and have new write / read
   functions that inser CONTINUE records etc. */
static MS_OLE_STREAM *
ms_ole_stream_duplicate (MS_OLE_STREAM *s)
{
	MS_OLE_STREAM *ans = g_new (MS_OLE_STREAM, 1);
	memcpy (ans, s, sizeof(MS_OLE_STREAM));
	return ans;
}

void
ms_ole_stream_close (MS_OLE_STREAM *s)
{
	if (s) {
		g_array_free (s->blocks, 0);
		g_free (s);
	}
}

/* You probably arn't too interested in the root directory anyway
   but this is first */
MS_OLE_DIRECTORY *
ms_ole_directory_new (MS_OLE *f)
{
	MS_OLE_DIRECTORY *d = g_new0 (MS_OLE_DIRECTORY, 1);
	d->file          = f;
	d->pps           = PPS_ROOT_BLOCK;
	d->primary_entry = PPS_ROOT_BLOCK;
	d->name          = PPS_NAME(f, d->pps);
	ms_ole_directory_enter (d);
	return d;
}

/**
 * Fills fields from the pps index
 **/
static void
directory_setup (MS_OLE_DIRECTORY *d)
{
	if (OLE_DEBUG>0)
		printf ("Setup pps = %d\n", d->pps);
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

	if (OLE_DEBUG>0)
		printf ("Forward trace\n");
	d->pps = tmp;

	directory_setup(d);
	if (OLE_DEBUG>0)
		printf ("Next '%s' %d %d\n", d->name, d->type, d->length);
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
	PPS_IDX max_pps = f->header.number_of_root_blocks*BB_BLOCK_SIZE/PPS_BLOCK_SIZE;
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

	for (lp=0;lp<BB_BLOCK_SIZE;lp++)
		SET_GUINT8(GET_BB_START_PTR(f, blk) + lp, 0);
  
	{ /* Append our new pps block to the chain */
		BBPtr ptr = f->header.root_startblock;
		while (NEXT_BB(f, ptr) != END_OF_CHAIN)
			ptr = NEXT_BB (f, ptr);
		SET_GUINT32(GET_BB_CHAIN_PTR (f, ptr), blk);
	}
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
	PPS_IDX p = next_free_pps(d->file);
	PPS_IDX prim;
	MS_OLE *f =d->file;
	MS_OLE_DIRECTORY *nd = g_new0 (MS_OLE_DIRECTORY, 1);
	SBPtr  startblock;
	int lp=0;

  /* Blank stuff I don't understand */
	for (lp=0;lp<PPS_BLOCK_SIZE;lp++)
		SET_GUINT8(PPS_PTR(f, p)+lp, 0);

	lp = 0;
	while (name[lp])
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

BIFF_QUERY *
ms_biff_query_new (MS_OLE_STREAM *ptr)
{
	BIFF_QUERY *bq   ;
	if (!ptr)
		return 0;
	bq = g_new0 (BIFF_QUERY, 1);
	bq->opcode        = 0;
	bq->length        = 0;
	bq->data_malloced = 0;
	bq->pos = ptr;
#if OLE_DEBUG > 0
	dump_biff(bq);
#endif
	return bq;
}

BIFF_QUERY *
ms_biff_query_copy (const BIFF_QUERY *p)
{
	BIFF_QUERY *bf = g_new (BIFF_QUERY, 1);
	memcpy (bf, p, sizeof (BIFF_QUERY));
	if (p->data_malloced)
	{
		bf->data = (guint8 *)g_malloc (p->length);
		memcpy (bf->data, p->data, p->length);
	}
	bf->pos=ms_ole_stream_duplicate (p->pos);
	return bf;
}

static int
ms_biff_merge_continues (BIFF_QUERY *bq, guint32 len)
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
#if OLE_DEBUG > 8
		printf ("Read raw : 0x%x -> 0x%x\n", chunk.data[0],
			chunk.data[chunk.length-1]);
#endif
		tmp[0] = 0; tmp[1] = 0; tmp[2] = 0; tmp[3] = 0;
		bq->pos->read_copy (bq->pos, tmp, 4);			
		total_len   += chunk.length;
		g_array_append_val (contin, chunk);

		chunk.length = BIFF_GETWORD (tmp+2);
	} while ((BIFF_GETWORD(tmp) & 0xff) == BIFF_CONTINUE);
	bq->pos->lseek (bq->pos, -4, MS_OLE_SEEK_CUR); /* back back off */

	bq->data = g_malloc (total_len);
	if (!bq->data)
		return 0;
	bq->length = total_len;
	d = bq->data;
	bq->data_malloced = 1;
	for (lp=0;lp<contin->len;lp++) {
		chunk = g_array_index (contin, chunk_t, lp);
#if OLE_DEBUG > 8
		printf ("Copying block stats with 0x%x ends with 0x%x len 0x%x\n",
			chunk.data[0], chunk.data[chunk.length-1], chunk.length);
		g_assert ((d-bq->data)+chunk.length<=total_len);
#endif
		memcpy (d, chunk.data, chunk.length);
		d+=chunk.length;
		g_free (chunk.data);
	}
	g_array_free (contin, 1);
#if OLE_DEBUG > 2
	printf ("MERGE %d CONTINUES... len 0x%x\n", contin->len, len);
	printf ("Biff read code 0x%x, length %d\n", bq->opcode, bq->length);
	dump_biff (bq);
#endif
	return 1;
}

/**
 * Returns 0 if has hit end
 * NB. if this crashes obscurely, array is being extended over the stack !
 **/
int
ms_biff_query_next (BIFF_QUERY *bq)
{
	guint8  tmp[4];
	int ans=1;

	if (!bq || bq->pos->position >= bq->pos->size)
		return 0;
	if (bq->data_malloced) {
		g_free (bq->data);
		bq->data_malloced = 0;
	}
	bq->streamPos = bq->pos->position;
	if (!bq->pos->read_copy (bq->pos, tmp, 4))
		return 0;
	bq->opcode = BIFF_GETWORD (tmp);
	bq->length = BIFF_GETWORD (tmp+2);
	bq->ms_op  = (bq->opcode>>8);
	bq->ls_op  = (bq->opcode&0xff);

	if (!(bq->data = bq->pos->read_ptr(bq->pos, bq->length))) {
		bq->data = g_new0 (guint8, bq->length);
		if (!bq->pos->read_copy(bq->pos, bq->data, bq->length)) {
			ans = 0;
			g_free(bq->data);
			bq->length = 0;
		} else
			bq->data_malloced = 1;
	}
	if (ans &&
	    bq->pos->read_copy (bq->pos, tmp, 4)) {
		if ((BIFF_GETWORD(tmp) & 0xff) == BIFF_CONTINUE)
			return ms_biff_merge_continues (bq, BIFF_GETWORD(tmp+2));
		bq->pos->lseek (bq->pos, -4, MS_OLE_SEEK_CUR); /* back back off */
#if OLE_DEBUG > 4
		printf ("Backed off\n");
#endif
	}

#if OLE_DEBUG > 2
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
ms_biff_query_destroy (BIFF_QUERY *bq)
{
	if (bq)
	{
		if (bq->data_malloced)
			g_free (bq->data);
		g_free (bq);
	}
}

/* FIXME: Too nasty ! */
MS_OLE_STREAM *
ms_biff_query_data_to_stream (BIFF_QUERY *bq)
{
	MS_OLE_STREAM *ans=ms_ole_stream_duplicate (bq->pos);
/*	ans->advance(ans, -bq->length); 
	This will never work !
*/
	/* Hack size down to biff length */
	/* Can't be done non-destructively ! sod ! */
	/* Should cut the length down a lot, hope we can know where
	   the end is somehow */
	return ans;
}

#if G_BYTE_ORDER == G_BIG_ENDIAN
double biff_getdouble(guint8 *p)
{
    double d;
    int i;
    guint8 *t = (guint8 *)&d;
    int sd = sizeof (d);

    for (i = 0; i < sd; i++)
      t[i] = p[sd - 1 - i];

    return d;
}
#endif
