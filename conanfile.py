from conan import ConanFile
from conan.tools.cmake import cmake_layout


class WealthTrackerRecipe(ConanFile):
    name = "wealthtracker"
    version = "0.1.0"
    package_type = "application"

    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"

    default_options = {
        "sqlcipher/*:shared": False,
        "openssl/*:shared": True,
        "sqlcipher/*:crypto_library": "openssl",
        "sqlcipher/*:temporary_store": "default_memory",
        "libsodium/*:shared": False,
        "xlsxio/*:shared": False,
        "boost/*:header_only": True,
        "catch2/*:with_main": True,
    }

    def requirements(self):
        self.requires("sqlcipher/4.16.0")
        self.requires("openssl/3.6.3")
        self.requires("libsodium/1.0.20")
        self.requires("xlsxio/0.2.34")
        self.requires("boost/1.88.0")
        self.test_requires("catch2/3.9.1")

    def build_requirements(self):
        self.tool_requires("cmake/4.1.2")
        self.tool_requires("ninja/1.12.1")

    def layout(self):
        cmake_layout(self)
        self.folders.generators = "generators"
