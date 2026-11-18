"""Bounded child-process execution with cooperative cancellation."""
from __future__ import annotations
import json
import os
from pathlib import Path
import queue
import subprocess
import signal
import tempfile
import threading
import time
from contextlib import contextmanager

@contextmanager
def exclusive_lock(path,busy='Another XZ Mods window is using the separation engine'):
    with Path(path).open('a+b') as handle:
        if handle.tell()==0:handle.write(b'0');handle.flush()
        handle.seek(0)
        try:
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(handle.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as exc:raise RuntimeError(busy) from exc
        try:yield
        finally:
            handle.seek(0)
            if os.name=='nt':msvcrt.locking(handle.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(handle.fileno(),fcntl.LOCK_UN)

class Cancelled(Exception):
    pass

def _spawn(argv,*,cwd=None,env=None,stdin=subprocess.DEVNULL,stdout,stderr):
    return subprocess.Popen([str(x) for x in argv],cwd=cwd,env=env,stdin=stdin,stdout=stdout,stderr=stderr,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0,start_new_session=os.name!='nt')

def _terminate(process):
    if process.poll() is not None:return
    if os.name=='nt':
        subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/taskkill.exe'),'/PID',str(process.pid),'/T','/F'],capture_output=True,
            creationflags=subprocess.CREATE_NO_WINDOW)
    else:os.killpg(process.pid,signal.SIGTERM)
    try:process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        if os.name=='nt':process.kill()
        else:os.killpg(process.pid,signal.SIGKILL)
        process.wait()

def _tail(output,limit=12000):
    output.seek(0,2);size=output.tell();output.seek(max(0,size-limit))
    return output.read().decode('utf8','replace')

class Job:
    def __init__(self,cancel_file=None):
        self.cancel_file=Path(cancel_file) if cancel_file else None
    def check(self):
        if self.cancel_file and self.cancel_file.exists():raise Cancelled('Cancelled')
    def progress(self,stage,message,**details):
        self.check()
        print(json.dumps({'event':'progress','stage':stage,'message':message,**details}),flush=True)
    def run(self,argv,*,cwd=None,env=None):
        self.check()
        with tempfile.TemporaryFile() as output:
            process=_spawn(argv,cwd=cwd,env=env,stdout=output,stderr=subprocess.STDOUT)
            try:
                while process.poll() is None:
                    self.check();time.sleep(.15)
                details=_tail(output)
                if process.returncode:raise RuntimeError(f'Process failed ({process.returncode}): {details}')
                return details
            except BaseException:
                _terminate(process)
                raise
    def worker(self,argv,*,env=None):
        self.check()
        return Worker(self,argv,env=env)

class Worker:
    """A JSON-line request/response child that stays alive between requests, cancelled with the job."""
    def __init__(self,job,argv,*,env=None):
        self.job=job
        self.log=tempfile.TemporaryFile()
        self.process=_spawn(argv,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log)
        self.lines=queue.Queue()
        self.reader=threading.Thread(target=self._pump,daemon=True);self.reader.start()
    def _pump(self):
        for raw in self.process.stdout:
            self.lines.put(raw)
        self.lines.put(None)
    def request(self,payload,on_event):
        """Send one request; forward events until a terminal one. Returns the terminal event."""
        try:
            self.job.check()
            self.process.stdin.write(json.dumps(payload).encode()+b'\n');self.process.stdin.flush()
            while True:
                try:raw=self.lines.get(timeout=.15)
                except queue.Empty:
                    self.job.check();continue
                if raw is None:
                    self.process.wait()
                    raise RuntimeError(f'Separation engine exited ({self.process.returncode}): {_tail(self.log)}')
                try:event=json.loads(raw)
                except ValueError:continue
                if not isinstance(event,dict) or 'event' not in event:continue
                if event['event'] in ('done','failed'):return event
                on_event(event)
        except BaseException:
            self.close(kill=True)
            raise
    def close(self,kill=False):
        if self.process.poll() is None:
            if kill:_terminate(self.process)
            else:
                try:
                    self.process.stdin.write(b'{"op":"quit"}\n');self.process.stdin.flush();self.process.stdin.close()
                    self.process.wait(timeout=10)
                except (OSError,subprocess.TimeoutExpired):_terminate(self.process)
        for stream in (self.process.stdin,self.process.stdout):
            if stream:stream.close()
        self.log.close()
    def __enter__(self):return self
    def __exit__(self,*error):self.close(kill=error[0] is not None)
