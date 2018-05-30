from ..overrides import override
from ..module import get_introspection_module
import os.path

Gnm = get_introspection_module('Gnm')

__all__ = []

# ----------------------------------------------------------------------------

def atomize_path(p):
    res=[]
    while 1:
        h,t = os.path.split(p)
        if t != "":
            res.append(t)
        if h == "":
            break
        if h == p:
            res.append(h)
            break
        p = h
    res.reverse()
    return res

l=atomize_path(os.path.dirname(__file__))
Gnm.in_tree = (len(l) > 3 and l[-3] == "introspection")
__all__.append('in_tree')

if Gnm.in_tree:
    # Somehow path gets dropped form g_get_prgname, so make up for that
    from gi.repository import GLib
    import sys
    GLib.set_prgname(sys.argv[0])

# ----------------------------------------------------------------------------

class Range(Gnm.Range):
    def __new__(cls,start_col=0,start_row=0,end_col=None,end_row=None):
        if end_col is None: end_col = start_col
        if end_row is None: end_row = start_row
        r = Gnm.Range.__new__(cls)
        r.init(start_col,start_row,end_col,end_row)
        return r

    def __init__(cls,*argv):
        pass

    __str__ = Gnm.Range.as_string

Range = override(Range)
__all__.append('Range')

# ----------------------------------------------------------------------------

def _valuetype_str(vt):
    return vt.value_name[6:]

Gnm.ValueType.__str__ = _valuetype_str

class Value(Gnm.Value):
    __repr__ = Gnm.Value.stringify

Value = override(Value)
__all__.append('Value')

# ----------------------------------------------------------------------------
