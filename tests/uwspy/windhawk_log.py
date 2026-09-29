"""Optional local-session OutputDebugString capture through bundled DbgViewMini."""
import ctypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid


def default_collector():
    return Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'Windhawk/UI/resources/app/extensions/windhawk/files/DbgViewMini.exe'


class WindhawkLog:
    def __init__(self, output, label):
        self.output=Path(output)
        self.label=label
        self.process=None
        self.thread=None
        self.error=None
        self.ready=threading.Event()
        self.marker='[WH] harness-capture-probe-' + str(uuid.uuid4())

    def start(self, executable):
        self.process=subprocess.Popen(
            [str(executable), '--local', '--no-buffering', '--pattern', '*[WH]*'],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW)
        self.thread=threading.Thread(target=self._read, daemon=True)
        self.thread.start()
        emit=ctypes.WinDLL('kernel32').OutputDebugStringW
        emit.argtypes=[ctypes.c_wchar_p]
        emit.restype=None
        deadline=time.monotonic()+5
        while not self.ready.wait(.1):
            self.check()
            if time.monotonic()>=deadline:
                raise RuntimeError('Windhawk log capture probe timed out; close other debug viewers and inspect windhawk.log')
            emit(self.marker)
        self.check()

    def _read(self):
        try:
            with (self.output/'windhawk.log').open('wb') as raw, (self.output/'windhawk.jsonl').open('w',encoding='utf-8') as annotated:
                for data in self.process.stdout:
                    raw.write(data);raw.flush()
                    line=data.decode('utf-8',errors='replace').rstrip('\r\n')
                    record={'utc':datetime.now(timezone.utc).isoformat(),
                            'monotonic':time.monotonic(),'label':self.label(),'line':line,
                            'probe':self.marker in line}
                    annotated.write(json.dumps(record,ensure_ascii=False)+'\n');annotated.flush()
                    if 'capture error:' in line:
                        self.error=line+'; close Windhawk Show log output / DebugView and retry'
                    if self.marker in line:self.ready.set()
        except Exception as error:
            self.error=f'Windhawk log reader failed: {error}'

    def check(self):
        if self.error:raise RuntimeError(self.error)
        if self.process and self.process.poll() is not None:
            raise RuntimeError('Windhawk log collector exited; see windhawk.log (another debug viewer may own capture)')
        if self.thread and not self.thread.is_alive():
            raise RuntimeError('Windhawk log reader stopped unexpectedly')

    def close(self):
        if self.process:
            if self.process.poll() is None:self.process.terminate()
            self.process.wait(timeout=5)
        if self.thread:
            self.thread.join(timeout=5)
            if self.thread.is_alive():raise RuntimeError('Windhawk log reader did not stop')
        if self.process:self.process.stdout.close()
        if self.error:raise RuntimeError(self.error)
