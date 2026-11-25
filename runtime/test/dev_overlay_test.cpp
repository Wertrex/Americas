// src/dev_overlay.h: the text of a sample, the counter differences, the phase bookkeeping behind its "now" line
// (submit_phases.h) and the pipeline builds in flight (dev_status.h). No window, GPU or game file.
#include "dev_overlay.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#define CHECK(condition) do {                                        \
    if (!(condition)) {                                              \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        return 1;                                                    \
    }                                                                \
} while (0)

namespace sp = leonida::submit_phases;
namespace dov = leonida::dev_overlay;

static bool has(const std::vector<std::string>& lines, const char* part) {
    for (const auto& line : lines)
        if (line.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    // A total that went down was reset by its log line: what it holds is the new part.
    std::uint64_t before = 0;
    CHECK(dov::grown(10, before) == 10 && before == 10);
    CHECK(dov::grown(25, before) == 15);
    CHECK(dov::grown(4, before) == 4 && before == 4);

    // Nested scopes: the inner one is running, then the outer one again, then none.
    CHECK(sp::running().phase == -1);
    {
        const sp::Scope replay(sp::replay);
        CHECK(sp::running().phase == sp::replay);
        {
            const sp::Scope textures(sp::textures);
            CHECK(sp::running().phase == sp::textures);
        }
        CHECK(sp::running().phase == sp::replay);
    }
    CHECK(sp::running().phase == -1);

    // Two threads whose scopes overlap and end in the order they began: no phase stays behind.
    {
        std::atomic<int> step{0};
        std::thread a([&] {
            const sp::Scope heaps(sp::heaps);
            step = 1;
            while (step.load() < 2) std::this_thread::yield();
        });
        std::thread b([&] {
            while (step.load() < 1) std::this_thread::yield();
            const sp::Scope shadows(sp::shadows);
            step = 2;
            while (step.load() < 3) std::this_thread::yield();
        });
        a.join();
        CHECK(sp::running().phase == sp::shadows);
        step = 3;
        b.join();
        CHECK(sp::running().phase == -1);
    }

    // The builds in flight and the last one's name, cut to its buffer.
    auto& builds = leonida::dev_status::builds();
    const auto done = builds.done.load();
    {
        const leonida::dev_status::Build one("one");
        const leonida::dev_status::Build two(std::string(500, 'x').c_str());
        CHECK(builds.active.load() == 2 && builds.last() == std::string(95, 'x'));
    }
    CHECK(builds.active.load() == 0 && builds.done.load() == done + 2);

    dov::Sample s;
    auto text = dov::lines(s, 12);
    CHECK(text.size() == 6);
    CHECK(has(text, "12 s") && has(text, "now: the title's own code") && has(text, "pipelines: 0 built"));
    CHECK(!has(text, "building"));
    s.frames = 8.44;
    s.phase = sp::textures;
    s.phase_for = 3.9;
    s.phase_ms[sp::shadows] = 2;
    s.phase_ms[sp::geometry] = 1.5;
    s.building = 3;
    s.built = 853;
    s.build = std::string(200, 'n');  // longer than the line's part for it
    s.drawn = 3029;
    s.skipped = 38363;
    text = dov::lines(s, 147);
    CHECK(has(text, "8.4 fps") && has(text, "now: refreshing textures for 3 s") && has(text, "copies 3.5"));
    CHECK(has(text, "853 built, 3 building (nnn") && has(text, "3029 drawn, 38363 skipped"));
    for (const auto& line : text) CHECK(line.size() < 160);
    std::puts("dev_overlay_test: ok");
    return 0;
}
