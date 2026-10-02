"""Record native foreground observations and Windhawk logs; Ctrl+C stops both."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import time

from windhawk_log import WindhawkLog, default_collector


class ForegroundLog:
    def __init__(self, output):
        self.output = output
        self.process = None
        self.thread = None
        self.ready = threading.Event()
        self.error = None

    def start(self, executable):
        self.stderr = (self.output / 'foreground-stderr.log').open('wb')
        try:
            self.process = subprocess.Popen(
                [str(executable), '--stdin-stop'], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=self.stderr,
                creationflags=subprocess.CREATE_NO_WINDOW)
        except BaseException:
            self.stderr.close()
            raise
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()
        deadline = time.monotonic() + 5
        while not self.ready.wait(.1):
            self.check()
            if time.monotonic() >= deadline:
                raise RuntimeError('Foreground observer startup timed out')
        self.check()

    def _read(self):
        try:
            with (self.output / 'foreground.jsonl').open('wb') as stream:
                for line in self.process.stdout:
                    stream.write(line)
                    stream.flush()
                    if json.loads(line).get('kind') == 'READY':
                        self.ready.set()
        except Exception as error:
            self.error = f'Foreground reader failed: {error}'

    def check(self):
        if self.error:
            raise RuntimeError(self.error)
        if self.process and self.process.poll() is not None:
            raise RuntimeError('Foreground observer exited; see foreground-stderr.log')
        if self.thread and not self.thread.is_alive():
            raise RuntimeError('Foreground observer reader stopped')

    def close(self):
        if not self.process:
            return
        forced = False
        try:
            # EOF is the observer's graceful stop request; no console signalling needed.
            self.process.stdin.close()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                forced = True
                self.process.kill()
                self.process.wait(timeout=5)
            self.thread.join(timeout=5)
            if self.thread.is_alive():
                raise RuntimeError('Foreground reader did not finish')
            if forced or self.process.returncode or self.error:
                raise RuntimeError(self.error or f'Observer shutdown failed (forced={forced}, exit={self.process.returncode})')
        finally:
            self.process.stdout.close()
            self.stderr.close()


def build_observer(compiler):
    root = Path(__file__).resolve().parent
    source = root / 'foreground_observer.c'
    binary = root / 'bin' / 'ForegroundObserver.exe'
    if not binary.exists() or binary.stat().st_mtime < source.stat().st_mtime:
        binary.parent.mkdir(exist_ok=True)
        print('Building foreground observer...', flush=True)
        result = subprocess.run([str(compiler), '-x', 'c', '-std=c11', '-target',
                        'x86_64-w64-mingw32', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(source), '-luser32', '-o', str(binary)],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise RuntimeError('Observer build failed:\n' + result.stdout.decode('utf-8', errors='replace'))
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'captures')
    parser.add_argument('--dbgview', type=Path, default=default_collector())
    parser.add_argument('--compiler', type=Path, default=Path(os.environ.get(
        'ProgramFiles', r'C:\Program Files')) / 'Windhawk/Compiler/bin/clang++.exe')
    parser.add_argument('--seconds', type=float, help='Stop automatically after this many seconds')
    args = parser.parse_args()
    if args.seconds is not None and not (0 < args.seconds < 86400):
        parser.error('--seconds must be positive and less than 86400')
    output = args.output.resolve() / datetime.now().strftime('focus-%Y%m%d-%H%M%S-%f')
    output.mkdir(parents=True, exist_ok=False)
    windhawk = WindhawkLog(output, lambda: 'manual-focus')
    foreground = ForegroundLog(output)
    status = 0
    try:
        binary = build_observer(args.compiler)
        windhawk.start(args.dbgview)
        foreground.start(binary)
        print('Both recordings ready. Enable Mod logs; keep other debug viewers closed.', flush=True)
        print(f'Logs: {output}', flush=True)
        print('Reproduce with the existing Calculators. Press Ctrl+C here to stop BOTH recordings.', flush=True)
        deadline = time.monotonic() + args.seconds if args.seconds is not None else None
        while deadline is None or time.monotonic() < deadline:
            windhawk.check()
            foreground.check()
            time.sleep(.25)
    except KeyboardInterrupt:
        print('\nStopping both recordings...', flush=True)
    except Exception as error:
        print(f'Capture failed: {error}', flush=True)
        status = 1
    finally:
        # A second Ctrl+C must not interrupt cleanup and leave a collector behind.
        previous = signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            for capture in (foreground, windhawk):
                try:
                    capture.close()
                except Exception as error:
                    print(f'Capture cleanup failed: {error}', flush=True)
                    status = 1
        finally:
            signal.signal(signal.SIGINT, previous)
        print(f'Evidence: {output}', flush=True)
    return status


if __name__ == '__main__':
    raise SystemExit(main())
