#include <catch2/catch_all.hpp>

#include "support/scoreview_engine_fake.h"
#include "test_support.h"

#include <draxul/scoreview/verovio_layout_engine.h>
#include <draxul/scoreview/window_engraver.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace draxul::scoreview;

namespace
{

constexpr std::string_view MINIMAL_SCORE = kScoreHostFixtureMinimalScore;

std::unique_ptr<VerovioLayoutEngine> make_engine()
{
    std::string error;
    auto engine = VerovioLayoutEngine::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engine != nullptr);
    return engine;
}

// The shared DeterministicLayoutEngine configured the way these lifetime
// cases need it: every load blocks on an explicit permit and then reports a
// deterministic engraving failure.
std::unique_ptr<DeterministicLayoutEngine> make_blocking_failing_engine(
    const std::shared_ptr<FakeEngineState>& state)
{
    return std::make_unique<DeterministicLayoutEngine>(
        state, std::string{}, /*block_load=*/true,
        /*require_timemap_for_midi=*/false, /*fail_load=*/true);
}

bool wait_until_idle(WindowEngraver& engraver)
{
    for (int i = 0; i < 400; ++i)
    {
        if (!engraver.busy())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return !engraver.busy();
}

} // namespace

TEST_CASE("verovio engine reports missing resources", "[scoreview]")
{
    std::string error;
    auto engine = VerovioLayoutEngine::create("/nonexistent/verovio-data", error);
    CHECK(engine == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("verovio engine lays out a minimal score", "[scoreview]")
{
    auto engine = make_engine();
    CHECK_FALSE(engine->is_loaded());
    CHECK(engine->page_count() == 0);
    CHECK(engine->render_page_svg(1).empty());

    std::string error;
    REQUIRE(engine->load(MINIMAL_SCORE, error));
    CHECK(engine->is_loaded());
    CHECK(engine->page_count() == 1);

    const std::string svg = engine->render_page_svg(1);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("viewBox") != std::string::npos);
    CHECK(svg.find("<use") != std::string::npos); // SMuFL glyph instances
    CHECK(svg.find("staff") != std::string::npos); // staff group class

    CHECK(engine->render_page_svg(0).empty());
    CHECK(engine->render_page_svg(2).empty());

    error.clear();
    CHECK_FALSE(engine->load("", error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("verovio engine rejects garbage input", "[scoreview]")
{
    auto engine = make_engine();
    std::string error;
    CHECK_FALSE(engine->load("this is not any kind of score", error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(engine->is_loaded());
}

TEST_CASE("verovio engine lays out the Grieg .mxl and reflows", "[scoreview]")
{
    auto engine = make_engine();
    LayoutOptions options;
    options.page_size_px = { 840, 1188 };
    options.pixel_scale = 1.0f;
    engine->set_options(options);

    const std::string bytes = draxul::tests::read_file(draxul::tests::project_root()
        / "plugins/scoreview/tests/fixtures/musicxml/grieg-waltz-op-12-no-2.mxl");
    std::string error;
    REQUIRE(engine->load(bytes, error));

    const int pages = engine->page_count();
    CHECK(pages >= 2); // 79 measures of piano music cannot fit one page
    for (int page = 1; page <= pages; ++page)
    {
        const std::string svg = engine->render_page_svg(page);
        INFO("page " << page);
        CHECK(svg.find("<svg") != std::string::npos);
        CHECK(svg.find("<use") != std::string::npos);
    }

    // Reflow to a much smaller page: layout survives and needs more pages.
    options.page_size_px = { 420, 594 };
    engine->set_options(options);
    CHECK(engine->page_count() > pages);
    CHECK(engine->render_page_svg(1).find("<svg") != std::string::npos);
}

TEST_CASE("window engraver engraves off-thread and echoes placement", "[scoreview]")
{
    std::string error;
    auto engraver = WindowEngraver::create(DRAXUL_VEROVIO_DATA_DIR, error);
    INFO(error);
    REQUIRE(engraver != nullptr);
    CHECK_FALSE(engraver->busy());

    WindowEngraver::Job job;
    job.window_xml = std::string(MINIMAL_SCORE);
    job.params.pixel_scale = 1.0f;
    job.params.marking_qpm = 120.0;
    job.first_bar = 3; // placement metadata is opaque to the engrave; echoed back
    job.count = 7;
    job.stream_offset_q = 12.0;
    const WindowEngraver::RequestId request_id = engraver->submit(std::move(job));
    REQUIRE(request_id != 0);

    // Poll until the worker finishes (bounded so a hang fails rather than spins).
    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done.has_value(); ++i)
    {
        done = engraver->poll();
        if (!done.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == request_id);
    CHECK(done->ok);
    CHECK(done->first_bar == 3);
    CHECK(done->count == 7);
    CHECK(done->stream_offset_q == Catch::Approx(12.0));
    REQUIRE(done->window.strip != nullptr);
    CHECK(done->window.flow.ready()); // the timemap joined at least one onset
    CHECK(done->window.flow.mode() == FlowController::TransportMode::Roll);
    CHECK(engraver->poll() == std::nullopt); // result already taken
    CHECK_FALSE(engraver->busy()); // back to idle, ready to reuse

    // Reusing the worker and invalidating an in-flight engrave must not
    // deadlock or expose the stale result.
    WindowEngraver::Job again;
    again.window_xml = std::string(MINIMAL_SCORE);
    again.params.marking_qpm = 120.0;
    REQUIRE(engraver->submit(std::move(again)) != 0);
    engraver->cancel();
    REQUIRE(wait_until_idle(*engraver));
    CHECK(engraver->poll() == std::nullopt);
}

TEST_CASE("window engraver reports a bad window without wedging", "[scoreview]")
{
    std::string error;
    auto engraver = WindowEngraver::create(DRAXUL_VEROVIO_DATA_DIR, error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job job;
    job.window_xml = "this is not a score";
    job.params.marking_qpm = 120.0;
    const WindowEngraver::RequestId request_id = engraver->submit(std::move(job));

    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done.has_value(); ++i)
    {
        done = engraver->poll();
        if (!done.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == request_id);
    CHECK_FALSE(done->ok); // garbage failed to engrave
    CHECK_FALSE(engraver->busy()); // and the worker is idle again
}

TEST_CASE("window engraver coalesces rapid requests and publishes only the latest generation",
    "[scoreview][engraver][lifetime]")
{
    auto state = std::make_shared<FakeEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(make_blocking_failing_engine(state), error);
    INFO(error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job first;
    first.window_xml = "first";
    first.first_bar = 1;
    const auto first_id = engraver->submit(std::move(first));
    REQUIRE(wait_for_loads(state, 1));

    WindowEngraver::Job superseded;
    superseded.window_xml = "superseded";
    superseded.first_bar = 2;
    const auto submit_start = std::chrono::steady_clock::now();
    const auto superseded_id = engraver->submit(std::move(superseded));
    WindowEngraver::Job latest;
    latest.window_xml = "latest";
    latest.first_bar = 3;
    const auto latest_id = engraver->submit(std::move(latest));
    const auto submit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - submit_start)
                               .count();
    CHECK(submit_ms < 50);
    CHECK(first_id < superseded_id);
    CHECK(superseded_id < latest_id);

    release_loads(state, 1);
    REQUIRE(wait_for_loads(state, 2));
    {
        std::lock_guard lock(state->mutex);
        REQUIRE(state->payloads.size() == 2);
        CHECK(state->payloads[0] == "first");
        CHECK(state->payloads[1] == "latest");
    }
    release_loads(state, 2);

    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done; ++i)
    {
        done = engraver->poll();
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == latest_id);
    CHECK(done->first_bar == 3);
    CHECK_FALSE(done->ok);
    CHECK_FALSE(engraver->busy());
}

TEST_CASE("window engraver cancellation is non-blocking generation invalidation",
    "[scoreview][engraver][lifetime]")
{
    auto state = std::make_shared<FakeEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(make_blocking_failing_engine(state), error);
    REQUIRE(engraver != nullptr);

    WindowEngraver::Job job;
    job.window_xml = "blocked";
    REQUIRE(engraver->submit(std::move(job)) != 0);
    REQUIRE(wait_for_loads(state, 1));

    const auto start = std::chrono::steady_clock::now();
    engraver->cancel();
    engraver->cancel();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                                .count();
    CHECK(elapsed_ms < 50);
    CHECK(engraver->busy()); // engine call may finish, but caller did not wait

    release_loads(state, 1);
    REQUIRE(wait_until_idle(*engraver));
    CHECK(engraver->poll() == std::nullopt);

    WindowEngraver::Job replacement;
    replacement.window_xml = "after-cancel";
    const auto replacement_id = engraver->submit(std::move(replacement));
    REQUIRE(replacement_id != 0);
    REQUIRE(wait_for_loads(state, 2));
    release_loads(state, 2);
    std::optional<WindowEngraver::Done> done;
    for (int i = 0; i < 400 && !done; ++i)
    {
        done = engraver->poll();
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(done.has_value());
    CHECK(done->request_id == replacement_id);
}

TEST_CASE("window engraver rejects a missing injected engine", "[scoreview][engraver]")
{
    std::string error;
    auto engraver = WindowEngraver::create(std::unique_ptr<ILayoutEngine>{}, error);
    CHECK(engraver == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("window engraver shutdown owns and joins an active engine job",
    "[scoreview][engraver][lifetime]")
{
    auto state = std::make_shared<FakeEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(make_blocking_failing_engine(state), error);
    REQUIRE(engraver != nullptr);
    WindowEngraver::Job job;
    job.window_xml = "shutdown";
    REQUIRE(engraver->submit(std::move(job)) != 0);
    REQUIRE(wait_for_loads(state, 1));

    std::atomic<bool> destroyed{ false };
    std::thread shutdown([owned = std::move(engraver), &destroyed]() mutable {
        owned.reset();
        destroyed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(destroyed.load()); // teardown joins rather than detaching Verovio
    release_loads(state, 1);
    shutdown.join();
    CHECK(destroyed.load());
    std::lock_guard lock(state->mutex);
    CHECK(state->destroyed == 1);
}
