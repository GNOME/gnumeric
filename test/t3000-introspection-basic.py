#!/usr/bin/python3 -B
# -----------------------------------------------------------------------------

import GnumericTest

import gi
gi.require_version('Gnm', '1.12')
from gi.repository import Gnm
Gnm.init()

print(Gnm.qnorm(0.4,0,1,1,0))
