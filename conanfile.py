from conan import ConanFile
from conan.tools.cmake import cmake_layout


class WealthTrackerRecipe(ConanFile):
    name = "wealthtracker"
    version = "0.1.0"
    package_type = "application"

    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"

    default_options = {
        "qt/*:shared": True,
        "qt/*:qtdeclarative": True,
        "qt/*:qtshadertools": True,
        "qt/*:qtimageformats": True,
        "qt/*:qttranslations": True,
        "qt/*:qttools": True,
        "qt/*:qtcharts": False,
        "qt/*:qtgraphs": False,
        "qt/*:qtnetworkauth": False,
        "qt/*:qthttpserver": False,
        "qt/*:qtwebengine": False,
        "qt/*:qtwebview": False,
        "qt/*:widgets": False,
        "qt/*:with_brotli": False,
        "qt/*:with_glib": False,
        "qt/*:with_icu": False,
        "qt/*:with_mysql": False,
        "qt/*:with_odbc": False,
        "qt/*:with_pq": False,
        "qt/*:with_sqlite3": False,
        "sqlcipher/*:shared": False,
        "sqlcipher/*:crypto_library": "openssl",
        "sqlcipher/*:temporary_store": "default_memory",
        "libsodium/*:shared": False,
        "xlsxio/*:shared": False,
        "boost/*:header_only": True,
        "catch2/*:with_main": True,
    }

    def requirements(self):
        self.requires("qt/6.11.1")
        self.requires("sqlcipher/4.16.0")
        self.requires("libsodium/1.0.20")
        self.requires("xlsxio/0.2.34")
        self.requires("boost/1.88.0")
        self.test_requires("catch2/3.9.1")

    def build_requirements(self):
        self.tool_requires("cmake/4.1.2")
        self.tool_requires("ninja/1.12.1")

    def layout(self):
        cmake_layout(self)
