"""Run the original 7.7 SlimBrowser DB against captured production menu JSON.

Requires Lupa (as used by the existing Connect tests) and the unmodified upstream
DB.lua in --reference. Jive's != syntax is translated to Lua 5.1's ~= only.
"""
import argparse
import json
from pathlib import Path
from lupa.lua51 import LuaRuntime

parser = argparse.ArgumentParser()
parser.add_argument('--reference', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
args = parser.parse_args()
page = json.loads(args.evidence.read_text())
assert page['count'] == 137 and len(page['item_loop']) == 137
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
_assert=assert
package.loaded['loop.base']={class=function(c)
 c.__index=c;setmetatable(c,{__call=function(cls,...)return cls.__init(cls,...)end})
end,rawnew=function(c,t)return setmetatable(t,c)end}
package.loaded['jive.ui.RadioGroup']=function()return {}end
package.loaded['jive.utils.debug']={}
package.loaded['jive.utils.log']={logger=function()return setmetatable({},
 {__index=function()return function()end end})end}
''')
lua.execute(args.reference.read_text().replace('!=', '~='), 'original_jive_db')
module = lua.globals().package.loaded['original_jive_db']
database = module.__init(module, lua.table_from({}))

def convert(value):
    if isinstance(value, dict):
        return lua.table_from({k: convert(v) for k, v in value.items()})
    if isinstance(value, list):
        return lua.table_from([convert(v) for v in value])
    return value

database.menuItems(database, convert(page))
assert database.size(database) == 137
assert database.playlistIndex(database) == page['playlist_cur_index'] + 1
for index in (1, 50, 100, 137):
    item, current = database.item(database, index)
    assert item.queue_entry_id == page['item_loop'][index-1]['queue_entry_id']
    assert current == (index == page['playlist_cur_index'] + 1)
print('Original Jive SlimBrowser.DB: 137 production rows, stable IDs, 0-to-1-based index conversion and active highlighting passed')
