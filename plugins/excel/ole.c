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

typedef struct {
	guint16 opcode;
	char *name;
} GENERIC_TYPE;

static GPtrArray *biff_types   = NULL;
static GPtrArray *escher_types = NULL;
typedef enum { eBiff=0, eEscher=1 } typeType;

static void
read_types (char *fname, GPtrArray **types, typeType t)
{
	FILE *file = fopen(fname, "r");
	char buffer[1024];
	*types = g_ptr_array_new ();
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
	for (lp=0;lp<biff_types->len;lp++) {
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

static MS_OLE_DIRECTORY *
get_file_handle (MS_OLE *ole, char *name)
{
	MS_OLE_DIRECTORY *dir;
	if (!name)
		return NULL;
	dir = ms_ole_directory_new (ole);
	while (ms_ole_directory_next(dir)) {
		if (g_strcasecmp(dir->name, name)==0) {
			return dir;
		}
	}	
	printf ("Stream '%s' not found\n", name);
	ms_ole_directory_destroy (dir);
	return NULL;
}

static void
list_files (MS_OLE *ole)
{
	MS_OLE_DIRECTORY *dir = ms_ole_directory_new (ole);
	while (ms_ole_directory_next(dir)) {
		printf ("'%s' : type %d, length %d bytes\n", dir->name, dir->type, dir->length);
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
	printf (" * biff <stream name>:   dump biff records\n");
	printf (" * draw <stream name>:   dump drawing records\n");
	printf (" * dump <stream name>:   dump stream\n");
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

	h->length   = BIFF_GETLONG(h->data+4);
	h->type     = BIFF_GETWORD(h->data+2);
	split       = BIFF_GETWORD(h->data+0);
	h->ver      = (split&0x0f);
	h->instance = (split>>4);
	return 1;
}
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
static void
biff_to_flat_data (const BIFF_QUERY *q, guint8 **data, guint32 *length)
{
	BIFF_QUERY *nq = ms_biff_query_copy (q);
	guint8 *ptr;

	*length=0;
	do {
		*length+=nq->length;
		ms_biff_query_next(nq);
	} while (nq->opcode == BIFF_CONTINUE);

	(*data) = g_malloc (*length);
	ptr=(*data);
	nq = ms_biff_query_copy (q);
	do {
		memcpy (ptr, nq->data, nq->length);
		ptr+=nq->length;
		ms_biff_query_next(nq);
	} while (nq->opcode == BIFF_CONTINUE);
}

/* ---------------------------- End cut ---------------------------- */

static void
dump_escher (guint8 *data, guint32 len)
{
	ESH_HEADER *h = esh_header_new (data, len);
	while (esh_header_next(h)) {
		printf ("Header: type 0x%4x : '%s', inst 0x%x ver 0x%x len 0x%x\n",
			h->type, get_escher_opcode_name (h->type), h->instance,
			h->ver, h->length);
		if (h->ver == 0xf) /* A container */
			dump_escher (data+ESH_HEADER_LEN, len-ESH_HEADER_LEN);
	}
	esh_header_destroy (h); 
}

int main (int argc, char **argv)
{
	MS_OLE *ole;
	int lp,exit=0,interact=0;
	char *buffer = g_new (char, 1024) ;

	if (argc<2)
		syntax_error(0);

	printf ("Ole file '%s'\n", argv[1]);
	ole = ms_ole_new (argv[1]);
	if (!ole)
		syntax_error ("Can't open file");

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
		char delim[]=" \t\n";

		if (interact) {
			fprintf (stdout,"> ");
			fflush (stdout);
			fgets (buffer, 1023, stdin);
		}

		ptr = strtok (buffer, delim);
		printf ("Command : '%s'\n", ptr);
		if (g_strcasecmp(ptr, "ls")==0) {
			list_files (ole);
		} else if (g_strcasecmp(ptr, "dump")==0) {
			MS_OLE_DIRECTORY *dir;
			ptr = strtok (NULL, delim);
			if ((dir = get_file_handle (ole, ptr)))
			{
				MS_OLE_STREAM *stream = ms_ole_stream_open (dir, 'r');
				guint8 *buffer = g_malloc (dir->length);
				stream->read_copy (stream, buffer, dir->length);
				printf ("Stream : '%s' length 0x%x\n", ptr, dir->length);
				if (buffer)
					dump (buffer, dir->length);
				else
					printf ("Failed read\n");
				ms_ole_stream_close (stream);
			} else {
				printf ("Need a stream name\n");
				return 0;
			}
		} else if (g_strcasecmp(ptr, "biff")==0) {
			MS_OLE_DIRECTORY *dir;
			ptr = strtok (NULL, delim);
			if ((dir = get_file_handle (ole, ptr)))
			{
				MS_OLE_STREAM *stream = ms_ole_stream_open (dir, 'r');
				BIFF_QUERY *q = ms_biff_query_new (stream);
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
			} else {
				printf ("Need a stream name\n");
				return 0;
			}
		} else if (g_strcasecmp(ptr, "draw")==0) { /* Assume its in a BIFF file */
			MS_OLE_DIRECTORY *dir;
			ptr = strtok (NULL, delim);
			if ((dir = get_file_handle (ole, ptr)))
			{
				MS_OLE_STREAM *stream = ms_ole_stream_open (dir, 'r');
				BIFF_QUERY *q = ms_biff_query_new (stream);
				while (ms_biff_query_next(q)) {
					if (q->ls_op == BIFF_MS_O_DRAWING ||
					    q->ls_op == BIFF_MS_O_DRAWING_GROUP ||
					    q->ls_op == BIFF_MS_O_DRAWING_SELECTION) {
						guint8 *data;
						guint32 len;
						guint32 str_pos=q->streamPos;
						biff_to_flat_data (q, &data, &len);
						printf("Drawing: '%s'\n", get_biff_opcode_name(q->opcode));
						dump_escher (data, len);
					}
				}
				printf ("\n");
				ms_ole_stream_close (stream);
			} else {
				printf ("Need a stream name\n");
				return 0;
			}			
		} else if (g_strcasecmp(ptr,"exit")==0 ||
			   g_strcasecmp(ptr,"quit")==0 ||
			   g_strcasecmp(ptr,"bye")==0)
			exit = 1;
	}
	while (!exit && interact);

	ms_ole_destroy (ole);
	return 1;
}
