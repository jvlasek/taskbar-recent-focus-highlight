"""Record manual Windhawk testing until Ctrl+C; no UWPSpy attachment needed."""
import argparse
from datetime import datetime
from pathlib import Path
import time
from windhawk_log import WindhawkLog, default_collector


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'captures',
                        help='Parent directory for a new timestamped recording')
    parser.add_argument('--dbgview', type=Path, default=default_collector())
    args = parser.parse_args()
    output = args.output.resolve() / datetime.now().strftime('manual-%Y%m%d-%H%M%S-%f')
    output.mkdir(parents=True, exist_ok=False)
    capture = WindhawkLog(output, lambda: 'manual')
    status = 0
    try:
        capture.start(args.dbgview)
        print('Recording ready. Enable Mod logs in Windhawk, then reproduce the issue.', flush=True)
        print(f'Logs: {output}', flush=True)
        print('Press Ctrl+C here to stop. Keep other debug viewers closed.', flush=True)
        while True:
            capture.check()
            time.sleep(0.25)
    except KeyboardInterrupt:
        print('\nStopping recording...')
    except Exception as error:
        print(f'Capture failed: {error}')
        status = 1
    finally:
        try:
            capture.close()
        except Exception as error:
            print(f'Capture cleanup failed: {error}')
            status = 1
        print(f'Evidence: {output}')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
