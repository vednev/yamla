#include <catch2/catch_all.hpp>
#include "core/prefs.hpp"

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>

// RAII helper: redirect $HOME to a temp directory
struct TempHome {
    std::string orig_home;
    std::string temp_dir;

    TempHome() {
        const char* h = std::getenv("HOME");
        if (h) orig_home = h;
        // Create unique temp directory
        char tmpl[] = "/tmp/yamla_test_home_XXXXXX";
        char* dir = ::mkdtemp(tmpl);
        temp_dir = dir;
        ::setenv("HOME", temp_dir.c_str(), 1);
        // Pre-create the .config directory so PrefsManager::save() can mkdir yamla/
        std::string config_dir = temp_dir + "/.config";
        ::mkdir(config_dir.c_str(), 0755);
    }

    ~TempHome() {
        // Restore original HOME
        if (orig_home.empty())
            ::unsetenv("HOME");
        else
            ::setenv("HOME", orig_home.c_str(), 1);

        // Clean up temp dir
        std::string cmd = "rm -rf \"" + temp_dir + "\"";
        (void)::system(cmd.c_str());
    }
};

TEST_CASE("Prefs: default values", "[prefs]") {
    Prefs p;
    REQUIRE(p.font_name == "Inter");
    REQUIRE(p.font_size == 13);
    REQUIRE(p.memory_limit_gb == 0);
    REQUIRE(p.prefer_checkboxes == false);
    REQUIRE(p.llm_model == "claude-opus-4-6");
    REQUIRE(p.llm_max_tokens == 4096);
}

TEST_CASE("Prefs: load missing file returns defaults", "[prefs]") {
    TempHome th;
    Prefs p = PrefsManager::load();
    REQUIRE(p.font_name == "Inter");
    REQUIRE(p.font_size == 13);
}

TEST_CASE("Prefs: save/load round-trip", "[prefs]") {
    TempHome th;
    Prefs orig;
    orig.font_name = "Fira Code";
    orig.font_size = 16;
    orig.memory_limit_gb = 8;
    orig.prefer_checkboxes = true;
    orig.llm_api_key = "sk-test-key";
    orig.llm_endpoint = "api.example.com";
    orig.llm_model = "gpt-4";
    orig.llm_max_tokens = 8192;
    orig.export_dir = "/tmp/exports";

    PrefsManager::save(orig);
    Prefs loaded = PrefsManager::load();

    REQUIRE(loaded.font_name == "Fira Code");
    REQUIRE(loaded.font_size == 16);
    REQUIRE(loaded.memory_limit_gb == 8);
    REQUIRE(loaded.prefer_checkboxes == true);
    REQUIRE(loaded.llm_api_key == "sk-test-key");
    REQUIRE(loaded.llm_endpoint == "api.example.com");
    REQUIRE(loaded.llm_model == "gpt-4");
    REQUIRE(loaded.llm_max_tokens == 8192);
    REQUIRE(loaded.export_dir == "/tmp/exports");
}

TEST_CASE("Prefs: font size clamping", "[prefs]") {
    TempHome th;

    // Too small → clamped to 10
    {
        Prefs p;
        p.font_size = 5;
        PrefsManager::save(p);
        Prefs loaded = PrefsManager::load();
        REQUIRE(loaded.font_size == 10);
    }
    // Too large → clamped to 20
    {
        Prefs p;
        p.font_size = 25;
        PrefsManager::save(p);
        Prefs loaded = PrefsManager::load();
        REQUIRE(loaded.font_size == 20);
    }
}

TEST_CASE("Prefs: max tokens clamping", "[prefs]") {
    TempHome th;

    // Too small → clamped to 256
    {
        Prefs p;
        p.llm_max_tokens = 100;
        PrefsManager::save(p);
        Prefs loaded = PrefsManager::load();
        REQUIRE(loaded.llm_max_tokens == 256);
    }
    // Too large → clamped to 32768
    {
        Prefs p;
        p.llm_max_tokens = 100000;
        PrefsManager::save(p);
        Prefs loaded = PrefsManager::load();
        REQUIRE(loaded.llm_max_tokens == 32768);
    }
}

TEST_CASE("Prefs: config path check", "[prefs]") {
    TempHome th;
    std::string path = PrefsManager::config_path();
    REQUIRE(path.find("/.config/yamla/prefs.json") != std::string::npos);
}

TEST_CASE("Prefs: special characters in values round-trip", "[prefs]") {
    TempHome th;
    Prefs orig;
    orig.llm_api_key = "key\"with\"quotes";
    orig.font_name = "Font\\With\\Backslash";
    orig.export_dir = "/path/with spaces/and\ttabs";
    orig.llm_endpoint = "host:port/path";

    PrefsManager::save(orig);
    Prefs loaded = PrefsManager::load();

    REQUIRE(loaded.llm_api_key == "key\"with\"quotes");
    REQUIRE(loaded.font_name == "Font\\With\\Backslash");
    REQUIRE(loaded.export_dir == "/path/with spaces/and\ttabs");
    REQUIRE(loaded.llm_endpoint == "host:port/path");
}

TEST_CASE("Prefs: file permissions on save", "[prefs]") {
    TempHome th;
    Prefs p;
    PrefsManager::save(p);

    std::string path = PrefsManager::config_path();
    struct stat st;
    REQUIRE(::stat(path.c_str(), &st) == 0);
#if !defined(_WIN32)
    // File should have 0600 permissions (owner read/write only)
    REQUIRE((st.st_mode & 0777) == 0600);
#endif

    // Save again — permissions should still be 0600
    p.font_size = 15;
    PrefsManager::save(p);
    REQUIRE(::stat(path.c_str(), &st) == 0);
#if !defined(_WIN32)
    REQUIRE((st.st_mode & 0777) == 0600);
#endif
}

TEST_CASE("Prefs: prefer_checkboxes round-trip", "[prefs]") {
    TempHome th;

    Prefs p;
    p.prefer_checkboxes = true;
    PrefsManager::save(p);
    Prefs loaded = PrefsManager::load();
    REQUIRE(loaded.prefer_checkboxes == true);

    p.prefer_checkboxes = false;
    PrefsManager::save(p);
    loaded = PrefsManager::load();
    REQUIRE(loaded.prefer_checkboxes == false);
}

TEST_CASE("Prefs: default recent_files is empty", "[prefs]") {
    Prefs p;
    REQUIRE(p.recent_files.empty());
}

TEST_CASE("Prefs: recent_files round-trip", "[prefs]") {
    TempHome th;
    Prefs orig;
    orig.recent_files = {"/path/to/mongod.log", "/data/diagnostic.data/metrics.2025", "/tmp/test.log"};
    PrefsManager::save(orig);
    Prefs loaded = PrefsManager::load();
    REQUIRE(loaded.recent_files.size() == 3);
    REQUIRE(loaded.recent_files[0] == "/path/to/mongod.log");
    REQUIRE(loaded.recent_files[1] == "/data/diagnostic.data/metrics.2025");
    REQUIRE(loaded.recent_files[2] == "/tmp/test.log");
}

TEST_CASE("Prefs: recent_files special characters round-trip", "[prefs]") {
    TempHome th;
    Prefs orig;
    orig.recent_files = {"/path/with spaces/file.log", "C:\\Users\\test\\mongod.log", "/path/with\"quotes\"/file.log"};
    PrefsManager::save(orig);
    Prefs loaded = PrefsManager::load();
    REQUIRE(loaded.recent_files.size() == 3);
    REQUIRE(loaded.recent_files[0] == "/path/with spaces/file.log");
    REQUIRE(loaded.recent_files[1] == "C:\\Users\\test\\mongod.log");
    REQUIRE(loaded.recent_files[2] == "/path/with\"quotes\"/file.log");
}

TEST_CASE("Prefs: empty recent_files round-trip", "[prefs]") {
    TempHome th;
    Prefs orig;
    orig.recent_files = {};
    PrefsManager::save(orig);
    Prefs loaded = PrefsManager::load();
    REQUIRE(loaded.recent_files.empty());
}
