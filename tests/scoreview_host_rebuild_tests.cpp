#include <catch2/catch_all.hpp>

#include "support/scoreview_host_fixture.h"
#include "support/temp_dir.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace
{

using draxul::scoreview::DeterministicLayoutEngine;
using draxul::scoreview::FakeEngineState;
using draxul::scoreview::kScoreHostFixtureMinimalScore;
using draxul::scoreview::read_verovio_svg_fixture;
using draxul::scoreview::release_loads;
using draxul::scoreview::ScoreHost;
using draxul::scoreview::ScoreHostTestAccess;
using draxul::scoreview::wait_for_host_install;
using draxul::scoreview::wait_for_loads;
using draxul::scoreview::WindowEngraver;

constexpr std::string_view kMinimalScore = kScoreHostFixtureMinimalScore;

} // namespace

TEST_CASE("ScoreHost restart and restyle remain nonblocking behind an active engrave",
    "[scoreview][host][engraver][lifetime]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    auto main_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(main_state, svg, false),
        kMinimalScore, error));
    INFO(error);

    auto worker_state = std::make_shared<FakeEngineState>();
    auto worker_engine = std::make_unique<DeterministicLayoutEngine>(worker_state, svg, true);
    auto engraver = WindowEngraver::create(std::move(worker_engine), error);
    INFO(error);
    REQUIRE(engraver);
    ScoreHostTestAccess::inject_engraver(host, std::move(engraver));

    ScoreHostTestAccess::set_transport(host, 1.0, 96.0, true);
    const auto old_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(old_strip);
    const double old_position = ScoreHostTestAccess::position_q(host);
    const double old_tempo = ScoreHostTestAccess::tempo_qpm(host);

    ScoreHostTestAccess::restyle_current_window(host);
    const auto first_request = ScoreHostTestAccess::pending_request(host);
    REQUIRE(first_request != 0);
    REQUIRE(wait_for_loads(worker_state, 1));

    const auto restart_start = std::chrono::steady_clock::now();
    ScoreHostTestAccess::restart(host);
    const auto restart_elapsed = std::chrono::steady_clock::now() - restart_start;
    const auto restart_request = ScoreHostTestAccess::pending_request(host);

    const auto restyle_start = std::chrono::steady_clock::now();
    ScoreHostTestAccess::restyle_current_window(host);
    const auto restyle_elapsed = std::chrono::steady_clock::now() - restyle_start;
    const auto latest_request = ScoreHostTestAccess::pending_request(host);

    CHECK(restart_elapsed < std::chrono::milliseconds(50));
    CHECK(restyle_elapsed < std::chrono::milliseconds(50));
    CHECK(first_request < restart_request);
    CHECK(restart_request < latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(old_position));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(old_tempo));
    CHECK(ScoreHostTestAccess::playing(host));

    // Exercise the host's generation check directly: a delayed completion
    // from the first request cannot clear or replace the latest pending job.
    ScoreHostTestAccess::deliver_stale_completion(host, first_request);
    CHECK(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());

    // Completing the first generation must not expose it to ScoreHost: the
    // newest restyle starts, while the old engraving and transport stay live.
    release_loads(worker_state, 1);
    REQUIRE(wait_for_loads(worker_state, 2));
    ScoreHostTestAccess::poll(host);
    CHECK(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());
    {
        std::lock_guard lock(worker_state->mutex);
        CHECK(worker_state->load_calls == 2);
        CHECK(worker_state->payloads.size() == 2);
    }

    release_loads(worker_state, 2);
    REQUIRE(wait_for_host_install(host));
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == 0);
    CHECK(ScoreHostTestAccess::strip(host).get() != old_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(old_position));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(old_tempo));
    CHECK(ScoreHostTestAccess::playing(host));
}

TEST_CASE("ScoreHost uses synchronous restart and restyle when its worker is unavailable",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    auto main_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(main_state, svg, false),
        kMinimalScore, error));
    INFO(error);
    REQUIRE(main_state->load_calls == 1);

    ScoreHostTestAccess::set_transport(host, 1.0, 102.0, true);
    const double tempo = ScoreHostTestAccess::tempo_qpm(host);
    const auto initial_strip = ScoreHostTestAccess::strip(host);
    ScoreHostTestAccess::restart(host);

    CHECK(main_state->load_calls == 2);
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == 0);
    CHECK(ScoreHostTestAccess::strip(host).get() != initial_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(0.0));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(host));

    ScoreHostTestAccess::set_transport(host, 1.0, tempo, true);
    const auto restarted_strip = ScoreHostTestAccess::strip(host);
    ScoreHostTestAccess::restyle_current_window(host);
    CHECK(main_state->load_calls == 3);
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::strip(host).get() != restarted_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(1.0));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(host));
}

TEST_CASE("failed initial slice interpretation reloads the complete source before flow fallback",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/1),
        kMinimalScore, error));
    REQUIRE(ScoreHostTestAccess::fallback_pending(host));
    CHECK(ScoreHostTestAccess::stream_windowed(host));
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::fallback_pending(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    REQUIRE(ScoreHostTestAccess::strip(host));
    REQUIRE(state->payloads.size() == 2);
    CHECK(state->payloads.back() == kMinimalScore);
}

TEST_CASE("failed async advance keeps the old engraving until whole-source fallback succeeds",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false),
        kMinimalScore, error));
    const auto valid_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(valid_strip);
    ScoreHostTestAccess::fail_async_advance(host);
    CHECK(ScoreHostTestAccess::fallback_pending(host));
    CHECK(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::stream_windowed(host));
    CHECK(ScoreHostTestAccess::strip(host).get() == valid_strip.get());
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::fallback_pending(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    REQUIRE(state->payloads.size() == 2);
    CHECK(state->payloads.back() == kMinimalScore);
}

TEST_CASE("failed full-source recovery retains the last valid engraving",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/2),
        kMinimalScore, error));
    const auto valid_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(valid_strip);
    ScoreHostTestAccess::fail_async_advance(host);
    ScoreHostTestAccess::relayout_flow(host);
    CHECK(ScoreHostTestAccess::fallback_pending(host));
    CHECK(ScoreHostTestAccess::stream_windowed(host));
    CHECK(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::strip(host).get() == valid_strip.get());
}

TEST_CASE("a failed initial slice cannot become the complete paged score",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/1),
        kMinimalScore, error));
    REQUIRE(ScoreHostTestAccess::fallback_pending(host));
    ScoreHostTestAccess::relayout_paged(host);
    REQUIRE(state->payloads.size() == 2);
    CHECK(state->payloads.back() == kMinimalScore);
    CHECK(ScoreHostTestAccess::paged_page_count(host) == 1);
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
}

TEST_CASE("restart and clear-progress stay monolithic after window fallback",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/1),
        kMinimalScore, error));
    const draxul::tests::TempDir progress("scoreview-fallback-progress");
    ScoreHostTestAccess::attach_analysis_source(host, progress.path);
    ScoreHostTestAccess::relayout_flow(host);
    REQUIRE_FALSE(ScoreHostTestAccess::stream_windowed(host));
    REQUIRE_FALSE(ScoreHostTestAccess::stream_active(host));

    ScoreHostTestAccess::restart(host);
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::strip(host));
    ScoreHostTestAccess::clear_progress(host);
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::strip(host));
    for (const std::string& payload : state->payloads)
    {
        if (payload != state->payloads.front())
            CHECK(payload == kMinimalScore);
    }
}
