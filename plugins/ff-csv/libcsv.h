
extern char *csv_error;
extern int csv_line;

struct csv_row
{
	char **data;
	int width;
};

struct csv_table
{
	struct csv_row *row;
	int height;
	int size;
};

extern int csv_load_table(FILE *f, struct csv_table *ptr);
extern void csv_destroy_table(struct csv_table *ptr);

#define CSV_ITEM(t,r,c)		((t)->row[(r)].data[(c)])
#define CSV_WIDTH(t,r)		((t)->row[(r)].width)
