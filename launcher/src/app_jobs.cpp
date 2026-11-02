// The steps a build or a download runs, the log they report into, and starting the
// game.

#include "app.h"

#include "disc.h"
#include "tools_package.h"

#include "util.h"

#include <chrono>
#include <thread>

namespace nla {

void App::start_job(std::vector<Task> tasks, JobOptions options) {
    if (busy_ || tasks.empty()) return;
    busy_ = true;
    job_run_ = std::move(options);
    activity_.begin();
    next_frame_ = 0;
    job_.start(
        std::move(tasks),
        [this](const std::wstring& text) { post([this, text] { on_log(text); }); },
        [this](const std::wstring& text) { post([this, text] { on_stage(text); }); },
        [this](int percent) { post([this, percent] { on_progress(percent); }); },
        [this](int code) { post([this, code] { on_job_done(code); }); });
    refresh();
}

void App::on_log(const std::wstring& text) { append_log(text); }

void App::on_stage(const std::wstring& text) {
    activity_.set_stage(text);
    invalidate();
}

void App::on_progress(int percent) {
    activity_.set_percent(percent);
    invalidate();
}

void App::on_job_done(int code) {
    busy_ = false;
    activity_.reset();
    // Taken out of the run before anything can start another one.
    JobOptions run = std::move(job_run_);
    job_run_ = {};
    const bool then_launch = run.then_launch;
    const bool exits = run.exits;
    const bool built = run.builds;
    const bool quiet = run.quiet;
    if (code == 0) {
        append_log(L"[launcher] done\n");
        // A job can be the one that installed the tools, so look for them again.
        tools_ = check_tools();
        // Back to the clean one-button view when there is nothing to read.
        activity_.fold();
        // What the mods' codes were when this build went in: a later change says the game
        // has to be built again.
        if (built && project_) {
            std::error_code ec;
            fs::create_directories(project_->build_dir(), ec);
            write_text_file(project_->code_stamp(),
                            narrow(read_text_file(project_->disc() / L"mods.code")));
        }
    } else if (code == 130) {
        append_log(L"[launcher] cancelled\n");
    } else {
        append_log(format(L"[launcher] failed (status %d); see the output above\n", code));
        if (!quiet) {
            show_message(L"The step did not finish",
                         L"See Activity for the command and the output it printed. The "
                         L"details there usually name the missing file or tool.");
        }
    }
    refresh();
    if (code == 0 && then_launch) continue_launch();
    // The helper that swaps in the new version is waiting for this process to go.
    if (code == 0 && exits) quit_ = true;
    if (run.done) {
        auto follow_up = std::move(run.done);
        run.done = nullptr;
        follow_up(code);
    }
}

void App::append_log(const std::wstring& text) {
    activity_.write(text);
    invalidate();
}

void App::copy_log() { window_.set_clipboard(activity_.text()); }

bool App::check_required_tools() {
    std::wstring lines;
    for (const auto& tool : tools_) {
        if (tool.required && !tool.found()) {
            if (!lines.empty()) lines += L"\n";
            lines += L"  " + tool.name + L": " + tool.hint;
        }
    }
    if (lines.empty()) return true;
    append_log(L"[launcher] missing build tools:\n" + lines + L"\n");
#if defined(__APPLE__)
    // The compiler and python3 are the only things the release does not carry, and Apple
    // installs both with one command that needs no account.
    if (!command_line_tools_installed()) {
        show_confirm(
            L"Apple's command line tools are needed",
            L"The game is compiled from your own disc, so it needs a C++ compiler, and on a "
            L"Mac that comes from Apple's command line tools.\n\n"
            L"They are free, need no Apple account, and the launcher starts Apple's own "
            L"installer and waits for it to finish (about 2 GB).",
            L"Install", [this] { start_job(apple_tools_tasks(), {.then_launch = true}); });
        return false;
    }
#endif
    if (tools_package_available()) {
        show_confirm(L"Build tools needed",
                     L"The build needs these tools, which were not found on PATH or beside "
                     L"the launcher:\n\n" + lines + L"\n\nThe launcher can download them (" +
                         tools_package_size() + L") into tools\\ beside itself.",
                     L"Download", [this] { install_tools(true); });
        return false;
    }
    show_message(L"Missing build tools",
                 L"The build needs these tools, which were not found on PATH or beside "
                 L"the launcher:\n\n" + lines);
    return false;
}

void App::install_tools(bool then_launch) {
    if (busy_) return;
    if (tools_package_available()) {
        append_log(L"[launcher] fetching the build tools\n");
        start_job(tools_package_tasks(), {.then_launch = then_launch});
        return;
    }
#if defined(__APPLE__)
    start_job(apple_tools_tasks(), {.then_launch = then_launch});
#endif
}

std::vector<Task> App::apple_tools_tasks() {
    std::vector<Task> tasks;
#if defined(__APPLE__)
    if (command_line_tools_installed()) return tasks;
    tasks.push_back({L"Opening Apple's installer…", [](Job& job) -> int {
                         job.log(L"$ xcode-select --install\n");
                         // Apple's command hands the download to its own process and
                         // returns, so the wait below is what sees it finish.
                         job.run_process(L"/usr/bin/xcode-select", {L"--install"}, home_dir());
                         job.log(L"[launcher] Apple's installer is open; it downloads about "
                                 L"2 GB\n");
                         return 0;
                     }});
    tasks.push_back({L"Waiting for the compiler…", [](Job& job) -> int {
                         for (int waited = 0; waited < 1800; waited += 2) {
                             if (job.cancel_flag().load()) {
                                 job.log(L"[launcher] stopped waiting; Apple's installer "
                                         L"carries on by itself\n");
                                 return 130;
                             }
                             for (const auto& tool : check_tools()) {
                                 if (tool.name == L"compiler" && tool.found()) {
                                     job.log(L"[launcher] the compiler is in place\n");
                                     return 0;
                                 }
                             }
                             std::this_thread::sleep_for(std::chrono::seconds(2));
                         }
                         job.log(L"[launcher] Apple's installer has not finished; when it "
                                 L"does, start the game again\n");
                         return 1;
                     }});
#endif
    return tasks;
}

bool App::repair_project() {
    if (!project_) return false;
    if (project_->complete()) return true;
    std::wstring error;
    if (!Project::create(project_->root(), &error)) {
        show_message(L"Could not set the folder up", error);
        return false;
    }
    auto root = project_->root();
    auto game = project_->game();
    project_.emplace(root, game);
    save_settings();
    append_log(format(L"[launcher] wrote the game folders in %ls\n",
                      project_->root().wstring().c_str()));
    refresh();
    return true;
}

// Asking for a disc image starts where the last one was, or at the project it is for.
void App::choose_image() {
    if (!project_) return;
    fs::path start =
        settings_.last_image.empty() ? project_->root() : fs::path(settings_.last_image);
    picker_.choose_path(PickerKind::Image, L"Choose your own disc image", start,
                        [this](const fs::path& chosen) { start_setup(chosen); });
}

void App::start_setup(const fs::path& image) {
    if (!project_ || busy_) return;
    bool then_launch = launch_wanted_;
    launch_wanted_ = false;
    std::error_code ec;
    if (!fs::is_regular_file(image, ec)) {
        show_message(L"Disc image", L"No such file:\n\n" + image.wstring());
        return;
    }
    // The game folders are the launcher's to write: a release carries none, so the first
    // disc chosen for a game sets it up where the launcher lives. A folder that is missing
    // the recompiler or the tool script -- one an earlier setup left half-written -- is
    // topped up here too, rather than skipped because its game folders happen to be there.
    if (!project_->complete()) {
        if (!repair_project()) return;
    }
    if (fs::exists(project_->dol(), ec)) {
        show_confirm(L"Replace the extracted disc?",
                     L"A disc is already extracted. Extract again from this image and "
                     L"replace it?",
                     L"Extract", [this, image, then_launch] { begin_setup(image, then_launch); });
        return;
    }
    begin_setup(image, then_launch);
}

void App::begin_setup(const fs::path& image, bool then_launch) {
    if (!project_ || busy_) return;
    settings_.last_image = image.wstring();
    save_settings();
    append_log(format(L"[launcher] image: %ls\n", image.c_str()));

    fs::path disc_dir = project_->disc();
    std::wstring disc_id = project_->disc_id();
    std::wstring expected = project_->expected_sha1();
    bool convert = needs_nodtool(image);

    auto converted = std::make_shared<std::wstring>();
    std::vector<Task> tasks;

    if (convert) {
        tasks.push_back({
            L"Converting the image to ISO…",
            [this, image, converted](Job& job) -> int {
                auto nodtool = find_tool(L"nodtool" + wstring(exe_suffix()));
                if (nodtool.empty()) {
                    job.log(L"[launcher] this image needs nodtool, which was not found. "
                            L"Install it with: cargo install --locked nodtool\n");
                    return 127;
                }
                fs::path output = fs::temp_directory_path() / L"naruto-launcher" / L"disc.iso";
                std::error_code ec;
                fs::create_directories(output.parent_path(), ec);
                fs::remove(output, ec);
                job.log(L"$ nodtool convert " + image.wstring() + L" " + output.wstring() +
                        L"\n");
                int code = job.run_process(nodtool, {L"convert", image.wstring(),
                                                     output.wstring()},
                                           project_ ? project_->root() : fs::path());
                if (code == 0) *converted = output.wstring();
                return code;
            }});
    }

    tasks.push_back({
        L"Extracting the disc…",
        [this, image, converted, convert, disc_dir, disc_id, expected](Job& job) -> int {
            fs::path source = convert ? fs::path(*converted) : image;
            if (source.empty()) {
                job.log(L"[launcher] the converted image is missing\n");
                return 1;
            }
            ExtractRequest request{source, disc_dir, disc_id, expected};
            auto progress = [&job](int percent, const std::wstring& stage_text) {
                job.progress(percent);
                job.stage(stage_text);
            };
            auto outcome = extract_disc(request, progress, job.cancel_flag());
            if (!outcome.ok) {
                job.log(L"[launcher] " + outcome.error + L"\n");
                return 1;
            }
            job.log(format(L"[launcher] %d files, %.1f MiB extracted\n", outcome.file_count,
                           static_cast<double>(outcome.bytes) / (1024.0 * 1024.0)));
            job.log(format(L"[launcher] main.dol %ls matches the supported disc\n",
                           outcome.dol_sha1.substr(0, 12).c_str()));
            if (convert) {
                std::error_code remove_ec;
                fs::remove(source, remove_ec);
            }
            return 0;
        }});

    // A step that failed offers its retry, and the other way out: another disc image, which
    // is what a player with the wrong disc, or a damaged one, needs next.
    start_job(std::move(tasks), {.then_launch = then_launch,
                                 .quiet = true,
                                 .done = [this, image, then_launch](int code) {
        if (code == 0 || code == 130) return;
        show_confirm(L"The disc was not set up",
                     L"Activity holds the command and the output it printed. A retry reads "
                     L"the same image again; another image is the way out of a disc that is "
                     L"not the one you meant, or one that cannot be read.",
                     L"Try again",
                     [this, image, then_launch] { begin_setup(image, then_launch); },
                          L"Use a different image", [this] { choose_image(); });
                         }});
}

std::vector<Task> App::build_tasks() {
    std::vector<Task> tasks;
    if (!project_) return tasks;
    auto cmake = find_tool(L"cmake" + wstring(exe_suffix()));
    auto ninja = find_tool(L"ninja" + wstring(exe_suffix()));
    if (cmake.empty() || ninja.empty()) return tasks;

    fs::path game_dir = project_->game_dir();
    fs::path build = project_->build_dir();
    fs::path root = project_->root();
    std::vector<std::wstring> configure = {
        L"-S", game_dir.wstring(), L"-B", build.wstring(), L"-G", L"Ninja",
        L"-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    };
    auto sdl = bundled_sdl2_prefix();
    if (!sdl.empty()) configure.push_back(L"-DGCN_SDL2_PREFIX=" + sdl.wstring());

    // In a release, name the bundled interpreter so CMake does not prefer a Python
    // registered elsewhere on the machine (or the Microsoft Store alias).
    auto python = find_tool(L"python" + wstring(exe_suffix()));
    if (python.empty()) python = find_tool(L"python3" + wstring(exe_suffix()));
    if (!python.empty()) {
        auto python_key = to_lower(python);
        auto tools_key = to_lower(launcher_tools_dir().wstring());
        if (!tools_key.empty() && python_key.rfind(tools_key, 0) == 0)
            configure.push_back(L"-DPython3_EXECUTABLE=" + python);
    }
#ifdef _WIN32
    // The OpenXR loader (used by --vr) does not compile with MinGW-w64. Without MSVC's
    // cl.exe the launcher configures the game without the PC VR frontend, as the rest of
    // the desktop build does not need it.
    if (find_tool(L"cl.exe").empty()) configure.push_back(L"-DGCN_OPENXR=OFF");
#endif

    // The recompiler needs the disc's function map, and a project the launcher wrote starts
    // without one: dtk makes it from the DOL the launcher extracted. dtk resolves the paths
    // in its config against the working directory and misreads a Windows drive letter, so
    // everything is relative to the game folder, which is where it runs.
    std::error_code symbols_ec;
    if (!fs::is_regular_file(project_->analysis_symbols(), symbols_ec)) {
        auto game_dir = project_->game_dir();
        auto hash = project_->expected_sha1();
        // A release carries dtk; a -Lean release or a checkout does not, and then the
        // pinned build is fetched once, so the first build needs nothing installed by hand.
        auto dtk = std::make_shared<std::wstring>(find_tool(L"dtk" + wstring(exe_suffix())));
        if (dtk->empty()) {
            tasks.push_back({L"Fetching dtk…", [dtk](Job& job) -> int {
                                 std::wstring error;
                                 *dtk = ensure_dtk(job, &error);
                                 if (dtk->empty()) {
                                     job.log(L"[launcher] dtk was not found and could not be "
                                             L"fetched: " + error + L"\n");
                                     return 127;
                                 }
                                 return 0;
                             }});
        }
        tasks.push_back({L"Reading the disc's symbols…", [dtk, game_dir,
                                                          hash](Job& job) -> int {
                             if (dtk->empty()) return 127;  // the step above said why
                             std::error_code ec;
                             fs::path work = fs::path(L"build") / L"dtk";
                             fs::create_directories(game_dir / work, ec);
                             fs::create_directories(game_dir / L"analysis", ec);
                             fs::path config = work / L"config.yml";
                             const std::wstring text =
                                 L"object: \"rom/disc/sys/main.dol\"\nhash: " + hash +
                                 L"\nsymbols: \"analysis/symbols.txt\"\n"
                                 L"splits: \"build/dtk/splits.txt\"\n";
                             if (!write_text_file(game_dir / config, narrow(text))) {
                                 job.log(L"[launcher] could not write " +
                                         (game_dir / config).wstring() + L"\n");
                                 return 1;
                             }
                             job.log(L"$ dtk dol split " + config.wstring() + L" " +
                                     work.wstring() + L"\n");
                             int code = job.run_process(
                                 *dtk, {L"dol", L"split", config.wstring(), work.wstring()},
                                 game_dir);
                             if (code != 0) return code;
                             if (!fs::is_regular_file(game_dir / L"analysis" / L"symbols.txt",
                                                      ec)) {
                                 job.log(L"[launcher] dtk wrote no symbol map\n");
                                 return 1;
                             }
                             return 0;
                         }});
    }

    tasks.push_back({L"Configuring CMake…", [cmake, configure, root](Job& job) -> int {
                         job.log(L"$ " + command_line(cmake, configure) + L"\n");
                         return job.run_process(cmake, configure, root);
                     }});
    tasks.push_back({L"Compiling the game…", [ninja, build, root](Job& job) -> int {
                         std::vector<std::wstring> args = {L"-C", build.wstring()};
                         job.log(L"$ " + command_line(ninja, args) + L"\n");
                         return job.run_process(ninja, args, root);
                     }});
    return tasks;
}

void App::start_build(bool then_launch) {
    if (!project_ || busy_) return;
    if (!check_required_tools()) return;
    // A folder set up by an older launcher can be missing the recompiler, and CMake then
    // fails on `add_subdirectory(../gcn-recomp)` before it reads a line of the game. That is
    // a setup the launcher can finish, so it is finished here rather than reported.
    if (!repair_project()) return;
    auto tasks = build_tasks();
    if (tasks.empty()) {
        show_message(L"Cannot build", L"cmake and ninja were not found.");
        return;
    }
    if (!status_.disc.extracted) {
        show_message(L"Build the game",
                     L"The build needs the extracted disc. Press Launch the game to choose "
                     L"your disc image first.");
        return;
    }
    // The failure gets a prompt of its own below, which says what can be done about it;
    // the generic "the step did not finish" one would only replace it.
    start_job(std::move(tasks), {.then_launch = then_launch,
                                 .builds = true,
                                 .quiet = true,
                                 .done = [this, then_launch](int code) {
        if (code == 0 || code == 130) return;
        show_confirm(L"The build did not finish",
                     L"Activity holds the command and the output it printed, which usually "
                     L"names the file or the tool that is missing. A retry builds again from "
                     L"where the folders are; another image is the way out of a disc that is "
                     L"not the one you meant.",
                     L"Try again", [this, then_launch] { start_build(then_launch); },
                          L"Use a different image", [this] { choose_image(); });
                         }});
}

void App::launch() {
    if (busy_) return;
    if (game_.running()) {
        game_.stop();
        return;
    }
    if (!project_) {
        launch_wanted_ = true;
        locate_project();
        return;
    }
    refresh();
    if (!status_.disc.verified) {
        launch_wanted_ = true;
        choose_image();
        return;
    }
    continue_launch();
}

void App::continue_launch() {
    if (!project_ || busy_) return;
    refresh();
    if (!status_.disc.verified) return;
    if (status_.built && !status_.stale_code) {
        play();
        return;
    }
    start_build(true);
}

void App::play() {
    if (!project_ || busy_ || game_.running()) return;
    if (!status_.built || !status_.disc.verified) {
        refresh();
        return;
    }
    // The runtime reads the mod list at boot, so it is brought up to date first: the disc
    // may have been extracted, or a mod switched on, since the last refresh.
    write_mod_overlay();
    std::vector<std::wstring> args = {
        L"--scale=" + std::to_wstring(settings_.scale),
    };
    if (settings_.mute_music) args.push_back(L"--no-music");
    if (!settings_.input_log) args.push_back(L"--no-input-log");
    // One pad per controller, and never fewer than the pads the picker names.
    int players = settings_.players;
    for (const auto& device : device_map_.devices())
        players = std::max(players, device.pad);
    args.push_back(L"--players=" + std::to_wstring(std::clamp(players, 1, 4)));
    args.push_back(L"--controls=" + project_->controls().wstring());

    settings_.game = project_->game();
    save_settings();
    append_log(L"\n$ " + command_line(project_->binary().wstring(), args) + L"\n");
    play_status_ = L"Starting…";
    bool started =
        game_.start(project_->binary().wstring(), args, project_->game_dir(),
                    [this](const std::wstring& text) {
                        post([this, text] { append_log(text); });
                    },
                    [this](int code, bool stopped) {
                        post([this, code, stopped] {
                            play_status_ = stopped
                                               ? L"Stopped from the launcher."
                                               : format(L"The game exited with status %d.",
                                                        code);
                            append_log(stopped
                                           ? L"[launcher] game stopped\n"
                                           : format(L"[launcher] game exited with status %d\n",
                                                    code));
                            refresh();
                        });
                    });
    if (!started) {
        play_status_.clear();
        show_message(L"The game did not start",
                     L"The launcher could not start the game. Build it first, then try "
                     L"again.");
    } else {
        play_status_ = L"Press Stop the game to end it.";
    }
    refresh();
}

void App::open_path(const fs::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        show_message(L"Not there yet", L"Nothing at:\n\n" + path.wstring());
        return;
    }
    std::wstring error;
    if (!nla::open_path(path, &error)) {
        show_message(L"Cannot open", error.empty() ? L"This system could not open:\n\n" +
                                                         path.wstring()
                                                   : error);
    }
}

// Who plays as whom: a row per console pad, and the device that plays as it.
void App::open_players() {
    refresh_devices();
    overlay_.show_panel(OverlayPage::Players, L"Players");
    close_popups();
    invalidate();
}

void App::refresh_devices() {
    device_map_.clear();
    // A stand-in list for the renders: the same devices whatever this machine has
    // plugged in, so the drawing is the same everywhere it is checked.
    if (rendering_) {
        device_map_.set_devices(
            {{L"Xbox Wireless Controller", 1, 0}, {L"PS4 Controller", 1, 2}});
        return;
    }
    std::error_code ec;
    // The game answers for itself: its own SDL sees the devices, and it names them exactly
    // as the pad pins in controls.txt do.
    if (!project_ || !fs::exists(project_->binary(), ec)) {
        device_map_.set_note(
            L"Build the game first: the launcher asks it which controllers it sees.");
        return;
    }
    std::string out;
    if (!capture_output(project_->binary().wstring(),
                        {L"--list-devices", L"--controls=" + project_->controls().wstring()}, &out))
        return;
    device_map_.load(widen(out));
    if (device_map_.devices().empty())
        device_map_.set_note(L"No controller found. Plug one in and press Refresh.");
}

// The pads this picker owns are the `pad = "<device>" <n>` and `keyboard_pad = <n>` lines.
// Everything else in controls.txt is the player's own mapping and is kept as written, and
// so is a pin for a controller that is unplugged right now: it is still who plays next
// time it is plugged in.
void App::write_pad_pins() {
    // A render checks the panel, not the player's disk: it runs with no game to read back
    // from, so its device list is a stand-in and must not reach controls.txt.
    if (!project_ || rendering_) return;
    const fs::path path = project_->controls();
    std::wstring text;
    std::error_code ec;
    if (fs::exists(path, ec)) text = read_text_file(path);
    std::map<std::wstring, int> pins;
    std::wstring kept;
    for (const auto& line : split(text, L'\n')) {
        auto words = split(trim(line), L' ');
        if (!words.empty() && words[0] == L"pad") {
            const size_t open = line.find(L'"', line.find(L"pad"));
            const size_t close = line.rfind(L'"');
            if (open != std::wstring::npos && close != std::wstring::npos && close > open) {
                const int pad = std::atoi(narrow(line.substr(close + 1)).c_str());
                if (pad >= 1 && pad <= 4) pins[line.substr(open + 1, close - open - 1)] = pad;
            }
            continue;
        }
        if (!words.empty() && words[0] == L"keyboard_pad") continue;
        kept += line;
        kept += L'\n';
    }
    // What the panel shows is what the game gets: every controller the panel finds a pad
    // for is pinned to it, so the first-free rule has nothing left to decide.
    const std::vector<int> pads = device_map_.resolved_pads();
    for (size_t i = 0; i < device_map_.devices().size(); ++i) {
        std::wstring label = device_map_.devices()[i].name;
        if (device_map_.devices()[i].nth > 1)
            label += L" #" + std::to_wstring(device_map_.devices()[i].nth);
        if (pads[i] == 0) pins.erase(label);
        else pins[label] = pads[i];
    }
    if (device_map_.keyboard_pad() != 1)
        kept += L"keyboard_pad = " + std::to_wstring(device_map_.keyboard_pad()) + L"\n";
    int highest = 1;
    for (const auto& [label, pad] : pins) {
        kept += L"pad = \"" + label + L"\" " + std::to_wstring(pad) + L"\n";
        highest = std::max(highest, pad);
    }
    if (!write_text_file(path, narrow(kept))) {
        show_message(L"Player pads", L"The launcher could not write\n\n" + path.wstring());
        return;
    }
    // A pad past the cap would be ignored by the game, so the cap follows the picks.
    settings_.players = std::max(settings_.players, highest);
    save_settings();
    append_log(L"[launcher] players: keyboard is player " +
               std::to_wstring(device_map_.keyboard_pad()) + L"\n");
    for (const auto& [label, pad] : pins)
        append_log(L"[launcher] players: \"" + label + L"\" is player " +
                   std::to_wstring(pad) + L"\n");
    refresh_devices();  // so the rows show what the game now reads back
    invalidate();
}

}  // namespace nla
