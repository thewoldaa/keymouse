// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include "app/cli.h"

using namespace km;

namespace {

// Builds an argv the way the C runtime hands it to wmain: entry zero is the
// program name, the rest are the arguments being tested.
struct Argv {
    explicit Argv(std::vector<std::wstring> values) : args_(std::move(values)) {
        args_.insert(args_.begin(), L"KeyMouse.exe");
        for (auto& arg : args_) {
            pointers_.push_back(arg.data());
        }
    }

    bool parse(CliOptions& out, std::string& error) {
        return CliOptions::parse(static_cast<int>(pointers_.size()), pointers_.data(), out,
                                 error);
    }

private:
    std::vector<std::wstring> args_;
    std::vector<wchar_t*> pointers_;
};

}  // namespace

TEST_CASE("cli: no arguments leaves every option at its default") {
    Argv argv({});
    CliOptions options;
    std::string error;

    CHECK(argv.parse(options, error));
    CHECK(error.empty());
    CHECK_FALSE(options.safe_mode);
    CHECK_FALSE(options.minimized);
    CHECK_FALSE(options.no_autostart);
    CHECK_FALSE(options.show_help);
    CHECK_FALSE(options.show_version);
    CHECK(options.locale.empty());
    CHECK(options.config_path.empty());
}

TEST_CASE("cli: flags are recognised in any case") {
    Argv argv({L"--MINIMIZED", L"--No-AutoStart", L"--SAFE-MODE"});
    CliOptions options;
    std::string error;

    CHECK(argv.parse(options, error));
    CHECK(options.minimized);
    CHECK(options.no_autostart);
    CHECK(options.safe_mode);
}

TEST_CASE("cli: short and long help and version forms") {
    for (const wchar_t* flag : {L"-h", L"--help", L"-v", L"--version"}) {
        Argv argv({flag});
        CliOptions options;
        std::string error;

        INFO(flag);
        CHECK(argv.parse(options, error));
        if (flag[1] == L'-' && flag[2] == L'h') {
            CHECK(options.show_help);
        } else if (flag[1] == L'-' && flag[2] == L'v') {
            CHECK(options.show_version);
        }
    }

    Argv both({L"--help", L"--version"});
    CliOptions options;
    std::string error;
    CHECK(both.parse(options, error));
    CHECK(options.show_help);
    CHECK(options.show_version);
}

TEST_CASE("cli: --locale takes the next argument") {
    Argv argv({L"--locale", L"ja"});
    CliOptions options;
    std::string error;

    CHECK(argv.parse(options, error));
    CHECK(options.locale == "ja");
}

TEST_CASE("cli: --locale without a value is an error") {
    Argv argv({L"--locale"});
    CliOptions options;
    std::string error;

    CHECK_FALSE(argv.parse(options, error));
    CHECK(error == "--locale requires a language code");
}

TEST_CASE("cli: a language code that cannot name a bundled translation is ignored") {
    // Anything outside ASCII cannot match locales/*.json, so it is dropped and
    // the application falls back to detecting the system language.
    Argv argv({L"--locale", std::wstring{L"k\x00E9" L"da"}});
    CliOptions options;
    std::string error;

    CHECK(argv.parse(options, error));
    CHECK(options.locale.empty());
}

TEST_CASE("cli: --config takes the next argument") {
    Argv argv({L"--config", L"C:\\Temp\\other.json"});
    CliOptions options;
    std::string error;

    CHECK(argv.parse(options, error));
    CHECK(options.config_path == L"C:\\Temp\\other.json");
}

TEST_CASE("cli: --config without a value is an error") {
    Argv argv({L"--config"});
    CliOptions options;
    std::string error;

    CHECK_FALSE(argv.parse(options, error));
    CHECK(error == "--config requires a path");
}

TEST_CASE("cli: an unknown option is reported by name") {
    Argv argv({L"--bogus"});
    CliOptions options;
    std::string error;

    CHECK_FALSE(argv.parse(options, error));
    CHECK(error == "Unknown option: --bogus");
}

TEST_CASE("cli: an unknown option with non ascii characters stays printable") {
    Argv argv({std::wstring{L"--bogus\x00E9"}});
    CliOptions options;
    std::string error;

    CHECK_FALSE(argv.parse(options, error));
    CHECK(error == "Unknown option: --bogus?");
}

TEST_CASE("cli: a later flag still parses after an error free argument") {
    Argv argv({L"--minimized", L"--locale", L"ru", L"--no-autostart"});
    CliOptions options;
    std::string error;

    CHECK(argv.parse(options, error));
    CHECK(options.minimized);
    CHECK(options.locale == "ru");
    CHECK(options.no_autostart);
}

TEST_CASE("cli: the usage text documents every option the parser accepts") {
    const std::string usage = usage_text();

    for (const char* option : {"--help", "--version", "--minimized", "--no-autostart",
                               "--locale", "--safe-mode", "--config"}) {
        INFO(option);
        CHECK(usage.find(option) != std::string::npos);
    }
}
