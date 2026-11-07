"""Native protocol gates only; these fixtures never qualify a physical decoder."""
import sys
from test_ownership_bridge import Driver

binary=sys.argv[1]
def setup(mask=12):
    d=Driver(binary)
    d.raw('!idle-policy '+str(mask))
    d.command('register',4)
    return d

for mask in (0,2,6,14,30):
    d=setup(mask)
    try:
        d.raw('!ready 1')
        d.command('admit-idle',4,7,expected=409)
        d.command('request',4,timeout=1000,expected=409)
    finally:d.close()

d=setup()
try:
    for who in (1,2,3):d.command('register',who,expected=409)
    d.command('request',4,timeout=1000,expected=409)
    d.command('admit-idle',4,7,expected=409) # missing resource roundtrip
    d.raw('!ready 1')
    for flags in (0,1,3,5,6):d.command('admit-idle',4,flags,expected=409)
    d.command('admit-idle',4,7,gen=99,expected=409)
    d.command('admit-idle',4,7,conn=99,expected=409)
    d.raw('!hold 1');d.command('admit-idle',4,7,expected=409)
    d.raw('!hold 0');d.command('admit-idle',4,7)
    assert d.snapshot['idle_only'] and d.snapshot['idle_admitted']
    d.command('admit-idle',4,7,expected=409)
    d.command('request',4,timeout=1000)
    assert d.snapshot['owner']==4 and d.snapshot['controls']
    d.command('admit-idle',4,7,expected=409)
    d.command('release',4,7)
    assert d.snapshot['owner']==0
    d.raw('!connect 9');d.command('query',4)
    assert not d.snapshot['idle_admitted']
    d.command('request',4,timeout=1000,expected=409)
finally:d.close()

# Existing bridge mode must never acquire the trusted policy by naming an op.
d=Driver(binary)
try:
    d.command('register',4);d.raw('!ready 1')
    d.command('admit-idle',4,7,expected=409)
finally:d.close()

# WP4a.5: suspend is re-applied every service tick; only real transitions
# may advance the owner revision (no per-tick churn).
d=setup()
try:
    d.raw('!ready 1');d.command('query',4)
    rev0=int(d.snapshot['revision'])
    d.raw('!hold 0');d.command('query',4)
    assert int(d.snapshot['revision'])==rev0
    d.raw('!hold 1');d.command('query',4)
    rev1=int(d.snapshot['revision']);assert rev1==rev0+1
    d.raw('!hold 1');d.command('query',4)
    assert int(d.snapshot['revision'])==rev1
    d.raw('!hold 0');d.command('query',4)
    assert int(d.snapshot['revision'])==rev1+1
finally:d.close()

# WP4a.5: a timed-out Music binding must release the idle admission so a
# fresh register+admit-idle re-qualifies instead of deadlocking (stale
# admission set, registration gone, re-admit refused).
d=setup()
try:
    d.raw('!ready 1');d.command('admit-idle',4,7)
    assert d.snapshot['idle_admitted']
    d.raw('!time 46000')
    d.command('query',4,expected=409)
    hello=d.raw('SBOWN1 hello 4 0 99 0 0 0 0 0 0 0')
    assert hello['status']==200 and not hello['reply']['idle_admitted'],hello
    d.inc[4]=0;d.command('register',4)
    d.command('admit-idle',4,7)
    assert d.snapshot['idle_admitted']
finally:d.close()

# WP4a.5: explicit Music fault releases the admission the same way.
d=setup()
try:
    d.raw('!ready 1');d.command('admit-idle',4,7)
    d.command('fault',4)
    hello=d.raw('SBOWN1 hello 4 0 99 0 0 0 0 0 0 0')
    assert hello['status']==200 and not hello['reply']['idle_admitted'],hello
    d.inc[4]=0;d.command('register',4)
    d.command('admit-idle',4,7)
    assert d.snapshot['idle_admitted']
finally:d.close()
print('Explicit Music-only admission: identity/IPC/proof/alarm/connection gates passed')
