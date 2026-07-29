#!/usr/bin/env python3
"""Source-level guards for the reusable native-world Neutral contract."""

from __future__ import annotations

import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]


class NativeWorldNeutralContractTest(unittest.TestCase):
    def test_neutral_observes_relocated_tables_and_complete_pool_identity(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")

        for address in ("0x00C8800C", "0x00965560", "0x008E3FB0", "0x00B74498", "0x00B744A4", "0x00B745BC"):
            self.assertIn(address, source)
        self.assertIn("result.modelInfoArray = modelInfos", source)
        self.assertIn("result.streamingInfoArray = streamingInfos", source)
        self.assertIn("result.fileIdLayout = layout", source)
        self.assertIn("result.txdFindCache", source)
        self.assertIn("poolFlagSnapshots", source)
        self.assertIn("memcmp(current.flags", source)

        baseline = source[source.index("void CaptureNativeWorldNeutralBaseline") : source.index("const SNativeWorldPackDescriptorSA& Pack")]
        self.assertIn("archiveStateHash", baseline)
        self.assertIn("streamHandleStateHash", baseline)
        self.assertIn("modelInfoArray", baseline)
        self.assertIn("streamingInfoArray", baseline)
        self.assertIn("g_nativeWorldNeutralBaseline.streamingEntries = sample.streamingEntries", baseline)

    def test_neutral_streaming_gate_walks_both_runtime_lists(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        streaming = (REPOSITORY / "Client/game_sa/CStreamingSA.cpp").read_text(encoding="utf-8")

        walk = source[source.index("SStreamingListTelemetry ReadStreamingListTelemetry") : source.index("SNativeWorldLifecycleTelemetry ReadNativeWorldLifecycleTelemetry")]
        self.assertIn("infos[startId].nextId != 0xFFFF", walk)
        self.assertIn("infos[endId].prevId != 0xFFFF", walk)
        self.assertIn("visited[current]", walk)
        self.assertIn("infos[current].nextId != previous", walk)
        self.assertIn("steps < layout.total", walk)
        self.assertIn("layout.loadedList, layout.loadedList + 1", source)
        self.assertIn("layout.requestedList, layout.requestedList + 1", source)
        self.assertIn("result.requestedList.nodes == 0", source)
        self.assertIn("result.requestedStreamingEntries == 0", source)

        self.assertIn("0x8E4BA0", streaming)
        self.assertIn("0x8E4B90", streaming)
        self.assertIn("priority counter can remain stale", source)
        self.assertNotIn("result.streaming.priorityRequests == 0", source)
        self.assertIn("result.streaming.channelError == -1", source)
        self.assertIn("result.streaming.streamTableShapeValid", source)

    def test_reserved_empty_lod_scratch_is_a_neutral_substrate(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")

        self.assertIn("result.lodArrayCount", source)
        self.assertIn("g_staticWorldV3LodArraysReserved", source)
        self.assertIn("if (result.lodArraysReserved)", source)
        self.assertIn("result.lodReservedArrays", source)
        self.assertIn("result.lodReservedNonNullEntries", source)
        self.assertIn("result.lodScratchNeutral", source)
        self.assertIn("!result.lodArraysReserved ||", source)
        self.assertIn("g_staticWorldV3LodState == EStaticWorldV3LodState::Reserved", source)
        self.assertIn("result.lodReservedNonNullEntries == 0", source)
        self.assertIn("result.lodScratchNeutral", source)
        self.assertNotIn("g_staticWorldV3LodState == EStaticWorldV3LodState::Off;\n        if (g_nativeWorldNeutralBaseline", source)

    def test_cache_commit_counter_is_explicitly_high_water_only(self) -> None:
        header = (REPOSITORY / "Client/game_sa/CNativeWorldCacheSA.h").read_text(encoding="utf-8")
        cache = (REPOSITORY / "Client/game_sa/CNativeWorldCacheSA.cpp").read_text(encoding="utf-8")
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")

        self.assertIn("committedGroupsHighWater", header)
        self.assertIn("Diagnostic high-water only", cache)
        session_neutral = source[source.index("result.sessionNeutral =") : source.index("result.ioQuiescent =")]
        self.assertNotIn("committedGroupsHighWater", session_neutral)
        self.assertIn("result.cache.processHandles == 0", session_neutral)

    def test_admission_baseline_is_not_mislabeled_as_runtime_neutral(self) -> None:
        source = (REPOSITORY / "Client/game_sa/CNativeWorldPackSA.cpp").read_text(encoding="utf-8")
        self.assertIn("admissionBaselineMatches", source)
        self.assertIn("admission-baseline-match=", source)
        self.assertNotIn("contentMatchesBaseline", source)
        self.assertNotIn(" baseline-match=%s", source)

        validation = source[
            source.index("bool ValidateStaticWorldV3RuntimeBaseline") : source.index("bool PrepareStaticWorldV3Transaction")
        ]
        self.assertIn("if (!current.admissionBaselineMatches)", validation)
        self.assertIn("GTA admission state changed after the runtime baseline capture", validation)
        self.assertNotIn("current.sessionNeutral", validation)


if __name__ == "__main__":
    unittest.main()
