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

#include "ms-ole.h"
#include "ms-biff.h"

void dump (BYTE *ptr, int len)
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

MS_OLE_FILE *new_ms_ole_file (const char *name)
{
  struct stat st ;
  MS_OLE_FILE *ptr = (MS_OLE_FILE *)malloc (sizeof(MS_OLE_FILE)) ;

  ptr->fd = open (name, O_RDONLY) ;
  if (!ptr->fd || fstat(ptr->fd, &st))
    {
      printf ("No such file\n") ;
      return 0 ;
    }
  ptr->length = st.st_size ;
  ptr->mem = mmap (0, ptr->length, PROT_READ, MAP_PRIVATE, ptr->fd, 0) ;

  if (GETLONG(ptr->mem    ) != 0xe011cfd0 ||
      GETLONG(ptr->mem + 4) != 0xe11ab1a1)
    {
      printf ("Failed magic number %x %x",
	      GETLONG(ptr->mem), GETLONG(ptr->mem+4)) ; return 0 ;
      free_ms_ole_file(ptr) ;
      return 0 ;
    }
  return ptr ;
}

void free_ms_ole_file (MS_OLE_FILE *ptr)
{
  munmap (ptr->mem, ptr->length) ;
  close (ptr->fd) ;
}

// Blocking functions

static void dump_header (MS_OLE_FILE *f)
{
  long lp ;
  MS_OLE_Header *h = &f->header ;
  printf ("--------------------------MS_OLE HEADER-------------------------\n") ;
  printf ("Num BBD Blocks : %ld Root %ld, SBD %ld\n",
	  h->number_of_bbd_blocks,
	  h->root_startblock,
	  h->sbd_startblock) ;
  for (lp=0;lp<h->number_of_bbd_blocks;lp++)
    printf ("bbd_list[%ld] = %ld\n", lp, h->bbd_list[lp]) ;

  printf ("Root blocks : %ld\n", h->number_of_root_blocks) ;
  for (lp=0;lp<h->number_of_root_blocks;lp++)
    printf ("root_list[%ld] = %ld\n", lp, h->root_list[lp]) ;

  printf ("sbd blocks : %ld\n", h->number_of_sbd_blocks) ;
  for (lp=0;lp<h->number_of_sbd_blocks;lp++)
    printf ("sbd_list[%ld] = %ld\n", lp, h->sbd_list[lp]) ;
  printf ("-------------------------------------------------------------\n") ;
}

static int read_header (MS_OLE_FILE *f)
{
  BYTE *ptr = f->mem ;
  MS_OLE_Header *header = &f->header ;

  // More magic numbers :-)
  header->number_of_bbd_blocks = GETLONG(ptr + 0x2c) ;
  header->root_startblock      = GETLONG(ptr + 0x30) ;
  header->sbd_startblock       = GETLONG(ptr + 0x3c) ;
  {
    int lp ;
    header->bbd_list             = (BBPtr *)malloc (sizeof(LONG) * header->number_of_bbd_blocks) ;
    for (lp=0;lp<header->number_of_bbd_blocks;lp++)
      header->bbd_list[lp]       = GETLONG(ptr + 0x4c + lp*4) ;
  }

  return 1 ;
}

// The BBD is very like a fat !
static BBPtr nextBB (MS_OLE_FILE *f, BBPtr p)
{
  int bbdblock = ((p*sizeof(BBPtr))/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((p*sizeof(BBPtr))%MS_OLE_BB_BLOCK_SIZE) ;
  BYTE *ptr ;
  BBPtr ans ;
  //  printf ("nextBBD for %ld in bbd %d, idx %d bytes in", p, bbdblock, idx) ;
  ptr = MS_OLE_GET_BB_START_PTR(f, f->header.bbd_list[bbdblock]) ;
  ans = GETLONG(ptr+idx) ;
  //  printf (" : is %ld\n", ans) ;
  return ans ;
}

// Dealing with small blocks

static SBPtr nextSB (MS_OLE_FILE *f, SBPtr p)
{
  int sbdblock = ((p*sizeof(SBPtr))/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((p*sizeof(SBPtr))%MS_OLE_BB_BLOCK_SIZE) ;
  BYTE *ptr ;
  BBPtr ans ;
  //  printf ("nextSBD for %ld in sbd %d, idx %d bytes in", p, sbdblock, idx) ;
  ptr = MS_OLE_GET_BB_START_PTR(f, f->header.sbd_list[sbdblock]) ;
  ans = GETLONG(ptr+idx) ;
  //  printf (" : is %ld\n", ans) ;
  return ans ;
}

static BYTE *MS_OLE_sb_to_ptr (MS_OLE_FILE *f, SBPtr pt)
{
  int sbfblock = ((pt*MS_OLE_SB_BLOCK_SIZE)/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((pt*MS_OLE_SB_BLOCK_SIZE)%MS_OLE_BB_BLOCK_SIZE) ;
  BYTE *ptr, *ans ;
  //  printf ("nextSB for %ld in sbd %d, idx %d bytes\n", pt, sbfblock, idx) ;
  ptr = MS_OLE_GET_BB_START_PTR(f, f->header.sbf_list[sbfblock]) ;
  ans = ptr+idx ;
  return ans ;
}

// Create a nice linear array and return count of the number in the array
static int create_link_array(MS_OLE_FILE *f, BBPtr first, BBPtr **array)
{
  BBPtr ptr = first ;
  int lp, num=0 ;

  while (ptr != MS_OLE_END_OF_CHAIN)	// Find how many there are
    {
      //      printf ("BBPtr : %ld\n", ptr) ;
      ptr = nextBB (f, ptr) ;
      num++;
    }

  *array  = (BBPtr *)malloc(sizeof(BBPtr) * num) ;

  lp = 0 ;
  ptr = first ;
  while (ptr != MS_OLE_END_OF_CHAIN)
    {
      *array[lp++] = ptr ;
      ptr = nextBB (f, ptr) ;
    }
  return num ;
}

static int create_root_list (MS_OLE_FILE *f)
{
  // Find blocks belonging to root ...
  f->header.number_of_root_blocks = create_link_array(f, f->header.root_startblock, &f->header.root_list) ;
  return (f->header.number_of_root_blocks!=0) ;
}

static int create_sbf_list (MS_OLE_FILE *f)
{
  // Find blocks containing all small blocks ...
  f->header.number_of_sbf_blocks = create_link_array(f, f->header.sbf_startblock, &f->header.sbf_list) ;
  return 1 ; //(f->header.number_of_sbf_blocks!=0) ;
}

static int create_sbd_list (MS_OLE_FILE *f)
{
  // Find blocks belonging to sbd ...
  f->header.number_of_sbd_blocks = create_link_array(f, f->header.sbd_startblock, &f->header.sbd_list) ;
  return 1 ; //(f->header.number_of_sbd_blocks!=0) ;
}

// Directory functions

static BYTE *PPSPtr_to_BYTE (MS_OLE_FILE *f, PPSPtr ptr)
{
  int ppsblock = ((ptr*MS_OLE_PPS_BLOCK_SIZE)/MS_OLE_BB_BLOCK_SIZE) ;
  int idx      = ((ptr*MS_OLE_PPS_BLOCK_SIZE)%MS_OLE_BB_BLOCK_SIZE) ;
  BYTE *ans ;
  assert (ppsblock<f->header.number_of_root_blocks) ;

  ans = MS_OLE_GET_BB_START_PTR(f,f->header.root_list[ppsblock]) + idx ;
  //  printf ("PPSPtr %ld -> block %d : BB root_list[%d] = %ld & idx = %d\n", ptr, ppsblock, ppsblock,
  //	  f->header.root_list[ppsblock], idx) ;
  return ans ;
}

// Is it in the directory ?
static MS_OLE_PPS *PPS_find (MS_OLE_FILE *f, PPSPtr pps_ptr)
{
  MS_OLE_PPS *me ;
  me = f->root ;
  while (me)
    {
      if (me->pps_me==pps_ptr) return me ;
      else me=me->next ;
    }
  printf ("cant find PPS %ld\n", pps_ptr) ;
  return 0 ;
}

static MS_OLE_PPS *PPS_read (MS_OLE_FILE *f, PPSPtr ptr)
{
  MS_OLE_PPS *me ;
  BYTE *mem ;
  int type, lp ;
  char PPS_TYPE_NAMES[3][24]={ "Storage ( directory )", "Stream ( file )", "Root dir"} ;

  // First see if we have him already ?
  if ((me=PPS_find(f, ptr)))
    return me ;

  printf ("PPS read : '%ld'\n", ptr) ;

  me = (MS_OLE_PPS *)malloc(sizeof(MS_OLE_PPS)) ;
  mem   = PPSPtr_to_BYTE(f, ptr) ;
  me->pps_me         = ptr ;

  // More Magic Numbers
  me->pps_sizeofname = GETWORD(mem + 0x40) ;
  if (me->pps_sizeofname == 0)	// Special case...
    {
      free (me) ;
      return NULL ;
    }
  type               = GETBYTE(mem + 0x42) ;
  me->pps_prev       = GETLONG(mem + 0x44) ;
  me->pps_next       = GETLONG(mem + 0x48) ;
  me->pps_dir        = GETLONG(mem + 0x4c) ;
  me->pps_startblock = GETLONG(mem + 0x74) ;
  me->pps_size       = GETLONG(mem + 0x78) ;
  if      (type == 1) me->pps_type = eStorage ;
  else if (type == 2) me->pps_type = eStream ;
  else if (type == 5) me->pps_type = eRoot ;
  else printf ("Unknown PPS type %d\n", type) ;

  me->pps_name = (WORD *)malloc(sizeof(WORD)*(me->pps_sizeofname+2)) ;
  printf ("Read pps field '") ;
  for (lp=0;lp<(me->pps_sizeofname/2);lp++)
    {
      me->pps_name[lp] = *(mem+lp*sizeof(WORD)) ;
      printf ("%c", (char)me->pps_name[lp]) ;
    }
  printf ("' : %s\n", PPS_TYPE_NAMES[me->pps_type]) ;
  me->pps_name[lp] = 0 ;

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

// Now enterpreting the PPS

// This copies files out to 
static void dump_files(MS_OLE_FILE *f)
{
  MS_OLE_PPS *ptr = f->root ;
  while (ptr)
    {
      printf ("Dealing with %ld: size %ld block %ld, dir %ld, prev %ld, next %ld\n", ptr->pps_me,
	      ptr->pps_size, ptr->pps_startblock, ptr->pps_dir, ptr->pps_prev, ptr->pps_next) ;
      if (ptr->pps_type == eStream)
	{
	  if (ptr->pps_size>=MS_OLE_BB_THRESHOLD)
	    {
	      BBPtr block = ptr->pps_startblock ;
	      FILE *foo ;
	      int len = ptr->pps_size ;
	      char name[4096] ;
	      printf ("Big Blocks -- I can dump this then:\n") ;
	      sprintf (name, "analyze/mtest.%ld", ptr->pps_me) ;
	      foo = fopen (name, "wb+") ;
	      if (!foo) return ;
	      while (block != MS_OLE_END_OF_CHAIN)
		{
		  BYTE *pt = MS_OLE_GET_BB_START_PTR(f,block) ;
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
	      sprintf (name, "analyze/mtest.%ld", ptr->pps_me) ;
	      foo = fopen (name, "wb+") ;
	      if (!foo) return ;
	      while (block != MS_OLE_END_OF_CHAIN)
		{
		  BYTE *pt = MS_OLE_sb_to_ptr(f, block) ;
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

int ms_ole_analyse_file (MS_OLE_FILE *f)
{
  assert (f->length>0x4c) ; // Bad show if not
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




//--------------- BIFF --------------

static BYTE *Block_to_mem (MS_OLE_FILE *f, LONG block, int small_blocks)
{
  return small_blocks?MS_OLE_sb_to_ptr(f, block):MS_OLE_GET_BB_START_PTR(f,block) ;
}

static void dump_stream (MS_OLE_STREAM_POS *p)
{
  printf ("block %ld, small? %d, block_left %d,length_left %d\n",
	  p->block, p->small_block, p->block_left,p->length_left) ;
}

static void dump_biff (BIFF_QUERY *bq)
{
  printf ("Opcode %d length %d malloced? %d\nData:\n", bq->opcode, bq->length, bq->data_malloced) ;
  dump (bq->data, bq->length) ;
  dump_stream (bq->pos) ;
}

MS_OLE_STREAM_POS *MS_OLE_open_stream (MS_OLE_FILE *f, MS_OLE_PPS *p)
{
  MS_OLE_STREAM_POS *ans    = (MS_OLE_STREAM_POS *)malloc(sizeof(MS_OLE_STREAM_POS)) ;
  ans->f = f ;
  ans->p = p ;
  ans->block       = p->pps_startblock ;
  ans->small_block = p->pps_size<MS_OLE_BB_THRESHOLD ;
  ans->length_left = p->pps_size ;
  ans->mem         = Block_to_mem (f, ans->block, ans->small_block) ;
  ans->block_left  = ans->small_block?MS_OLE_SB_BLOCK_SIZE:MS_OLE_BB_BLOCK_SIZE ;
  return ans ;
}

static BYTE MS_OLE_next_stream_byte (MS_OLE_STREAM_POS *p)
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
static int ms_biff_collate_block (BIFF_QUERY *bq)
{
  MS_OLE_STREAM_POS *p = bq->pos ;
  
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
      BYTE *ptr     = (BYTE *)malloc (bq->length*sizeof(BYTE)) ;
      int  len      = bq->length ;

      // First see if all the blocks are contiguous ?
      if (bq->length>64) // Serious time penalty here ?
      {
	LONG curblk, newblk ;
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

BIFF_QUERY *new_ms_biff_query_file (MS_OLE_FILE *ptr)
{
  BIFF_QUERY *bq    = (BIFF_QUERY *)malloc(sizeof(BIFF_QUERY)) ;
  bq->opcode        = 0 ;
  bq->length        = 0 ;
  bq->data_malloced = 0 ;

  { // Find the right Stream ... John 4:13-14
    MS_OLE_PPS *p  = ptr->root ;
    // The thing to seek; first the kingdom of God, then his:
    WORD seek[] = { 'B', 'O', 'O', 'K', 0 } ;
    int found = 0 ;
    while (!found && (p=p->next))
      {
	if (p->pps_type == eStream)
	  { // Compare name
	    int lp, lpc ;
	    lp = 0 ; lpc = 0 ;
	    while (p->pps_name[lp]!=0)
	      {
		// printf ("Does '%c' == '%c' ?\n", toupper(p->pps_name[lp]), seek[lpc]) ;
		if (toupper(p->pps_name[lp]) == seek[lpc])
		  lpc++ ;
		else
		  lpc = 0 ;
		if (seek[lpc] == 0)
		  {
		    found = 1 ;
		    break ;
		  }
		lp++ ;
	      }
	  }
      }
    if (found)
      {
	printf ("Found Excel Stream : %ld\n", p->pps_me) ;
	bq->pos = MS_OLE_open_stream (ptr, p) ;
      }
    else
      printf ("No Excel file found\n") ;
  }
  dump_biff(bq) ;
  return bq ;
}

BIFF_QUERY *new_ms_biff_query_here (MS_OLE_STREAM_POS *p)
{
  BIFF_QUERY *bq    = (BIFF_QUERY *)malloc(sizeof(BIFF_QUERY)) ;
  bq->opcode        = 0 ;
  bq->length        = 0 ;
  bq->data_malloced = 0 ;
  bq->pos           = p ;
  bq->data          = 0 ;
  return bq ;
}

BIFF_QUERY *copy_ms_biff_query (const BIFF_QUERY *p)
{
  BIFF_QUERY *bf = (BIFF_QUERY *)malloc(sizeof(BIFF_QUERY)) ;
  memcpy (bf, p, sizeof (BIFF_QUERY)) ;
  if (p->data_malloced)
    {
      bf->data = (BYTE *)malloc (p->length) ;
      memcpy (bf->data, p->data, p->length) ;
    }
  return bf ;
}

// Returns 0 if has hit end
int ms_next_biff (BIFF_QUERY *bq)
{
  int ans ;

  if (bq->pos->length_left < 4) // The end has come : 2 Peter 3:10
      return 0 ;
  if (bq->data_malloced)
    {
      bq->data_malloced = 0 ;
      free (bq->data) ;
    }

  bq->streamPos = (LONG)(bq->pos->p->pps_size - bq->pos->length_left) ;
  bq->opcode = MS_OLE_next_stream_byte (bq->pos) + (MS_OLE_next_stream_byte (bq->pos)<<8) ;
  bq->length = MS_OLE_next_stream_byte (bq->pos) + (MS_OLE_next_stream_byte (bq->pos)<<8) ;
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

void free_ms_biff_query (BIFF_QUERY *bq)
{
  if (bq->data_malloced)
    free (bq->data) ;
  free (bq) ;
}



