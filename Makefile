# ============================================================
#  YAMLA — Makefile
#  Targets: all, deps, clean, run
# ============================================================

CXX      := clang++
CONAN_PC := build/conan

# Locate Conan-installed imgui bindings (backends need to be compiled from source)
IMGUI_PKG  := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --variable=prefix imgui 2>/dev/null)
IMGUI_BIND := $(IMGUI_PKG)/res/bindings

# Use Conan for imgui, implot, simdjson.
# Use the system (Homebrew) SDL2 to avoid macOS version mismatch crashes
# from the Conan-prebuilt SDL2 (built for macOS 26, mismatches the runtime).
CONAN_CFLAGS := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --cflags imgui implot simdjson 2>/dev/null)
CONAN_LIBS   := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --libs   imgui implot simdjson 2>/dev/null)
SDL2_CFLAGS  := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS    := $(shell pkg-config --libs   sdl2 2>/dev/null)

PKG_CFLAGS := $(CONAN_CFLAGS) $(SDL2_CFLAGS)
PKG_LIBS   := $(CONAN_LIBS)   $(SDL2_LIBS)

CXXFLAGS := -std=c++17 -O3 -march=native -Wall -Wextra -Wno-unused-parameter \
            -Isrc \
            -I$(IMGUI_BIND) \
            $(PKG_CFLAGS)

# Platform-specific OpenGL linking
UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
  GL_LIBS := -framework OpenGL
else ifeq ($(OS), Windows_NT)
  GL_LIBS := -lopengl32
else
  GL_LIBS := -lGL
endif

LDFLAGS := $(PKG_LIBS) $(GL_LIBS)

TARGET   := yamla
BUILDDIR := build/obj
SRCDIR   := src

# ImGui SDL2 + OpenGL3 backends (must compile from source — not in the lib)
BACKEND_SRCS := $(IMGUI_BIND)/imgui_impl_sdl2.cpp \
                $(IMGUI_BIND)/imgui_impl_opengl3.cpp
BACKEND_OBJS := $(BUILDDIR)/backends/imgui_impl_sdl2.o \
                $(BUILDDIR)/backends/imgui_impl_opengl3.o

# Collect all .cpp files recursively under src/
SRCS := $(shell find $(SRCDIR) -name '*.cpp')
OBJS := $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(SRCS))

ALL_OBJS := $(OBJS) $(BACKEND_OBJS)

# ---- Targets -----------------------------------------------

.PHONY: all deps clean run

all: $(TARGET)

# Install Conan deps and generate pkg-config files
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

$(BUILDDIR)/backends/imgui_impl_sdl2.o: $(IMGUI_BIND)/imgui_impl_sdl2.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/backends/imgui_impl_opengl3.o: $(IMGUI_BIND)/imgui_impl_opengl3.cpp
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
