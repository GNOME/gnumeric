from Gnumeric import *
import string

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
		raise GnumericError, GnumericErrorVALUE
	else:
		return val

def func_capwords(str):
	'@FUNCTION=PY_CAPWORDS\n'\
	'@SYNTAX=PY_CAPWORDS (string)\n'\
	'@DESCRIPTION='\
	'\n'\
	'@EXAMPLES=\n'\
	'PY_CAPWORDS("Hello world") equals "Hello World")'\
	'\n'\
	'@SEEALSO='

	return string.capwords(str)

test_functions = {
	'py_printf': func_printf,
	'py_capwords': ('s', 'string', func_capwords)
}
