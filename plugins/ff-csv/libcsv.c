/*
 *		Loader for comma delimited database files
 *
 *		(c) Copright 1998 Building Number Three Ltd
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */




/*
 *	Take a table of good old comma seperate junk, and load it into
 * 	a table
 */
 

#include <stdio.h>
#include <string.h> 
#include "libcsv.h"

static char linebuf[16384];		/* Hack for now */
char *csv_error;
int csv_line;

static char *csv_strdup(const char *p)
{
	char *n=strdup(p);
	if(n==NULL)
		csv_error="out of memory";
	return n;
}

static int count_fields(char *ptr)
{	
	int count=0;
	int quoted=0;
	
	if(*ptr==0)
		return -1;		/* Blank line flag */
		
	while(*ptr)
	{
		/* Embedded quote */
		if(*ptr=='"' && ptr[1]=='"')
		{
			ptr+=2;
			continue;
		}
		if(*ptr=='"')
			quoted=1-quoted;
		else if(*ptr==',')
			count+=1-quoted;
		ptr++;
	}
	return count+1;
}

static char *cut_quoted(char **p)
{
	char *x;
	char *e;
	char *n;
	
	x=*p;
	x++;	/* Open quote */
	
	e=x;
	
	while(*x)
	{
		if(*x=='"' && x[1]=='"')
		{
			memmove(x,x+1,strlen(x));
			x++;
			continue;
		}
		if(*x=='"')
		{
			*x=0;
			x++;
/*			printf("%p %p (%s)\n", e, x, e);*/
			n=csv_strdup(e);
			*p=x;
			return n;
		}
		x++;
	}
	/* Error */
	csv_error="missing quote";
	return NULL;
}		

static char *cut_comma(char **ptr)
{
	char *e=strchr(*ptr,',');
	char *n=*ptr;
	
	if(e==NULL)
	{
		e=n+strlen(n);
		*ptr=e;
		return csv_strdup(n);
	}
	*e=0;
	*ptr=e+1;
	return csv_strdup(n);
}

static int smash_fields(char *ptr, char **array, int len)
{
	if(*ptr==0)
	{
		int i;
		for(i=0;i<len;i++)
			array[i]="";
		return 0;
	}
	

	while(*ptr)
	{
		if(*ptr=='"')
			*array=cut_quoted(&ptr);
		else
			*array=cut_comma(&ptr);
/*		printf("[%s]\n", *array);*/
		if(*array++ == NULL)
			return -1;
	}
	return 0;
}


static char * read_line(FILE *f)
{
	int n;
	if(fgets(linebuf, sizeof(linebuf),f)==NULL)
		return NULL;
	csv_line++;
	n=strlen(linebuf);
	if(n && linebuf[n-1]=='\n')
		linebuf[--n]=0;
	if(n && linebuf[n-1]=='\r')
		linebuf[n-1]=0;
	return linebuf;
}
			
int csv_load_table(FILE *f, struct csv_table *ptr)
{
	int width;
	int length;
	int size;
	
	csv_line = -1;
	
	if(read_line(f)==NULL)
		return -1;
	
	size=256;
	
	ptr->row=(struct csv_row *)malloc(sizeof(struct csv_row)*size);
	if(ptr->row==NULL)
	{
		csv_error="out of memory";
		return -1;
	}
	length=0;
	
	do
	{
		width = count_fields(linebuf);
		if(width < 1)
			width = 1;
		ptr->row[length].data=(char **)malloc(width*sizeof(char *));
		ptr->row[length].width = width;
		if(ptr->row[length].data==NULL)
		{
			csv_error="out of memory";
			return -1;
		}
		memset(ptr->row[length].data, 0, width*sizeof(char *));
		
		if(smash_fields(linebuf, ptr->row[length].data, width)==-1)
			return -1;
		length++;
		
/*		printf("-------------\n");*/
		fflush(stdout);
		if(length==size)
		{
			size+=256;
			ptr->row=(struct csv_row *)realloc(ptr->row, sizeof(struct csv_row)*size);
			if(ptr->row==NULL)
			{
				csv_error="out of memory";
				return -1;
			}
		}
		if(read_line(f)==NULL)
			break;
	}
	while(1);
	ptr->height = length;
	ptr->size = size;
	return 0;
}

void csv_destroy_table(struct csv_table *table)
{
	int i, j;
		
	for(i=0;i<table->height;i++)
	{
		struct csv_row *r=&table->row[i];
		for(j=0;j<r->width;j++)
			if(r->data[j])
				free(r->data[j]);
	}
	free (table->row);
}

#ifdef TEST
int main(int argc, char *argv)
{
	struct csv_table t;
	printf("Got %d\n", parse_table(stdin, &t));
}
#endif
