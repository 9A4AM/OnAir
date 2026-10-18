#!/usr/bin/env python3
# click the n-th tab (default: the second) of the main tab row: find the active tab's top line, then click right of it
import numpy as np, subprocess, sys, os, time
n=int(sys.argv[1]) if len(sys.argv)>1 else 2
raw=subprocess.run(['ffmpeg','-loglevel','error','-f','x11grab','-video_size','1920x1080','-i',os.environ.get('DISPLAY',':87'),'-frames:v','1','-f','rawvideo','-pix_fmt','gray','-'],capture_output=True).stdout
a=np.frombuffer(raw,np.uint8).reshape(1080,1920)
for y in range(80,320):
    if (a[y,305:375]>110).mean()>.85:
        x=405 if n==2 else 470
        subprocess.run(['xdotool','mousemove',str(x),str(y+8)]); time.sleep(.25); subprocess.run(['xdotool','mousemove',str(x+2),str(y+8)]); time.sleep(.15)
        subprocess.run(['xdotool','mousedown','1']); time.sleep(.12); subprocess.run(['xdotool','mouseup','1']); time.sleep(.3)
        subprocess.run(['xdotool','mousemove','1915','1075']); print('clicked tab at',x,y+8); break
else: print('tab row not found')
