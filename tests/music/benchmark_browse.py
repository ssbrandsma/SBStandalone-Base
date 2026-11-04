from test_browse_http import run
import sys
for count in (1000,10000,50000):
 run(sys.argv[1],sys.argv[2],count=count,benchmark=True)
