from ..overrides import override
from ..module import get_introspection_module

Gnm = get_introspection_module('Gnm')

__all__ = []

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
