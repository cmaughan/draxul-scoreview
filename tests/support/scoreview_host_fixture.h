#pragma once

// Headless ScoreHost fixture (kanban 15): the shared test seam and fakes for
// driving the real host without a window, renderer, audio device, user
// dialog, or real Verovio toolkit. ScoreHostTestAccess is the ONE friend of
// ScoreHost (declared in score_host.h) — its definition lives here so every
// host-level suite shares it. The blockable DeterministicLayoutEngine fake
// (and its permit/wait helpers) lives in scoreview_engine_fake.h so suites
// that cannot see controller internals share the same engine fake.

#include "scoreview_engine_fake.h"

#include <draxul/scoreview/player_input_rig.h>
#include <draxul/scoreview/score_runtime.h>

#include "score_stream_controller.h"
#include "score_view_model.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace draxul
{
namespace scoreview
{

using ScoreHost = ScoreRuntime;

// This is the sole friend of ScoreHost used by tests. It drives the same
// methods as the UI while keeping test setup independent of devices.
class ScoreHostTestAccess
{
public:
    using GateInput = ScoreHost::GateInput;

    static bool prime_window(ScoreHost& host, std::unique_ptr<ILayoutEngine> engine,
        std::string_view source, std::string& error)
    {
        host.engine_ = std::move(engine);
        host.source_bytes_ = std::string(source);
        if (!host.stream_->load_source(std::string(source), error))
            return false;
        host.view_mode_ = ScoreHost::ViewMode::Flow;
        host.stream_->set_windowed(true);
        host.stream_->set_active(false);
        host.stream_->set_initial_window_installed(false);
        host.piece_marking_qpm_ = 120.0;
        if (!host.rebuild_window(0, 0.0, false))
        {
            error = "initial synchronous window build failed";
            return false;
        }
        return true;
    }

    static bool prime_paged(ScoreHost& host, std::unique_ptr<ILayoutEngine> engine,
        std::string_view source, std::string& error)
    {
        if (!engine->load(source, error))
            return false;
        host.engine_ = std::move(engine);
        host.source_bytes_ = std::string(source);
        host.view_mode_ = ScoreHost::ViewMode::Paged;
        host.layout_dirty_ = true;
        return true;
    }

    static void inject_engraver(ScoreHost& host, std::unique_ptr<WindowEngraver> engraver)
    {
        host.stream_->adopt_engraver(std::move(engraver));
    }

    static void set_transport(ScoreHost& host, double position_q, double tempo_qpm, bool playing)
    {
        host.flow_.seek(position_q);
        host.flow_.set_tempo_qpm(tempo_qpm);
        if (playing)
            host.flow_.play();
        else
            host.flow_.pause();
    }

    static void restart(ScoreHost& host)
    {
        host.restart_stream(true);
    }

    static void restyle_current_window(ScoreHost& host)
    {
        host.proportional_spacing_ = !host.proportional_spacing_;
        host.reengrave_flow_in_place();
    }

    static void poll(ScoreHost& host)
    {
        host.poll_async_engrave();
    }

    static void deliver_stale_completion(ScoreHost& host, WindowEngraver::RequestId request_id)
    {
        WindowEngraver::Done done;
        done.request_id = request_id;
        done.ok = true;
        host.handle_async_engrave_done(std::move(done));
    }

    static std::shared_ptr<const ScoreDrawList> strip(const ScoreHost& host)
    {
        return host.strip_;
    }

    static WindowEngraver::RequestId pending_request(const ScoreHost& host)
    {
        return host.stream_->pending_request();
    }

    static bool async_pending(const ScoreHost& host)
    {
        return host.stream_->async_in_flight();
    }

    static double position_q(const ScoreHost& host)
    {
        return host.flow_.position_q();
    }

    static double tempo_qpm(const ScoreHost& host)
    {
        return host.flow_.tempo_qpm();
    }

    static bool playing(const ScoreHost& host)
    {
        return host.flow_.playing();
    }

    // Input selection (kanban 15/16): the same swap-in-place path the UI
    // uses. Mic is deliberately not offered here — it would touch the real
    // permission/device layer; MicPlayerInput has its own fake-ops suite.
    static bool select_input(
        ScoreHost& host, GateInput input, int midi_port = -1)
    {
        return host.set_gate_input(input, 60.0, 1.0, midi_port);
    }

    static PlayerInputRig::Kind input_kind(const ScoreHost& host)
    {
        return host.input_rig_.kind();
    }

    static void relayout_paged(ScoreHost& host, int width = 800, int height = 600)
    {
        host.viewport_.pixel_size = { width, height };
        host.viewport_.pixel_scale = 1.0f;
        host.view_mode_ = ScoreHost::ViewMode::Paged;
        host.relayout();
    }

    static size_t paged_guided_glyph_count(const ScoreHost& host)
    {
        size_t guided = 0;
        if (!host.page_note_highlights_)
            return 0;
        for (const ScoreHighlightState& colors : *host.page_note_highlights_)
        {
            for (const uint8_t guide : colors.glyph_guide)
                guided += guide != 0 ? 1 : 0;
        }
        return guided;
    }

    static bool show_note_colors(const ScoreHost& host)
    {
        return host.show_note_colors_;
    }

    static void set_note_colors(ScoreHost& host, bool on)
    {
        ScoreInspectorIntents intents;
        intents.show_note_colors = on;
        host.apply_inspector_intents(intents);
    }
};

inline std::string read_verovio_svg_fixture()
{
    const auto path = std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "plugins/scoreview/tests/fixtures/svg/verovio-minimal-c4.svg";
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

inline bool wait_for_host_install(ScoreHost& host)
{
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        ScoreHostTestAccess::poll(host);
        if (!ScoreHostTestAccess::async_pending(host))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// Counting IHostCallbacks: proves the host requests frames (and nothing
// else) without a window; lifetime is the test's, so use-after-shutdown
// would trip sanitizers.
class CountingHostCallbacks final : public ScoreRuntimeCallbacks
{
public:
    void request_frame() override
    {
        ++frames;
    }
    int frames = 0;
};

} // namespace scoreview
} // namespace draxul
