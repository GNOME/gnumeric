import gnumeric

def gnumeric_mid(text,start_num,num_chars):
	return text[start_num-1:start_num+num_chars-1];

help_mid = \
	"@FUNCTION=MID\n" 						\
	"@SYNTAX=MID(text,start,num_chars)\n"				\
	"@DESCRIPTION="							\
	"Returns a specific number of characters from a text string, " 	\
	"starting at START and spawning NUM_CHARS.  Index is counted "  \
	"starting from one"

gnumeric.register_function ("mid", "sff", "text,start_num,num_chars", help_mid, gnumeric_mid);
