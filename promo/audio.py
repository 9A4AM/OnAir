import numpy as np, wave
SR=44100; DUR=53.9; N=int(SR*DUR); rng=np.random.default_rng(7)
L=np.zeros(N); R=np.zeros(N); T=np.arange(N)/SR
def add(sig,t0,pan=0.0,g=1.0):
    i=int(t0*SR); 
    if i<0: sig=sig[-i:]; i=0
    j=min(N,i+len(sig)); sig=sig[:j-i]*g
    L[i:j]+=sig*(1-max(0,pan)); R[i:j]+=sig*(1+min(0,pan))
def env(n,a,d_pow=3.0):
    x=np.arange(n)/SR; e=np.minimum(1,x/max(a,1e-4)); return e*np.exp(-d_pow*x/(n/SR))
def fftfilt(x,fn):
    X=np.fft.rfft(x); f=np.fft.rfftfreq(len(x),1/SR); return np.fft.irfft(X*fn(f),len(x))
def noise(dur): return rng.standard_normal(int(dur*SR))
def whoosh(t0,dur=1.3,up=True,g=.5):
    n=int(dur*SR); x=np.arange(n)/n; w=noise(dur); out=np.zeros(n)
    # swept bandpass via chunked filtering
    for k in range(0,n,2048):
        c=(300+ (x[k]**1.6 if up else (1-x[k])**1.6)*7000); seg=w[k:k+2048]
        S=np.fft.rfft(seg*np.hanning(len(seg))); f=np.fft.rfftfreq(len(seg),1/SR); S*=np.exp(-((np.log(f+1)-np.log(c))**2)/.5)
        out[k:k+2048]+=np.fft.irfft(S,len(seg))
    e=np.sin(np.pi*x)**2; out*=e; out/=np.abs(out).max()+1e-9; add(out,t0,0,g)
def boom(t0,g=.9,f0=70):
    n=int(2.2*SR); x=np.arange(n)/SR; fr=f0*np.exp(-x*1.3)+32; ph=2*np.pi*np.cumsum(fr)/SR; s=np.sin(ph)*np.exp(-x*1.8)
    s+=0.4*fftfilt(noise(2.2),lambda f:np.exp(-f/2500))*np.exp(-x*5); add(s,t0,0,g)
def kick(t0,g=.5):
    n=int(.35*SR); x=np.arange(n)/SR; fr=120*np.exp(-x*18)+42; ph=2*np.pi*np.cumsum(fr)/SR; add(np.sin(ph)*np.exp(-x*9),t0,0,g)
def tick(t0,f=1800,g=.12,pan=0):
    n=int(.12*SR); x=np.arange(n)/SR; add(np.sin(2*np.pi*f*x)*np.exp(-x*45),t0,pan,g)
def hat(t0,g=.05,pan=0):
    n=int(.08*SR); x=np.arange(n)/SR; h=np.diff(noise(.08+1/SR),prepend=0)[:n]; add(h*np.exp(-x*50),t0,pan,g)
def pluck(t0,f,g=.08,pan=0,dur=1.6):
    n=int(dur*SR); x=np.arange(n)/SR; s=sum(np.sin(2*np.pi*f*k*x)/k**1.5 for k in (1,2,3,4))*np.exp(-x*2.4); s*=np.minimum(1,x/.004); add(s,t0,pan,g)
def pad(t0,t1,notes,g=.05):
    n=int((t1-t0)*SR); x=np.arange(n)/SR; out=np.zeros(n)
    for i,f in enumerate(notes):
        for det in (-.3,0,.3):
            ff=f*(1+det*.004); out+=np.sin(2*np.pi*ff*x+np.sin(x*.4+i)*1.5)+0.35*np.sin(4*np.pi*ff*x)
    out=fftfilt(out,lambda f:1/(1+(f/900)**2)); out/=np.abs(out).max()+1e-9
    a=np.minimum(1,x/2.0)*np.minimum(1,(t1-t0-x)/2.0); add(out*a*(1+.15*np.sin(x*.8)),t0,0,g*3)
def hz(m): return 440*2**((m-69)/12)
TR=[5.5,10.1,18.9,25.8,32.7,41.0,46.3]
prog=[[45,57,60,64],[41,53,57,60],[48,60,64,67],[43,55,59,62]]
for k in range(0,8):
    t0=k*7.6
    if t0>=DUR: break
    pad(t0,min(DUR,t0+8.6),[hz(m) for m in prog[k%4]],g=.05)
# hook: three hits, each led by a short riser
for t0 in (0.0,2.0,4.0):
    if t0>0: whoosh(t0-.9,.9,True,.4)
    boom(t0+.02,.75,85)
    for i,m in enumerate([69,76,81]): pluck(t0+.03+i*.07,hz(m),.05,(-1)**i*.4,1.0)
whoosh(5.0,.8,True,.4)
# logo pop
boom(5.62,.9,90)
for i,m in enumerate([81,84,88,93]): pluck(5.7+i*.09,hz(m),.07,(-1)**i*.4,2.0)
# transitions
for i,tr in enumerate(TR[1:],1): whoosh(tr-.12,1.2,up=i%2==1,g=.42)
# callout / checklist ticks
for k,t0 in enumerate([12.0,12.5,13.0]): tick(t0,2400,.09,(-1)**k*.4)
for k,t0 in enumerate([19.8,20.5,21.2,21.9]): tick(t0,1500+k*200,.12)
for k,t0 in enumerate([23.9,24.5]): tick(t0,2600,.09,(-1)**k*.5)
for k,t0 in enumerate([26.2,26.7,27.2]): tick(t0,1800+k*250,.09)
for k,t0 in enumerate([34.5,35.7,38.1]): tick(t0,2200,.09,(-1)**k*.5)
for k,t0 in enumerate([42.4,42.75,43.1]): tick(t0,2000+k*300,.1)
# beat from the logo on, 112 bpm
bt=60/112; t=6.2
while t<45.5:
    kick(t,.30 if t<10 else .42); hat(t+bt/2,.05+(.02 if t>18 else 0))
    if t>18 and int((t-6.2)/bt)%2==1: hat(t+bt*.25,.03,.3)
    t+=bt
arp=[[69,72,76,81],[65,69,72,77],[72,76,79,84],[67,71,74,79]]
t=10.4; step=bt/2; j=0
while t<45.5:
    ch=arp[int((t-10.4)//(bt*8))%4]; pluck(t,hz(ch[j%4]),.035,(-1)**j*.5,.9); j+=1; t+=step
whoosh(44.4,2.0,True,.5)
boom(46.5,1.0,80)
for i,m in enumerate([69,76,81,84,88,93]): pluck(46.55+i*.1,hz(m),.08,(-1)**i*.5,2.5)
pad(46.3,53.9,[hz(m) for m in (45,57,64,69,72)],g=.06)
# air shimmer high
sh=fftfilt(noise(DUR),lambda f:(f>4000)*(1/(1+(f/9000)**2)))*.004; L+=sh; R+=sh[::-1]
# master fades
fade=np.minimum(1,T/1.0)*np.minimum(1,(DUR-T)/2.0); L*=fade; R*=fade
M=np.stack([L,R],1); M/= np.abs(M).max()*1.15
M=np.tanh(M*1.3)/np.tanh(1.3)
w=wave.open('audio.wav','wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes((M*32000).astype('<i2').tobytes()); w.close()
