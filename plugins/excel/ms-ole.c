/*
 * ms-ole.c: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
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

#define MS_OLE_SPECIAL_BLOCK  0xfffffffd
#define MS_OLE_END_OF_CHAIN   0xfffffffe
#define MS_OLE_UNUSED_BLOCK   0xffffffff

#define MS_OLE_BB_BLOCK_SIZE     512
#define MS_OLE_SB_BLOCK_SIZE      64

/**
 * These look _ugly_ but reduce to a few shifts, bit masks and adds
 * Under optimisation these are _really_ quick !
 * NB. Parameters are always: 'MS_OLE *', followed by a guint32 block.
 **/

/* Find the position of the start in memory of a big block   */
#define MS_OLE_GET_BB_START_PTR(f,n) ((guint8 *)(f->mem+(n+1)*MS_OLE_BB_BLOCK_SIZE))
/* Find the position of the start in memory of a small block */
#define MS_OLE_GET_SB_START_PTR(f,n) ( MS_OLE_GET_BB_START_PTR((f), (f)->header.sbf_list[((MS_OLE_SB_BLOCK_SIZE*(n))/MS_OLE_BB_BLOCK_SIZE)]) + \
				       (MS_OLE_SB_BLOCK_SIZE*(n)%MS_OLE_BB_BLOCK_SIZE) )


/* Gives the position in memory, as a GUINT8 *, of the BB entry ( in a chain ) */
#define GET_BB_CHAIN_PTR(f,n) (MS_OLE_GET_BB_START_PTR((f), (f)->header.bbd_list[(((n)*sizeof(BBPtr))/MS_OLE_BB_BLOCK_SIZE)]) + \
                              (((n)*sizeof(BBPtr))%MS_OLE_BB_BLOCK_SIZE))


/* Gives the position in memory, as a GUINT8 *, of the SB entry ( in a chain ) */
#define GET_SB_CHAIN_PTR(f,n) (MS_OLE_GET_BB_START_PTR(f, f->header.sbd_list[(((n)*sizeof(SBPtr))/MS_OLE_BB_BLOCK_SIZE)]) + \
                              (((n)*sizeof(SBPtr))%MS_OLE_BB_BLOCK_SIZE))

/* Returns the next BB after this one in the chain */
#define NEXT_BB(f,n) (GET_GUINT32(GET_BB_CHAIN_PTR(f,n)))
/* Returns the next SB after this one in the chain */
#define NEXT_SB(f,n) (GET_GUINT32(GET_SB_CHAIN_PTR(f,n)))

#define MS_OLE_BB_THRESHOLD   0x1000

#define MS_OLE_PPS_BLOCK_SIZE 0x80
#define MS_OLE_PPS_END_OF_CHAIN 0xffffffff

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
	  h->number_of_bbd_blocks,
	  (int)h->root_startblock,
	  (int)h->sbd_startblock) ;
  for (lp=0;lp<h->number_of_bbd_blocks;lp++)
    printf ("bbd_list[%d] = %d\n", lp, (int)h->bbd_list[lp]) ;

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
  guint8 *ptr = f->mem ;
  MS_OLE_HEADER *header = &f->header ;

  /* Magic numbers */
  header->number_of_bbd_blocks = GET_GUINT32(ptr + 0x2c) ;
  header->root_startblock      = GET_GUINT32(ptr + 0x30) ;
  header->sbd_startblock       = GET_GUINT32(ptr + 0x3c) ;
  /* So: the Big Block Description (bbd) is read, it is a chain of BBs containing
   effectively a FAT of chains of other BBs, so the theoretical max size = 128 BB Fat blocks
   Thus = 128*512*512/4 blocks ~= 8.4MBytes */
  {
    int lp ;
    header->bbd_list             = g_new (BBPtr, header->number_of_bbd_blocks) ;
    for (lp=0;lp<header->number_of_bbd_blocks;lp++)
      header->bbd_list[lp]       = GET_GUINT32(ptr + 0x4c + lp*4) ;
  }
  return 1 ;
}

static void
dump_allocation (MS_OLE *f)
{
  int blk, dep ;
  printf ("Big block allocation\n") ;

  dep = 0 ;
  while (dep<f->header.number_of_bbd_blocks)
    {
      printf ("FAT block %d\n", dep) ;
      for (blk=0;blk<MS_OLE_BB_BLOCK_SIZE/sizeof(BBPtr);blk++)
	{
	  guint32 type ;
	  type = GET_GUINT32(MS_OLE_GET_BB_START_PTR(f, f->header.bbd_list[dep]) + blk*sizeof(BBPtr)) ;
	  if ((blk + dep*(MS_OLE_BB_BLOCK_SIZE/sizeof(BBPtr)))*MS_OLE_BB_BLOCK_SIZE > f->length)
	    printf ("*") ;
	  else if (type == MS_OLE_SPECIAL_BLOCK)
	    printf ("S") ;
	  else if (type == MS_OLE_UNUSED_BLOCK)
	    printf (".") ;
	  else if (type == MS_OLE_END_OF_CHAIN)
	    printf ("X") ;
	  else
	    printf ("O") ;
	  if (blk%16==15)
	    printf (" - %d\n", blk) ;
	}
      dep++ ;
    }

  printf ("Small block allocation\n") ;
  dep = 0 ;
  while (dep<f->header.number_of_sbd_blocks)
    {
      printf ("SB block %d ( = BB block %d )\n", dep, f->header.sbd_list[dep]) ;
      for (blk=0;blk<MS_OLE_BB_BLOCK_SIZE/sizeof(SBPtr);blk++)
	{
	  guint32 type ;
	  type = GET_GUINT32(MS_OLE_GET_BB_START_PTR(f, f->header.sbd_list[dep]) + blk*sizeof(SBPtr)) ;
	  if (type == MS_OLE_SPECIAL_BLOCK)
	    printf ("S") ;
	  else if (type == MS_OLE_UNUSED_BLOCK)
	    printf (".") ;
	  else if (type == MS_OLE_END_OF_CHAIN)
	    printf ("X") ;
	  else
	    printf ("O") ;
	  if (blk%16==15)
	    printf (" - %d\n", blk) ;
	}
      dep++ ;
    }
}

/* Create a nice linear array and return count of the number in the array */
static int
create_link_array(MS_OLE *f, BBPtr first, BBPtr **array)
{
  BBPtr ptr = first ;
  int lp, num=0 ;

  while (ptr != MS_OLE_END_OF_CHAIN)	// Find how many there are
    {
      //      printf ("BBPtr : %d\n", ptr) ;
      ptr = NEXT_BB (f, ptr) ;
      num++;
    }

  if (num == 0)
    *array = 0 ;
  else
    *array  = g_new (BBPtr, num) ;

  lp = 0 ;
  ptr = first ;
  while (ptr != MS_OLE_END_OF_CHAIN)
    {
      (*array)[lp++] = ptr ;
      ptr = NEXT_BB (f, ptr) ;
    }
  return num ;
}

static int
create_root_list (MS_OLE *f)
{
  // Find blocks beguint32ing to root ...
  f->header.number_of_root_blocks = create_link_array(f, f->header.root_startblock, &f->header.root_list) ;
  return (f->header.number_of_root_blocks!=0) ;
}

static int
create_sbf_list (MS_OLE *f)
{
  // Find blocks containing all small blocks ...
  f->header.number_of_sbf_blocks = create_link_array(f, f->header.sbf_startblock, &f->header.sbf_list) ;
  return 1 ; //(f->header.number_of_sbf_blocks!=0) ;
}

static int
create_sbd_list (MS_OLE *f)
{
  // Find blocks beguint32ing to sbd ...
  f->header.number_of_sbd_blocks = create_link_array(f, f->header.sbd_startblock, &f->header.sbd_list) ;
  return 1 ; //(f->header.number_of_sbd_blocks!=0) ;
}

// Directory functions

static guint8 *
PPSPtr_to_guint8 (MS_OLE *f, PPSPtr ptr)
{
  int ppsblock = ((ptr*MS_OLE_PPS_BLOCK_SIZE)/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((ptr*MS_OLE_PPS_BLOCK_SIZE)%MS_OLE_BB_BLOCK_SIZE) ;
  guint8 *ans ;
  assert (ppsblock<f->header.number_of_root_blocks) ;

  ans = MS_OLE_GET_BB_START_PTR(f,f->header.root_list[ppsblock]) + idx ;
  //  printf ("PPSPtr %d -> block %d : BB root_list[%d] = %d & idx = %d\n", ptr, ppsblock, ppsblock,
  //	  f->header.root_list[ppsblock], idx) ;
  return ans ;
}

// Is it in the directory ?
static MS_OLE_PPS *
PPS_find (MS_OLE *f, PPSPtr pps_ptr)
{
  MS_OLE_PPS *me ;
  me = f->root ;
  while (me)
    {
      if (me->pps_me==pps_ptr) return me ;
      else me=me->next ;
    }
  printf ("cant find PPS %d\n", pps_ptr) ;
  return 0 ;
}

static MS_OLE_PPS *
PPS_read (MS_OLE *f, PPSPtr ptr)
{
  MS_OLE_PPS *me ;
  guint8 *mem ;
  int type, lp ;
  int name_length ;
  char PPS_TYPE_NAMES[3][24]={ "Storage ( directory )", "Stream ( file )", "Root dir"} ;

  // First see if we have him already ?
  if ((me=PPS_find(f, ptr)))
    return me ;

  printf ("PPS read : '%d'\n", (int)ptr) ;

  me         = g_new (MS_OLE_PPS, 1) ;
  mem        = PPSPtr_to_guint8(f, ptr) ;
  me->pps_me = ptr ;

  if ((name_length = GET_GUINT16(mem + 0x40)) == 0)
    {
      printf ("Duff zero length filename\n") ;
      free (me) ;
      return NULL ;
    }

  type               = GET_GUINT8(mem + 0x42) ;
  me->pps_prev       = GET_GUINT32(mem + 0x44) ;
  me->pps_next       = GET_GUINT32(mem + 0x48) ;
  me->pps_dir        = GET_GUINT32(mem + 0x4c) ;
  me->pps_startblock = GET_GUINT32(mem + 0x74) ;
  me->pps_size       = GET_GUINT32(mem + 0x78) ;
  if      (type == 1) me->pps_type = MS_OLE_PPS_STORAGE ;
  else if (type == 2) me->pps_type = MS_OLE_PPS_STREAM ;
  else if (type == 5) me->pps_type = MS_OLE_PPS_ROOT ;
  else printf ("Unknown PPS type %d\n", type) ;

  /*  dump (mem, name_length*2) ; */
  me->pps_name = pps_get_text (mem, name_length) ;

  if (f->root == NULL)
    {
      f->root = me ;
      f->end  = me ;
    }
  else
    {
      f->end->next = me ;
      f->end = me ;
    }  
  me->next = NULL ;

  if (me->pps_next != MS_OLE_PPS_END_OF_CHAIN)
    PPS_read (f, me->pps_next) ;
  if (me->pps_prev != MS_OLE_PPS_END_OF_CHAIN)
    PPS_read (f, me->pps_prev) ;
  if (me->pps_dir != MS_OLE_PPS_END_OF_CHAIN)
    PPS_read (f, me->pps_dir) ;

  /* Find the last block */
  if (me->pps_type == MS_OLE_PPS_STREAM)
    {
      int lp ;
      if (me->pps_size>=MS_OLE_BB_THRESHOLD)
	{
	  BBPtr p = me->pps_startblock ;
	  for (lp=0;lp<me->pps_size/MS_OLE_BB_BLOCK_SIZE;lp++)
	    {
	      if (p == MS_OLE_END_OF_CHAIN)
		printf ("Warning: bad file length in '%s'\n", me->pps_name) ;
	      else if (p == MS_OLE_SPECIAL_BLOCK)
		printf ("Warning: special block in '%s'\n", me->pps_name) ;
	      else if (p == MS_OLE_UNUSED_BLOCK)
		printf ("Warning: unused block in '%s'\n", me->pps_name) ;
	      else
		p = NEXT_BB(f, p) ;
	    }
	  me->end_block = p ;
	  /* These will eventually need freeing */
	  if (p != MS_OLE_END_OF_CHAIN && NEXT_BB(f, p) != MS_OLE_END_OF_CHAIN)
	    printf ("FIXME: Extra useless blocks on end of '%s'\n", me->pps_name) ;
	}
      else
	{
	  SBPtr p = me->pps_startblock ;
	  for (lp=0;lp<me->pps_size/MS_OLE_SB_BLOCK_SIZE;lp++)
	    {
	      if (p == MS_OLE_END_OF_CHAIN)
		printf ("Warning: bad file length in '%s'\n", me->pps_name) ;
	      else if (p == MS_OLE_SPECIAL_BLOCK)
		printf ("Warning: special block in '%s'\n", me->pps_name) ;
	      else if (p == MS_OLE_UNUSED_BLOCK)
		printf ("Warning: unused block in '%s'\n", me->pps_name) ;
	      else
		p = NEXT_SB(f, p) ;
	    }
	  me->end_block = p ;
	  if (p != MS_OLE_END_OF_CHAIN && NEXT_SB(f, p) != MS_OLE_END_OF_CHAIN)
	    printf ("FIXME: Extra useless blocks on end of '%s'\n", me->pps_name) ;
	}
    }
  else
    me->end_block = MS_OLE_END_OF_CHAIN ;
  return me ;
}

static int
ms_ole_analyse (MS_OLE *f)
{
  if (!read_header(f)) return 0 ;
  if (!create_root_list(f)) return 0 ;
  if (!create_sbd_list(f)) return 0 ;
  f->root = f->end = NULL ;
  PPS_read(f, 0) ;
  f->header.sbf_startblock = f->root->pps_startblock ;
  if (!create_sbf_list(f)) return 0 ;
  dump_header(f) ;
  //  dump_files(f) ;
  dump_allocation (f) ;
  return 1 ;
}

MS_OLE *
ms_ole_new (const char *name)
{
  struct stat st ;
  int file ;
  MS_OLE *ptr ;

  ptr = g_new (MS_OLE, 1) ;

  file = open (name, O_RDONLY) ;
  if (!file || fstat(file, &st))
    {
      printf ("No such file '%s'\n", name) ;
      return 0 ;
    }
  ptr->length = st.st_size ;
  if (ptr->length<=0x4c)  /* Bad show */
    {
      printf ("File '%s' too short\n", name) ;
      return 0 ;
    }
  if (ptr->length%MS_OLE_BB_BLOCK_SIZE)
    printf ("Warning file '%s':%d non-integer number of blocks\n", name, ptr->length) ;

  ptr->mem = mmap (0, ptr->length, PROT_READ, MAP_PRIVATE, file, 0) ;
  close (file) ;

  if (GET_GUINT32(ptr->mem    ) != 0xe011cfd0 ||
      GET_GUINT32(ptr->mem + 4) != 0xe11ab1a1)
    {
      printf ("Failed magic number %x %x",
	      GET_GUINT32(ptr->mem), GET_GUINT32(ptr->mem+4)) ;
      ms_ole_destroy (ptr) ;
      return 0 ;
    }
  if (!ms_ole_analyse (ptr))
    {
      printf ("Directory error\n") ;
      ms_ole_destroy(ptr) ;
      return 0 ;
    }
  return ptr ;
}

void
ms_ole_destroy (MS_OLE *ptr)
{
  munmap (ptr->mem, ptr->length) ;
  free (ptr) ;
}

static void
dump_stream (MS_OLE_STREAM *s)
{
  if (s->pps->pps_size>=MS_OLE_BB_THRESHOLD)
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
  if (s->block == s->pps->end_block)
    {
      block_left = s->pps->pps_size % MS_OLE_BB_BLOCK_SIZE - s->offset ;
      if (length>block_left)
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
    }
  
  block_left = MS_OLE_BB_BLOCK_SIZE - s->offset ;
  if (length<=block_left) /* Just return the pointer then */
    return (MS_OLE_GET_BB_START_PTR(s->file, s->block) + s->offset) ;
  
  /* Is it contiguous ? */
  {
    guint32 curblk, newblk ;
    int  ln     = length ;
    int  blklen = block_left ;
    int  contig = 1 ;
    
    curblk = s->block ;
    if (curblk == MS_OLE_END_OF_CHAIN)
      {
	printf ("Trying to read beyond end of stream\n") ;
	return 0 ;
      }
    
    while (ln>blklen && contig)
      {
	ln-=blklen ;
	blklen = MS_OLE_BB_BLOCK_SIZE ;
	newblk = NEXT_BB(s->file, curblk) ;
	if (newblk != curblk+1)
	  return 0 ;
	curblk = newblk ;
	if (curblk == MS_OLE_END_OF_CHAIN)
	  {
	    printf ("End of chain error\n") ;
	    return 0 ;
	  }
      }
    /* Straight map, simply return a pointer */
    return MS_OLE_GET_BB_START_PTR(s->file, s->block) + s->offset ;
  }
}

static guint8*
ms_ole_read_ptr_sb (MS_OLE_STREAM *s, guint32 length)
{
  int block_left ;
  if (s->block == s->pps->end_block)
    {
      block_left = s->pps->pps_size % MS_OLE_SB_BLOCK_SIZE - s->offset ;
      if (length>block_left)
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
    }
  
  block_left = MS_OLE_SB_BLOCK_SIZE - s->offset ;
  if (length<=block_left) /* Just return the pointer then */
    return (MS_OLE_GET_SB_START_PTR(s->file, s->block) + s->offset) ;
  
  /* Is it contiguous ? */
  {
    guint32 curblk, newblk ;
    int  ln     = length ;
    int  blklen = block_left ;
    int  contig = 1 ;
    
    curblk = s->block ;
    if (curblk == MS_OLE_END_OF_CHAIN)
      {
	printf ("Trying to read beyond end of stream\n") ;
	return 0 ;
      }
    
    while (ln>blklen && contig)
      {
	ln-=blklen ;
	blklen = MS_OLE_SB_BLOCK_SIZE ;
	newblk = NEXT_SB(s->file, curblk) ;
	if (newblk != curblk+1)
	  return 0 ;
	curblk = newblk ;
	if (curblk == MS_OLE_END_OF_CHAIN)
	  {
	    printf ("End of chain error\n") ;
	    return 0 ;
	  }
      }
    /* Straight map, simply return a pointer */
    return MS_OLE_GET_SB_START_PTR(s->file, s->block) + s->offset ;
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
  if (s->block == s->pps->end_block)
    {
      block_left = s->pps->pps_size % MS_OLE_BB_BLOCK_SIZE - s->offset ;
      if (length>block_left) /* Just return the pointer then */
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
      memcpy (ptr, MS_OLE_GET_BB_START_PTR(s->file, s->block) + s->offset, length) ;
      return 1 ;
    }

  block_left = MS_OLE_BB_BLOCK_SIZE - s->offset ;

  /* Block by block copy */
  {
    int cpylen ;
    int offset = s->offset ;
    int block  = s->block ;
    int bytes  = length ;
    guint8 *src ;
    
    while (bytes>0)
      {
	int cpylen = MS_OLE_BB_BLOCK_SIZE - offset ;
	if (cpylen>bytes)
	  cpylen = bytes ;
	src = MS_OLE_GET_BB_START_PTR(s->file, block) + offset ;
	
	if (block == s->pps->end_block && cpylen > s->pps->pps_size%MS_OLE_BB_BLOCK_SIZE)
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
  if (s->block == s->pps->end_block)
    {
      block_left = s->pps->pps_size % MS_OLE_SB_BLOCK_SIZE - s->offset ;
      if (length>block_left)
	{
	  printf ("Requesting beyond end of stream by %d bytes\n",
		  length - block_left) ;
	  return 0 ;
	}
      memcpy (ptr, MS_OLE_GET_SB_START_PTR(s->file, s->block) + s->offset, length) ;
      return 1 ;
    }

  block_left = MS_OLE_SB_BLOCK_SIZE - s->offset ;

  /* Block by block copy */
  {
    int cpylen ;
    int offset = s->offset ;
    int block  = s->block ;
    int bytes  = length ;
    guint8 *src ;
    
    while (bytes>0)
      {
	int cpylen = MS_OLE_SB_BLOCK_SIZE - offset ;
	if (cpylen>bytes)
	  cpylen = bytes ;
	src = MS_OLE_GET_SB_START_PTR(s->file, block) + offset ;
	
	if (block == s->pps->end_block && cpylen > s->pps->pps_size%MS_OLE_SB_BLOCK_SIZE)
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
  int numblk = (bytes+s->offset)/MS_OLE_BB_BLOCK_SIZE ;
  g_assert (bytes>=0) ;
  s->offset = (s->offset+bytes)%MS_OLE_BB_BLOCK_SIZE ;
  while (s->block != MS_OLE_END_OF_CHAIN)
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
  int numblk = (bytes+s->offset)/MS_OLE_SB_BLOCK_SIZE ;
  g_assert (bytes>=0) ;
  s->offset = (s->offset+bytes)%MS_OLE_SB_BLOCK_SIZE ;
  while (s->block != MS_OLE_END_OF_CHAIN)
    if (numblk==0)
	return ;
    else
      {
	s->block = NEXT_SB(s->file, s->block) ;
	numblk -- ;
      }
}


static MS_OLE_PPS *
find_stream (MS_OLE *f, char *name)
{
  MS_OLE_PPS *p=f->root ;
  while ((p=p->next))
    {
      if (p->pps_type == MS_OLE_PPS_STREAM)
	{
	  int lp, lpc ;
	  lp = 0 ; lpc = 0 ;
	  while (p->pps_name[lp]!=0)
	    {
	      if (p->pps_name[lp] == name[lpc])
		lpc++ ;
	      else
		lpc = 0 ;
	      if (name[lpc] == 0)
		return p ;
	      lp++ ;
	    }
	}
    }
  printf ("No file '%s' found\n", name) ;
  return 0;
}


/**
 * Creates an extra block on the end of a BB file 
 **/
static void
ms_ole_addblock_bb (MS_OLE_STREAM *s)
{
  /*  guint32 lastblk = s->pps->end_block ;
      guint32 newblk  = next_free_block (s->file) ; */
}

static void
ms_ole_addblock_sb (MS_OLE_STREAM *s)
{
  printf ("Placeholder\n") ;
}

MS_OLE_STREAM *
ms_ole_stream_open (MS_OLE *f, char *name, char mode)
{
  MS_OLE_PPS *p=find_stream (f, name) ;
  MS_OLE_STREAM *ans ;
  g_assert (mode == 'r') ;

  if (!p)
    return 0 ;

  ans         = g_new (MS_OLE_STREAM, 1) ;
  ans->file   = f ;
  ans->pps    = p ;
  ans->block  = p->pps_startblock ;
  if (ans->block == MS_OLE_SPECIAL_BLOCK ||
      ans->block == MS_OLE_END_OF_CHAIN)
    {
      printf ("Bad file block record\n") ;
      free (ans) ;
      return 0 ;
    }
  ans->offset = 0 ;
  if (p->pps_size>=MS_OLE_BB_THRESHOLD)
    {
      ans->read_copy = ms_ole_read_copy_bb ;
      ans->read_ptr  = ms_ole_read_ptr_bb ;
      ans->advance   = ms_ole_advance_bb ;
      ans->addblock  = ms_ole_addblock_bb ;
    }
  else
    {
      ans->read_copy    = ms_ole_read_copy_sb ;
      ans->read_ptr     = ms_ole_read_ptr_sb ;
      ans->advance      = ms_ole_advance_sb ;
      ans->addblock     = ms_ole_addblock_sb ;
    }
  ans->write  = 0 ;
  return ans ;
}

void
ms_ole_stream_unlink (MS_OLE *f, char *name)
{
}

void
ms_ole_stream_close (MS_OLE_STREAM *st)
{
  free (st) ;
}

/* You probably arn't too interested in the root directory anyway
   but this is first */
MS_OLE_DIRECTORY *
ms_ole_directory_new (MS_OLE *f)
{
  MS_OLE_DIRECTORY *ans = g_new (MS_OLE_DIRECTORY, 1) ;
  ans->file     = f;
  ans->pps      = f->root ;
  ans->name     = f->root->pps_name ;
  ans->type     = 'r' ;
  ans->length   = 0 ;
  return ans ;
}

int
ms_ole_directory_next (MS_OLE_DIRECTORY *d)
{
  if (!d || !d->pps || !(d->pps=d->pps->next))
    return 0 ;

  d->name   = d->pps->pps_name ;
  d->type   = d->pps->pps_type ;
  d->length = d->pps->pps_size ;

  return 1 ;
}

void
ms_ole_directory_destroy (MS_OLE_DIRECTORY *d)
{
  if (d)
    free (d) ;
}

// Organise & if neccessary copy the blocks...
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

  if (!bq || bq->streamPos >= bq->pos->pps->pps_size)
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

