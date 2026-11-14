from conan import ConanFile
from conan.tools.cmake import cmake_layout
import json
from pathlib import Path


class WealthTrackerRecipe(ConanFile):
    name = "wealthtracker"
    exports = "version.json"
    package_type = "application"

    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"

    def set_version(self):
        metadata = json.loads((Path(self.recipe_folder) / "version.json").read_text(encoding="utf-8"))
        self.version = metadata["product"]

    default_options = {
        "sqlcipher/*:shared": False,
        "openssl/*:shared": True,
        "sqlcipher/*:crypto_library": "openssl",
        "sqlcipher/*:temporary_store": "default_memory",
        "libsodium/*:shared": False,
        "boost/*:header_only": True,
        "catch2/*:with_main": True,
    }

    def requirements(self):
        self.requires("sqlcipher/4.16.0")
        self.requires("openssl/3.6.5")
        self.requires("libsodium/1.0.22")
        self.requires("boost/1.88.0")
        self.test_requires("catch2/3.9.1")

    def build_requirements(self):
        self.tool_requires("cmake/4.1.2")
        self.tool_requires("ninja/1.12.1")

    def layout(self):
        cmake_layout(self)
        self.folders.generators = "generators"
