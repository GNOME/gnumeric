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
#define GETguint8(p)  (*(p+0))
#define GETguint16(p) (*(p+0)+(*(p+1)<<8))
#define GETguint32(p) (*(p+0)+ \
		    (*(p+1)<<8)+ \
		    (*(p+2)<<16)+ \
		    (*(p+3)<<24))

#define MS_OLE_SPECIAL_BLOCK  0xfffffffd
#define MS_OLE_END_OF_CHAIN   0xfffffffe
#define MS_OLE_UNUSED_BLOCK   0xffffffff

#define MS_OLE_BB_BLOCK_SIZE     512
// N.B. First block is block '-1' :-)
#define MS_OLE_GET_BB_START_PTR(f,n) (f->mem+(n+1)*MS_OLE_BB_BLOCK_SIZE)

#define MS_OLE_BB_THRESHOLD   0x1000

// But small block numbers  are actualy offsets into a file
// of Big Blocks...
#define MS_OLE_SB_BLOCK_SIZE      64

#define MS_OLE_PPS_BLOCK_SIZE 0x80
#define MS_OLE_PPS_END_OF_CHAIN 0xffffffff

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
  MS_OLE_Header *h = &f->header ;
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
  MS_OLE_Header *header = &f->header ;

  // More magic numbers :-)
  header->number_of_bbd_blocks = GETguint32(ptr + 0x2c) ;
  header->root_startblock      = GETguint32(ptr + 0x30) ;
  header->sbd_startblock       = GETguint32(ptr + 0x3c) ;
  {
    int lp ;
    header->bbd_list             = g_new (BBPtr, header->number_of_bbd_blocks) ;
    for (lp=0;lp<header->number_of_bbd_blocks;lp++)
      header->bbd_list[lp]       = GETguint32(ptr + 0x4c + lp*4) ;
  }

  return 1 ;
}

// The BBD is very like a fat !
static BBPtr
nextBB (MS_OLE *f, BBPtr p)
{
  int bbdblock = ((p*sizeof(BBPtr))/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((p*sizeof(BBPtr))%MS_OLE_BB_BLOCK_SIZE) ;
  guint8 *ptr ;
  BBPtr ans ;
  //  printf ("nextBBD for %ld in bbd %d, idx %d guint8s in", p, bbdblock, idx) ;
  ptr = MS_OLE_GET_BB_START_PTR(f, f->header.bbd_list[bbdblock]) ;
  ans = GETguint32(ptr+idx) ;
  //  printf (" : is %ld\n", ans) ;
  return ans ;
}

// Dealing with small blocks

static SBPtr
nextSB (MS_OLE *f, SBPtr p)
{
  int sbdblock = ((p*sizeof(SBPtr))/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((p*sizeof(SBPtr))%MS_OLE_BB_BLOCK_SIZE) ;
  guint8 *ptr ;
  BBPtr ans ;
  //  printf ("nextSBD for %ld in sbd %d, idx %d guint8s in", p, sbdblock, idx) ;
  ptr = MS_OLE_GET_BB_START_PTR(f, f->header.sbd_list[sbdblock]) ;
  ans = GETguint32(ptr+idx) ;
  //  printf (" : is %ld\n", ans) ;
  return ans ;
}

static guint8 *
MS_OLE_sb_to_ptr (MS_OLE *f, SBPtr pt)
{
  int sbfblock = ((pt*MS_OLE_SB_BLOCK_SIZE)/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((pt*MS_OLE_SB_BLOCK_SIZE)%MS_OLE_BB_BLOCK_SIZE) ;
  guint8 *ptr, *ans ;
  //  printf ("nextSB for %ld in sbd %d, idx %d guint8s\n", pt, sbfblock, idx) ;
  ptr = MS_OLE_GET_BB_START_PTR(f, f->header.sbf_list[sbfblock]) ;
  ans = ptr+idx ;
  return ans ;
}

// Create a nice linear array and return count of the number in the array
static int
create_link_array(MS_OLE *f, BBPtr first, BBPtr **array)
{
  BBPtr ptr = first ;
  int lp, num=0 ;

  while (ptr != MS_OLE_END_OF_CHAIN)	// Find how many there are
    {
      //      printf ("BBPtr : %ld\n", ptr) ;
      ptr = nextBB (f, ptr) ;
      num++;
    }

  *array  = g_new (BBPtr, num) ;

  lp = 0 ;
  ptr = first ;
  while (ptr != MS_OLE_END_OF_CHAIN)
    {
      (*array)[lp++] = ptr ;
      ptr = nextBB (f, ptr) ;
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
  //  printf ("PPSPtr %ld -> block %d : BB root_list[%d] = %ld & idx = %d\n", ptr, ppsblock, ppsblock,
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

  me = g_new (MS_OLE_PPS, 1) ;
  mem   = PPSPtr_to_guint8(f, ptr) ;
  me->pps_me         = ptr ;

  if ((name_length = GETguint16(mem + 0x40)) == 0)
    {
      printf ("Duff zero length filename\n") ;
      free (me) ;
      return NULL ;
    }

  type               = GETguint8(mem + 0x42) ;
  me->pps_prev       = GETguint32(mem + 0x44) ;
  me->pps_next       = GETguint32(mem + 0x48) ;
  me->pps_dir        = GETguint32(mem + 0x4c) ;
  me->pps_startblock = GETguint32(mem + 0x74) ;
  me->pps_size       = GETguint32(mem + 0x78) ;
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
  ptr->mem = mmap (0, ptr->length, PROT_READ, MAP_PRIVATE, file, 0) ;
  close (file) ;

  if (GETguint32(ptr->mem    ) != 0xe011cfd0 ||
      GETguint32(ptr->mem + 4) != 0xe11ab1a1)
    {
      printf ("Failed magic number %x %x",
	      GETguint32(ptr->mem), GETguint32(ptr->mem+4)) ;
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

// This copies files out to 
static void
dump_files(MS_OLE *f)
{
  MS_OLE_PPS *ptr = f->root ;
  while (ptr)
    {
      printf ("Dealing with %d: size %d block %d, dir %d, prev %d, next %d\n", (int)ptr->pps_me,
	      ptr->pps_size, (int)ptr->pps_startblock, (int)ptr->pps_dir, (int)ptr->pps_prev, (int)ptr->pps_next) ;
      if (ptr->pps_type == MS_OLE_PPS_STREAM)
	{
	  if (ptr->pps_size>=MS_OLE_BB_THRESHOLD)
	    {
	      BBPtr block = ptr->pps_startblock ;
	      FILE *foo ;
	      int len = ptr->pps_size ;
	      char name[4096] ;
	      printf ("Big Blocks -- I can dump this then:\n") ;
	      sprintf (name, "analyze/mtest.%d", (int)ptr->pps_me) ;
	      foo = fopen (name, "wb+") ;
	      if (!foo) return ;
	      while (block != MS_OLE_END_OF_CHAIN)
		{
		  guint8 *pt = MS_OLE_GET_BB_START_PTR(f,block) ;
		  //		  dump (pt, MS_OLE_BB_BLOCK_SIZE) ;
		  fwrite (pt, 1, (len>MS_OLE_BB_BLOCK_SIZE)?MS_OLE_BB_BLOCK_SIZE:len, foo) ;
		  len-=MS_OLE_BB_BLOCK_SIZE ;
		  block = nextBB(f, block) ;
		}
	      fclose (foo) ;
	    }
	  else
	    {
	      FILE *foo ;
	      int len = ptr->pps_size ;
	      char name[4096] ;
	      SBPtr block = ptr->pps_startblock ;
	      printf ("Small Blocks -- I can dump this then:\n") ;
	      sprintf (name, "analyze/mtest.%d", (int)ptr->pps_me) ;
	      foo = fopen (name, "wb+") ;
	      if (!foo) return ;
	      while (block != MS_OLE_END_OF_CHAIN)
		{
		  guint8 *pt = MS_OLE_sb_to_ptr(f, block) ;
		  dump (pt, MS_OLE_SB_BLOCK_SIZE) ;
		  fwrite (pt, 1, (len>MS_OLE_SB_BLOCK_SIZE)?MS_OLE_SB_BLOCK_SIZE:len, foo) ;
		  len-=MS_OLE_SB_BLOCK_SIZE ;
		  block = nextSB(f, block) ;
		}
	      fclose (foo) ;
	      printf ("Small blocks\n") ;
	    }
	}
      ptr = ptr->next ;
    }
}

//--------------- BIFF --------------

static guint8 *
Block_to_mem (MS_OLE *f, guint32 block, int small_blocks)
{
  return small_blocks?MS_OLE_sb_to_ptr(f, block):MS_OLE_GET_BB_START_PTR(f,block) ;
}

static void
dump_stream (MS_OLE_STREAM *p)
{
  printf ("block %d, small? %d, block_left %d,length_left %d\n",
	  p->block, p->small_block, p->block_left,p->length_left) ;
}

static void
dump_biff (BIFF_QUERY *bq)
{
  printf ("Opcode %d length %d malloced? %d\nData:\n", bq->opcode, bq->length, bq->data_malloced) ;
  dump (bq->data, bq->length) ;
  dump_stream (bq->pos) ;
}

MS_OLE_STREAM *
ms_ole_stream_open (MS_OLE *f, char *name, char mode)
{
  MS_OLE_PPS *p=f->root ;

  g_assert (mode == 'r') ;
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
		{
		  MS_OLE_STREAM *ans    = g_new (MS_OLE_STREAM, 1) ;
		  ans->f = f ;
		  ans->p = p ;
		  ans->block       = p->pps_startblock ;
		  ans->small_block = p->pps_size<MS_OLE_BB_THRESHOLD ;
		  ans->length_left = p->pps_size ;
		  ans->mem         = Block_to_mem (f, ans->block, ans->small_block) ;
		  ans->block_left  = ans->small_block?MS_OLE_SB_BLOCK_SIZE:MS_OLE_BB_BLOCK_SIZE ;
		  return ans ;
		}
	      lp++ ;
	    }
	}
    }
  printf ("No file '%s' found\n", name) ;
  return 0;
}

void
ms_ole_stream_close (MS_OLE_STREAM *st)
{
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

static guint8
ms_ole_next_stream_byte (MS_OLE_STREAM *p)
{
  if (p->block_left>0)
    {
      p->block_left-- ;
      p->length_left-- ;
      return *(p->mem++) ;
    }
  else if (p->length_left>0)
    {
      p->block      = p->small_block?nextSB(p->f, p->block):nextBB(p->f, p->block) ;
      p->block_left = p->small_block?MS_OLE_SB_BLOCK_SIZE:MS_OLE_BB_BLOCK_SIZE ;
      p->mem        = Block_to_mem (p->f, p->block, p->small_block) ;
      p->block_left-- ;
      p->length_left-- ;
      return *(p->mem++) ;
    }
  else
    printf ("TRYING TO READ PAST EOF\n") ;
  return 0 ;
}

// Organise & if neccessary copy the blocks...
static int
ms_biff_collate_block (BIFF_QUERY *bq)
{
  MS_OLE_STREAM *p = bq->pos ;
  
  if (p->length_left==0)
    {
      printf ("LL=0 error\n") ;
      return 0 ;
    }
  if (bq->length<=p->block_left) // Just return the pointer then
    {
      bq->data = p->mem ;
      p->mem+=bq->length ;
      p->block_left-= bq->length ;
      p->length_left-= bq->length ;
    }
  else if (bq->length<=p->length_left) // Copying possibly required
    {
      guint8 *ptr     = g_new (guint8, bq->length) ;
      int  len      = bq->length ;

      // First see if all the blocks are contiguous ?
      if (bq->length>64) // Serious time penalty here ?
      {
	guint32 curblk, newblk ;
	int  ln=len ;
	int  blklen=p->block_left ;
	int  contig = 1 ;

	curblk = p->block ;
	while (ln>blklen && contig)
	  {
	    //	    printf ("Length : %d\n", ln) ;
	    ln-=blklen ;
	    blklen = p->small_block?MS_OLE_SB_BLOCK_SIZE:MS_OLE_BB_BLOCK_SIZE ;
	    newblk = p->small_block?nextSB(p->f, curblk):nextBB(p->f, curblk) ;
	    if (newblk != curblk+1)
	      contig = 0 ;
	    curblk = newblk ;
	    if (curblk == MS_OLE_END_OF_CHAIN)
	      {
		printf ("End of chain error\n") ;
		return 0 ;
	      }
	  }
	if (contig)
	  {
	    //	    printf ("Contiguous : Straight map\n") ;
	    bq->data = p->mem ; // Straigt map
	    free (ptr) ;
	    p->block_left = blklen-ln ;
	    p->length_left-= len ;
	    p->mem+= len ;
	    p->block = curblk ;
	    return 1 ;
	  }
      }

      bq->data = ptr ;
      bq->data_malloced = 1 ;
      while (len>0)
	{
	  int cpylen = len>p->block_left?p->block_left:len ;
	  memcpy (ptr, p->mem, cpylen) ;
	  ptr           += cpylen ;
	  len           -= cpylen ;
	  p->block_left -= cpylen ;
	  p->length_left-= cpylen ;
	  if (p->block_left==0)
	    {
	      p->block      = p->small_block?nextSB(p->f, p->block):nextBB(p->f, p->block) ;
	      p->block_left = p->small_block?MS_OLE_SB_BLOCK_SIZE:MS_OLE_BB_BLOCK_SIZE ;
	      p->mem        = Block_to_mem (p->f, p->block, p->small_block) ;
	    }
	  else
	    p->mem      += cpylen ;
	}
    }
  else
    printf ("TRYING TO READ BLOCK PAST EOF\n") ;
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
  bq->length        = 0 ;
  bq->data_malloced = 0 ;
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

/* Returns 0 if has hit end */
int
ms_biff_query_next (BIFF_QUERY *bq)
{
  int ans ;

  if (!bq || bq->pos->length_left < 4) // The end has come : 2 Peter 3:10
      return 0 ;
  if (bq->data_malloced)
    {
      bq->data_malloced = 0 ;
      free (bq->data) ;
    }

  bq->streamPos = (guint32)(bq->pos->p->pps_size - bq->pos->length_left) ;
  bq->opcode = ms_ole_next_stream_byte (bq->pos) + (ms_ole_next_stream_byte (bq->pos)<<8) ;
  bq->length = ms_ole_next_stream_byte (bq->pos) + (ms_ole_next_stream_byte (bq->pos)<<8) ;
  bq->ms_op  = (bq->opcode>>8);
  bq->ls_op  = (bq->opcode&0xff);

  if (!bq->length)
    {
      bq->data = 0 ;
      return 1 ;
    }
  ans = ms_biff_collate_block (bq) ;
  //  dump_biff (bq) ;
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
