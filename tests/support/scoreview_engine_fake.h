#pragma once

// The ONE blockable, deterministic fake layout engine shared by every
// ScoreView suite (host orchestration/rebuild, worker stress, and the layout
// tests' engraver-lifetime cases). load() records its payload and (optionally)
// waits until the test grants a permit, so worker interleavings are driven,
// not raced; fail_load makes the engrave report a deterministic failure and
// the destroyed counter proves teardown owns the engine.

#include <draxul/scoreview/layout_engine.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{
namespace scoreview
{

struct FakeEngineState
{
    std::mutex mutex;
    std::condition_variable changed;
    int load_calls = 0;
    int permits = 0;
    int destroyed = 0;
    std::vector<std::string> payloads;
};

class DeterministicLayoutEngine final : public ILayoutEngine
{
public:
    DeterministicLayoutEngine(
        std::shared_ptr<FakeEngineState> state, std::string svg, bool block_load,
        bool require_timemap_for_midi = false, bool fail_load = false)
        : state_(std::move(state))
        , svg_(std::move(svg))
        , block_load_(block_load)
        , require_timemap_for_midi_(require_timemap_for_midi)
        , fail_load_(fail_load)
    {
    }

    ~DeterministicLayoutEngine() override
    {
        std::lock_guard lock(state_->mutex);
        ++state_->destroyed;
        state_->changed.notify_all();
    }

    bool load(std::string_view bytes, std::string& error) override
    {
        std::unique_lock lock(state_->mutex);
        ++state_->load_calls;
        const int call = state_->load_calls;
        state_->payloads.emplace_back(bytes);
        state_->changed.notify_all();
        if (block_load_)
            state_->changed.wait(lock, [&]() { return state_->permits >= call; });
        if (fail_load_)
        {
            error = "deterministic fake engraving failure";
            return false;
        }
        loaded_ = true;
        error.clear();
        return true;
    }

    void set_options(const LayoutOptions&) override {}
    bool is_loaded() const override
    {
        return loaded_;
    }
    int page_count() override
    {
        return loaded_ ? 1 : 0;
    }
    std::string render_page_svg(int page_number) override
    {
        return loaded_ && page_number == 1 ? svg_ : std::string{};
    }
    std::string render_timemap() override;
    int midi_pitch_for_element(const std::string& element_id) override
    {
        if (require_timemap_for_midi_ && !timemap_rendered_)
            return -1;
        return element_id == "usfythd" ? 60 : -1;
    }
    int note_letter_for_element(const std::string& element_id) override
    {
        return element_id == "usfythd" ? 0 : -1;
    }
    std::vector<std::string> tie_end_ids() override
    {
        return {};
    }

private:
    std::shared_ptr<FakeEngineState> state_;
    std::string svg_;
    bool block_load_ = false;
    bool require_timemap_for_midi_ = false;
    bool fail_load_ = false;
    bool timemap_rendered_ = false;
    bool loaded_ = false;
};

inline constexpr std::string_view kScoreHostFixtureMinimalScore
    = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <key><fifths>0</fifths></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <clef><sign>G</sign><line>2</line></clef>
      </attributes>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><type>whole</type></note>
    </measure>
  </part>
</score-partwise>)xml";

inline constexpr std::string_view kScoreHostFixtureTimemap = R"json([
  {"qstamp": 0, "tempo": 120, "on": ["usfythd"]},
  {"qstamp": 4, "off": ["usfythd"]}
])json";

inline std::string DeterministicLayoutEngine::render_timemap()
{
    timemap_rendered_ = true;
    return loaded_ ? std::string(kScoreHostFixtureTimemap) : std::string{};
}

inline bool wait_for_loads(const std::shared_ptr<FakeEngineState>& state, int count)
{
    std::unique_lock lock(state->mutex);
    return state->changed.wait_for(
        lock, std::chrono::seconds(2), [&]() { return state->load_calls >= count; });
}

inline void release_loads(const std::shared_ptr<FakeEngineState>& state, int count)
{
    std::lock_guard lock(state->mutex);
    state->permits = std::max(state->permits, count);
    state->changed.notify_all();
}

} // namespace scoreview
} // namespace draxul
