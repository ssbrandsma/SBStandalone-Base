"""Real protocol core, deterministic clock and underlying Music resource gate."""
import json, subprocess, sys, time
class Driver:
    def __init__(self,binary):
        self.process=subprocess.Popen(binary if isinstance(binary,list) else [binary],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        self.process.stdin.reconfigure(newline='\n')
        self.sequence=[0]*5;self.inc=[0]*5;self.snapshot=None
    def raw(self,text):
        self.process.stdin.write(text+'\n');self.process.stdin.flush()
        return json.loads(self.process.stdout.readline())
    def command(self,op,who,flags=0,req=0,timeout=0,expected=200,gen=None,conn=None):
        self.sequence[who]+=1;v=self.snapshot or {'generation':'0','connection':'0'}
        fields=['SBOWN1',op,who,self.inc[who],self.sequence[who],gen or v['generation'],conn or v['connection'],req,timeout,flags,0,0]
        if op=='register':fields[5:7]=[0,0]
        result=self.raw(' '.join(map(str,fields)));assert result['status']==expected,(fields,result)
        if 'reply' in result:
            self.snapshot=result['reply'];self.inc[who]=int(self.snapshot['incarnation'])
        return result
    def close(self):
        self.process.stdin.close();assert self.process.wait(timeout=5)==0
if __name__=='__main__':
    d=Driver(sys.argv[1])
    try:
        for owner in range(1,5):d.command('register',owner)
        for invalid in ['','SBOWN1 query','SBOWN1 query 1 1 18446744073709551616 0 0 0 0 0 0 0','SBOWN1  query 1 1 9 0 0 0 0 0 0 0','X'*385]:
            assert d.raw(invalid)['status']==400
        for owner in range(1,4):d.command('idle',owner,7)
        d.command('request',1,timeout=1000,expected=409)
        d.command('idle',4,7,expected=409) # no fake resource release
        d.raw('!ready 1');d.command('idle',4,7)
        for source in (1,2,3):
            d.command('request',source,timeout=1000)
            d.command('request',4,timeout=1000)
            req=d.snapshot['request'];oldgen=d.snapshot['generation']
            d.command('ack',source,3,req) # incomplete proof refuses grant
            assert d.snapshot['owner']==source and not d.snapshot['controls']
            d.command('release',source,7)
            d.command('request',source,timeout=1000)
            d.command('request',4,timeout=1000)
            d.command('ack',source,7,d.snapshot['request'])
            assert d.snapshot['owner']==4
            d.command('request',source,timeout=1000)
            req=d.snapshot['request'];d.raw('!ready 0')
            d.command('ack',4,7,req,expected=409)
            d.raw('!ready 1');d.command('ack',4,7,int(req)-1,expected=409)
            d.command('ack',4,7,req)
            assert d.snapshot['owner']==source
            d.command('release',source,7)
        d.command('request',1,timeout=1000);d.command('request',2,timeout=1000)
        d.command('request',3,timeout=1000,expected=409)
        d.raw('!hold 1');d.command('ack',1,7,d.snapshot['request'],expected=409)
        d.raw('!hold 0');d.command('release',1,7)
        # Music reconnection needs a fresh connection AND native resource proof.
        d.command('request',4,timeout=1000)
        d.command('reconnect',4,7,expected=409)
        d.raw('!connect 9');d.command('query',4)
        d.raw('!ready 0');d.command('reconnect',4,7,expected=409)
        d.raw('!ready 1');d.command('reconnect',4,3,expected=409)
        d.command('reconnect',4,7)
        assert d.snapshot['owner']==0
        d.command('reconnect',4,7,expected=409)
        d.command('request',1,timeout=1000);d.raw('!connect 10')
        d.command('request',2,timeout=1000,expected=409)
        d.command('query',1);d.command('reconnect',1,7)
        d.command('request',2,timeout=1000);d.command('request',3,timeout=10)
        d.raw('!time 20');d.command('ack',2,7,d.snapshot['request'],expected=409)
        d.command('release',2,7);d.command('request',3,timeout=1000)
        d.command('fault',3);d.command('request',2,timeout=1000,expected=409)
        print('Authenticated-core typed protocol, six directed Music handoffs, incomplete proof, resource gate, stale request, alarm, reconnect, timeout and crash passed')
    finally:d.close()
