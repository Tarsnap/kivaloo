"""Real KVLDS/LBS daemons; delay one genuine APPEND response without changing bytes."""
import ctypes, hashlib, json, os, pathlib, platform, signal, socket, struct, subprocess, tempfile, threading, time
R=pathlib.Path(__file__).resolve().parent
ctypes.CDLL(None).prctl(36,1,0,0,0)

def receive(sock,n):
    chunks=[]
    while n:
        b=sock.recv(n)
        if not b:return None
        chunks.append(b);n-=len(b)
    return b''.join(chunks)

class Proxy:
    def __init__(self,path,target,arm):
        self.arm=arm; self.target=target; self.messages=[];self.requests={};self.delayed=False;self.sockets=[];self.threads=[]
        self.server=socket.socket(socket.AF_UNIX);self.server.bind(str(path));self.server.listen(1)
        t=threading.Thread(target=self.accept,daemon=True);self.threads.append(t);t.start()
    def accept(self):
        try:
            client,_=self.server.accept();server=socket.socket(socket.AF_UNIX);server.connect(str(self.target));self.sockets=[client,server]
            for left,right,request in ((client,server,True),(server,client,False)):
                t=threading.Thread(target=self.pump,args=(left,right,request),daemon=True);self.threads.append(t);t.start()
        except OSError as e:self.messages.append({'accept_error':str(e)})
    def pump(self,left,right,request):
        try:
            while True:
                h=receive(left,16)
                if h is None:return
                ident,length=struct.unpack('>QI',h[:12])
                if length>1024*1024:raise ValueError('unexpected fixture frame size')
                payload=receive(left,length+4)
                if payload is None:return
                if request:
                    kind=struct.unpack('>I',payload[:4])[0];self.requests[ident]=kind
                else:
                    kind=self.requests.get(ident)
                    if kind==2 and self.arm.exists() and not self.delayed:
                        self.delayed=True;self.messages.append({'delayed_response_id':ident,'record_length':length,'delay_seconds':2.2,'unchanged_packet_sha256':hashlib.sha256(h+payload).hexdigest()})
                        time.sleep(2.2)
                right.sendall(h+payload)
        except (OSError,ValueError) as e:self.messages.append({'pump_error':str(e)})
    def close(self):
        for s in self.sockets:
            try:s.shutdown(socket.SHUT_RDWR)
            except OSError:pass
            s.close()
        self.server.close()
        for t in self.threads:t.join(timeout=.5)

def wait_daemon(pid,limit):
    until=time.monotonic()+limit
    while time.monotonic()<until:
        p,status=os.waitpid(pid,os.WNOHANG)
        if p:return os.waitstatus_to_exitcode(status)
        time.sleep(.02)
    return None

def terminate(pid):
    if not pid:return
    try:os.kill(pid,signal.SIGTERM)
    except ProcessLookupError:return
    try:os.waitpid(pid,0)
    except ChildProcessError:pass

results=[]
for variant in ('before','after'):
    with tempfile.TemporaryDirectory(prefix='kvclean334-') as td:
        root=pathlib.Path(td); (root/'storage').mkdir();lbs=root/'lbs.sock';via=root/'via.sock';kv=root/'kv.sock';arm=root/'delay-next-append'
        lpid=kpid=None;proxy=None
        with (R/(variant+'-lbs.log')).open('w') as llog,(R/(variant+'-kvlds.log')).open('w') as klog:
            try:
                lc=[str(R.parent/'lbs-recovery/lbs-before'),'-s',str(lbs),'-d',str(root/'storage'),'-b','1024','-n','1','-p',str(root/'lbs.pid')]
                subprocess.run(lc,stdout=llog,stderr=llog,check=True,timeout=5);lpid=int((root/'lbs.pid').read_text())
                proxy=Proxy(via,lbs,arm)
                kc=[str(R/('kvlds-'+variant)),'-s',str(kv),'-l',str(via),'-C','1024','-S','1000000000000','-1','-p',str(root/'kv.pid')]
                subprocess.run(kc,stdout=klog,stderr=klog,check=True,timeout=5);kpid=int((root/'kv.pid').read_text())
                begin=time.monotonic()
                try:
                    client=subprocess.run([str(R/'client'),str(kv),str(arm)],capture_output=True,text=True,timeout=12)
                    result={'variant':variant,'client_exit':client.returncode,'client_stdout':client.stdout,'client_stderr':client.stderr,'client_seconds':round(time.monotonic()-begin,6)}
                except subprocess.TimeoutExpired as e:
                    result={'variant':variant,'client_exit':None,'client_timeout':True,'client_stdout':str(e.stdout),'client_stderr':str(e.stderr)}
                code=wait_daemon(kpid,3)
                result['kvlds_exit']=code
                if code is not None:kpid=None
                result['storage_files']=[p.name for p in (root/'storage').iterdir()];result['lbs_command']=lc;result['kvlds_command']=kc;result['proxy_messages']=list(proxy.messages)
                results.append(result)
            finally:
                terminate(kpid)
                if proxy:proxy.close()
                terminate(lpid)
        results[-1]['kvlds_log']=(R/(variant+'-kvlds.log')).read_text()
        print(json.dumps(results[-1],indent=2),flush=True)
manifest={'source_commit':'787805e011a62c7b8a87bfae1ed156e5adb4af2c','platform':platform.platform(),'compiler':subprocess.check_output(['gcc','--version'],text=True).splitlines()[0],'scope':'two real daemons, one delayed genuine APPEND response per variant; only normal callback_clean group-release repair in counterfactual','results':results,'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in R.iterdir() if p.is_file() and p.suffix in ('.c','.py','.log')}}
(R/'result.json').write_text(json.dumps(manifest,indent=2)+'\n')
