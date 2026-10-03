import argparse
import json
import signal
import sys
import traceback

from .common import BackendError, Paths
from .service import PROTOCOL_VERSION, Service, validate_request


MAX_REQUEST_BYTES = 1024 * 1024


def main() -> int:
    parser = argparse.ArgumentParser(description="Headless Forest backend; accepts one JSON request on stdin.")
    parser.add_argument("--data-root", help="Isolate all mutable data under this directory.")
    args = parser.parse_args()
    service = None
    try:
        raw = sys.stdin.buffer.read(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            raise BackendError("Backend request exceeds the size limit.")
        try:
            request = json.loads(raw)
        except (ValueError, UnicodeDecodeError):
            raise BackendError("Backend request must be valid UTF-8 JSON.") from None
        # Validate protocol before opening or creating the library.
        validate_request(request)
        if request.get("action") in ("download_proton", "download_latest_proton"):
            def cancel(signum, frame):
                raise BackendError("Download cancelled.", "cancelled")
            signal.signal(signal.SIGTERM, cancel)
        def progress(event):
            print("FOREST_PROGRESS " + json.dumps(event), file=sys.stderr, flush=True)
        service = Service(Paths.create(args.data_root), progress)
        response = {"protocol": PROTOCOL_VERSION, "ok": True, "data": service.dispatch(request)}
        exit_code = 0
    except BackendError as exc:
        response = {"protocol": PROTOCOL_VERSION, "ok": False,
                    "error": {"code": exc.code, "message": str(exc)}}
        exit_code = 1
    except Exception as exc:
        traceback.print_exc(file=sys.stderr)
        response = {"protocol": PROTOCOL_VERSION, "ok": False,
                    "error": {"code": "backend_error", "message": str(exc)}}
        exit_code = 1
    finally:
        if service:
            service.close()
    print(json.dumps(response, ensure_ascii=False))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
