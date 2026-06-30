from __future__ import annotations

import json
import unittest
from unittest.mock import patch

from discovery import DISCOVER_MAGIC, hub_payload, subnet_broadcast


class SubnetBroadcastTests(unittest.TestCase):
    def test_class_c_broadcast(self) -> None:
        self.assertEqual(subnet_broadcast("192.168.0.30"), "192.168.0.255")

    def test_other_subnet(self) -> None:
        self.assertEqual(subnet_broadcast("10.20.5.1"), "10.20.5.255")

    def test_invalid_falls_back_to_global(self) -> None:
        self.assertEqual(subnet_broadcast("bad"), "255.255.255.255")


class HubPayloadTests(unittest.TestCase):
    @patch("discovery.get_lan_ip", return_value="192.168.0.30")
    def test_payload_fields(self, _mock: object) -> None:
        raw = hub_payload()
        assert raw is not None
        doc = json.loads(raw.decode())
        self.assertEqual(doc["mqtt_host"], "192.168.0.30")
        self.assertEqual(doc["mqtt_port"], 18830)
        self.assertEqual(doc["ota_host"], "192.168.0.30")
        self.assertEqual(doc["ota_port"], 18081)

    @patch("discovery.get_lan_ip", return_value=None)
    def test_missing_lan_ip(self, _mock: object) -> None:
        self.assertIsNone(hub_payload())


class DiscoveryMagicTests(unittest.TestCase):
    def test_magic_is_ascii(self) -> None:
        self.assertEqual(DISCOVER_MAGIC, b"PHUB_DISCOVER")


if __name__ == "__main__":
    unittest.main()
