import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ConfigureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wt configure ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.script = self.root / "scripts/configure.sh"
        self.script.parent.mkdir()
        shutil.copyfile(Path(__file__).resolve().parents[2] / "scripts/configure.sh", self.script)
        self.tools = self.root / "build/tooling/bin"
        self.tools.mkdir(parents=True)
        self.executable(self.tools / "uname", "echo Darwin")
        self.executable(self.tools / "cmake", 'printf "%s\\n" "$@" > "$WT_TEST_CMAKE_ARGS"')
        self.executable(self.tools / "clang++", "exit 0")
        self.executable(self.tools / "clang-scan-deps", "exit 0")
        self.arguments = self.root / "cmake-args"
        self.environment = dict(os.environ, WEALTHTRACKER_LLVM_ROOT=str(self.tools.parent),
                                WT_TEST_CMAKE_ARGS=str(self.arguments))
        self.qt = self.root / "build/qt/6.11.1/macos/lib/cmake"

    def executable(self, path, body):
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)

    def configure(self):
        return subprocess.run(["sh", str(self.script), "macos-debug"], cwd=self.root,
                              env=self.environment, text=True, capture_output=True)

    def package(self, name):
        config = self.qt / name / (name + "Config.cmake")
        config.parent.mkdir(parents=True, exist_ok=True)
        config.touch()

    def test_missing_qt_stops_before_cmake(self):
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing or incomplete", result.stderr)
        self.assertIn("./scripts/bootstrap.sh", result.stderr)
        self.assertFalse(self.arguments.exists())

    def test_missing_webview_stops_before_cmake(self):
        self.package("Qt6")
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("WebView", result.stderr)
        self.assertFalse(self.arguments.exists())

    def test_available_packages_reach_cmake(self):
        self.package("Qt6")
        self.package("Qt6WebView")
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.arguments.read_text().splitlines()
        self.assertEqual(arguments[:2], ["--preset", "macos-debug"])
        self.assertIn("-DCMAKE_CXX_COMPILER=" + str(self.tools / "clang++"), arguments)


if __name__ == "__main__":
    unittest.main()
