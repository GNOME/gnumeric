import gnumeric
import gnumeric_defs

def gnumeric_mid(context,text,start_num,num_chars):
    return text[start_num-1:start_num+num_chars-1];

help_mid = \
    "@FUNCTION=PY_MID\n"                       \
    "@SYNTAX=PY_MID(text,start,num_chars)\n"               \
    "@DESCRIPTION="                         \
    "Returns a specific number of characters from a text string, "  \
    "starting at START and spawning NUM_CHARS.  Index is counted "  \
    "starting from one"

gnumeric.register_function("py_mid", "Python", "sff",
                           "text, start_num, num_chars",
                           help_mid, gnumeric_mid);

# This is a totally pointless function. But it illustrates how to invoke a
# gnumeric function from Python. Note that the argument list must be a
# sequence. Caveat: "(1)" is not a tuple. "(1,)" and "(1,2)" are.
def py_abs(context, f):
    return gnumeric.apply(context, "abs", (f,))

help_py_abs = """@FUNCTION=PY_ABS
@SYNTAX=PY_ABS(num)
@DESCRIPTION=Return the absolute value of number."""

gnumeric.register_function("py_abs", "Python", "f", "num",
                           help_py_abs, py_abs);

# load user init-file if present
def run_user_init_file():
    import os
    home_gnumericrc = os.environ["HOME"] + "/.gnumeric/rc.py"
    if os.path.isfile(home_gnumericrc):
        execfile(home_gnumericrc, globals())

run_user_init_file()

