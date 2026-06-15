from __future__ import annotations

import asyncio
import json
import logging

from config import DISCOVERY_PORT, MQTT_PORT, OTA_PORT
from netutil import get_lan_ip

logger = logging.getLogger(__name__)

DISCOVER_MAGIC = b"PHUB_DISCOVER"


class DiscoveryProtocol(asyncio.DatagramProtocol):
    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.transport = transport
        logger.info("UDP discovery listening on :%d", DISCOVERY_PORT)

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if data.strip() != DISCOVER_MAGIC:
            return
        lan_ip = get_lan_ip()
        if not lan_ip:
            logger.warning("Discovery request from %s but no LAN IP", addr)
            return
        payload = json.dumps(
            {
                "mqtt_host": lan_ip,
                "mqtt_port": MQTT_PORT,
                "ota_host": lan_ip,
                "ota_port": OTA_PORT,
            }
        ).encode()
        self.transport.sendto(payload, addr)
        logger.info("Discovery reply to %s -> mqtt %s:%d", addr, lan_ip, MQTT_PORT)


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
