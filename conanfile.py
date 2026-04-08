from conan import ConanFile


class YamlaConan(ConanFile):
    name = "yamla"
    version = "0.0.1"
    settings = "os", "compiler", "build_type", "arch"
    generators = "PkgConfigDeps", "VirtualRunEnv"

    def requirements(self):
        self.requires("sdl/2.28.5")
        self.requires("simdjson/3.12.3")
        self.requires("imgui/1.90.5")
        self.requires("implot/0.16")

    def configure(self):
        self.options["imgui"].shared = False
        self.options["imgui"].with_sdl2 = True
        self.options["imgui"].with_opengl3 = True
