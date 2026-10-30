// 팀원 2: debounce 단위 테스트 (PC에서 g++ 로 실행, Arduino 불필요)
//   docker compose run --rm fw sh test.sh
// debounce.cpp 의 TODO 를 구현하면 전부 PASS 해야 한다.
#include <cstdio>
#include "debounce.h"

static int g_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("    FAIL line %d: %s\n", __LINE__, #cond);          \
            g_failed++;                                                      \
        }                                                                    \
    } while (0)

static Debouncer make()
{
    Debouncer d;
    debounce_init(&d, DebounceConfig{2, 1500});   // present_frames=2, absent_ms=1500
    return d;
}

// 얼굴 있는 프레임 n개를 100ms 간격으로 넣는다. 마지막 시각을 반환.
static uint32_t feed(Debouncer *d, bool detected, int n, uint32_t t)
{
    for (int i = 0; i < n; i++, t += 100) debounce_update(d, detected, t);
    return t - 100;
}

static void test_initial_absent()
{
    Debouncer d = make();
    CHECK(d.state == PRESENCE_ABSENT);
}

static void test_one_frame_is_not_enough()
{
    Debouncer d = make();
    CHECK(debounce_update(&d, true, 1000) == false);
    CHECK(d.state == PRESENCE_ABSENT);
}

static void test_consecutive_frames_turn_present()
{
    Debouncer d = make();
    debounce_update(&d, true, 1000);
    CHECK(debounce_update(&d, true, 1100) == true);   // 전환되는 순간에만 true
    CHECK(d.state == PRESENCE_PRESENT);
    CHECK(debounce_update(&d, true, 1200) == false);  // 이미 PRESENT
}

static void test_miss_resets_streak()
{
    Debouncer d = make();
    debounce_update(&d, true, 1000);
    debounce_update(&d, false, 1100);
    debounce_update(&d, true, 1200);
    CHECK(d.state == PRESENCE_ABSENT);
}

static void test_short_miss_keeps_present()
{
    Debouncer d = make();
    uint32_t t = feed(&d, true, 3, 1000);        // PRESENT, 마지막 검출 1200
    CHECK(d.state == PRESENCE_PRESENT);
    CHECK(debounce_update(&d, false, t + 500) == false);
    CHECK(debounce_update(&d, false, t + 1400) == false);
    CHECK(d.state == PRESENCE_PRESENT);
}

static void test_long_miss_turns_absent_once()
{
    Debouncer d = make();
    uint32_t t = feed(&d, true, 3, 1000);
    CHECK(debounce_update(&d, false, t + 1500) == true);
    CHECK(d.state == PRESENCE_ABSENT);
    CHECK(debounce_update(&d, false, t + 1600) == false);   // 한 번만 true
}

static void test_detection_in_gap_restarts_timer()
{
    Debouncer d = make();
    uint32_t t = feed(&d, true, 3, 1000);
    debounce_update(&d, false, t + 500);
    debounce_update(&d, true, t + 800);                      // 잠깐 다시 보임 → 타이머 리셋
    CHECK(debounce_update(&d, false, t + 2000) == false);    // 800 기준 1200ms 경과 → 아직 PRESENT
    CHECK(d.state == PRESENCE_PRESENT);
    CHECK(debounce_update(&d, false, t + 2300) == true);     // 800 기준 1500ms 경과 → ABSENT
}

static void test_tick_turns_absent_without_frames()
{
    Debouncer d = make();
    uint32_t t = feed(&d, true, 3, 1000);
    CHECK(debounce_tick(&d, t + 1000) == false);
    CHECK(debounce_tick(&d, t + 1500) == true);
    CHECK(d.state == PRESENCE_ABSENT);
}

static void test_tick_does_nothing_when_absent()
{
    Debouncer d = make();
    CHECK(debounce_tick(&d, 100000) == false);
    CHECK(d.state == PRESENCE_ABSENT);
}

static void test_needs_frames_again_after_absent()
{
    Debouncer d = make();
    uint32_t t = feed(&d, true, 3, 1000);
    debounce_update(&d, false, t + 1500);                    // ABSENT
    CHECK(debounce_update(&d, true, t + 1600) == false);     // 1프레임으로는 부족
    CHECK(debounce_update(&d, true, t + 1700) == true);
}

static void test_millis_wraparound()
{
    Debouncer d = make();
    uint32_t t = feed(&d, true, 3, 0xFFFFFF00u);             // 마지막 검출 0xFFFFFFC8
    CHECK(d.state == PRESENCE_PRESENT);
    CHECK(debounce_update(&d, false, 500) == false);         // 0으로 돌아간 뒤 약 556ms 경과
    CHECK(debounce_update(&d, false, t + 1500) == true);     // 오버플로된 덧셈도 정상 비교
}

int main()
{
    struct { const char *name; void (*fn)(); } tests[] = {
        {"initial_absent", test_initial_absent},
        {"one_frame_is_not_enough", test_one_frame_is_not_enough},
        {"consecutive_frames_turn_present", test_consecutive_frames_turn_present},
        {"miss_resets_streak", test_miss_resets_streak},
        {"short_miss_keeps_present", test_short_miss_keeps_present},
        {"long_miss_turns_absent_once", test_long_miss_turns_absent_once},
        {"detection_in_gap_restarts_timer", test_detection_in_gap_restarts_timer},
        {"tick_turns_absent_without_frames", test_tick_turns_absent_without_frames},
        {"tick_does_nothing_when_absent", test_tick_does_nothing_when_absent},
        {"needs_frames_again_after_absent", test_needs_frames_again_after_absent},
        {"millis_wraparound", test_millis_wraparound},
    };

    int failed_tests = 0;
    for (auto &t : tests) {
        int before = g_failed;
        t.fn();
        bool ok = g_failed == before;
        if (!ok) failed_tests++;
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", t.name);
    }
    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    std::printf("\n%d/%d passed\n", total - failed_tests, total);
    return failed_tests == 0 ? 0 : 1;
}
