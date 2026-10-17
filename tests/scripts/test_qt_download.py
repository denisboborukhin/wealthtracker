import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, Mock, patch


script = Path(__file__).resolve().parents[2] / "scripts/install_qt_macos.py"
spec = importlib.util.spec_from_file_location("install_qt_macos", script)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class QtDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.cache = Path(self.temporary.name)
        self.content = b"verified archive fixture"
        self.digest = hashlib.sha256(self.content).hexdigest()
        self.mirrors = ["https://first.example/qt/", "https://second.example/qt/"]
        self.opener = Mock()
        self.patch = patch.object(installer.urllib.request, "build_opener", return_value=self.opener)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def download(self):
        return installer.download_archive("repository/sdk.7z", self.digest, self.cache, self.mirrors)

    def test_verified_archive_is_cached_and_rechecked(self):
        self.opener.open.return_value = io.BytesIO(self.content)
        result = self.download()
        self.assertEqual(result.read_bytes(), self.content)
        self.assertEqual(self.download(), result)
        self.opener.open.assert_called_once()

    def test_corrupt_cache_is_replaced_only_after_verification(self):
        (self.cache / "sdk.7z").write_bytes(b"corrupt")
        self.opener.open.return_value = io.BytesIO(self.content)
        self.assertEqual(self.download().read_bytes(), self.content)

    def test_corrupt_download_fails_closed_without_trying_another_mirror(self):
        self.opener.open.return_value = io.BytesIO(b"corrupt")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.download()
        self.opener.open.assert_called_once()
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_network_failure_uses_second_https_mirror(self):
        self.opener.open.side_effect = [OSError("unreachable"), io.BytesIO(self.content)]
        self.assertEqual(self.download().read_bytes(), self.content)
        self.assertEqual(self.opener.open.call_args.args[0],
                         "https://second.example/qt/repository/sdk.7z")

    def test_oversized_download_is_rejected(self):
        self.opener.open.return_value = io.BytesIO(self.content)
        with patch.object(installer, "MAXIMUM_ARCHIVE_BYTES", 4):
            with self.assertRaisesRegex(ValueError, "size limit"):
                self.download()
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_insecure_urls_and_redirects_are_rejected(self):
        for url in ["http://mirror.example/", "https://user:secret@mirror.example/",
                    "https://mirror.example:8443/", "https://mirror.example/?q=1"]:
            with self.subTest(url=url), self.assertRaises(ValueError):
                installer.https_url(url)
        with self.assertRaises(ValueError):
            installer.HttpsRedirectHandler().redirect_request(
                None, None, 302, "Found", {}, "http://mirror.example/archive")

    def test_no_extraction_when_any_archive_fails_verification(self):
        with patch.object(installer.sys, "argv", [str(script), self.mirrors[0], str(self.cache)]):
            with patch.object(installer, "download_archive", side_effect=ValueError("SHA-256 mismatch")):
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    installer.main()
        self.assertFalse((self.cache / "6.11.1").exists())

    def test_extraction_uses_the_presets_sdk_directory(self):
        seven_zip = MagicMock()
        updater = Mock()
        modules = {"py7zr": seven_zip, "aqt": Mock(),
                   "aqt.archives": Mock(), "aqt.updater": updater}
        with patch.dict(installer.sys.modules, modules):
            with patch.object(installer.sys, "argv", [str(script), self.mirrors[0], str(self.cache)]):
                with patch.object(installer, "download_archive", return_value=self.cache / "sdk.7z"):
                    installer.main()
        extractor = seven_zip.SevenZipFile.return_value.__enter__.return_value
        extractor.extractall.assert_called_with(path=self.cache.resolve() / "6.11.1/macos")
        updater.Updater.update.assert_called_once()


if __name__ == "__main__":
    unittest.main()
