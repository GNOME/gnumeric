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

/* Implementational detail - not for global header */

#define OLE_DEBUG 0

/* These take a _guint8_ pointer */
#define GET_GUINT8(p)  (*(p+0))
#define GET_GUINT16(p) (*(p+0)+(*(p+1)<<8))
#define GET_GUINT32(p) (*(p+0)+ \
		    (*(p+1)<<8)+ \
		    (*(p+2)<<16)+ \
		    (*(p+3)<<24))

#define SET_GUINT8(p,n)  (*(p+0)=n)
#define SET_GUINT16(p,n) ((*(p+0)=((n)&0xff)), \
                          (*(p+1)=((n)>>8)&0xff))
#define SET_GUINT32(p,n) ((*(p+0)=((n))&0xff), \
                          (*(p+1)=((n)>>8)&0xff), \
                          (*(p+2)=((n)>>16)&0xff), \
                          (*(p+3)=((n)>>24)&0xff))


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

void
dump (guint8 *ptr, int len)
{
	int lp,lp2;
#define OFF (lp2+(lp<<4))
#define OK (len-OFF>0)
	for (lp = 0;lp<(len+15)/16;lp++)
	{
		printf ("%8x  |  ", lp*16);
		for (lp2=0;lp2<16;lp2++)
			OK?printf("%2x ", ptr[OFF]):printf("XX ");
		printf ("  |  ");
		for (lp2=0;lp2<16;lp2++)
			printf ("%c", OK?(ptr[OFF]>'!'&&ptr[OFF]<127?ptr[OFF]:'.'):'*');
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
	BYTE *inb;
	
	if (!length) 
		return 0;
	
	ans = (char *)g_malloc (sizeof(char) * length + 1);
	
	skip = (ptr[0] < 0x30); /* Magic unicode number */
	if (skip)
		inb = ptr + 2;
	else
		inb = ptr;
	for (lp=0;lp<length;lp++)
	{
		ans[lp] = (char) *inb;
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
	printf ("New OLE file\n");
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
	if (PPS_GET_SIZE(s->file, s->pps)>=BB_THRESHOLD)
		printf ("Big block : ");
	else
		printf ("Small block : ");
	printf ("block %d, offset %d\n", s->block, s->offset);
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

static guint8*
ms_ole_read_ptr_bb (MS_OLE_STREAM *s, guint32 length)
{
	int block_left;
	if (s->block == END_OF_CHAIN)
	{
		printf ("Reading from NULL file\n");
		return 0;
	}
  
	block_left = BB_BLOCK_SIZE - s->offset;
	if (length<=block_left) /* Just return the pointer then */
		return (GET_BB_START_PTR(s->file, s->block) + s->offset);
  
	/* Is it contiguous ? */
	{
		guint32 curblk, newblk;
		int  ln     = length;
		int  blklen = block_left;
		int  contig = 1;
    
		curblk = s->block;
		if (curblk == END_OF_CHAIN)
		{
			printf ("Trying to read beyond end of stream\n");
			return 0;
		}
    
		while (ln>blklen && contig)
		{
			ln-=blklen;
			blklen = BB_BLOCK_SIZE;
			newblk = NEXT_BB(s->file, curblk);
			if (newblk != curblk+1)
				return 0;
			curblk = newblk;
			if (curblk == END_OF_CHAIN)
			{
				printf ("End of chain error\n");
				return 0;
			}
		}
		/* Straight map, simply return a pointer */
		return GET_BB_START_PTR(s->file, s->block) + s->offset;
	}
}

static guint8*
ms_ole_read_ptr_sb (MS_OLE_STREAM *s, guint32 length)
{
	int block_left;
	if (s->block == END_OF_CHAIN)
	{
		printf ("Reading from NULL file\n");
		return 0;
	}
  
	block_left = SB_BLOCK_SIZE - s->offset;
	if (length<=block_left) /* Just return the pointer then */
		return (GET_SB_START_PTR(s->file, s->block) + s->offset);
  
	/* Is it contiguous ? */
	{
		guint32 curblk, newblk;
		int  ln     = length;
		int  blklen = block_left;
		int  contig = 1;
    
		curblk = s->block;
		if (curblk == END_OF_CHAIN)
		{
			printf ("Trying to read beyond end of stream\n");
			return 0;
		}
    
		while (ln>blklen && contig)
		{
			ln-=blklen;
			blklen = SB_BLOCK_SIZE;
			newblk = NEXT_SB(s->file, curblk);
			if (newblk != curblk+1)
				return 0;
			curblk = newblk;
			if (curblk == END_OF_CHAIN)
			{
				printf ("End of chain error\n");
				return 0;
			}
		}
		/* Straight map, simply return a pointer */
		return GET_SB_START_PTR(s->file, s->block) + s->offset;
	}
}

/**
 *  Returns:
 *  0 - on error
 *  1 - on success
 **/
static gboolean
ms_ole_read_copy_bb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int block_left;
	if (s->block == END_OF_CHAIN)
	{
		printf ("Reading from NULL file\n");
		return 0;
	}

	block_left = BB_BLOCK_SIZE - s->offset;

	/* Block by block copy */
	{
		int cpylen;
		int offset = s->offset;
		int block  = s->block;
		int bytes  = length;
		guint8 *src;
    
		while (bytes>0)
		{
			int cpylen = BB_BLOCK_SIZE - offset;
			if (cpylen>bytes)
				cpylen = bytes;
			src = GET_BB_START_PTR(s->file, block) + offset;
	
			if (block == s->end_block && cpylen + offset > PPS_GET_SIZE(s->file, s->pps)%BB_BLOCK_SIZE)
			{
				printf ("Trying to read beyond end of stream\n");
				return 0;
			}

			memcpy (ptr, src, cpylen);
			ptr   += cpylen;
			bytes -= cpylen;
	
			offset = 0;
			block  = NEXT_BB(s->file, block);	  
		}
	}
	s->advance (s, length);
	return 1;
}

static gboolean
ms_ole_read_copy_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int block_left;
	if (s->block == END_OF_CHAIN)
	{
		printf ("Reading from NULL file\n");
		return 0;
	}

	block_left = SB_BLOCK_SIZE - s->offset;

	/* Block by block copy */
	{
		int cpylen;
		int offset = s->offset;
		int block  = s->block;
		int bytes  = length;
		guint8 *src;
    
		while (bytes>0)
		{
			int cpylen = SB_BLOCK_SIZE - offset;
			if (cpylen>bytes)
				cpylen = bytes;
			src = GET_SB_START_PTR(s->file, block) + offset;
	
			if (block == s->end_block && cpylen + offset > PPS_GET_SIZE(s->file,s->pps)%SB_BLOCK_SIZE)
			{
				printf ("Trying to read beyond end of stream\n");
				return 0;
			}

			memcpy (ptr, src, cpylen);
			ptr   += cpylen;
			bytes -= cpylen;
	
			offset = 0;
			block  = NEXT_SB(s->file, block);
		}
	}
	s->advance (s, length);
	return 1;
}

static void
ms_ole_write_bb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int cpylen;
	int offset = s->offset;
	guint32 block   = s->block;
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
				s->block = block;
			}
			SET_GUINT32(GET_BB_CHAIN_PTR(s->file, block), END_OF_CHAIN);
			s->end_block = block;
			printf ("Linked stuff\n");
			dump_allocation(s->file);
		}
		
		dest = GET_BB_START_PTR(s->file, block) + offset;

		PPS_SET_SIZE(s->file, s->pps, PPS_GET_SIZE(s->file,s->pps)+cpylen);
		printf ("Copy %d bytes to block %d\n", cpylen, block);
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;
		
		offset = 0;
		lastblk = block;
		block  = NEXT_BB(s->file, block);
	}
	s->advance (s, length);
	return;
}

static void
ms_ole_advance_bb (MS_OLE_STREAM *s, gint32 bytes)
{
	guint32 newoff = (bytes+s->offset);
	guint32 numblk = newoff/BB_BLOCK_SIZE;
	guint32 lastblk = END_OF_CHAIN;
	g_assert (bytes>=0);

/*	printf ("Advance from %d:%d by %d bytes\n", s->block, s->offset, bytes); */

	s->offset = newoff%BB_BLOCK_SIZE;
		
	while (s->block != END_OF_CHAIN)
		if (numblk==0)
			return;
		else
		{
			lastblk = s->block;
			s->block = NEXT_BB(s->file, s->block);
			numblk --;
		}
	s->block = lastblk; /* Special case to save blocks on write */
	s->offset = BB_BLOCK_SIZE;
}

static void
ms_ole_advance_sb (MS_OLE_STREAM *s, gint32 bytes)
{
	guint32 newoff = (bytes+s->offset);
	guint32 numblk = newoff/SB_BLOCK_SIZE;
	guint32 lastblk = END_OF_CHAIN;
	g_assert (bytes>=0);

/*	printf ("Advance from %d:%d by %d bytes\n", s->block, s->offset, bytes); */

	s->offset = newoff%SB_BLOCK_SIZE;
		
	while (s->block != END_OF_CHAIN)
		if (numblk==0)
			return;
		else
		{
			lastblk = s->block;
			s->block = NEXT_SB(s->file, s->block);
			numblk --;
		}
	s->block = lastblk; /* Special case to save blocks on write */
	s->offset = SB_BLOCK_SIZE;
}

static void
ms_ole_write_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
	int cpylen;
	int offset = s->offset;
	guint32 block   = s->block;
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
				s->block = block;
				printf ("Start of SB file\n");
			}
			SET_GUINT32(GET_SB_CHAIN_PTR(s->file, block), END_OF_CHAIN);
			s->end_block = block;
			printf ("Linked stuff\n");
/*			dump_allocation(s->file); */
		}
		
		dest = GET_SB_START_PTR(s->file, block) + offset;
		
		g_assert (cpylen>=0);
		PPS_SET_SIZE(s->file, s->pps, PPS_GET_SIZE(s->file,s->pps)+cpylen);
		
		memcpy (dest, ptr, cpylen);
		ptr   += cpylen;
		bytes -= cpylen;

		/* Must be exactly filling the block */
		if (PPS_GET_SIZE(s->file, s->pps) >= BB_THRESHOLD)
		{
			SBPtr sb_start_block = PPS_GET_STARTBLOCK(s->file, s->pps);
			SBPtr sbblk = sb_start_block;
			guint32 size = PPS_GET_SIZE(s->file, s->pps);
			int cnt;

			printf ("\n\n--- Converting ---\n\n\n");

			s->read_copy = ms_ole_read_copy_bb;
			s->read_ptr  = ms_ole_read_ptr_bb;
			s->advance   = ms_ole_advance_bb;
			s->write     = ms_ole_write_bb;

			g_assert (size%SB_BLOCK_SIZE == 0);

			/* Convert the file to BBlocks */
			PPS_SET_SIZE(s->file, s->pps, 0);
			s->block = END_OF_CHAIN;
			s->end_block = END_OF_CHAIN;
			s->offset = 0;

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
	s->advance (s, length);
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
	s->block  = PPS_GET_STARTBLOCK(f,p);
	if (s->block == SPECIAL_BLOCK)
/*	    s->block == END_OF_CHAIN) */
	{
		printf ("Bad file block record\n");
		g_free (s);
		return 0;
	}

	s->offset = 0;
	if (PPS_GET_SIZE(f, p)>=BB_THRESHOLD)
	{
		BBPtr b = PPS_GET_STARTBLOCK(f,p);

		s->read_copy = ms_ole_read_copy_bb;
		s->read_ptr  = ms_ole_read_ptr_bb;
		s->advance   = ms_ole_advance_bb;
		s->write     = ms_ole_write_bb;

		for (lp=0;lp<PPS_GET_SIZE(f,p)/BB_BLOCK_SIZE;lp++)
		{
			if (b == END_OF_CHAIN)
				printf ("Warning: bad file length in '%s'\n", PPS_NAME(f,p));
			else if (b == SPECIAL_BLOCK)
				printf ("Warning: special block in '%s'\n", PPS_NAME(f,p));
			else if (b == UNUSED_BLOCK)
				printf ("Warning: unused block in '%s'\n", PPS_NAME(f,p));
			else
				b = NEXT_BB(f, b);
		}
		s->end_block = b;
		if (b != END_OF_CHAIN && NEXT_BB(f, b) != END_OF_CHAIN)
			printf ("FIXME: Extra useless blocks on end of '%s'\n", PPS_NAME(f,p));
	}
	else
	{
		SBPtr b = PPS_GET_STARTBLOCK(f,p);

		s->read_copy    = ms_ole_read_copy_sb;
		s->read_ptr     = ms_ole_read_ptr_sb;
		s->advance      = ms_ole_advance_sb;
		s->write        = ms_ole_write_sb;

		for (lp=0;lp<PPS_GET_SIZE(f,p)/SB_BLOCK_SIZE;lp++)
		{
			if (b == END_OF_CHAIN)
				printf ("Warning: bad file length in '%s'\n", PPS_NAME(f,p));
			else if (b == SPECIAL_BLOCK)
				printf ("Warning: special block in '%s'\n", PPS_NAME(f,p));
			else if (b == UNUSED_BLOCK)
				printf ("Warning: unused block in '%s'\n", PPS_NAME(f,p));
			else
				b = NEXT_SB(f, b);
		}
		s->end_block = b;
		if (b != END_OF_CHAIN && NEXT_SB(f, b) != END_OF_CHAIN)
			printf ("FIXME: Extra useless blocks on end of '%s'\n", PPS_NAME(f,p));
	}
	return s;
}

void
ms_ole_stream_close (MS_OLE_STREAM *s)
{
/* No caches to write, nothing */
	g_free (s);
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

	printf ("Forward trace\n");
	d->pps = tmp;

	directory_setup(d);
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

/* Organise & if neccessary copy the blocks... */
static int
ms_biff_collate_block (BIFF_QUERY *bq)
{
	if (!(bq->data = bq->pos->read_ptr(bq->pos, bq->length)))
	{
		bq->data = g_new0 (guint8, bq->length);
		bq->data_malloced = 1;
		if (!bq->pos->read_copy(bq->pos, bq->data, bq->length))
			return 0;
	}
	else
		bq->pos->advance(bq->pos, bq->length);
	return 1;
}

BIFF_QUERY *
ms_biff_query_new (MS_OLE_STREAM *ptr)
{
	BIFF_QUERY *bq   ;
	if (!ptr)
		return 0;
	bq = g_new0 (BIFF_QUERY, 1);
	bq->opcode        = 0;
	bq->length        =-4;
	bq->data_malloced = 0;
	bq->streamPos     = 0;
	bq->pos = ptr;
	dump_biff(bq);
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
	return bf;
}

/**
 * Returns 0 if has hit end
 * NB. if this crashes obscurely, array is being extended over the stack !
 **/
int
ms_biff_query_next (BIFF_QUERY *bq)
{
	int ans;
	guint8 array[4];

	if (!bq || bq->streamPos >= PPS_GET_SIZE(bq->pos->file, bq->pos->pps))
		return 0;

	bq->streamPos+=bq->length + 4;

	if (bq->data_malloced)
	{
		bq->data_malloced = 0;
		g_free (bq->data);
	}

	if (!bq->pos->read_copy (bq->pos, array, 4))
		return 0;

	bq->opcode = BIFF_GETWORD(array);
	bq->length = BIFF_GETWORD(array+2);
	/*  printf ("Biff read code 0x%x, length %d\n", bq->opcode, bq->length); */
	bq->ms_op  = (bq->opcode>>8);
	bq->ls_op  = (bq->opcode&0xff);

	if (!bq->length)
	{
		bq->data = 0;
		return 1;
	}
	ans = ms_biff_collate_block (bq);
/*	printf ("OLE-BIFF: Opcode 0x%x length %d\n", bq->opcode, bq->length) ; */
/*	dump_biff (bq); */
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

#if G_BYTE_ORDER != G_LITTLE_ENDIAN
double biff_getdouble(guint8 *p)
{
    double d;
    int i;
    guint8 *t = (guint8 *)&d;

    p += 4;
    for (i = 0; i < 4; i++)
        *t++ = *p++;

    p -= 8;
    for (i = 0; i < 4; i++)
	*t++ = *p++;
    return d;
}
#endif
