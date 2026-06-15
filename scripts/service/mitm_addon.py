"""
mitmproxy addon — logs all HTTP requests/responses (used with mitmdump -s mitm_addon.py)
"""

import json
import re

from mitmproxy import http

HIGHLIGHT = "\033[93m"
RED = "\033[91m"
CYAN = "\033[96m"
GREEN = "\033[92m"
RESET = "\033[0m"

INTERESTING_PATTERNS = [
    (re.compile(r"(?i)(token|api.?key|secret|password|passwd|auth|authorization)"), RED),
    (re.compile(r"(?i)(device.?id|device_id|devid|uuid|serial|mac|imei)"), CYAN),
    (re.compile(r"(?i)(server|host|endpoint|url|broker|mqtt)"), GREEN),
]


def _highlight_interesting(text: str) -> str:
    for pattern, color in INTERESTING_PATTERNS:
        text = pattern.sub(lambda m: f"{color}{m.group(0)}{RESET}", text)
    return text


def request(flow: http.HTTPFlow) -> None:
    req = flow.request
    print(f"\n{HIGHLIGHT}>>> REQUEST{RESET}  {req.method} {req.pretty_url}")

    if req.headers:
        for k, v in req.headers.items():
            line = f"  {k}: {v}"
            print(_highlight_interesting(line))

    if req.content:
        body = req.content.decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
            body = json.dumps(parsed, indent=2, ensure_ascii=False)
        except Exception:
            pass
        print(f"  Body:\n{_highlight_interesting(body)}")


def response(flow: http.HTTPFlow) -> None:
    resp = flow.response
    print(f"\n{GREEN}<<< RESPONSE{RESET} {resp.status_code} {flow.request.pretty_url}")

    if resp.headers:
        for k, v in resp.headers.items():
            print(f"  {k}: {v}")

    if resp.content:
        body = resp.content.decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
            body = json.dumps(parsed, indent=2, ensure_ascii=False)
        except Exception:
            pass
        if len(body) > 2000:
            print(f"  Body: [truncated, {len(body)} bytes]")
        else:
            print(f"  Body:\n{_highlight_interesting(body)}")
