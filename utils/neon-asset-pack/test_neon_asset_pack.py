import unittest

from cryptography.exceptions import InvalidTag

from neon_asset_pack import pack_asset, unpack_asset


class NeonAssetPackTests(unittest.TestCase):
    def setUp(self):
        self.key = bytes(range(32))
        self.package_id = bytes(range(16))
        self.resource = "neon-encrypted-assets"
        self.path = "models/example.dff.neonasset"
        self.plaintext = b"DFF fixture bytes\x00\x01\x02"

    def test_round_trip_is_bound_to_resource_path_and_type(self):
        container = pack_asset(self.plaintext, self.key, self.package_id, self.resource, self.path, "dff", nonce=b"N" * 12)
        asset_type, plaintext = unpack_asset(container, self.key, self.package_id, self.resource, self.path)
        self.assertEqual(asset_type, "dff")
        self.assertEqual(plaintext, self.plaintext)

    def test_ciphertext_tampering_is_rejected(self):
        container = bytearray(pack_asset(self.plaintext, self.key, self.package_id, self.resource, self.path, "dff", nonce=b"N" * 12))
        container[-17] ^= 1
        with self.assertRaises(InvalidTag):
            unpack_asset(bytes(container), self.key, self.package_id, self.resource, self.path)

    def test_path_or_resource_substitution_is_rejected(self):
        container = pack_asset(self.plaintext, self.key, self.package_id, self.resource, self.path, "dff", nonce=b"N" * 12)
        with self.assertRaises(InvalidTag):
            unpack_asset(container, self.key, self.package_id, self.resource, "models/renamed.dff.neonasset")
        with self.assertRaises(InvalidTag):
            unpack_asset(container, self.key, self.package_id, "other-resource", self.path)

    def test_parent_paths_are_rejected(self):
        with self.assertRaises(ValueError):
            pack_asset(self.plaintext, self.key, self.package_id, self.resource, "models/../secret.dff.neonasset", "dff")


if __name__ == "__main__":
    unittest.main()
