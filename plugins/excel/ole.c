/**
 * ole.c: OLE2 file format helper program,
 *        good for dumping OLE streams, and
 * corresponding biff records, and hopefuly
 * some more ...
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ms-ole.h"
#include "ms-biff.h"
#include "biff-types.h"

#define BIFF_TYPES_FILE    "biff-types.h"
#define ESCHER_TYPES_FILE  "escher-types.h"

char delim[]=" \t\n";

typedef struct {
	guint16 opcode;
	char *name;
} GENERIC_TYPE;

static GPtrArray *biff_types   = NULL;
static GPtrArray *escher_types = NULL;
typedef enum { eBiff=0, eEscher=1 } typeType;

static void dump_summary (MsOleStream *ole);

static void
read_types (char *fname, GPtrArray **types, typeType t)
{
	FILE *file = fopen(fname, "r");
	char buffer[1024];
	*types = g_ptr_array_new ();
	if (!file) {
		char *newname = g_strconcat ("../", fname, NULL);
		file = fopen (newname, "r");
	}
	if (!file) {
		printf ("Can't find vital file '%s'\n", fname);
		return;
	}
	while (!feof(file)) {
		char *p;
		fgets(buffer,1023,file);
		for (p=buffer;*p;p++)
			if (*p=='0' && *(p+1)=='x') {
				GENERIC_TYPE *bt = g_new (GENERIC_TYPE,1);
				char *name, *pt;
				bt->opcode=strtol(p+2,0,16);
				pt = buffer;
				while (*pt && *pt != '#') pt++;      /* # */
				while (*pt && !isspace(*pt)) pt++;  /* define */
				while (*pt &&  isspace(*pt)) pt++;  /* '   ' */
				if (t==eBiff) {
					while (*pt && *pt != '_') pt++;     /* BIFF_ */
					name = *pt?pt+1:pt;
				} else
					name = pt;
				while (*pt && !isspace(*pt)) pt++;
				bt->name=g_strndup(name, (pt-name));
				g_ptr_array_add (*types, bt);
				break;
			}
	}
	fclose (file);
}

static char*
get_biff_opcode_name (guint16 opcode)
{
	int lp;
	if (!biff_types)
		read_types (BIFF_TYPES_FILE, &biff_types, eBiff);
	/* Count backwars to give preference to non-filtered record types */
	for (lp=biff_types->len; --lp >= 0 ;) {
		GENERIC_TYPE *bt = g_ptr_array_index (biff_types, lp);
		if (bt->opcode>0xff) {
			if (bt->opcode == opcode)
				return bt->name;
		} else {
			if (bt->opcode == (opcode&0xff))
				return bt->name;
		}
	}
	return "Unknown";
}

static char*
get_escher_opcode_name (guint16 opcode)
{
	int lp;
	if (!escher_types)
		read_types (ESCHER_TYPES_FILE, &escher_types, eEscher);
	for (lp=0;lp<escher_types->len;lp++) {
		GENERIC_TYPE *bt = g_ptr_array_index (escher_types, lp);
		if (bt->opcode>0xff) {
			if (bt->opcode == opcode)
				return bt->name;
		} else {
			if (bt->opcode == (opcode&0xff))
				return bt->name;
		}
	}
	return "Unknown";
}

static MsOleDirectory *
get_file_handle (MsOle *ole, char *name)
{
	MsOleDirectory *dir;
	if (!name)
		return NULL;
	dir = ms_ole_directory_new (ole);
	while (ms_ole_directory_next(dir)) {
		if (!dir->name) {
			printf ("Odd: NULL dirctory name\n");
			continue;
		}
		if (g_strcasecmp(dir->name, name)==0) {
			return dir;
		}
	}	
	printf ("Stream '%s' not found\n", name);
	ms_ole_directory_destroy (dir);
	return NULL;
}

static void
list_files (MsOle *ole)
{
	MsOleDirectory *dir = ms_ole_directory_new (ole);
	g_assert (dir);
	while (ms_ole_directory_next(dir)) {
		printf ("'%25s' : type %d, length %d bytes\n", dir->name, dir->type, dir->length);
	}
}

static void
syntax_error(char *err)
{
	if (err) {
		printf("Error; '%s'\n",err);
		exit(1);
	}
		
	printf ("Sytax:\n");
	printf (" ole <ole-file> [-i] [commands...]\n\n");
	printf (" -i: Interactive, queries for fresh commands\n\n");
	printf ("command can be one or all of:\n");
	printf (" * ls:                   list files\n");
	printf (" * biff    <stream name>:   dump biff records, merging continues\n");
	printf (" * biffraw <stream name>:   dump biff records no merge + raw data\n");
	printf (" * draw    <stream name>:   dump drawing records\n");
	printf (" * dump    <stream name>:   dump stream\n");
	printf (" * summary <stream name>:   dump document summary info\n");
	printf (" * debug                :   dump internal ole library status\n");
	printf (" Raw transfer commands\n");
	printf (" * get     <stream name> <fname>\n");
	printf (" * put     <fname> <stream name>\n");
	printf (" * copyin  [<fname>,]...\n");
	printf (" * quit,exit,bye:        exit\n");
	exit(1);
}

/* ---------------------------- Start cut from ms-escher.c ---------------------------- */


typedef struct { /* See: S59FDA.HTM */
	guint    ver:4;
	guint    instance:12;
	guint16  type;   /* fbt */
	guint32  length;
	guint8  *data;
	guint32  length_left;
	gboolean first;
} ESH_HEADER;
#define ESH_HEADER_LEN 8

static ESH_HEADER *
esh_header_new (guint8 *data, gint32 length)
{
	ESH_HEADER *h = g_new (ESH_HEADER,1);
	h->length=0;
	h->type=0;
	h->instance=0;
	h->data=data;
	h->length_left=length;
	h->first = TRUE;
	return h;
}

static int
esh_header_next (ESH_HEADER *h)
{
	guint16 split;
	g_return_val_if_fail(h, 0);
	g_return_val_if_fail(h->data, 0);

	if (h->length_left < h->length + ESH_HEADER_LEN*2)
		return 0;

	if (h->first==TRUE)
		h->first = FALSE;
	else {
		h->data+=h->length+ESH_HEADER_LEN;
		h->length_left-=h->length+ESH_HEADER_LEN;
	}

	h->length   = MS_OLE_GET_GUINT32(h->data+4);
	h->type     = MS_OLE_GET_GUINT16(h->data+2);
	split       = MS_OLE_GET_GUINT16(h->data+0);
	h->ver      = (split&0x0f);
	h->instance = (split>>4);
#if ESH_HEADER_DEBUG > 0
	printf ("Next header length 0x%x(=%d), type 0x%x, ver 0x%x, instance 0x%x\n",
		h->length, h->length, h->type, h->ver, h->instance);
#endif
	return 1;
}

/* static ESH_HEADER *
esh_header_contained (ESH_HEADER *h)
{
	if (h->length_left<ESH_HEADER_LEN)
		return NULL;
	g_assert (h->data[h->length_left-1] == *//* Check that pointer *//*
		  h->data[h->length_left-1]);
	return esh_header_new (h->data+ESH_HEADER_LEN,
			       h->length-ESH_HEADER_LEN);
}*/

static void
esh_header_destroy (ESH_HEADER *h)
{
	if (h)
		g_free(h);
}
/**
 *  Builds a flat record by merging CONTINUE records,
 *  Have to do until we move this into ms_ole.c
 *  pass pointers to your length & data variables.
 *  This is dead sluggish.
 **/
static int
biff_to_flat_data (const BiffQuery *q, guint8 **data, guint32 *length)
{
	BiffQuery *nq = ms_biff_query_copy (q);
	guint8 *ptr;
	int cnt=0;

	*length=0;
	do {
		*length+=nq->length;
		ms_biff_query_next(nq);
		cnt++;
	} while (nq->opcode == BIFF_CONTINUE ||
		 nq->opcode == BIFF_MS_O_DRAWING ||
		 nq->opcode == BIFF_MS_O_DRAWING_GROUP);

	printf ("MERGING %d continues\n", cnt);
	(*data) = g_malloc (*length);
	ptr=(*data);
	nq = ms_biff_query_copy (q);
	do {
		memcpy (ptr, nq->data, nq->length);
		ptr+=nq->length;
		ms_biff_query_next(nq);
	} while (nq->opcode == BIFF_CONTINUE ||
		 nq->opcode == BIFF_MS_O_DRAWING ||
		 nq->opcode == BIFF_MS_O_DRAWING_GROUP);
	return cnt;
}

/* ---------------------------- End cut ---------------------------- */

static void
dump_escher (guint8 *data, guint32 len, int level)
{
	ESH_HEADER *h = esh_header_new (data, len);
	while (esh_header_next(h)) {
		int lp;
		for (lp=0;lp<level;lp++) printf ("-");
		printf ("Header: type 0x%4x : '%15s', inst 0x%x ver 0x%x len 0x%x\n",
			h->type, get_escher_opcode_name (h->type), h->instance,
			h->ver, h->length);
		if (h->ver == 0xf) /* A container */
			dump_escher (h->data+ESH_HEADER_LEN, h->length-ESH_HEADER_LEN, level+1);
	}
	esh_header_destroy (h); 
}

static void
do_dump (MsOle *ole)
{
	char *ptr;
	MsOleDirectory *dir;

	ptr = strtok (NULL, delim);
	if ((dir = get_file_handle (ole, ptr)))
	{
		MsOleStream *stream = ms_ole_stream_open (dir, 'r');
		guint8 *buffer = g_malloc (dir->length);
		stream->read_copy (stream, buffer, dir->length);
		printf ("Stream : '%s' length 0x%x\n", ptr, dir->length);
		if (buffer)
			dump (buffer, dir->length);
		else
			printf ("Failed read\n");
		ms_ole_stream_close (stream);
	} else
		printf ("Need a stream name\n");
}

static void
do_biff (MsOle *ole)
{
	char *ptr;
	MsOleDirectory *dir;
	
	ptr = strtok (NULL, delim);
	if ((dir = get_file_handle (ole, ptr)))
	{
		MsOleStream *stream = ms_ole_stream_open (dir, 'r');
		BiffQuery *q = ms_biff_query_new (stream);
		guint16 last_opcode=0xffff;
		guint32 last_length=0;
		guint32 count=0;
		while (ms_biff_query_next(q)) {
			if (q->opcode == last_opcode &&
			    q->length == last_length)
				count++;
			else {
				if (count>0)
					printf (" x %d\n", count+1);
				else
					printf ("\n");
				count=0;
				printf ("Opcode 0x%3x : %15s, length %d",
					q->opcode, get_biff_opcode_name (q->opcode), q->length);
			}
			last_opcode=q->opcode;
			last_length=q->length;
		}
		printf ("\n");
		ms_ole_stream_close (stream);
	} else
		printf ("Need a stream name\n");
}

static void
do_biff_raw (MsOle *ole)
{
	char *ptr;
	MsOleDirectory *dir;
	
	ptr = strtok (NULL, delim);
	if ((dir = get_file_handle (ole, ptr)))
	{
		MsOleStream *stream = ms_ole_stream_open (dir, 'r');
		guint8 data[4], *buffer;
		
		buffer = g_new (guint8, 65550);
		while (stream->read_copy (stream, data, 4)) {
			guint32 len=MS_OLE_GET_GUINT16(data+2);
			printf ("0x%4x Opcode 0x%3x : %15s, length 0x%x (=%d)\n", stream->position,
				MS_OLE_GET_GUINT16(data), get_biff_opcode_name (MS_OLE_GET_GUINT16(data)),
				len, len);
			stream->read_copy (stream, buffer, len);
			dump (buffer, len);
			buffer[0]=0;
			buffer[len-1]=0;
		}
		ms_ole_stream_close (stream);
	} else
		printf ("Need a stream name\n");
}

static void
do_draw (MsOle *ole)
{
	char *ptr;
	MsOleDirectory *dir;

	ptr = strtok (NULL, delim);
	if ((dir = get_file_handle (ole, ptr)))
	{
		MsOleStream *stream = ms_ole_stream_open (dir, 'r');
		BiffQuery *q = ms_biff_query_new (stream);
		while (ms_biff_query_next(q)) {
			if (q->ls_op == BIFF_MS_O_DRAWING ||
			    q->ls_op == BIFF_MS_O_DRAWING_GROUP ||
			    q->ls_op == BIFF_MS_O_DRAWING_SELECTION) {
				guint8 *data;
				guint32 len;
				guint skip = biff_to_flat_data (q, &data, &len) - 1;
				printf("Drawing: '%s'\n", get_biff_opcode_name(q->opcode));
				dump_escher (data, len, 0);
				while (skip > 0 && ms_biff_query_next(q)) skip--;
			}
		}
		printf ("\n");
		ms_ole_stream_close (stream);
	} else
		printf ("Need a stream name\n");
}

static void
do_get (MsOle *ole)
{
	char *from, *to;
	MsOleDirectory *dir;

	from = strtok (NULL, delim);
	to   = strtok (NULL, delim);
	if ((dir = get_file_handle (ole, from)))
	{
		MsOleStream *stream = ms_ole_stream_open (dir, 'r');
		guint8 *buffer = g_malloc (dir->length);
		FILE *f = fopen (to, "w");
		stream->read_copy (stream, buffer, dir->length);
		printf ("Stream : '%s' length 0x%x\n", from, dir->length);
		if (f && buffer) {
			fwrite (buffer, 1, dir->length, f);
			fclose (f);
		} else
			printf ("Failed write to '%s'\n", to);
		ms_ole_stream_close (stream);

	} else
		printf ("Need a stream name\n");
}

static void
really_put (MsOle *ole, char *from, char *to)
{
	MsOleDirectory *dir;
	MsOleStream *stream;
	char buffer[8200];

	if (!from || !to) {
		printf ("Null name\n");
		return;
	}

	if (!(dir = get_file_handle (ole, to)))
		dir = ms_ole_directory_create (ms_ole_get_root(ole),
					       to, MsOlePPSStream);
		
	if (dir)
	{
		FILE *f = fopen (from, "r");
		size_t len;
		int block=0;

		stream = ms_ole_stream_open (dir, 'w');
		if (!f || !stream) {
			printf ("Failed write\n");
			return;
		}

		stream->lseek (stream, 0, MsOleSeekSet);
	       
		do {
			guint32 lenr = 1+ (int)(8192.0*rand()/(RAND_MAX+1.0));
			len = fread (buffer, 1, lenr, f);
			printf ("Transfering block %d = %d bytes\n", block++, len); 
			stream->write (stream, buffer, len);
		} while (!feof(f) && len>0);

		fclose (f);
		ms_ole_stream_close (stream);
	} else
		printf ("Need a stream name\n");
}

static void
do_summary (MsOle *ole)
{
	char *ptr;
	MsOleDirectory *dir;
	
	ptr = strtok (NULL, delim);
	if ((dir = get_file_handle (ole, ptr)))
	{
		MsOleStream *stream = ms_ole_stream_open (dir, 'r');
		dump_summary (stream);
		ms_ole_stream_close (stream);
	} else
		printf ("Need a stream name\n");
}

static void
do_put (MsOle *ole)
{
	char *from, *to;

	from = strtok (NULL, delim);
	to   = strtok (NULL, delim);

	if (!from || !to) {
		printf ("put <filename> <stream>\n");
		return;
	}

	really_put (ole, from, to);
}

static void
do_copyin (MsOle *ole)
{
	char *from;

	do {
		from = strtok (NULL, delim);
		if (from)
			really_put (ole, from, from);
	} while (from);
}

int main (int argc, char **argv)
{
	MsOle *ole;
	int lp,exit=0,interact=0;
	char *buffer = g_new (char, 1024) ;

	if (argc<2)
		syntax_error(0);

	printf ("Ole file '%s'\n", argv[1]);
	ole = ms_ole_open (argv[1]);
	if (!ole) {
		printf ("Creating new file '%s'\n", argv[1]);
		ole = ms_ole_create (argv[1]);
	}
	if (!ole)
		syntax_error ("Can't open file or create new one");

	if (argc<=2)
		syntax_error ("Need command or -i");

	if (argc>2 && argv[argc-1][0]=='-'
	    && argv[argc-1][1]=='i') 
		interact=1;
	else {
		char *str=g_strdup(argv[2]) ;
		for (lp=3;lp<argc;lp++)
			str=g_strconcat(str," ",argv[lp],NULL); /* Mega leak :-) */
		buffer = str; /* and again */
	}

	do
	{
		char *ptr;

		if (interact) {
			fprintf (stdout,"> ");
			fflush (stdout);
			fgets (buffer, 1023, stdin);
		}

		ptr = strtok (buffer, delim);
		if (!ptr && interact) continue;
		if (!interact)
			printf ("Command : '%s'\n", ptr);
		if (g_strcasecmp(ptr, "ls")==0) {
			list_files (ole);
		} else if (g_strcasecmp(ptr, "dump")==0)
			do_dump (ole);
		else if (g_strcasecmp(ptr, "biff")==0)
			do_biff (ole);
		else if (g_strcasecmp(ptr, "biffraw")==0)
			do_biff_raw (ole);
		else if (g_strcasecmp(ptr, "draw")==0) /* Assume its in a BIFF file */
			do_draw (ole);
		else if (g_strcasecmp(ptr, "get")==0)
			do_get (ole);
		else if (g_strcasecmp(ptr, "put")==0)
			do_put (ole);
		else if (g_strcasecmp(ptr, "copyin")==0)
			do_copyin (ole);
		else if (g_strcasecmp(ptr, "summary")==0)
			do_summary (ole);
		else if (g_strcasecmp(ptr, "debug")==0)
			ms_ole_debug (ole, 1);
		else if (g_strcasecmp(ptr,"exit")==0 ||
			   g_strcasecmp(ptr,"quit")==0 ||
			   g_strcasecmp(ptr,"q")==0 ||
			   g_strcasecmp(ptr,"bye")==0)
			exit = 1;
	}
	while (!exit && interact);

	ms_ole_destroy (ole);
	return 1;
}

/* ------------------------------------------------------------------- */
/*            Developed here for convenience only                      */
/* ------------------------------------------------------------------- */

typedef struct _MsOleSummaryHeader MsOleSummaryHeader;
typedef struct _MsOleSummaryRecord MsOleSummaryRecord;
typedef guint32 MsOleSummaryFileTime;

#define SUMMARY_DEBUG 1

#define RECORD_HEADER 10
#define HEADER_LEN    0x38

/* LONG  = 4 bytes */
/* DWORD = 2 bytes ( signed ? ) */
struct _MsOleSummaryHeader {
	guint32     num_records;
	GPtrArray  *records;
};

struct _MsOleSummaryRecord {
	MsOlePos	offset;
	guint32		type;
	gchar	*txt;
};

static gchar *sum_names[] = {
	"Unknown",				/* 0x0 */
	"Codepage Property",                    /* 0x1 */
	"Title",                                /* 0x2 */
	"Subject",                              /* 0x3 */
	"Author",                               /* 0x4 */
	"Keywords",                             /* 0x5 */
	"Comments",                             /* 0x6 */
	"Template",                             /* 0x7 */
	"Last Saved By",                        /* 0x8 */
	"Revision Number",                      /* 0x9 */
	"Unknown",                              /* 0xa */
	"Last printed summary properties",      /* 0xb */
	"Create time/date",                     /* 0xc */
	"Last save time/date",                  /* 0xd */
	"Page count",                           /* 0xe */
	"Word count",                           /* 0xf */
	"Character count",                      /* 0x10 */
	"Unknown",                              /* 0x11 */
	"Creating Application",                 /* 0x12 */
	"Security"                              /* 0x13 */
};

static void
read_records (MsOleStream *s, MsOleSummaryRecord *sr)
{
	guint8  data[64];
	guint8 *mem;
	gint32 len, rec_type;

	s->lseek (s, sr->offset, MsOleSeekSet);
	g_return_if_fail (s->read_copy (s, data, 4));

	rec_type = MS_OLE_GET_GUINT32 (data);
#if SUMMARY_DEBUG > 0
	printf ("Next record at 0x%x : '%s' : type 0x%x\n",
		sr->offset, sum_names[sr->type], rec_type);
#endif

	if (rec_type == 0x1e) { /* String */
		g_return_if_fail (s->read_copy (s, data, 4));
		len = MS_OLE_GET_GUINT32 (data);

		mem = g_malloc (len+1);
		g_return_if_fail (s->read_copy (s, mem, len));
/*		dump (mem, len); */

		sr->txt = mem;

		printf ("string '%s'\n", sr->txt);
	} else if (rec_type == 0x3) {
		g_return_if_fail (s->read_copy (s, data, 4));
		printf ("Integer : 0x%x\n", MS_OLE_GET_GUINT32 (data));
	} else if (rec_type == 0x40) {
		g_return_if_fail (s->read_copy (s, data, 8));
		printf ("Timestamp : 0x%x%x\n", MS_OLE_GET_GUINT32 (data + 4),
			MS_OLE_GET_GUINT32 (data));
	} else {
		printf ("Unknown type:\n");
		g_return_if_fail (s->read_copy (s, data, 32));
		dump (data, 32);
	}
}

static void
dump_summary (MsOleStream *s)
{
	MsOleSummaryHeader *sh = g_new (MsOleSummaryHeader, 1);
	/* Dwords, GUID, DW, FileTimes, DW */
	guint8 data[HEADER_LEN];
	int lp;

	g_return_if_fail (s);
	g_return_if_fail (HEADER_LEN <= s->size);

	g_return_if_fail (s->read_copy (s, data, HEADER_LEN));

	sh->num_records = MS_OLE_GET_GUINT32 (data + 0x34);
	sh->records     = g_ptr_array_new ();

	printf ("Summary info header len 0x%x = 0x%x records\n",
		HEADER_LEN, sh->num_records);
	dump (data, HEADER_LEN);

	for (lp = 0; lp < sh->num_records; lp++) {
		MsOleSummaryRecord *sr = g_new (MsOleSummaryRecord, 1);
		guint8 data[8];

		g_return_if_fail (s->read_copy (s, data, 8));
		sr->offset = ((0x0b + 2) * 8) - HEADER_LEN + MS_OLE_GET_GUINT32 (data+4);
		sr->type   = MS_OLE_GET_GUINT32(data);
		printf ("Record type %x at offset 0x%x\n", sr->type, MS_OLE_GET_GUINT32(data+4));
		g_ptr_array_add (sh->records, sr);
	}

	for (lp = 0; lp < sh->num_records; lp++) {
		if (lp)
			read_records (s, g_ptr_array_index (sh->records, lp));
	}
}
