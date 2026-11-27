#!/usr/bin/env python3
"""Check that the audit distinguishes code presence from behavior and missing handlers."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("protocol_audit", ROOT / "tools/protocol_audit.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class AuditTests(unittest.TestCase):
    def test_comments_and_strings_are_not_handlers(self):
        source = '''
        // On<FakePacket>([](const auto&) {});
        const char* example = "On<FakePacket>([](const auto&) {})";
        /* On<FakePacket>([](const auto&) {}); */
        void Session::Register() {
            On<RealPacket>([this](const auto& packet) {
                if (packet.ready) { SendReply(packet); }
            });
            OnPartial<PartialPacket>([](const auto& packet) { Log(packet); });
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/Handlers.cpp").write_text(source)
            evidence = AUDIT.source_evidence(root, {"FakePacket", "RealPacket", "PartialPacket"})
        self.assertEqual(evidence["FakePacket"]["handlers"], [])
        handler = evidence["RealPacket"]["handlers"][0]
        self.assertEqual(handler["kind"], "typed")
        self.assertEqual(handler["registration_function"], "Register")
        self.assertEqual(handler["send_calls_in_handler"], ["SendReply"])
        self.assertEqual(evidence["PartialPacket"]["handlers"][0]["kind"], "partial")

    def test_noop_does_not_hide_active_handler(self):
        source = '''
        void Session::Register() {
            On<RealPacket>([](const auto& p) { Apply(p); });
            On<EmptyPacket>([](const auto&) {});
            accept.template operator()<RealPacket, IgnoredPacket>();
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/Handlers.cpp").write_text(source)
            evidence = AUDIT.source_evidence(root, {"RealPacket", "EmptyPacket", "IgnoredPacket", "MissingPacket"})
        self.assertEqual(AUDIT.receive_status(evidence["RealPacket"]["handlers"]), "typed_handler_present")
        self.assertEqual(AUDIT.receive_status(evidence["EmptyPacket"]["handlers"]), "explicit_no_op")
        self.assertEqual(AUDIT.receive_status(evidence["IgnoredPacket"]["handlers"]), "explicit_no_op")
        self.assertEqual(AUDIT.receive_status(evidence["MissingPacket"]["handlers"]), "no_receive_handler_found")

    def test_recorded_client_replay_is_not_live_receive_handling(self):
        source = '''
        void Session::InjectClientPacket() {
            replayClientDispatcher.On<RecordedPacket>([](const auto& p) { Replay(p); });
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/Replay.cpp").write_text(source)
            evidence = AUDIT.source_evidence(root, {"RecordedPacket"})
        self.assertEqual(evidence["RecordedPacket"]["handlers"][0]["scope"], "recorded_client_replay")
        self.assertEqual(AUDIT.receive_status(evidence["RecordedPacket"]["handlers"]), "no_receive_handler_found")

    def test_current_schema_is_fully_enumerated_without_claiming_parity(self):
        data = AUDIT.make_inventory(ROOT, ROOT / "protocol/bedrock-1.26.60.json",
                                    ROOT / "protocol/overrides.json")
        spec = json.loads((ROOT / "protocol/bedrock-1.26.60.json").read_text())
        expected = next(f["type"][1]["mappings"] for f in spec["types"]["mcpe_packet"][1] if f["name"] == "name")
        self.assertEqual({p["id"] for p in data["packets"]}, {int(i) for i in expected})
        self.assertEqual(len(data["packets"]), len(expected))
        self.assertTrue(all(p["behavioral_equivalence"] == "not_established" for p in data["packets"]))
        self.assertEqual(data["summary"]["behavioral_equivalence_established"], 0)

    def test_protocol_enum_matches_the_reference_build(self):
        types = json.loads((ROOT / "protocol/bedrock-1.26.60.json").read_text())["types"]
        self.assertEqual(types["InputData"][1]["mappings"]["65"], "internal_update")
        self.assertEqual(types["Action"][1]["mappings"]["38"], "internal_update")

    def test_reference_handlers_require_the_same_executable_catalog(self):
        packet = {"packet_id": 1, "name": "TestPacket", "get_name_rva": "0x1000",
                  "get_id_rva": "0x2000", "vtable_rva": "0x3000",
                  "client_dispatch": {"handler_rva": "0x4000", "verified_ret_only": True}}
        manifest = {"metadata": {"pdb_match": True, "section_header_match": True,
                                 "executable": {"sha256": "test-executable"},
                                 "pdb": {"sha256": "test-pdb"}}, "packets": [packet]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest))
            dispatch, identity = AUDIT.reference_handlers(path, {1: packet}, "test-executable")
            self.assertTrue(dispatch[1]["verified_ret_only"])
            self.assertEqual(identity["executable_sha256"], "test-executable")
            with self.assertRaisesRegex(ValueError, "requires an executable packet catalog"):
                AUDIT.reference_handlers(path, {})
            with self.assertRaisesRegex(ValueError, "requires the catalog's executable SHA-256"):
                AUDIT.reference_handlers(path, {1: packet})
            with self.assertRaisesRegex(ValueError, "executable SHA-256 differs"):
                AUDIT.reference_handlers(path, {1: packet}, "different-executable-same-addresses")
            with self.assertRaisesRegex(ValueError, "different packets"):
                AUDIT.reference_handlers(path, {2: packet}, "test-executable")
            with self.assertRaisesRegex(ValueError, "identity differs"):
                AUDIT.reference_handlers(path, {1: {**packet, "get_name_rva": "0x9999"}}, "test-executable")
            manifest["metadata"]["pdb_match"] = False
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "no verified"):
                AUDIT.reference_handlers(path, {1: packet}, "test-executable")


if __name__ == "__main__":
    unittest.main()
