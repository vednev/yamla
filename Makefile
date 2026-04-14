# ============================================================
#  YAMLA — Makefile
#  Targets: all, deps, clean, run
# ============================================================

CXX      ?= clang++
CONAN_PC := build/conan

# Locate Conan-installed imgui bindings (backends compiled from source)
IMGUI_PKG  := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --variable=prefix imgui 2>/dev/null)
IMGUI_BIND := $(IMGUI_PKG)/res/bindings

# Conan for imgui, implot, simdjson
# SDL2 from Homebrew (system-native, avoids macOS malloc crash from Conan build)
# OpenSSL from Homebrew (needed by cpp-httplib for HTTPS)
CONAN_CFLAGS := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --cflags imgui implot simdjson 2>/dev/null)
CONAN_LIBS   := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --libs   imgui implot simdjson 2>/dev/null)
SDL2_CFLAGS  := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS    := $(shell pkg-config --libs   sdl2 2>/dev/null)
SSL_CFLAGS   := $(shell pkg-config --cflags openssl 2>/dev/null)
SSL_LIBS     := $(shell pkg-config --libs   openssl 2>/dev/null)
ZLIB_CFLAGS  := $(shell pkg-config --cflags zlib 2>/dev/null)
ZLIB_LIBS    := $(shell pkg-config --libs   zlib 2>/dev/null)

# Fallback: if pkg-config can't find zlib, use system -lz directly
ifeq ($(ZLIB_LIBS),)
  ZLIB_LIBS := -lz
endif

PKG_CFLAGS := $(CONAN_CFLAGS) $(SDL2_CFLAGS) $(SSL_CFLAGS) $(ZLIB_CFLAGS)
PKG_LIBS   := $(CONAN_LIBS)   $(SDL2_LIBS)   $(SSL_LIBS)   $(ZLIB_LIBS)

# ---- Platform detection ------------------------------------
UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
  # macOS — SDL2 Renderer → Metal (CAMetalLayer, no OpenGL required)
  PLATFORM_CFLAGS  :=
  PLATFORM_LIBS    := -framework Metal -framework QuartzCore -framework AppKit -framework UniformTypeIdentifiers
  BACKEND_SRCS     := $(IMGUI_BIND)/imgui_impl_sdl2.cpp \
                      $(IMGUI_BIND)/imgui_impl_sdlrenderer2.cpp
  BACKEND_OBJS     := build/obj/backends/imgui_impl_sdl2.o \
                      build/obj/backends/imgui_impl_sdlrenderer2.o

else ifeq ($(OS), Windows_NT)
  # Windows — SDL2 window + DirectX 11 renderer
  PLATFORM_CFLAGS  :=
  PLATFORM_LIBS    := -ld3d11 -ldxgi -ld3dcompiler
  BACKEND_SRCS     := $(IMGUI_BIND)/imgui_impl_sdl2.cpp \
                      $(IMGUI_BIND)/imgui_impl_dx11.cpp
  BACKEND_OBJS     := build/obj/backends/imgui_impl_sdl2.o \
                      build/obj/backends/imgui_impl_dx11.o

else
  # Linux — SDL2 + OpenGL 3.3 (unchanged)
  GTK3_CFLAGS      := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
  GTK3_LIBS        := $(shell pkg-config --libs   gtk+-3.0 2>/dev/null)
  PLATFORM_CFLAGS  := $(GTK3_CFLAGS)
  PLATFORM_LIBS    := -lGL $(GTK3_LIBS)
  BACKEND_SRCS     := $(IMGUI_BIND)/imgui_impl_sdl2.cpp \
                      $(IMGUI_BIND)/imgui_impl_opengl3.cpp
  BACKEND_OBJS     := build/obj/backends/imgui_impl_sdl2.o \
                      build/obj/backends/imgui_impl_opengl3.o
endif

# ---- Flags -------------------------------------------------
# -march=native is skipped on CI (clang rejects it when it can't detect the
# host microarchitecture, e.g. on GitHub's virtualised arm64 runners).
# Use -O3 only; simdjson's runtime dispatch still selects the best SIMD path.
MARCH := $(if $(CI),,-march=native)
CXXFLAGS := -std=c++17 -O3 $(MARCH) -Wall -Wextra -Wno-unused-parameter \
            -Isrc \
            -I$(IMGUI_BIND) \
            -Ivendor/httplib \
            -Ivendor/md4c \
            -Ivendor/nfd \
            $(PKG_CFLAGS) $(PLATFORM_CFLAGS)

# C flags for md4c (compiled as C, not C++)
CFLAGS := -O3 $(MARCH) -Wall -Ivendor/md4c

LDFLAGS := $(PKG_LIBS) $(PLATFORM_LIBS)

TARGET   := yamla
BUILDDIR := build/obj
SRCDIR   := src

# Collect all .cpp files recursively under src/
SRCS := $(shell find $(SRCDIR) -name '*.cpp')
OBJS := $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(SRCS))

# Vendor C sources (md4c)
VENDOR_C_SRCS := vendor/md4c/md4c.c
VENDOR_C_OBJS := $(BUILDDIR)/vendor/md4c.o

# Vendor NFD-extended (platform-conditional backend)
ifeq ($(UNAME), Darwin)
  VENDOR_NFD_SRCS := vendor/nfd/nfd_cocoa.m
  VENDOR_NFD_OBJS := $(BUILDDIR)/vendor/nfd_cocoa.o
else
  VENDOR_NFD_SRCS := vendor/nfd/nfd_gtk.cpp
  VENDOR_NFD_OBJS := $(BUILDDIR)/vendor/nfd_gtk.o
endif

ALL_OBJS := $(OBJS) $(BACKEND_OBJS) $(VENDOR_C_OBJS) $(VENDOR_NFD_OBJS)

# ---- Targets -----------------------------------------------

.PHONY: all deps clean run test

all: $(TARGET)

deps:
	@echo "Installing Conan dependencies..."
	/Library/Frameworks/Python.framework/Versions/3.13/bin/conan install . \
	    --output-folder=$(CONAN_PC) \
	    --build=missing \
	    -s build_type=Release \
	    -s compiler.cppstd=17
	@echo "Done."

$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET)"

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule for any backend .cpp → backend .o
$(BUILDDIR)/backends/%.o: $(IMGUI_BIND)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule for vendor C sources (md4c)
$(BUILDDIR)/vendor/md4c.o: vendor/md4c/md4c.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for vendor NFD (macOS: Objective-C)
$(BUILDDIR)/vendor/nfd_cocoa.o: vendor/nfd/nfd_cocoa.m
	@mkdir -p $(dir $@)
	$(CC) -O3 $(MARCH) -Ivendor/nfd $(SDL2_CFLAGS) -fno-objc-arc -c $< -o $@

# Rule for vendor NFD (Linux: C++)
$(BUILDDIR)/vendor/nfd_gtk.o: vendor/nfd/nfd_gtk.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)

run: all
	./$(TARGET)

# Download bundled fonts (OFL-licensed) into vendor/fonts/
# Run once: make fonts
fonts:
	@mkdir -p vendor/fonts
	@echo "Downloading Inter..."
	@curl -fsSL "https://github.com/rsms/inter/releases/download/v4.0/Inter-4.0.zip" -o /tmp/_inter.zip && \
	  unzip -jo /tmp/_inter.zip "extras/ttf/Inter-Regular.ttf" -d vendor/fonts/ && \
	  rm /tmp/_inter.zip
	@echo "Downloading JetBrains Mono..."
	@curl -fsSL "https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip" -o /tmp/_jbmono.zip && \
	  unzip -jo /tmp/_jbmono.zip "fonts/ttf/JetBrainsMono-Regular.ttf" -d vendor/fonts/ && \
	  rm /tmp/_jbmono.zip
	@echo "Downloading Fira Code..."
	@curl -fsSL "https://github.com/tonsky/FiraCode/releases/download/6.2/Fira_Code_v6.2.zip" -o /tmp/_firacode.zip && \
	  unzip -jo /tmp/_firacode.zip "ttf/FiraCode-Regular.ttf" -d vendor/fonts/ && \
	  rm /tmp/_firacode.zip
	@echo "Downloading IBM Plex Sans..."
	@curl -fsSL "https://github.com/IBM/plex/releases/download/%40ibm%2Fplex-sans%401.1.0/ibm-plex-sans.zip" -o /tmp/_plexsans.zip && \
	  unzip -jo /tmp/_plexsans.zip "ibm-plex-sans/fonts/complete/ttf/IBMPlexSans-Regular.ttf" -d vendor/fonts/ && \
	  rm /tmp/_plexsans.zip
	@echo "Fonts ready in vendor/fonts/:"
	@ls -lh vendor/fonts/

# ---- Test target -------------------------------------------
CATCH2_CFLAGS := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --cflags catch2-with-main 2>/dev/null)
CATCH2_LIBS   := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --libs   catch2-with-main 2>/dev/null)

TEST_SRCS := $(shell find test -name '*.cpp' 2>/dev/null)
TEST_OBJS := $(patsubst test/%.cpp, $(BUILDDIR)/test/%.o, $(TEST_SRCS))

# Source files needed by tests (exclude main.cpp, ui/, llm_client.cpp)
TEST_DEP_SRCS := $(filter-out src/main.cpp, $(filter-out src/ui/%, $(filter-out src/llm/llm_client.cpp, $(SRCS))))
TEST_DEP_OBJS := $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(TEST_DEP_SRCS))

TEST_CXXFLAGS := $(CXXFLAGS) $(CATCH2_CFLAGS)
TEST_LDFLAGS  := $(CATCH2_LIBS) $(CONAN_LIBS) $(SSL_LIBS) $(ZLIB_LIBS)

test: $(TEST_OBJS) $(TEST_DEP_OBJS)
	$(CXX) $(TEST_CXXFLAGS) -o build/run_tests $^ $(TEST_LDFLAGS)
	./build/run_tests

$(BUILDDIR)/test/%.o: test/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TEST_CXXFLAGS) -c $< -o $@
