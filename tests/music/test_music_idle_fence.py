"""Host vendor-Lua compatibility via Lua 5.4; not physical qualification."""
import sys
from pathlib import Path
try:
    from lupa.lua54 import LuaRuntime
except ImportError:
    print('Lua 5.4 Python runtime unavailable; run this check with the configured Lupa environment')
    raise SystemExit(77)
lua=LuaRuntime(unpack_returned_tuples=True)
lua.globals().arg=lua.table_from(sys.argv[1:4])
lua.execute(Path(__file__).with_suffix('.lua').read_text())
