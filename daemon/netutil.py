from __future__ import annotations

import os
import socket


def get_lan_ip() -> str | None:
    """Best-effort primary LAN IPv4 (same idea as scripts/lan_ip.sh)."""
    override = os.getenv("HUB_LAN_IP", "").strip()
    if override:
        return override

    for iface in ("en0", "en1"):
        try:
            import fcntl
            import struct

            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            ip = socket.inet_ntoa(
                fcntl.ioctl(
                    sock.fileno(),
                    0x8915,  # SIOCGIFADDR
                    struct.pack("256s", iface.encode()[:15]),
                )[20:24]
            )
            sock.close()
            if ip and not ip.startswith("127."):
                return ip
        except OSError:
            continue

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(("8.8.8.8", 80))
        ip = sock.getsockname()[0]
        sock.close()
        if ip and not ip.startswith("127."):
            return ip
    except OSError:
        pass

    return None


def hub_lan_ip() -> str:
    from config import OTA_HOST

    return get_lan_ip() or OTA_HOST
