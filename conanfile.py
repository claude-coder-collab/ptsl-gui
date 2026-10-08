from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class PtslGuiDependencies(ConanFile):
    """Third-party libraries for the distributable (macOS 13.3+) build; development builds use Homebrew instead."""

    settings = "os", "arch", "compiler", "build_type"

    def requirements(self) -> None:
        self.requires("protobuf/6.33.5")
        self.requires("nlohmann_json/3.12.0")
        self.requires("catch2/3.16.0")

    def generate(self) -> None:
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()
        CMakeDeps(self).generate()
