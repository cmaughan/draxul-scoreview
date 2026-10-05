#include <draxul/scoreview/player_model.h>

#include <cstdio>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <system_error>

namespace draxul
{
namespace scoreview
{

using nlohmann::json;

namespace
{

constexpr int kSchemaVersion = 1;
// Keep the file bounded: forever-stats aggregate, session history caps.
constexpr size_t kMaxSessions = 200;

// Onset qstamps are map keys in JSON; fixed precision keeps them stable
// across float formatting differences.
std::string qstamp_key(double q)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f", q);
    return buffer;
}

PlayerModel::HandTally& hand_for_staff(PlayerModel::BarTally& tally, int staff)
{
    return staff == 2 ? tally.left : tally.right;
}

const PlayerModel::HandTally& hand_for_staff(const PlayerModel::BarTally& tally, int staff)
{
    return staff == 2 ? tally.left : tally.right;
}

int hand_staff_for_outcome(const NoteOutcome& outcome)
{
    if (outcome.staff == 1 || outcome.staff == 2)
        return outcome.staff;
    return outcome.pitch < PlayerModel::kHandSplitMidi ? 2 : 1;
}

void push_hand_pass(PlayerModel::HandTally& hand, bool dirty)
{
    hand.recent_passes.push_back(dirty ? 0 : 1);
    if (hand.recent_passes.size() > static_cast<size_t>(PlayerModel::kRecentEncounters))
        hand.recent_passes.erase(hand.recent_passes.begin());
    hand.consecutive_clean = dirty ? 0 : hand.consecutive_clean + 1;
    ++hand.pass_count;
}

// Saved progress is untrusted: a syntactically valid file can still carry
// wrong-typed fields or malformed numeric keys. Every read below validates
// the type and range and throws ProgressFormatError otherwise, so
// deserialize() can reject the whole record without publishing partial state.
struct ProgressFormatError
{
};

const json& require_object(const json& value)
{
    if (!value.is_object())
        throw ProgressFormatError();
    return value;
}

double finite_number(const json& value)
{
    if (!value.is_number())
        throw ProgressFormatError();
    const double number = value.get<double>();
    if (!std::isfinite(number))
        throw ProgressFormatError();
    return number;
}

double read_double(const json& object, const char* key, double fallback)
{
    const auto found = object.find(key);
    return found == object.end() ? fallback : finite_number(*found);
}

// Non-negative int counter/day field, range-checked before narrowing (a
// float or out-of-range integer must not reach a static_cast to int).
int read_count(const json& object, const char* key, int fallback)
{
    const auto found = object.find(key);
    if (found == object.end())
        return fallback;
    if (found->is_number_unsigned())
    {
        const auto value = found->get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            throw ProgressFormatError();
        return static_cast<int>(value);
    }
    if (found->is_number_integer())
    {
        const auto value = found->get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<int>::max())
            throw ProgressFormatError();
        return static_cast<int>(value);
    }
    throw ProgressFormatError();
}

std::string read_string(const json& object, const char* key, const std::string& fallback)
{
    const auto found = object.find(key);
    if (found == object.end())
        return fallback;
    if (!found->is_string())
        throw ProgressFormatError();
    return found->get<std::string>();
}

void read_passes(const json& object, const char* key, std::vector<uint8_t>& out)
{
    const auto found = object.find(key);
    if (found == object.end())
        return;
    if (!found->is_array())
        throw ProgressFormatError();
    for (const auto& pass : *found)
    {
        if (!pass.is_number_integer())
            throw ProgressFormatError();
        out.push_back(pass.get<std::int64_t>() != 0 ? uint8_t{ 1 } : uint8_t{ 0 });
    }
}

int parse_int_key(const std::string& key)
{
    int value = 0;
    const char* first = key.data();
    const char* last = first + key.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (key.empty() || ec != std::errc() || ptr != last)
        throw ProgressFormatError();
    return value;
}

double parse_double_key(const std::string& key)
{
    // qstamp_key() writes "%.4f"; read it back with the matching C parser.
    if (key.empty() || std::isspace(static_cast<unsigned char>(key.front())))
        throw ProgressFormatError();
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(key.c_str(), &end);
    if (errno == ERANGE || end != key.c_str() + key.size() || !std::isfinite(value))
        throw ProgressFormatError();
    return value;
}

} // namespace

void PlayerModel::TimingStats::add(double delta_q)
{
    ++samples;
    const double d = delta_q - mean_q;
    mean_q += d / samples;
    m2_q += d * (delta_q - mean_q);
}

double PlayerModel::TimingStats::variance_q() const
{
    return samples > 1 ? m2_q / (samples - 1) : 0.0;
}

void PlayerModel::OnsetStats::push_recent(double quality)
{
    recent.push_back(quality);
    if (recent.size() > static_cast<size_t>(kRecentEncounters))
        recent.erase(recent.begin());
}

double PlayerModel::OnsetStats::recent_mean() const
{
    if (recent.empty())
        return 0.0;
    double sum = 0.0;
    for (const double q : recent)
        sum += q;
    return sum / static_cast<double>(recent.size());
}

void PlayerModel::set_piece(const std::string& title, double marking_qpm, double quarters_per_bar)
{
    title_ = title;
    marking_qpm_ = marking_qpm;
    quarters_per_bar_ = quarters_per_bar > 0.0 ? quarters_per_bar : 4.0;
}

int PlayerModel::civil_day_from_iso(const std::string& iso)
{
    // Howard Hinnant's days-from-civil; only the date part matters here.
    int y = 0;
    int m = 0;
    int d = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1
        || d > 31)
        return 0;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy
        = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void PlayerModel::begin_session(const std::string& start_iso)
{
    current_day_ = civil_day_from_iso(start_iso);
    if (session_active_)
        return;
    Session session;
    session.start_iso = start_iso;
    sessions_.push_back(std::move(session));
    if (sessions_.size() > kMaxSessions)
        sessions_.erase(sessions_.begin());
    session_active_ = true;
    session_notes_ = 0;
}

void PlayerModel::end_session(int seconds, double tempo_frac)
{
    if (!session_active_ || sessions_.empty())
        return;
    Session& session = sessions_.back();
    session.seconds = seconds;
    session.notes = session_notes_;
    session.end_tempo_frac = tempo_frac;
    last_tempo_frac_ = tempo_frac;
    best_tempo_frac_ = std::max(best_tempo_frac_, tempo_frac);
    session_active_ = false;
    close_open_pass(); // a traversal in flight still counts
}

void PlayerModel::close_open_pass()
{
    if (open_pass_bar_ >= 0 && open_pass_outcomes_ > 0)
    {
        BarTally& tally = bar_tally_[open_pass_bar_];
        tally.recent_passes.push_back(open_pass_dirty_ ? 0 : 1);
        dirty_pass_count_ += open_pass_dirty_ ? 1 : 0;
        if (tally.recent_passes.size() > static_cast<size_t>(kRecentEncounters))
            tally.recent_passes.erase(tally.recent_passes.begin());
        tally.consecutive_clean = open_pass_dirty_ ? 0 : tally.consecutive_clean + 1;
        ++tally.pass_count;
        // Day-separated clean re-encounter: the spacing schedule's currency.
        if (open_pass_dirty_)
            tally.spaced_streak = 0;
        else if (current_day_ > 0 && tally.last_pass_day > 0
            && tally.last_pass_day < current_day_)
            ++tally.spaced_streak;
        if (current_day_ > 0)
            tally.last_pass_day = current_day_;
        if (tally.ladder_frac <= 0.0)
            tally.ladder_frac = kLadderStart;
        tally.ladder_frac = std::clamp(tally.ladder_frac
                + (open_pass_dirty_ ? -kLadderDrop : kLadderStep),
            kLadderFloor, kLadderCap);
        if (open_pass_left_seen_)
            push_hand_pass(tally.left, open_pass_left_dirty_);
        if (open_pass_right_seen_)
            push_hand_pass(tally.right, open_pass_right_dirty_);
    }
    open_pass_bar_ = -1;
    open_pass_dirty_ = false;
    open_pass_outcomes_ = 0;
    open_pass_left_seen_ = false;
    open_pass_left_dirty_ = false;
    open_pass_right_seen_ = false;
    open_pass_right_dirty_ = false;
}

void PlayerModel::apply(const NoteOutcome& outcome)
{
    ++total_notes_;
    ++session_notes_;
    if (outcome.stray)
    {
        // A wrong note fumbles whatever pass is in flight — playing THROUGH
        // an error must never count as a clean traversal.
        open_pass_dirty_ = open_pass_bar_ >= 0 ? true : open_pass_dirty_;
        if (open_pass_left_seen_)
            open_pass_left_dirty_ = true;
        if (open_pass_right_seen_)
            open_pass_right_dirty_ = true;
        // Attribute the stray to nearby pitches: a fluff one or two
        // semitones off a known note is that note's trouble, not noise.
        for (auto& [pitch, stats] : pitch_)
        {
            if (std::abs(pitch - outcome.pitch) <= 2)
                ++stats.wrong_near;
        }
        return;
    }
    PitchStats& pitch = pitch_[outcome.pitch];
    // Drill-bar outcomes (onset_q sentinel) train pitch/chord statistics
    // but must not write mastery for a bar location they don't have.
    const bool drill = outcome.onset_q <= kDrillOnsetSentinel + 1.0;
    OnsetStats* onset = drill ? nullptr : &onset_[outcome.onset_q];
    if (outcome.verdict == NoteVerdict::Correct)
    {
        ++pitch.hit;
        pitch.timing.add(outcome.delta_q);
        if (onset != nullptr)
        {
            ++onset->hit;
            onset->timing.add(outcome.delta_q);
            onset->push_recent(outcome.quality);
        }
    }
    else
    {
        ++pitch.miss;
        if (onset != nullptr)
        {
            ++onset->miss;
            onset->push_recent(0.0);
        }
    }

    // Per-bar right/wrong, split by hand (drills carry no bar location).
    if (!drill && quarters_per_bar_ > 0.0)
    {
        const int bar = static_cast<int>(std::floor(outcome.onset_q / quarters_per_bar_));
        // Pass tracking: the outcome stream runs in transport order and each
        // stream slot is a whole bar, so moving to a different bar closes
        // the previous bar's traversal.
        if (bar != open_pass_bar_)
        {
            close_open_pass();
            open_pass_bar_ = bar;
        }
        BarTally& tally = bar_tally_[bar];
        // Hand attribution: the engraved staff when known (1 = RH, 2 = LH);
        // the middle-C pitch split only as the fallback the header admits
        // is fuzzy.
        const int staff = hand_staff_for_outcome(outcome);
        HandTally& hand = hand_for_staff(tally, staff);
        const bool correct = outcome.verdict == NoteVerdict::Correct;
        ++(correct ? tally.hit : tally.miss);
        ++(correct ? hand.hit : hand.miss);

        ++open_pass_outcomes_;
        if (!correct)
            open_pass_dirty_ = true;
        if (staff == 2)
        {
            open_pass_left_seen_ = true;
            open_pass_left_dirty_ = open_pass_left_dirty_ || !correct;
        }
        else
        {
            open_pass_right_seen_ = true;
            open_pass_right_dirty_ = open_pass_right_dirty_ || !correct;
        }
    }
}

void PlayerModel::clear_progress()
{
    pitch_.clear();
    onset_.clear();
    chord_.clear();
    bar_tally_.clear();
    sessions_.clear();
    best_tempo_frac_ = 0.0;
    last_tempo_frac_ = 0.0;
    total_notes_ = 0;
    session_active_ = false;
    session_notes_ = 0;
    open_pass_bar_ = -1;
    open_pass_dirty_ = false;
    open_pass_outcomes_ = 0;
    open_pass_left_seen_ = false;
    open_pass_left_dirty_ = false;
    open_pass_right_seen_ = false;
    open_pass_right_dirty_ = false;
    extra_json_.clear();
}

int PlayerModel::bar_consecutive_clean(int bar_index) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() ? found->second.consecutive_clean : 0;
}

int PlayerModel::bar_pass_count(int bar_index) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() ? found->second.pass_count : 0;
}

int PlayerModel::bar_hand_consecutive_clean(int bar_index, int staff) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() ? hand_for_staff(found->second, staff).consecutive_clean : 0;
}

int PlayerModel::bar_hand_pass_count(int bar_index, int staff) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() ? hand_for_staff(found->second, staff).pass_count : 0;
}

int PlayerModel::bar_last_pass_day(int bar_index) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() ? found->second.last_pass_day : 0;
}

int PlayerModel::bar_spaced_streak(int bar_index) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() ? found->second.spaced_streak : 0;
}

double PlayerModel::bar_tempo_ladder(int bar_index) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() && found->second.ladder_frac > 0.0
        ? found->second.ladder_frac
        : kLadderStart;
}

bool PlayerModel::bar_last_pass_dirty(int bar_index) const
{
    const auto found = bar_tally_.find(bar_index);
    return found != bar_tally_.end() && !found->second.recent_passes.empty()
        && found->second.recent_passes.back() == 0;
}

bool PlayerModel::bar_hand_last_pass_dirty(int bar_index, int staff) const
{
    const auto found = bar_tally_.find(bar_index);
    if (found == bar_tally_.end())
        return false;
    const HandTally& hand = hand_for_staff(found->second, staff);
    return !hand.recent_passes.empty() && hand.recent_passes.back() == 0;
}

int PlayerModel::bar_encounters(int bar_index) const
{
    const double bar_start = bar_index * quarters_per_bar_;
    const double bar_end = bar_start + quarters_per_bar_;
    int count = 0;
    for (auto it = onset_.lower_bound(bar_start); it != onset_.end() && it->first < bar_end; ++it)
    {
        if (!it->second.recent.empty())
            ++count;
    }
    return count;
}

void PlayerModel::apply(const ChordOutcome& outcome)
{
    ChordStats& stats = chord_[chord_key(outcome.pitches)];
    switch (outcome.result)
    {
    case ChordOutcome::Result::Clean:
        ++stats.clean;
        break;
    case ChordOutcome::Result::Split:
        ++stats.split;
        break;
    case ChordOutcome::Result::Miss:
        ++stats.miss;
        break;
    }
}

int PlayerModel::onset_trailing_correct(double onset_q) const
{
    const auto found = onset_.find(onset_q);
    if (found == onset_.end())
        return 0;
    int trailing = 0;
    for (auto it = found->second.recent.rbegin(); it != found->second.recent.rend(); ++it)
    {
        if (*it <= 0.0)
            break;
        ++trailing;
    }
    return trailing;
}

double PlayerModel::bar_mastery(int bar_index) const
{
    const double bar_start = bar_index * quarters_per_bar_;
    const double bar_end = bar_start + quarters_per_bar_;
    double sum = 0.0;
    int count = 0;
    for (auto it = onset_.lower_bound(bar_start); it != onset_.end() && it->first < bar_end; ++it)
    {
        sum += it->second.recent_mean();
        ++count;
    }
    return count > 0 ? sum / count : 0.0;
}

std::string PlayerModel::chord_key(const std::vector<int>& sorted_pitches)
{
    std::string key;
    for (size_t i = 0; i < sorted_pitches.size(); ++i)
    {
        if (i > 0)
            key += '+';
        key += std::to_string(sorted_pitches[i]);
    }
    return key;
}

std::string PlayerModel::serialize() const
{
    json doc = extra_json_.empty() ? json::object() : json::parse(extra_json_, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        doc = json::object();

    doc["version"] = kSchemaVersion;
    doc["piece"] = { { "title", title_ }, { "marking_qpm", marking_qpm_ },
        { "quarters_per_bar", quarters_per_bar_ } };
    doc["tempo"] = { { "best_frac", best_tempo_frac_ }, { "last_frac", last_tempo_frac_ } };
    doc["total_notes"] = total_notes_;

    json sessions = json::array();
    for (const Session& session : sessions_)
    {
        sessions.push_back({ { "start", session.start_iso }, { "seconds", session.seconds },
            { "notes", session.notes }, { "end_tempo_frac", session.end_tempo_frac } });
    }
    doc["sessions"] = sessions;

    json pitch = json::object();
    for (const auto& [midi, stats] : pitch_)
    {
        pitch[std::to_string(midi)] = { { "hit", stats.hit }, { "miss", stats.miss },
            { "wrong_near", stats.wrong_near }, { "dt_mean_q", stats.timing.mean_q },
            { "dt_var_q", stats.timing.variance_q() }, { "dt_n", stats.timing.samples },
            { "dt_m2", stats.timing.m2_q } };
    }
    doc["pitch"] = pitch;

    json onset = json::object();
    for (const auto& [q, stats] : onset_)
    {
        onset[qstamp_key(q)] = { { "hit", stats.hit }, { "miss", stats.miss },
            { "dt_mean_q", stats.timing.mean_q }, { "dt_n", stats.timing.samples },
            { "dt_m2", stats.timing.m2_q }, { "recent", stats.recent } };
    }
    doc["onset"] = onset;

    json chord = json::object();
    for (const auto& [key, stats] : chord_)
    {
        chord[key] = { { "clean", stats.clean }, { "split", stats.split },
            { "miss", stats.miss } };
    }
    doc["chord"] = chord;

    json bars = json::object();
    for (const auto& [bar, t] : bar_tally_)
    {
        bars[std::to_string(bar)]
            = { { "hit", t.hit }, { "miss", t.miss }, { "lh_hit", t.left.hit },
                  { "lh_miss", t.left.miss }, { "rh_hit", t.right.hit },
                  { "rh_miss", t.right.miss }, { "lh_passes", t.left.recent_passes },
                  { "lh_clean_streak", t.left.consecutive_clean },
                  { "lh_pass_count", t.left.pass_count },
                  { "rh_passes", t.right.recent_passes },
                  { "rh_clean_streak", t.right.consecutive_clean },
                  { "rh_pass_count", t.right.pass_count }, { "passes", t.recent_passes },
                  { "clean_streak", t.consecutive_clean }, { "pass_count", t.pass_count },
                  { "ladder", t.ladder_frac }, { "last_day", t.last_pass_day },
                  { "spaced_streak", t.spaced_streak } };
    }
    doc["bars"] = bars;

    return doc.dump(2);
}

bool PlayerModel::deserialize(const std::string& json_text)
{
    const json doc = json::parse(json_text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
        return false;

    // Restore into a scratch copy and publish it only once every field has
    // validated: a well-formed file with a wrong-typed field or a malformed
    // numeric key must leave this model exactly as it was (the caller then
    // keeps its fresh session) rather than throwing half way through.
    PlayerModel next = *this;
    next.pitch_.clear();
    next.onset_.clear();
    next.chord_.clear();
    next.bar_tally_.clear();
    next.sessions_.clear();
    try
    {
        if (const auto piece = doc.find("piece"); piece != doc.end())
        {
            const json& p = require_object(*piece);
            next.title_ = read_string(p, "title", next.title_);
            next.marking_qpm_ = read_double(p, "marking_qpm", next.marking_qpm_);
            next.quarters_per_bar_ = read_double(p, "quarters_per_bar", next.quarters_per_bar_);
            if (!(next.quarters_per_bar_ > 0.0))
                throw ProgressFormatError();
        }
        next.best_tempo_frac_ = 0.0;
        next.last_tempo_frac_ = 0.0;
        if (const auto tempo = doc.find("tempo"); tempo != doc.end())
        {
            const json& t = require_object(*tempo);
            next.best_tempo_frac_ = read_double(t, "best_frac", 0.0);
            next.last_tempo_frac_ = read_double(t, "last_frac", 0.0);
        }
        next.total_notes_ = read_count(doc, "total_notes", 0);

        if (const auto sessions = doc.find("sessions"); sessions != doc.end())
        {
            if (!sessions->is_array())
                throw ProgressFormatError();
            for (const json& item : *sessions)
            {
                const json& entry = require_object(item);
                Session session;
                session.start_iso = read_string(entry, "start", "");
                session.seconds = read_count(entry, "seconds", 0);
                session.notes = read_count(entry, "notes", 0);
                session.end_tempo_frac = read_double(entry, "end_tempo_frac", 0.0);
                next.sessions_.push_back(std::move(session));
            }
        }
        if (const auto pitch = doc.find("pitch"); pitch != doc.end())
        {
            for (const auto& [key, item] : require_object(*pitch).items())
            {
                const json& value = require_object(item);
                PitchStats stats;
                stats.hit = read_count(value, "hit", 0);
                stats.miss = read_count(value, "miss", 0);
                stats.wrong_near = read_count(value, "wrong_near", 0);
                stats.timing.samples = read_count(value, "dt_n", 0);
                stats.timing.mean_q = read_double(value, "dt_mean_q", 0.0);
                stats.timing.m2_q = read_double(value, "dt_m2", 0.0);
                next.pitch_[parse_int_key(key)] = std::move(stats);
            }
        }
        if (const auto onset = doc.find("onset"); onset != doc.end())
        {
            for (const auto& [key, item] : require_object(*onset).items())
            {
                const json& value = require_object(item);
                OnsetStats stats;
                stats.hit = read_count(value, "hit", 0);
                stats.miss = read_count(value, "miss", 0);
                stats.timing.samples = read_count(value, "dt_n", 0);
                stats.timing.mean_q = read_double(value, "dt_mean_q", 0.0);
                stats.timing.m2_q = read_double(value, "dt_m2", 0.0);
                if (const auto recent = value.find("recent"); recent != value.end())
                {
                    if (!recent->is_array())
                        throw ProgressFormatError();
                    for (const json& q : *recent)
                        stats.recent.push_back(finite_number(q));
                }
                next.onset_[parse_double_key(key)] = std::move(stats);
            }
        }
        if (const auto chord = doc.find("chord"); chord != doc.end())
        {
            for (const auto& [key, item] : require_object(*chord).items())
            {
                const json& value = require_object(item);
                ChordStats stats;
                stats.clean = read_count(value, "clean", 0);
                stats.split = read_count(value, "split", 0);
                stats.miss = read_count(value, "miss", 0);
                next.chord_[key] = stats;
            }
        }
        if (const auto bars = doc.find("bars"); bars != doc.end())
        {
            for (const auto& [key, item] : require_object(*bars).items())
            {
                const json& value = require_object(item);
                BarTally tally;
                tally.hit = read_count(value, "hit", 0);
                tally.miss = read_count(value, "miss", 0);
                tally.left.hit = read_count(value, "lh_hit", 0);
                tally.left.miss = read_count(value, "lh_miss", 0);
                tally.right.hit = read_count(value, "rh_hit", 0);
                tally.right.miss = read_count(value, "rh_miss", 0);
                read_passes(value, "lh_passes", tally.left.recent_passes);
                tally.left.consecutive_clean = read_count(value, "lh_clean_streak", 0);
                tally.left.pass_count = read_count(value, "lh_pass_count", 0);
                read_passes(value, "rh_passes", tally.right.recent_passes);
                tally.right.consecutive_clean = read_count(value, "rh_clean_streak", 0);
                tally.right.pass_count = read_count(value, "rh_pass_count", 0);
                read_passes(value, "passes", tally.recent_passes);
                tally.consecutive_clean = read_count(value, "clean_streak", 0);
                tally.pass_count = read_count(value, "pass_count", 0);
                tally.ladder_frac = read_double(value, "ladder", 0.0);
                tally.last_pass_day = read_count(value, "last_day", 0);
                tally.spaced_streak = read_count(value, "spaced_streak", 0);
                next.bar_tally_[parse_int_key(key)] = tally;
            }
        }

        // Preserve fields this build doesn't understand (newer schema data).
        json extra = doc;
        for (const char* known : { "version", "piece", "tempo", "total_notes", "sessions",
                 "pitch", "onset", "chord", "bars" })
            extra.erase(known);
        next.extra_json_ = extra.empty() ? std::string() : extra.dump();
    }
    catch (const ProgressFormatError&)
    {
        return false;
    }
    catch (const json::exception&)
    {
        return false;
    }

    next.session_active_ = false;
    next.session_notes_ = 0;
    next.open_pass_bar_ = -1;
    next.open_pass_dirty_ = false;
    next.open_pass_outcomes_ = 0;
    next.open_pass_left_seen_ = false;
    next.open_pass_left_dirty_ = false;
    next.open_pass_right_seen_ = false;
    next.open_pass_right_dirty_ = false;
    *this = std::move(next);
    return true;
}

} // namespace scoreview
} // namespace draxul
