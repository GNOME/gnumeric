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

/* Gives the position in memory, as a GUINT8 *, of the SB entry ( in a chain ) */
#define GET_SB_CHAIN_PTR(f,n) (GET_BB_START_PTR(f, f->header.sbd_list[(((n)*sizeof(SBPtr))/BB_BLOCK_SIZE)]) + \
                              (((n)*sizeof(SBPtr))%BB_BLOCK_SIZE))

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
  int lp,lp2 ;
  #define OFF (lp2+(lp<<4))
  #define OK (len-OFF>0)
  for (lp = 0;lp<(len+15)/16;lp++)
    {
      printf ("%8x  |  ", lp*16) ;
      for (lp2=0;lp2<16;lp2++)
	OK?printf("%2x ", ptr[OFF]):printf("XX ") ;
      printf ("  |  ") ;
     for (lp2=0;lp2<16;lp2++)
	printf ("%c", OK?(ptr[OFF]>'!'&&ptr[OFF]<127?ptr[OFF]:'.'):'*') ;
      printf ("\n") ;
    }
  #undef OFF
  #undef OK
}

/* FIXME: This needs proper unicode support ! current support is a guess */
/* NB. Different from biff_get_text, looks like a bug ! */
static char *
pps_get_text (BYTE *ptr, int length)
{
  int lp, skip ;
  char *ans ;
  BYTE *inb ;

  if (!length) 
    return 0 ;

  ans = (char *)malloc(sizeof(char)*length+1) ;

  skip = (ptr[0] < 0x30) ; /* Magic unicode number */
  if (skip)
    inb = ptr + 2 ;
  else
    inb = ptr ;
  for (lp=0;lp<length;lp++)
    {
      ans[lp] = (char) *inb ;
      inb+=2 ;
    }
  ans[lp] = 0 ;
  return ans ;
}

static void
dump_header (MS_OLE *f)
{
  int lp ;
  MS_OLE_HEADER *h = &f->header ;
  printf ("--------------------------MS_OLE HEADER-------------------------\n") ;
  printf ("Num BBD Blocks : %d Root %d, SBD %d\n",
	  GET_NUM_BBD_BLOCKS(f),
	  (int)h->root_startblock,
	  (int)h->sbd_startblock) ;
  for (lp=0;lp<GET_NUM_BBD_BLOCKS(f);lp++)
    printf ("GET_BBD_LIST[%d] = %d\n", lp, GET_BBD_LIST(f,lp)) ;

  printf ("Root blocks : %d\n", h->number_of_root_blocks) ;
  for (lp=0;lp<h->number_of_root_blocks;lp++)
      printf ("root_list[%d] = %d\n", lp, (int)h->root_list[lp]) ;

  printf ("sbd blocks : %d\n", h->number_of_sbd_blocks) ;
  for (lp=0;lp<h->number_of_sbd_blocks;lp++)
    printf ("sbd_list[%d] = %d\n", lp, (int)h->sbd_list[lp]) ;
  printf ("-------------------------------------------------------------\n") ;
}

static int
read_header (MS_OLE *f)
{
  MS_OLE_HEADER *header = &f->header ;

  header->root_startblock      = GET_ROOT_STARTBLOCK(f) ;
  header->sbd_startblock       = GET_SBD_STARTBLOCK(f) ;
  return 1 ;
}

static void
dump_allocation (MS_OLE *f)
{
  int blk, dep, lp ;
  printf ("Big block allocation\n") ;

  dep = 0 ;
  blk = 0 ;
  while (dep<GET_NUM_BBD_BLOCKS(f))
    {
      printf ("FAT block %d\n", dep) ;
      for (lp=0;lp<BB_BLOCK_SIZE/sizeof(BBPtr);lp++)
	{
	  guint32 type ;
	  type = GET_GUINT32(GET_BB_CHAIN_PTR(f, blk)) ;
	  if ((blk + dep*(BB_BLOCK_SIZE/sizeof(BBPtr)))*BB_BLOCK_SIZE > f->length)
	    printf ("*") ;
	  else if (type == SPECIAL_BLOCK)
	    printf ("S") ;
	  else if (type == UNUSED_BLOCK)
	    printf (".") ;
	  else if (type == END_OF_CHAIN)
	    printf ("X") ;
	  else
	    printf ("O") ;
	  blk++ ;
	  if (blk%16==15)
	    printf (" - %d\n", blk) ;
	}
      dep++ ;
    }

  printf ("Small block allocation\n") ;
  dep = 0 ;
  blk = 0 ;
  while (dep<f->header.number_of_sbd_blocks)
    {
      printf ("SB block %d ( = BB block %d )\n", dep, f->header.sbd_list[dep]) ;
      for (lp=0;lp<BB_BLOCK_SIZE/sizeof(SBPtr);lp++)
	{
	  guint32 type ;
	  type = GET_GUINT32(GET_SB_CHAIN_PTR(f, blk)) ;
	  if (type == SPECIAL_BLOCK)
	    printf ("S") ;
	  else if (type == UNUSED_BLOCK)
	    printf (".") ;
	  else if (type == END_OF_CHAIN)
	    printf ("X") ;
	  else
	    printf ("O") ;
	  blk++ ;
	  if (blk%16==15)
	    printf (" - %d\n", blk) ;
	}
      dep++ ;
    }
}

/* Create a nice linear array and return count of the number in the array */
static int
read_link_array(MS_OLE *f, BBPtr first, BBPtr **array)
{
  BBPtr ptr = first ;
  int lp, num=0 ;

  while (ptr != END_OF_CHAIN)	/* Count the items */
    {
      ptr = NEXT_BB (f, ptr) ;
      num++;
    }

  if (num == 0)
    *array = 0 ;
  else
    *array  = g_new (BBPtr, num) ;

  lp = 0 ;
  ptr = first ;
  while (ptr != END_OF_CHAIN)
    {
      (*array)[lp++] = ptr ;
      ptr = NEXT_BB (f, ptr) ;
    }
  return num ;
}

static int
read_root_list (MS_OLE *f)
{
  /* Find blocks belonging to root ... */
  f->header.number_of_root_blocks = read_link_array(f, f->header.root_startblock, &f->header.root_list) ;
  return (f->header.number_of_root_blocks!=0) ;
}

static int
read_sbf_list (MS_OLE *f)
{
  /* Find blocks containing all small blocks ... */
  f->header.number_of_sbf_blocks = read_link_array(f, f->header.sbf_startblock, &f->header.sbf_list) ;
  return 1 ;
}

static int
read_sbd_list (MS_OLE *f)
{
  /* Find blocks belonging to sbd ... */
  f->header.number_of_sbd_blocks = read_link_array(f, f->header.sbd_startblock, &f->header.sbd_list) ;
  return 1 ;
}

static int
ms_ole_analyse (MS_OLE *f)
{
  if (!read_header(f)) return 0 ;
  if (!read_root_list(f)) return 0 ;
  if (!read_sbd_list(f)) return 0 ;
  f->header.sbf_startblock = PPS_GET_STARTBLOCK(f, PPS_ROOT_BLOCK) ;
  if (!read_sbf_list(f)) return 0 ;
  dump_header(f) ;
  dump_allocation (f) ;

  {
    int lp ;
    for (lp=0;lp<BB_BLOCK_SIZE/PPS_BLOCK_SIZE;lp++)
      {
	printf ("PPS %d type %d, prev %d next %d, dir %d\n", lp, PPS_GET_TYPE(f,lp),
		PPS_GET_PREV(f,lp), PPS_GET_NEXT(f,lp), PPS_GET_DIR(f,lp)) ;
	dump (PPS_PTR(f, lp), PPS_BLOCK_SIZE) ;
      }
  }
  return 1 ;
}

MS_OLE *
ms_ole_create (const char *name)
{
  struct stat st ;
  int file ;
  MS_OLE *f ;
  int init_blocks = 5, lp ;
  guint8 *ptr ;
  guint32 root_startblock = 0 ;
  guint32 sbd_startblock  = 0, zero = 0 ;
  char title[] ="Root Entry" ;

  if ((file = open (name, O_RDWR|O_CREAT|O_TRUNC|O_NONBLOCK,
		    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)) == -1)
    {
      printf ("Can't create file '%s'\n", name) ;
      return 0 ;
    }

    if ((lseek (file, BB_BLOCK_SIZE*init_blocks - 1, SEEK_SET)==(off_t)-1) ||
      (write (file, &zero, 1)==-1))
    {
      printf ("Serious error extending file to %d bytes\n", BB_BLOCK_SIZE*init_blocks) ;
      return 0 ;
    }

  f = g_new (MS_OLE, 1) ;
  f->file_descriptor = file ;
  fstat(file, &st) ;
  f->length = st.st_size ;
  if (f->length%BB_BLOCK_SIZE)
    printf ("Warning file %d non-integer number of blocks\n", f->length) ;

  f->mem  = mmap (0, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0) ;
  if (!f->mem)
    {
      printf ("Serious error mapping file to %d bytes\n", BB_BLOCK_SIZE*init_blocks) ;
      close(file) ;
      free(f) ;
      return 0 ;
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
    SET_GUINT32(f->mem + lp*4, END_OF_CHAIN) ;

  SET_GUINT32(f->mem, 0xe011cfd0) ; /* Magic number */
  SET_GUINT32(f->mem + 4, 0xe11ab1a1) ;
  SET_NUM_BBD_BLOCKS(f, 1) ;
  SET_BBD_LIST(f, 0, 1) ;

  f->header.root_startblock = 0 ;
  f->header.sbd_startblock  = 2 ;
  SET_ROOT_STARTBLOCK(f, f->header.root_startblock) ;
  SET_SBD_STARTBLOCK (f, f->header.sbd_startblock) ;


  /* The first PPS block : 0 */

  f->header.number_of_root_blocks = 1 ;
  f->header.root_list = g_new (BBPtr, 1) ;
  f->header.root_list[0] = 0 ;

  lp = 0 ;
  ptr = f->mem + BB_BLOCK_SIZE ;
  while (title[lp])
    *ptr++ = title[lp++] ;

  for (;lp<PPS_BLOCK_SIZE;lp++) /* Blank stuff I don't understand */
    *ptr++ = 0 ;

  PPS_SET_NAME_LEN(f, PPS_ROOT_BLOCK, lp) ;

  PPS_SET_STARTBLOCK(f, PPS_ROOT_BLOCK, 3) ; /* Start of the sbf file */
  PPS_SET_TYPE(f, PPS_ROOT_BLOCK, MS_OLE_PPS_ROOT) ;
  PPS_SET_DIR (f, PPS_ROOT_BLOCK, PPS_END_OF_CHAIN) ;
  PPS_SET_NEXT(f, PPS_ROOT_BLOCK, PPS_END_OF_CHAIN) ;
  PPS_SET_PREV(f, PPS_ROOT_BLOCK, PPS_END_OF_CHAIN) ;
  PPS_SET_SIZE(f, PPS_ROOT_BLOCK, 0) ;

  /* the first BBD block : 1 */

  for (lp=0;lp<BB_BLOCK_SIZE/4;lp++)
    SET_GUINT32(GET_BB_START_PTR(f,1) + lp*4, END_OF_CHAIN) ;

  SET_GUINT32(GET_BB_CHAIN_PTR(f,1), SPECIAL_BLOCK) ; /* Itself */
  SET_GUINT32(GET_BB_CHAIN_PTR(f,2), END_OF_CHAIN) ;  /* SBD chain */
  SET_GUINT32(GET_BB_CHAIN_PTR(f,3), END_OF_CHAIN) ;  /* SBF stream */

  /* the first SBD block : 2 */
  for (lp=0;lp<BB_BLOCK_SIZE/4;lp++)
    SET_GUINT32(GET_BB_START_PTR(f,2) + lp*4, UNUSED_BLOCK) ;

  f->header.number_of_sbd_blocks = 1 ;
  f->header.sbd_list = g_new (BBPtr, 1) ;
  f->header.sbd_list[0] = 0 ;

  /* the first SBF block : 3 */
  for (lp=0;lp<BB_BLOCK_SIZE/4;lp++) /* Fill with zeros */
    SET_GUINT32(GET_BB_START_PTR(f,2) + lp*4, 0) ;

  f->header.number_of_sbf_blocks = 1 ;
  f->header.sbf_list = g_new (BBPtr, 1) ;
  f->header.sbf_list[0] = 0 ;

  dump_header(f) ;
  dump_allocation (f) ;

  {
    int lp ;
    for (lp=0;lp<BB_BLOCK_SIZE/PPS_BLOCK_SIZE;lp++)
      {
	printf ("PPS %d type %d, prev %d next %d, dir %d\n", lp, PPS_GET_TYPE(f,lp),
		PPS_GET_PREV(f,lp), PPS_GET_NEXT(f,lp), PPS_GET_DIR(f,lp)) ;
	dump (PPS_PTR(f, lp), PPS_BLOCK_SIZE) ;
      }
  }

  /*  printf ("\n\nEntire created file\n\n\n") ;
      dump(f->mem, init_blocks*BB_BLOCK_SIZE) ; */

  return f ;
}


MS_OLE *
ms_ole_new (const char *name)
{
  struct stat st ;
  int file ;
  MS_OLE *f ;

  printf ("New OLE file '%s'\n", name) ;
  f = g_new (MS_OLE, 1) ;

  f->file_descriptor = file = open (name, O_RDWR) ;
  if (file == -1 || fstat(file, &st))
    {
      printf ("No such file '%s'\n", name) ;
      return 0 ;
    }
  f->length = st.st_size ;
  if (f->length<=0x4c)  /* Bad show */
    {
      printf ("File '%s' too short\n", name) ;
      return 0 ;
    }
  if (f->length%BB_BLOCK_SIZE)
    printf ("Warning file '%s':%d non-integer number of blocks\n", name, f->length) ;

  f->mem = mmap (0, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0) ;

  if (GET_GUINT32(f->mem    ) != 0xe011cfd0 ||
      GET_GUINT32(f->mem + 4) != 0xe11ab1a1)
    {
      printf ("Failed magic number %x %x\n",
	      GET_GUINT32(f->mem), GET_GUINT32(f->mem+4)) ;
      ms_ole_destroy (f) ;
      return 0 ;
    }
  if (!ms_ole_analyse (f))
    {
      printf ("Directory error\n") ;
      ms_ole_destroy(f) ;
      return 0 ;
    }
  printf ("New OLE file\n") ;
  return f ;
}

void
ms_ole_destroy (MS_OLE *f)
{
  if (f)
    {
      munmap (f->mem, f->length) ;
      close (f->file_descriptor) ;
      free (f) ;
      printf ("Closing file\n") ;
    }
  else
    printf ("Closing NULL file\n") ;
}

static void
dump_stream (MS_OLE_STREAM *s)
{
  if (PPS_GET_SIZE(s->file, s->pps)>=BB_THRESHOLD)
    printf ("Big block : ") ;
  else
    printf ("Small block : ") ;
  printf ("block %d, offset %d\n", s->block, s->offset) ;
}

static void
dump_biff (BIFF_QUERY *bq)
{
  printf ("Opcode %d length %d malloced? %d\nData:\n", bq->opcode, bq->length, bq->data_malloced) ;
  if (bq->length>0)
    dump (bq->data, bq->length) ;
  dump_stream (bq->pos) ;
}

static guint8*
ms_ole_read_ptr_bb (MS_OLE_STREAM *s, guint32 length)
{
  int block_left ;
  if (s->block == s->end_block)
    {
      block_left = PPS_GET_SIZE(s->file,s->pps) % BB_BLOCK_SIZE - s->offset ;
      if (length>block_left)
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
    }
  
  block_left = BB_BLOCK_SIZE - s->offset ;
  if (length<=block_left) /* Just return the pointer then */
    return (GET_BB_START_PTR(s->file, s->block) + s->offset) ;
  
  /* Is it contiguous ? */
  {
    guint32 curblk, newblk ;
    int  ln     = length ;
    int  blklen = block_left ;
    int  contig = 1 ;
    
    curblk = s->block ;
    if (curblk == END_OF_CHAIN)
      {
	printf ("Trying to read beyond end of stream\n") ;
	return 0 ;
      }
    
    while (ln>blklen && contig)
      {
	ln-=blklen ;
	blklen = BB_BLOCK_SIZE ;
	newblk = NEXT_BB(s->file, curblk) ;
	if (newblk != curblk+1)
	  return 0 ;
	curblk = newblk ;
	if (curblk == END_OF_CHAIN)
	  {
	    printf ("End of chain error\n") ;
	    return 0 ;
	  }
      }
    /* Straight map, simply return a pointer */
    return GET_BB_START_PTR(s->file, s->block) + s->offset ;
  }
}

static guint8*
ms_ole_read_ptr_sb (MS_OLE_STREAM *s, guint32 length)
{
  int block_left ;
  if (s->block == s->end_block)
    {
      block_left = PPS_GET_SIZE(s->file,s->pps) % SB_BLOCK_SIZE - s->offset ;
      if (length>block_left)
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
    }
  
  block_left = SB_BLOCK_SIZE - s->offset ;
  if (length<=block_left) /* Just return the pointer then */
    return (GET_SB_START_PTR(s->file, s->block) + s->offset) ;
  
  /* Is it contiguous ? */
  {
    guint32 curblk, newblk ;
    int  ln     = length ;
    int  blklen = block_left ;
    int  contig = 1 ;
    
    curblk = s->block ;
    if (curblk == END_OF_CHAIN)
      {
	printf ("Trying to read beyond end of stream\n") ;
	return 0 ;
      }
    
    while (ln>blklen && contig)
      {
	ln-=blklen ;
	blklen = SB_BLOCK_SIZE ;
	newblk = NEXT_SB(s->file, curblk) ;
	if (newblk != curblk+1)
	  return 0 ;
	curblk = newblk ;
	if (curblk == END_OF_CHAIN)
	  {
	    printf ("End of chain error\n") ;
	    return 0 ;
	  }
      }
    /* Straight map, simply return a pointer */
    return GET_SB_START_PTR(s->file, s->block) + s->offset ;
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
  int block_left ;
  if (s->block == s->end_block)
    {
      block_left = PPS_GET_SIZE(s->file, s->pps) % BB_BLOCK_SIZE - s->offset ;
      if (length>block_left) /* Just return the pointer then */
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
      memcpy (ptr, GET_BB_START_PTR(s->file, s->block) + s->offset, length) ;
      return 1 ;
    }

  block_left = BB_BLOCK_SIZE - s->offset ;

  /* Block by block copy */
  {
    int cpylen ;
    int offset = s->offset ;
    int block  = s->block ;
    int bytes  = length ;
    guint8 *src ;
    
    while (bytes>0)
      {
	int cpylen = BB_BLOCK_SIZE - offset ;
	if (cpylen>bytes)
	  cpylen = bytes ;
	src = GET_BB_START_PTR(s->file, block) + offset ;
	
	if (block == s->end_block && cpylen > PPS_GET_SIZE(s->file, s->pps)%BB_BLOCK_SIZE)
	  {
	    printf ("Trying to read beyond end of stream\n") ;
	    return 0 ;
	  }

	memcpy (ptr, src, cpylen) ;
	ptr   += cpylen ;
	bytes -= cpylen ;
	
	offset = 0 ;
	block  = NEXT_BB(s->file, block) ;	  
      }
  }
  return 1 ;
}

static gboolean
ms_ole_read_copy_sb (MS_OLE_STREAM *s, guint8 *ptr, guint32 length)
{
  int block_left ;
  if (s->block == s->end_block)
    {
      block_left = PPS_GET_SIZE(s->file,s->pps) % SB_BLOCK_SIZE - s->offset ;
      if (length>block_left)
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
      memcpy (ptr, GET_SB_START_PTR(s->file, s->block) + s->offset, length) ;
      return 1 ;
    }

  block_left = SB_BLOCK_SIZE - s->offset ;

  /* Block by block copy */
  {
    int cpylen ;
    int offset = s->offset ;
    int block  = s->block ;
    int bytes  = length ;
    guint8 *src ;
    
    while (bytes>0)
      {
	int cpylen = SB_BLOCK_SIZE - offset ;
	if (cpylen>bytes)
	  cpylen = bytes ;
	src = GET_SB_START_PTR(s->file, block) + offset ;
	
	if (block == s->end_block && cpylen > PPS_GET_SIZE(s->file,s->pps)%SB_BLOCK_SIZE)
	  {
	    printf ("Trying to read beyond end of stream\n") ;
	    return 0 ;
	  }

	memcpy (ptr, src, cpylen) ;
	ptr   += cpylen ;
	bytes -= cpylen ;
	
	offset = 0 ;
	block  = NEXT_SB(s->file, block) ;
      }
  }
  return 1 ;
}

static void
ms_ole_advance_bb (MS_OLE_STREAM *s, gint32 bytes)
{
  int numblk = (bytes+s->offset)/BB_BLOCK_SIZE ;
  g_assert (bytes>=0) ;
  s->offset = (s->offset+bytes)%BB_BLOCK_SIZE ;
  while (s->block != END_OF_CHAIN)
    if (numblk==0)
	return ;
    else
      {
	s->block = NEXT_BB(s->file, s->block) ;
	numblk -- ;
      }
}

static void
ms_ole_advance_sb (MS_OLE_STREAM *s, gint32 bytes)
{
  int numblk = (bytes+s->offset)/SB_BLOCK_SIZE ;
  g_assert (bytes>=0) ;
  s->offset = (s->offset+bytes)%SB_BLOCK_SIZE ;
  while (s->block != END_OF_CHAIN)
    if (numblk==0)
	return ;
    else
      {
	s->block = NEXT_SB(s->file, s->block) ;
	numblk -- ;
      }
}

static void
extend_file (MS_OLE *f, int blocks)
{
  struct stat st ;
  int file = f->file_descriptor ;
  guint8 *newptr, zero = 0 ;

  g_assert (munmap(f->mem, f->length) != -1) ;
  /* Extend that file by blocks */

  if ((fstat(file, &st)==-1) ||
      (lseek (file, st.st_size + BB_BLOCK_SIZE*blocks - 1, SEEK_SET)==(off_t)-1) ||
      (write (file, &zero, 1)==-1))
    {
      printf ("Serious error extending file\n") ;
      f->mem = 0 ;
      return ;
    }

  fstat(file, &st) ;
  f->length = st.st_size ;
  if (f->length%BB_BLOCK_SIZE)
    printf ("Warning file %d non-integer number of blocks\n", f->length) ;
  newptr = mmap (0, f->length, PROT_READ|PROT_WRITE, MAP_SHARED, file, 0) ;
  if (newptr != f->mem)
    printf ("Memory map moved \n") ;
  f->mem = newptr ;
}

static guint32
next_free_bb (MS_OLE *f)
{
  guint32 dep, blk, sblk ;
  guint32 idx, lp ;
  
  dep = 0 ;
  blk = 0 ;
  while (dep<GET_NUM_BBD_BLOCKS(f))
    {
      for (sblk=0;sblk<BB_BLOCK_SIZE/sizeof(BBPtr);sblk++)
	{
	  if (GET_GUINT32(GET_BB_START_PTR(f, GET_BBD_LIST(f, dep) + sblk*sizeof(BBPtr)))
	      == UNUSED_BLOCK)
	    {
	      /* Extend and remap file */
	      if (blk*BB_BLOCK_SIZE > f->length)
		{
		  printf ("Extend & remap file ...\n") ;
		  extend_file(f, 1) ;
		  g_assert (blk*BB_BLOCK_SIZE <= f->length) ;
		}
	      printf ("Unused block at %d\n", blk) ;
	      return blk ;
	    }
	}
      dep++ ;
    }

  printf ("Out of unused BB space !\n") ;
  extend_file (f,2) ;
  blk = (f->length/BB_BLOCK_SIZE)-2 ;
  idx = GET_NUM_BBD_BLOCKS(f) ;
  g_assert (idx<(BB_BLOCK_SIZE - 0x4c)/4) ;
  SET_NUM_BBD_BLOCKS(f, idx+1) ;
  SET_BBD_LIST(f, idx+1, blk) ;
  /* Setup that block */
  for (lp=0;lp<BB_BLOCK_SIZE/sizeof(BBPtr);lp++)
    SET_GUINT32(GET_BB_START_PTR(f, blk) + lp*sizeof(BBPtr), UNUSED_BLOCK) ;

  return 0 ;
}

static guint32
next_free_sb (MS_OLE *f)
{
  guint32 dep, blk, sblk ;
  guint32 idx, lp ;
  
  dep = 0 ;
  blk = 0 ;
  while (dep<f->header.number_of_sbd_blocks)
    {
      for (sblk=0;sblk<BB_BLOCK_SIZE/sizeof(SBPtr);sblk++)
	{
	  if (GET_GUINT32(GET_BB_START_PTR(f, f->header.sbd_list[dep]) + sblk*sizeof(SBPtr))
	      == UNUSED_BLOCK)
	    {
	      /* Extend and remap file */
	      if (blk > f->header.number_of_sbf_blocks*BB_BLOCK_SIZE/SB_BLOCK_SIZE)
		{
		  printf ("Extend & remap file ...\n") ;
		  extend_file(f, 1) ;
		  g_assert (blk <= f->header.number_of_sbf_blocks*BB_BLOCK_SIZE/SB_BLOCK_SIZE) ;
		}
	      printf ("Unused small block at %d\n", blk) ;
	      return blk ;
	    }
	}
      dep++ ;
    }

  printf ("Out of unused SB space !\n") ;
  g_assert (0) ;
  /*  FIXME, something clever needs to happen here !

      extend_file (f,2) ;
  blk = (f->length/BB_BLOCK_SIZE)-2 ;
  idx = GET_NUM_BBD_BLOCKS(f) ;
  g_assert (idx<(BB_BLOCK_SIZE - 0x4c)/4) ;
  SET_NUM_BBD_BLOCKS(f, idx+1) ;
  SET_BBD_LIST(f, idx+1, blk) ;
   Setup that block 
  for (lp=0;lp<BB_BLOCK_SIZE/sizeof(BBPtr);lp++)
    SET_GUINT32(GET_BB_START_PTR(f, blk) + lp*sizeof(BBPtr), UNUSED_BLOCK) ;
*/
  return 0 ;
}

/**
 * Creates an extra block on the end of a BB file 
 * Leaving the pps record in an unusual state, to be
 * fixed by all users.
 **/
static void
ms_ole_addblock_bb (MS_OLE_STREAM *s)
{
  guint32 lastblk = s->end_block ;
  guint32 newblk  = next_free_bb (s->file) ;
  SET_GUINT32(GET_BB_CHAIN_PTR(s->file, lastblk), newblk) ;
  SET_GUINT32(GET_BB_CHAIN_PTR(s->file, newblk), END_OF_CHAIN) ;
  s->end_block = newblk ;
}

static void
ms_ole_addblock_sb (MS_OLE_STREAM *s)
{
  printf ("Placeholder\n") ;
}

MS_OLE_STREAM *
ms_ole_stream_open (MS_OLE_DIRECTORY *d, char mode)
{
  PPS_IDX p=d->pps ;
  MS_OLE *f=d->file ;
  MS_OLE_STREAM *s ;
  int lp ;

  if (!p)
    return 0 ;

  s         = g_new (MS_OLE_STREAM, 1) ;
  s->file   = f ;
  s->pps    = p ;
  s->block  = PPS_GET_STARTBLOCK(f,p) ;
  if (s->block == SPECIAL_BLOCK ||
      s->block == END_OF_CHAIN)
    {
      printf ("Bad file block record\n") ;
      free (s) ;
      return 0 ;
    }

  s->offset = 0 ;
  if (PPS_GET_SIZE(f, p)>=BB_THRESHOLD)
    {
      BBPtr b = PPS_GET_STARTBLOCK(f,p) ;

      s->read_copy = ms_ole_read_copy_bb ;
      s->read_ptr  = ms_ole_read_ptr_bb ;
      s->advance   = ms_ole_advance_bb ;
      s->addblock  = ms_ole_addblock_bb ;

      for (lp=0;lp<PPS_GET_SIZE(f,p)/BB_BLOCK_SIZE;lp++)
	{
	  if (b == END_OF_CHAIN)
	    printf ("Warning: bad file length in '%s'\n", PPS_NAME(f,p)) ;
	  else if (b == SPECIAL_BLOCK)
	    printf ("Warning: special block in '%s'\n", PPS_NAME(f,p)) ;
	  else if (b == UNUSED_BLOCK)
	    printf ("Warning: unused block in '%s'\n", PPS_NAME(f,p)) ;
	  else
	    b = NEXT_BB(f, b) ;
	}
      s->end_block = b ;
      if (b != END_OF_CHAIN && NEXT_BB(f, b) != END_OF_CHAIN)
	printf ("FIXME: Extra useless blocks on end of '%s'\n", PPS_NAME(f,p)) ;
    }
  else
    {
      SBPtr b = PPS_GET_STARTBLOCK(f,p) ;

      s->read_copy    = ms_ole_read_copy_sb ;
      s->read_ptr     = ms_ole_read_ptr_sb ;
      s->advance      = ms_ole_advance_sb ;
      s->addblock     = ms_ole_addblock_sb ;

      for (lp=0;lp<PPS_GET_SIZE(f,p)/SB_BLOCK_SIZE;lp++)
	{
	  if (b == END_OF_CHAIN)
	    printf ("Warning: bad file length in '%s'\n", PPS_NAME(f,p)) ;
	  else if (b == SPECIAL_BLOCK)
	    printf ("Warning: special block in '%s'\n", PPS_NAME(f,p)) ;
	  else if (b == UNUSED_BLOCK)
	    printf ("Warning: unused block in '%s'\n", PPS_NAME(f,p)) ;
	  else
	    b = NEXT_SB(f, b) ;
	}
      s->end_block = b ;
      if (b != END_OF_CHAIN && NEXT_SB(f, b) != END_OF_CHAIN)
	printf ("FIXME: Extra useless blocks on end of '%s'\n", PPS_NAME(f,p)) ;
    }
  s->write  = 0 ;
  return s ;
}

void
ms_ole_stream_close (MS_OLE_STREAM *s)
{
/* No caches to write, nothing */
  free (s) ;
}

/* You probably arn't too interested in the root directory anyway
   but this is first */
MS_OLE_DIRECTORY *
ms_ole_directory_new (MS_OLE *f)
{
  MS_OLE_DIRECTORY *d = g_new (MS_OLE_DIRECTORY, 1) ;
  d->file          = f;
  d->pps           = PPS_ROOT_BLOCK ;
  d->primary_entry = PPS_ROOT_BLOCK ;
  d->name          = PPS_NAME(f, d->pps) ;
  ms_ole_directory_enter (d) ;
  return d ;
}

/**
 * Fills fields from the pps index
 **/
static void
directory_setup (MS_OLE_DIRECTORY *d)
{
  printf ("Setup pps = %d\n", d->pps) ;
  free (d->name) ;
  d->name   = PPS_NAME(d->file, d->pps) ;
  d->type   = PPS_GET_TYPE(d->file, d->pps) ;
  d->length = PPS_GET_SIZE(d->file, d->pps) ;
}

/**
 * This navigates by offsets from the primary_entry
 **/
int
ms_ole_directory_next (MS_OLE_DIRECTORY *d)
{
  int offset ;
  PPS_IDX tmp ;

  if (!d)
    return 0 ;

  /* If its primary just go ahead */
  if (d->pps != d->primary_entry)
    {
      /* Checking back up the chain */
      offset = 0 ;
      tmp = d->primary_entry ;
      while (tmp != PPS_END_OF_CHAIN &&
	     tmp != d->pps)
	{
	  tmp = PPS_GET_PREV(d->file, tmp) ;
	  offset++ ;
	}
      if (d->pps == PPS_END_OF_CHAIN ||
	  tmp != PPS_END_OF_CHAIN)
	{
	  offset-- ;
	  printf ("Back trace by %d\n", offset) ;
	  tmp = d->primary_entry ;
	  while (offset > 0)
	    {
	      tmp = PPS_GET_PREV(d->file, tmp) ;
	      offset-- ;
	    }
	  d->pps = tmp ;
	  directory_setup(d) ;
	  return 1 ;
	}
    }

  /* Go down the chain, ignoring the primary entry */
  tmp = PPS_GET_NEXT(d->file, d->pps) ;
  if (tmp == PPS_END_OF_CHAIN)
    return 0 ;

  printf ("Forward trace\n") ;
  d->pps = tmp ;

  directory_setup(d) ;
  printf ("Next '%s' %d %d\n", d->name, d->type, d->length) ;
  return 1 ;
}

void
ms_ole_directory_enter (MS_OLE_DIRECTORY *d)
{
  if (!d || d->pps==PPS_END_OF_CHAIN)
    return ;

  if (!((PPS_GET_TYPE(d->file, d->pps) == MS_OLE_PPS_STORAGE) ||
	(PPS_GET_TYPE(d->file, d->pps) == MS_OLE_PPS_ROOT)))
    {
      printf ("Bad type %d %d\n", PPS_GET_TYPE(d->file, d->pps), MS_OLE_PPS_ROOT) ;
      return ;
    }

  if (PPS_GET_DIR(d->file, d->pps) != PPS_END_OF_CHAIN)
    {
      d->primary_entry = PPS_GET_DIR(d->file, d->pps);
      /* So it will wind in from the start on 'next' */
      d->pps = PPS_END_OF_CHAIN ;
    }
  return ;
}

static void
free_allocation (MS_OLE *f, PPS_IDX pps)
{
  if (PPS_GET_SIZE(f,pps) >= BB_THRESHOLD)
    { /* Big Blocks */
      BBPtr p ;
      p = PPS_GET_STARTBLOCK (f, pps) ;
      while (p != END_OF_CHAIN)
	{
	  BBPtr next = NEXT_BB(f,p) ;
	  SET_GUINT32 (GET_BB_CHAIN_PTR(f,p), UNUSED_BLOCK) ;
	  p = next ;
	}
    }
  else
    {
      SBPtr p ;
      p = PPS_GET_STARTBLOCK (f, pps) ;
      while (p != END_OF_CHAIN)
	{
	  SBPtr next = NEXT_SB(f,p) ;
	  SET_GUINT32 (GET_SB_CHAIN_PTR(f,p), UNUSED_BLOCK) ;
	  p = next ;
	}
    }
}

void
ms_ole_directory_unlink (MS_OLE_DIRECTORY *d)
{
  if (d->pps != d->primary_entry &&
      PPS_GET_NEXT(d->file, d->pps) == PPS_END_OF_CHAIN &&
      PPS_GET_PREV(d->file, d->pps) == PPS_END_OF_CHAIN)
    { /* Little, lost & loosely attached */
      PPS_SET_NAME_LEN (d->file, d->pps, 0) ; /* Zero its name */
      free_allocation (d->file, d->pps) ;
    }
  else
    printf ("Unlink failed\n") ;
}

static PPS_IDX
next_free_pps (MS_OLE *f)
{
  PPS_IDX p = PPS_ROOT_BLOCK ;
  PPS_IDX max_pps = f->header.number_of_root_blocks*BB_BLOCK_SIZE/PPS_BLOCK_SIZE ;
  guint32 blk, lp ;
  while (p<max_pps)
    {
      if (PPS_GET_NAME_LEN(f, p) == 0)
	return p ;
      p++ ;
    }
  /* We need to extend the pps then */
  blk = next_free_bb(f) ;
  SET_GUINT32(GET_BB_CHAIN_PTR(f,blk), END_OF_CHAIN) ;

  for (lp=0;lp<BB_BLOCK_SIZE;lp++)
    SET_GUINT8(GET_BB_START_PTR(f, blk) + lp, 0) ;
  
  { /* Append our new pps block to the chain */
    BBPtr ptr = f->header.root_startblock ;
    while (NEXT_BB(f, ptr) != END_OF_CHAIN)
      ptr = NEXT_BB (f, ptr) ;
    SET_GUINT32(GET_BB_CHAIN_PTR (f, ptr), blk) ;
  }
  return max_pps ;
}

/**
 * This is passed the handle of a directory in which to create the
 * new stream / directory.
 **/
MS_OLE_DIRECTORY *
ms_ole_directory_create (MS_OLE_DIRECTORY *d, char *name, PPS_TYPE type)
{
  /* Find a free PPS */
  PPS_IDX p = next_free_pps(d->file) ;
  PPS_IDX prim ;
  MS_OLE_DIRECTORY *nd = g_new (MS_OLE_DIRECTORY, 1) ;
  SBPtr  startblock ;
  int lp=0 ;

  /* Blank stuff I don't understand */
  for (lp=0;lp<PPS_BLOCK_SIZE;lp++)
    SET_GUINT8(PPS_PTR(d->file, p)+lp, 0) ;

  lp = 0 ;
  while (name[lp])
    {
      SET_GUINT8(PPS_PTR(d->file, p) + lp*2, 0) ;
      SET_GUINT8(PPS_PTR(d->file, p) + lp*2 + 1, name[lp]) ;
      lp++ ;
    }
  PPS_SET_NAME_LEN(d->file, p, lp) ;
  startblock = next_free_sb(d->file) ;
  PPS_SET_STARTBLOCK(d->file, p, startblock) ;
  SET_GUINT32(GET_SB_CHAIN_PTR(d->file, startblock), END_OF_CHAIN) ;

  /* Chain into the directory */
  prim = PPS_GET_DIR (d->file, d->pps) ;
  if (prim == PPS_END_OF_CHAIN)
    {
      prim = p ;
      PPS_SET_DIR (d->file, d->pps, p) ;
      PPS_SET_DIR (d->file, p, PPS_END_OF_CHAIN) ;
      PPS_SET_NEXT(d->file, p, PPS_END_OF_CHAIN) ;
      PPS_SET_PREV(d->file, p, PPS_END_OF_CHAIN) ;
    }
  else /* FIXME: this should insert in alphabetic order */
    {
      PPS_IDX oldnext = PPS_GET_NEXT(d->file, prim) ;
      PPS_SET_NEXT(d->file, prim, p) ;
      PPS_SET_NEXT(d->file, p, oldnext) ;
      PPS_SET_PREV(d->file, p, PPS_END_OF_CHAIN) ;
    }

  PPS_SET_TYPE(d->file, p, type) ;
  PPS_SET_SIZE(d->file, p, 0) ;

  nd->file     = d->file ;
  nd->pps      = PPS_END_OF_CHAIN ;
  nd->name     = PPS_NAME(d->file, p) ;
  nd->primary_entry = prim ;

  return nd ;
}

void
ms_ole_directory_destroy (MS_OLE_DIRECTORY *d)
{
  if (d)
    free (d) ;
}

/* Organise & if neccessary copy the blocks... */
static int
ms_biff_collate_block (BIFF_QUERY *bq)
{
  if (!(bq->data = bq->pos->read_ptr(bq->pos, bq->length)))
    {
      bq->data = g_new (guint8, bq->length) ;
      bq->data_malloced = 1 ;
      if (!bq->pos->read_copy(bq->pos, bq->data, bq->length))
	return 0 ;
    }
  bq->pos->advance(bq->pos, bq->length) ;
  return 1 ;
}

BIFF_QUERY *
ms_biff_query_new (MS_OLE_STREAM *ptr)
{
  BIFF_QUERY *bq    ;
  if (!ptr)
    return 0 ;
  bq = g_new (BIFF_QUERY, 1) ;
  bq->opcode        = 0 ;
  bq->length        =-4 ;
  bq->data_malloced = 0 ;
  bq->streamPos     = 0 ;
  bq->pos = ptr ;
  dump_biff(bq) ;
  return bq ;
}

BIFF_QUERY *
ms_biff_query_copy (const BIFF_QUERY *p)
{
  BIFF_QUERY *bf = g_new (BIFF_QUERY, 1) ;
  memcpy (bf, p, sizeof (BIFF_QUERY)) ;
  if (p->data_malloced)
    {
      bf->data = (guint8 *)malloc (p->length) ;
      memcpy (bf->data, p->data, p->length) ;
    }
  return bf ;
}

/**
 * Returns 0 if has hit end
 * NB. if this crashes obscurely, array is being extended over the stack !
 **/
int
ms_biff_query_next (BIFF_QUERY *bq)
{
  int ans ;
  guint8 array[4] ;

  if (!bq || bq->streamPos >= PPS_GET_SIZE(bq->pos->file, bq->pos->pps))
    return 0 ;

  bq->streamPos+=bq->length + 4 ;

  if (bq->data_malloced)
    {
      bq->data_malloced = 0 ;
      free (bq->data) ;
    }

  if (!bq->pos->read_copy (bq->pos, array, 4))
    return 0 ;
  bq->pos->advance (bq->pos, 4) ;

  bq->opcode = BIFF_GETWORD(array) ;
  bq->length = BIFF_GETWORD(array+2) ;
  /*  printf ("Biff read code 0x%x, length %d\n", bq->opcode, bq->length) ; */
  bq->ms_op  = (bq->opcode>>8);
  bq->ls_op  = (bq->opcode&0xff);

  if (!bq->length)
    {
      bq->data = 0 ;
      return 1 ;
    }
  ans = ms_biff_collate_block (bq) ;
  /* dump_biff (bq) ; */
  return (ans) ;
}

void
ms_biff_query_destroy (BIFF_QUERY *bq)
{
  if (bq)
    {
      if (bq->data_malloced)
	free (bq->data) ;
      free (bq) ;
    }
}
