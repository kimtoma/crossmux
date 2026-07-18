import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
QUEUE_HEADER = ROOT / "src/reading_sync/ReadingSyncQueue.h"
QUEUE_SOURCE = ROOT / "src/reading_sync/ReadingSyncQueue.cpp"


class ReadingSyncQueueSchemaTest(unittest.TestCase):
    def test_schema_two_keeps_wire_schema_one_and_migrates_schema_one(self):
        header = QUEUE_HEADER.read_text(encoding="utf-8")
        source = QUEUE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kSchemaVersion = 2", header)
        self.assertIn("const ReadingSyncAcceptedSummary* lastAccepted() const", header)
        self.assertIn("sourceSchema == 1 || sourceSchema == kSchemaVersion", source)
        self.assertIn("ReadingSyncMetadata::kWireSchemaVersion", source)
        self.assertIn('document["lastAccepted"]', source)
        self.assertIn("shouldDiscardOrphanedAcceptedFingerprint", source)
        self.assertIn("loaded.lastAcceptedFingerprint_.clear()", source)
        self.assertIn("sourceSchema == 1 || repairedAcceptedFingerprint", source)
        self.assertIn("constexpr size_t MAX_QUEUE_BYTES = 8192", source)

    def test_worst_case_valid_queue_fits_existing_eight_kibibyte_cap(self):
        emoji = "😀"
        sha = "a" * 64
        pending = {
            "schemaVersion": 1,
            "sequence": 4294967294,
            "bookId": emoji * 128,
            "title": emoji * 150,
            "author": emoji * 100,
            "progressPercent": 100,
            "lastReadAt": "x" * 64,
            "isbn13": "9781234567897",
            "coverSha256": sha,
            "coverMime": "image/jpeg",
        }
        fixture = {
            "schemaVersion": 2,
            "nextSequence": 4294967295,
            "lastAcceptedFingerprint": "f" * 64,
            "authPaused": False,
            "terminal": False,
            "terminalReason": "",
            "pending": pending,
            "cover": {
                "bookId": pending["bookId"],
                "sha256": sha,
                "mime": "image/jpeg",
                "path": f"/.crosspoint/reading_sync/covers/{sha}.jpg",
            },
            "lastAccepted": {
                "title": emoji * 150,
                "author": emoji * 100,
                "progressPercent": 100,
                "lastReadAt": "x" * 64,
                "acceptedAt": 4294967295,
                "coverState": "uploaded",
            },
        }

        encoded = json.dumps(fixture, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.assertLessEqual(len(encoded), 8192)


if __name__ == "__main__":
    unittest.main()
