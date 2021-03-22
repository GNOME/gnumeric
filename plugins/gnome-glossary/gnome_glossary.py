from Gnumeric import *
import string
import gsf
from os.path import basename, splitext
from time import strftime, localtime, time

def save_sheet_as_po(sheet, output):
	(lang, _) = splitext(basename(output.name()))
		
	for col in range(2, sheet.get_extent().end.col + 1):
		if string.lower(sheet[col, 0].get_value_as_string()) == lang:
			lang_col = col
			break
	else:
		lang_col = col + 1
	header = open(plugin_info.get_dir_name() + '/glossary-po-header').read()
	str = header % {'lang': lang, 'creation_date': strftime('%Y-%m-%d %H:%M%Z', localtime(time()))}
	output.write(len(str), str)
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
		print(e)
		output.write(len('\n'), '\n')
		for d in string.split(e['definition'], '\n'):
			str = '#. %s\n' % d
			output.write(len(str), str)
		str = 'msgid "%s"\n' % e['term']
		output.write(len(str), str)
		str = 'msgstr "%s"\n' % e['translation']
		output.write(len(str), str)

def po_file_save(wb, output):
	for sheet in wb.sheets():
		if sheet[0, 0].get_value_as_string() == 'Term':
			save_sheet_as_po (sheet, output)
			break
	else:
		raise GnumericError('Could not find Gnome Glossary sheet')
