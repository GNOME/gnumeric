from Gnumeric import *
import string
from os.path import basename
from time import strftime, localtime, time

def save_sheet_as_po(sheet, file_name):
	name = basename(file_name)
	try:
		lang = name[:string.rindex(name, '.')]
	except ValueError:
		lang = name
	for col in range(2, sheet.get_extent().end.col + 1):
		if string.lower(sheet[col, 0].get_value_as_string()) == lang:
			lang_col = col
			break
	else:
		lang_col = col + 1
	header = open(plugin_info.get_dir_name() + '/glossary-po-header').read()
	fpo = open(file_name, 'w')
	fpo.write(header % {'lang': lang, 'creation_date': strftime('%Y-%m-%d %H:%M%Z', localtime(time()))})
	entries = []
	for row in range(1, sheet.get_extent().end.row + 1):
		term = sheet[0, row].get_value_as_string()
		definition = sheet[1, row].get_value_as_string()
		translation = sheet[lang_col, row].get_value_as_string()
		if term:
			entries.append({'term': term, 'definition': definition, 'translation' : translation})
		elif definition:
			entries[-1]['definition'] = '%s\n%s' % (entries[-1]['definition'], definition)
	for e in entries:
		fpo.write('\n')
		for d in string.split(e['definition'], '\n'):
			fpo.write('#. %s\n' % d)
		fpo.write('msgid "%s"\n' % e['term'])
		fpo.write('msgstr "%s"\n' % e['translation'])
	fpo.close()

def po_file_save(wb, file_name):
	for sheet in wb.get_sheets():
		if sheet[0, 0].get_value_as_string() == 'Term':
			save_sheet_as_po (sheet, file_name)
			break
	else:
		raise GnumericError, 'Could not find Gnome Glossary sheet'
