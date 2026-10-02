import numpy as np, wave
SR=44100; DUR=59.4; N=int(SR*DUR); rng=np.random.default_rng(7)
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
S=[(0,5.8),(5.1,10.7),(9.9,19.5),(18.8,25),(24.3,29.7),(29,34.4),(33.6,39.8),(39.1,44.5),(43.8,48.8),(48.1,52.7),(51.8,59.4)]
# harmony: Am - F - C - G style progression over 8s bars
prog=[[45,57,60,64],[41,53,57,60],[48,60,64,67],[43,55,59,62]]
for k in range(0,8):
    t0=k*7.6; 
    if t0>=DUR: break
    pad(t0,min(DUR,t0+8.6),[hz(m) for m in prog[k%4]],g=.05)
# whooshes at transitions
for i,(a,b) in enumerate(S[1:],1):
    tr=S[i][0]; whoosh(tr-.1,1.2,up=i%2==1,g=.45)
# opening: sparse ping at peaks
tick(.9+.6,2200,.10); tick(1.4+.6,1500,.08); tick(3.4,2600,.1,.3)
# logo pop
boom(7.0,.8,90); 
for i,m in enumerate([81,84,88,93]): pluck(7.05+i*.09,hz(m),.07,(-1)**i*.4)
# lamps in app
for i in range(11): tick(9.9+.9+i*.22,1500+i*130,.1,-.3+i*.06)
tick(9.9+.9+11*.22+.1,3000,.12)
# callouts
for k,t0 in enumerate([3.0,3.5,4.0,4.5,5.0]): tick(9.9+t0,2400,.08,(-1)**k*.5)
# standards scan ticks
for k,t0 in enumerate([2.2,2.9,3.6]): tick(18.8+t0,1900+k*300,.12)
# stats: counter ticks
for i in range(11): tick(24.3+.5+i*.17,1200+i*80,.07)
boom(24.3+.4,.5,70)
# beat from s4 onwards (kick on beat, hats off-beat), 112 bpm -> .536s
bt=60/112
t=18.8
while t<51.5:
    kick(t, .32 if t<24 else .45); hat(t+bt/2,.05 + (.02 if t>29 else 0)); 
    if t>29 and int((t-18.8)/bt)%2==1: hat(t+bt*.25,.03,.3)
    t+=bt
# arpeggio from 29
arp=[[69,72,76,81],[65,69,72,77],[72,76,79,84],[67,71,74,79]]
t=29.0; step=bt/2; j=0
while t<51.5:
    ch=arp[int((t-29)//(bt*8))%4]; pluck(t,hz(ch[j%4]),.035,(-1)**j*.5,.9); j+=1; t+=step
# build into outro
whoosh(49.8,2.4,True,.5)
# outro hit
boom(52.15,1.0,80)
for i,m in enumerate([69,76,81,84,88,93]): pluck(52.2+i*.1,hz(m),.08,(-1)**i*.5,2.5)
pad(52,59.4,[hz(m) for m in (45,57,64,69,72)],g=.06)
# air shimmer high
sh=fftfilt(noise(DUR),lambda f:(f>4000)*(1/(1+(f/9000)**2)))*.004; L+=sh; R+=sh[::-1]
# master fades
fade=np.minimum(1,T/1.0)*np.minimum(1,(DUR-T)/2.0); L*=fade; R*=fade
M=np.stack([L,R],1); M/= np.abs(M).max()*1.15
M=np.tanh(M*1.3)/np.tanh(1.3)
w=wave.open('audio.wav','wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes((M*32000).astype('<i2').tobytes()); w.close()
