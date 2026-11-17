# Prototype: split a PlugY .sss into pages and item records (JM-delimited), group socketed children, verify counts.
import struct,re,sys
def bits(d): return ''.join(format(b,'08b')[::-1] for b in d)
def val(s,p,n): return int(s[p:p+n][::-1],2)
def parse(path):
    d=open(path,'rb').read()
    assert d[:4]==b'SSS\0' and d[4:6]==b'02'
    gold=struct.unpack_from('<I',d,6)[0]; npages=struct.unpack_from('<I',d,10)[0]
    pos=14; pages=[]
    for pg in range(npages):
        assert d[pos:pos+2]==b'ST',(pg,pos)
        flags=struct.unpack_from('<I',d,pos+2)[0]; e=d.index(0,pos+6); name=d[pos+6:e].decode('latin1')
        assert d[e+1:e+3]==b'JM'; cnt=struct.unpack_from('<H',d,e+3)[0]
        start=e+5
        pages.append(dict(flags=flags,name=name,count=cnt,start=start,hdr=pos))
        pos=start  # records follow; the next page begins at the next 'ST' that follows the last record (found below)
        # find end of this page's data: next page header position is located by scanning for the following 'ST' + valid header
        nxt=None
        if pg+1<npages:
            q=start
            while True:
                q=d.index(b'ST',q)
                try:
                    e2=d.index(0,q+6)
                    if d[e2+1:e2+3]==b'JM' and e2-q<40 and all(32<=c<127 for c in d[q+6:e2]): nxt=q;break
                except ValueError: pass
                q+=1
            pos=nxt
        else: nxt=len(d)
        pages[-1]['end']=nxt
    return d,gold,pages
def records(d,pg):
    """split a page's data region into item records by 'JM' markers followed by a plausible item head"""
    a,b=pg['start'],pg['end']; s=None; out=[]
    i=a
    cuts=[]
    while True:
        i=d.find(b'JM',i,b)
        if i<0: break
        cuts.append(i); i+=2
    # keep only plausible heads: version 100..105 at bit 32..42 of flags, mode<=6
    good=[]
    for c in cuts:
        if c+12>b: continue
        h=bits(d[c+2:c+12])
        ver=val(h,32,10); mode=val(h,42,3)
        if 99<=ver<=106 and mode<=6: good.append(c)
    recs=[(good[k],good[k+1] if k+1<len(good) else b) for k in range(len(good))]
    return recs
if __name__=='__main__':
    d,gold,pages=parse(sys.argv[1])
    print('pages',len(pages),'gold',gold,'size',len(d))
    bad=0; tot=0; kids=0
    for i,pg in enumerate(pages):
        recs=records(d,pg); top=0
        for a,b in recs:
            h=bits(d[a+2:a+12]); mode=val(h,42,3)
            if mode==6: kids+=1
            else: top+=1
        tot+=top
        if top!=pg['count']:
            bad+=1
            if bad<=8: print('page',i+1,'header count',pg['count'],'parsed top-level',top,'records',len(recs))
    print('top-level items',tot,'socket children',kids,'pages with count mismatch',bad)
