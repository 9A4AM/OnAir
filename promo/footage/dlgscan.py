#!/usr/bin/env python3
# flags captures whose frames show the 'share positions with map.on-air.dev' dialog: its blue title bar sits at about x 706-1212, y 424-436
import sys, subprocess, numpy as np
def frame(path,t):
    raw=subprocess.run(['ffmpeg','-loglevel','error','-ss',str(t),'-i',path,'-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','-'],capture_output=True).stdout
    return np.frombuffer(raw,np.uint8).reshape(1080,1920,3) if len(raw)==1080*1920*3 else None
def has_dialog(path):
    for t in (1.0,3.0,6.0):
        f=frame(path,t)
        if f is None: continue
        bar=f[428:434,740:1180].astype(int); r,g,b=bar[...,0].mean(),bar[...,1].mean(),bar[...,2].mean()
        if b>r+35 and b>g+15 and b>90: return True
    return False
if __name__=='__main__':
    bad=[p for p in sys.argv[1:] if has_dialog(p)]
    print('dialog in:',' '.join(p.split('/')[-1] for p in bad) if bad else 'none')
