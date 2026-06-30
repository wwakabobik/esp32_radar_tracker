from __future__ import annotations

import asyncio
import json
import logging

from config import DISCOVERY_PORT, MQTT_PORT, OTA_PORT
from netutil import get_lan_ip

logger = logging.getLogger(__name__)

DISCOVER_MAGIC = b"PHUB_DISCOVER"


def hub_payload() -> bytes | None:
    lan_ip = get_lan_ip()
    if not lan_ip:
        return None
    return json.dumps(
        {
            "mqtt_host": lan_ip,
            "mqtt_port": MQTT_PORT,
            "ota_host": lan_ip,
            "ota_port": OTA_PORT,
        }
    ).encode()


def subnet_broadcast(lan_ip: str) -> str:
    parts = lan_ip.split(".")
    if len(parts) == 4:
        return f"{parts[0]}.{parts[1]}.{parts[2]}.255"
    return "255.255.255.255"


class DiscoveryProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.transport = transport
        logger.info("UDP discovery listening on :%d", DISCOVERY_PORT)

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if data.strip() != DISCOVER_MAGIC:
            return
        payload = hub_payload()
        if not payload:
            logger.warning("Discovery request from %s but no LAN IP", addr)
            return
        self.transport.sendto(payload, addr)
        logger.info("Discovery reply to %s -> %s", addr, payload.decode())


async def discovery_beacon_loop() -> None:
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(asyncio.DatagramProtocol, local_addr=("0.0.0.0", 0))
    try:
        while True:
            payload = hub_payload()
            if payload:
                lan_ip = get_lan_ip()
                if lan_ip:
                    for target in (subnet_broadcast(lan_ip), "255.255.255.255"):
                        transport.sendto(payload, (target, DISCOVERY_PORT))
            await asyncio.sleep(45)
    finally:
        transport.close()


async def discovery_loop() -> None:
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        DiscoveryProtocol,
        local_addr=("0.0.0.0", DISCOVERY_PORT),
    )
    try:
        await asyncio.Future()
    finally:
        transport.close()
