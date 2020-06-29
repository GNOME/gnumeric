from Gnumeric import *
import string

# Here is a function with variable number of arguments
def func_printf(format, *args):
	'@FUNCTION=PY_PRINTF\n'\
	'@SYNTAX=PY_PRINTF (format,...)\n'\
	'@DESCRIPTION='\
	'\n'\
	'@EXAMPLES=\n'\
	'PY_PRINTF("Test: %.2f",12) equals "Test: 12.00")'\
	'\n'\
	'@SEEALSO='

	try:
		val = format % args
	except TypeError:
		raise GnumericError(GnumericErrorVALUE)
	else:
		return val

# Here is a function with one argument
def func_capwords(str):
	'@FUNCTION=PY_CAPWORDS\n'\
	'@SYNTAX=PY_CAPWORDS (sentence)\n'\
	'@DESCRIPTION='\
	'\n'\
	'@EXAMPLES=\n'\
	'PY_CAPWORDS("Hello world") equals "Hello World")'\
	'\n'\
	'@SEEALSO='

	return string.capwords(str)

# Here is a function which calls a spreadsheet function
def func_bitand(num1, num2):
	'@GNM_FUNC_HELP_NAME@BITAND:bitwise AND of its two arguments.\n'\
	'@GNM_FUNC_HELP_ARG@number1:first value\n'\
	'@GNM_FUNC_HELP_ARG@number2:second value\n'\
	'@GNM_FUNC_HELP_EXAMPLES@=PY_BITAND(12, 6)\n'\
	'@GNM_FUNC_HELP_SEEALSO@BITAND'
	gnm_bitand=functions['bitand']
	return gnm_bitand(num1, num2)

test_functions = {
	# Here we tell Gnumeric that the Python function 'func_printf'
	# provides the spreadsheet function 'py_printf', and that
	# 'py_printf' may be called with any number of arguments [1].
	'py_printf': func_printf,

	# 'func_capwords' provides the spreadsheet function 'py_capwords'.
	# 'py_capwords' should be called with a single argument.
	# This should be a string ('s'), and the argument name is 'sentence'.
	'py_capwords': ('s', 'sentence', func_capwords),

	# 'func_bitand' provides 'bitand', which should be called with two
	# numeric arguments (ff) named 'num1' and 'num2'.
	'py_bitand':   ('ff', 'num1, num2', func_bitand)
}


# [1] Actually, the 'def func_printf' statement says that it requires at least
#     one argument. But there is no way to express that in the syntax used in
#     the 'test_functions' dictionary.
