// SPDX-License-Identifier: MIT
//
// scratchpads-umbriel — a gtk4-layer-shell overlay for switching between
// Umbriel scratchpads.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm/application.h>
#include <glibmm/main.h>

#include "model/config_store.hpp"
#include "model/scratchpad.hpp"
#include "model/types.hpp"
#include "spm_config.hpp"
#include "ui/overlay_window.hpp"
#include "umbriel/ipc.hpp"

namespace {

struct Options {
    std::string config_path;  // --config
    std::string socket_path;  // --socket
    bool layer_shell = true;
    bool show_help = false;
    bool dump = false;      // --dump
    int quit_after_ms = 0;  // --quit-after <ms>, for smoke tests
    int argc = 0;
    std::vector<char*> argv;
};

void print_help(const char* program) {
    std::printf(
        "Usage: %s [options]\n\n"
        "A keyboard-driven layer-shell overlay for switching Umbriel scratchpads.\n"
        "Launch it from a keybind: the overlay lists every scratchpad, `j`/`k` moves\n"
        "the selection, `space` toggles the selected one and `esc` dismisses.\n\n"
        "Options:\n"
        "  -c, --config <path>   Use this Umbriel configuration instead of the search path\n"
        "      --socket <path>   Override the Umbriel IPC socket (default: $UMBRIEL_SOCKET)\n"
        "      --window          Use a normal window instead of a layer-shell surface\n"
        "      --dump            Print scratchpads and their windows as text, then exit\n"
        "  -h, --help            Show this help\n"
        "  -v, --version         Show the version\n",
        program);
}

Options parse_options(int argc, char** argv) {
    Options options;
    options.argv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--config" || argument == "-c") {
            if (i + 1 < argc) {
                options.config_path = argv[++i];
            }
        } else if (argument.rfind("--config=", 0) == 0) {
            options.config_path = argument.substr(9);
        } else if (argument == "--socket") {
            if (i + 1 < argc) {
                options.socket_path = argv[++i];
            }
        } else if (argument.rfind("--socket=", 0) == 0) {
            options.socket_path = argument.substr(9);
        } else if (argument == "--window" || argument == "--no-layer-shell") {
            options.layer_shell = false;
        } else if (argument == "--dump") {
            options.dump = true;
        } else if (argument == "--quit-after") {
            if (i + 1 < argc) {
                options.quit_after_ms = std::atoi(argv[++i]);
            }
        } else if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--version" || argument == "-v") {
            std::printf("scratchpads-umbriel %s\n", SPM_VERSION);
            std::exit(0);
        } else {
            options.argv.push_back(argv[i]);
        }
    }
    options.argc = static_cast<int>(options.argv.size());
    return options;
}

void print_scratchpads(const spm::ConfigStore& config) {
    std::string error;
    nlohmann::json reply;
    if (!spm::umbriel_query("windows", &reply, &error)) {
        std::fprintf(stderr, "scratchpads-umbriel: %s\n", error.c_str());
        std::exit(1);
    }

    const auto windows = spm::parse_windows(reply);
    const auto scratchpads = spm::build_scratchpads(config.defs(), windows);

    std::printf("config: %s\n", config.primary_config().empty() ? "(none)"
                                                                : config.primary_config().c_str());
    std::printf("loaded: %zu file(s)\n", config.loaded_files().size());

    for (const auto& sp : scratchpads) {
        std::printf("\n[%s]%s  %zu window(s), %d visible\n", sp.name.c_str(),
                    sp.implicit ? " (implicit)" : "", sp.windows.size(), sp.visible_count());
        if (sp.spawn_when_empty) {
            std::printf("    spawn_when_empty: %s\n", sp.spawn_when_empty->c_str());
        }
        for (const auto& w : sp.windows) {
            std::printf("    %-8s %-20s %s\n",
                        w.visible ? (w.focused ? "focused" : "visible") : "hidden",
                        w.app_id.c_str(), w.title.c_str());
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options = parse_options(argc, argv);
    if (options.show_help) {
        print_help(argv[0]);
        return 0;
    }
    if (!options.socket_path.empty()) {
        ::setenv("UMBRIEL_SOCKET", options.socket_path.c_str(), 1);
    }

    if (options.dump) {
        spm::ConfigStore config(options.config_path);
        std::string error;
        config.load(&error);
        if (!error.empty()) {
            std::fprintf(stderr, "scratchpads-umbriel: %s\n", error.c_str());
        }
        print_scratchpads(config);
        return 0;
    }

    auto app = Gtk::Application::create("dev.noctalia.ScratchpadManager");
    if (!app) {
        std::fprintf(stderr, "scratchpads-umbriel: cannot connect to a display\n");
        return 1;
    }

    spm::UmbrielIpc ipc;
    spm::ConfigStore config(options.config_path);

    std::unique_ptr<spm::OverlayWindow> window;
    app->signal_activate().connect([&] {
        if (!window) {
            window = std::make_unique<spm::OverlayWindow>(ipc, config, options.layer_shell);
            app->add_window(*window);
            ipc.start();
        }
        window->toggle_visible();
    });

    if (options.quit_after_ms > 0) {
        Glib::signal_timeout().connect(
            [&app] {
                app->quit();
                return false;
            },
            options.quit_after_ms);
    }

    const int status = app->run(options.argc, options.argv.data());
    ipc.stop();
    return status;
}
