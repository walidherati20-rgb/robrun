/* silent_aim_test.c - Offline tests for the silent-aim angle mapping.
 * Pure math: no driver, process, thread or game memory is touched.
 */
#include <stdio.h>
#include <math.h>
#include "rr_silent.h"

static int g_failures;
#define CHECK(condition, message) do { \
    if (!(condition)) { printf("FAIL: %s\n", message); ++g_failures; } \
} while (0)

static int near(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}
static int unit_close(RR_V3 a, RR_V3 b, float eps) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
static float dot(RR_V3 a, RR_V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static void test_basis(void) {
    RR_V3 f = rr_look_forward(RR_LOOK_HYP_0, 0.0f, 0.0f);
    CHECK(unit_close(f, (RR_V3){0, 0, 1}, 1e-5f), "zero angles look down +Z");
    f = rr_look_forward(RR_LOOK_HYP_0, 90.0f, 0.0f);
    CHECK(unit_close(f, (RR_V3){1, 0, 0}, 1e-5f), "yaw 90 looks down +X");
    f = rr_look_forward(RR_LOOK_HYP_0, 0.0f, 30.0f);
    CHECK(near(f.y, -0.5f, 1e-5f), "positive pitch looks down (Unity order)");
    CHECK(near(f.z, 0.8660254f, 1e-5f), "pitch keeps the horizontal length");
    f = rr_look_forward(RR_LOOK_HYP_2, 0.0f, 30.0f);
    CHECK(near(f.y, 0.5f, 1e-5f), "hypothesis 2 flips pitch");
    f = rr_look_forward(RR_LOOK_HYP_1, 90.0f, 0.0f);
    CHECK(near(f.x, -1.0f, 1e-5f), "hypothesis 1 flips yaw");
}

static void test_roundtrip(void) {
    const float yaws[] = { -179.0f, -135.0f, -90.0f, -37.5f, 0.0f, 12.0f, 90.0f, 179.0f };
    const float pitches[] = { -60.0f, -22.0f, -1.0f, 0.0f, 8.5f, 44.0f, 75.0f };
    for (int hyp = 0; hyp < 4; ++hyp) {
        for (size_t i = 0; i < sizeof(yaws) / sizeof(yaws[0]); ++i) {
            for (size_t j = 0; j < sizeof(pitches) / sizeof(pitches[0]); ++j) {
                RR_V3 f = rr_look_forward(hyp, yaws[i], pitches[j]);
                float yaw = 0.0f, pitch = 0.0f;
                CHECK(rr_look_angles(hyp, &f, &yaw, &pitch), "angles accept a unit vector");
                RR_V3 back = rr_look_forward(hyp, yaw, pitch);
                CHECK(dot(f, back) > 0.9999f, "angles/forward round-trip keeps the direction");
                CHECK(yaw > -180.0f && yaw <= 180.0f, "yaw is wrapped to (-180, 180]");
                CHECK(near(pitch, pitches[j], 0.02f), "pitch round-trips");
            }
        }
    }
}

static void test_calibrate(void) {
    for (int hyp = 0; hyp < 4; ++hyp) {
        float yaw = 37.0f + (float)hyp * 11.0f;
        float pitch = 22.0f - (float)hyp * 13.0f;
        RR_V3 f = rr_look_forward(hyp, yaw, pitch);
        float score = 0.0f;
        int found = rr_look_calibrate(yaw, pitch, &f, &score);
        CHECK(found >= 0, "calibration finds a hypothesis");
        CHECK(score > 0.999f, "calibration score matches the live rotation");
        float ry = 0.0f, rp = 0.0f;
        CHECK(rr_look_angles(found, &f, &ry, &rp), "calibrated hypothesis inverts");
        RR_V3 back = rr_look_forward(found, ry, rp);
        CHECK(dot(f, back) > 0.9999f, "calibrated hypothesis preserves the direction");
    }
    RR_V3 zero = { 0, 0, 0 };
    CHECK(rr_look_calibrate(10.0f, 5.0f, &zero, NULL) == -1, "zero forward is rejected");
    RR_V3 f = { 0, 1, 0 };
    CHECK(rr_look_calibrate(0.0f, 89.9f, &f, NULL) == -1, "near-pole pitch is ambiguous");
    float nan_angle = NAN;
    CHECK(rr_look_calibrate(nan_angle, 0.0f, &f, NULL) == -1, "NaN yaw is rejected");
}

static void test_degenerate(void) {
    RR_V3 zero = { 0, 0, 0 };
    float yaw = 1.0f, pitch = 1.0f;
    CHECK(!rr_look_angles(0, &zero, &yaw, &pitch), "zero direction is rejected");
    CHECK(!rr_look_angles(0, NULL, &yaw, &pitch), "null direction is rejected");
    float tiny = 1e-6f;
    RR_V3 v = { tiny, 0, 0 };
    CHECK(!rr_look_angles(0, &v, &yaw, &pitch), "sub-epsilon direction is rejected");
    /* Straight down: yaw is undefined but the call must stay finite. */
    RR_V3 down = { 0, -1, 0 };
    CHECK(rr_look_angles(0, &down, &yaw, &pitch), "straight down still returns angles");
    CHECK(isfinite(yaw) && near(pitch, 90.0f, 0.01f), "straight down maps to +90 pitch");
}

static void test_pin(void) {
    RR_V3 in = { 0, 0, 4 };
    RR_V3 u = { 0, 0, 0 };
    CHECK(rr_dir_unit(&in, &u), "normalise accepts a long vector");
    CHECK(unit_close(u, (RR_V3){0, 0, 1}, 1e-6f), "normalise returns a unit vector");
    RR_V3 zero = { 0, 0, 0 };
    CHECK(!rr_dir_unit(&zero, &u), "normalise rejects zero");
    RR_V3 tiny = { 1e-6f, 0, 0 };
    CHECK(!rr_dir_unit(&tiny, &u), "normalise rejects sub-epsilon");
    CHECK(!rr_dir_unit(NULL, &u), "normalise rejects null");

    float exact[3] = { 0, 0, 1 };
    CHECK(!rr_sa_needs_write(exact, &u), "an aligned copy is left alone");
    float off[3] = { 0.01745f, 0, 0.99985f };   /* 1 degree off */
    CHECK(rr_sa_needs_write(off, &u), "a drifted copy is rewritten");
    float backwards[3] = { 0, 0, -1 };
    CHECK(rr_sa_needs_write(backwards, &u), "a reversed copy is rewritten");
    float nan_copy[3] = { NAN, 0, 1 };
    CHECK(rr_sa_needs_write(nan_copy, &u), "an unreadable copy is rewritten");
    CHECK(rr_sa_needs_write(NULL, &u), "a missing copy is rewritten");
}

static void test_input_angles(void) {
    RR_V3 eye = { 0, 0, 0 };
    float yaw = 0.0f, pitch = 0.0f;

    RR_V3 ahead = { 0, 0, 10 };
    CHECK(rr_sa_input_angles(&eye, &ahead, &yaw, &pitch), "straight ahead is accepted");
    CHECK(near(yaw, 0.0f, 1e-6f) && near(pitch, 0.0f, 1e-6f), "ahead maps to yaw 0 pitch 0");

    RR_V3 right = { 10, 0, 0 };
    CHECK(rr_sa_input_angles(&eye, &right, &yaw, &pitch), "right is accepted");
    CHECK(near(yaw, 1.5707963f, 1e-5f), "right maps to +90 degrees of yaw");
    CHECK(near(pitch, 0.0f, 1e-6f), "right keeps pitch at 0");

    RR_V3 behind = { 0, 0, -10 };
    CHECK(rr_sa_input_angles(&eye, &behind, &yaw, &pitch), "behind is accepted");
    CHECK(fabsf(fabsf(yaw) - 3.1415927f) < 1e-5f, "behind maps to +-180 degrees of yaw");

    RR_V3 above = { 0, 100, 1 };
    CHECK(rr_sa_input_angles(&eye, &above, &yaw, &pitch), "above is accepted");
    CHECK(pitch < 0.0f, "a target above the eye gives a negative pitch");
    CHECK(near(pitch, -1.5533431f, 1e-5f), "steep pitch clamps to -89 degrees");

    RR_V3 below = { 0, -100, 1 };
    CHECK(rr_sa_input_angles(&eye, &below, &yaw, &pitch), "below is accepted");
    CHECK(near(pitch, 1.5533431f, 1e-5f), "steep pitch clamps to +89 degrees");

    RR_V3 same = { 0, 0, 0 };
    CHECK(!rr_sa_input_angles(&eye, &same, &yaw, &pitch), "a zero-length aim is rejected");
    CHECK(!rr_sa_input_angles(NULL, &ahead, &yaw, &pitch), "a missing eye is rejected");
}

static void test_quaternion(void) {
    const float identity[4] = { 0, 0, 0, 1 };
    CHECK(unit_close(rr_quat_forward(identity), (RR_V3){0, 0, 1}, 1e-5f),
          "identity quaternion looks down +Z");
    float yaw90[4] = { 0, 0.70710678f, 0, 0.70710678f };
    CHECK(unit_close(rr_quat_forward(yaw90), (RR_V3){1, 0, 0}, 1e-5f),
          "90 degree yaw quaternion looks down +X");
    float pitch90[4] = { 0.70710678f, 0, 0, 0.70710678f };
    CHECK(unit_close(rr_quat_forward(pitch90), (RR_V3){0, -1, 0}, 1e-5f),
          "positive X rotation looks down");
}

int main(void) {
    test_basis();
    test_roundtrip();
    test_calibrate();
    test_degenerate();
    test_pin();
    test_input_angles();
    test_quaternion();
    if (g_failures) {
        printf("SILENT AIM TEST FAILED (%d)\n", g_failures);
        return 1;
    }
    printf("SILENT AIM TEST OK\n");
    return 0;
}
