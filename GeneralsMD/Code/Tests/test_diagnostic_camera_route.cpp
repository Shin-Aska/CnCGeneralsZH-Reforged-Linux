#include "test_harness.h"
#include "Platform/DiagnosticCameraRoute.h"
#include <string>

static bool readRoute(DiagnosticCameraRoute::Route &route, const std::string &text)
{
    FILE *file = std::tmpfile();
    CHECK(file != nullptr);
    if (!file) return false;
    std::fwrite(text.data(), 1, text.size(), file);
    std::rewind(file);
    const bool result = route.Read(file);
    std::fclose(file);
    return result;
}

static const char *header = "logic_frame,offset_x,offset_y,zoom_scale,angle_offset\n";

TEST(diagnostic_camera_route_absolute_and_fractional_logic_time)
{
    DiagnosticCameraRoute::Route route;
    CHECK(readRoute(route, std::string(header) + "109000,0,0,1,0\n109100,300,-100,0.8,0.4\n109200,0,0,1,0\n"));
    CHECK(route.FirstFrame() == 109000);
    const auto middle = route.Sample(109050);
    CHECK(middle.x == 150);
    CHECK(middle.y == -50);
    CHECK_NEAR(middle.zoom, 0.9, 1e-12);
    CHECK_NEAR(route.Sample(109050.5).x, 151.5, 1e-12);
    CHECK(route.Sample(108000).x == 0);
    const auto returned = route.Sample(110000);
    CHECK(returned.x == 0 && returned.y == 0 && returned.zoom == 1 && returned.angle == 0);
}

TEST(diagnostic_camera_route_rejects_invalid_routes_atomically)
{
    const char *invalid[] = {
        "0,0,0,1,0\n", // fewer than two knots
        "0,1,0,1,0\n1,0,0,1,0\n", // anchor must be the unmodified saved pose
        "0,0,0,1,0\n0,2,0,1,0\n", // duplicate times
        "0,0,0,1,0\n-1,0,0,1,0\n",
        "0,0,0,1,0\n1.5,0,0,1,0\n",
        "0,0,0,1,0\n2147483648,0,0,1,0\n",
        "0,0,0,1,0\n1,nan,0,1,0\n",
        "0,0,0,1,0\n1,0,inf,1,0\n",
        "0,0,0,1,0\n1,1000001,0,1,0\n",
        "0,0,0,1,0\n1,0,0,0,0\n",
        "0,0,0,1,0\n1,0,0,1,7\n",
        "0,0,0,1,0\n1,0,0,1,0,trailing\n",
        "0,0,0,1,0\n1,0,0,1,0\nmalformed\n"
    };
    for (const char *body : invalid) {
        DiagnosticCameraRoute::Route route;
        CHECK(!readRoute(route, std::string(header) + body));
        CHECK(route.Empty());
    }
    DiagnosticCameraRoute::Route route;
    CHECK(!readRoute(route, "wrong_header\n0,0,0,1,0\n1,0,0,1,0\n"));
    CHECK(!readRoute(route, std::string(header) + "0,0,0,1,0\n1,0,0,1,0" + std::string(600, ' ')));
}

static DiagnosticCameraRoute::Anchor exampleAnchor()
{
    return {{1500, 1700, -0.0f, std::nextafter(1.0f, 2.0f), 0.5f, 0.3f, 310, 10, 1,
        0, -400, 320, 0.87f, -0.5f, -0.3f, 0.5f, 0.3f}, 0};
}

static std::string anchorText(const DiagnosticCameraRoute::Anchor &anchor)
{
    FILE *file = std::tmpfile();
    CHECK(file != nullptr);
    if (!file) return {};
    CHECK(anchor.Write(file));
    std::rewind(file);
    std::string text;
    char line[256];
    while (std::fgets(line, sizeof(line), file)) text += line;
    std::fclose(file);
    return text;
}

static bool readAnchor(DiagnosticCameraRoute::Anchor &anchor, const std::string &text)
{
    FILE *file = std::tmpfile();
    CHECK(file != nullptr);
    if (!file) return false;
    std::fwrite(text.data(), 1, text.size(), file);
    std::rewind(file);
    const bool result = anchor.Read(file);
    std::fclose(file);
    return result;
}

TEST(diagnostic_camera_anchor_round_trips_exact_float_inputs)
{
    const auto source = exampleAnchor();
    CHECK(source.Valid());
    const std::string text = anchorText(source);
    DiagnosticCameraRoute::Anchor loaded;
    CHECK(readAnchor(loaded, text));
    CHECK(std::memcmp(source.values, loaded.values, sizeof(source.values)) == 0);
    CHECK(loaded.realZoom == source.realZoom);
    CHECK(anchorText(loaded) == text);
}

TEST(diagnostic_camera_anchor_rejects_invalid_or_partial_state_atomically)
{
    const auto source = exampleAnchor();
    const std::string text = anchorText(source);
    const char *from[] = {"STATE_V1", "zoom 1.00000012", "pitch 0.300000012", "fx_pitch 1", "offset_z 320",
        "fov 0.870000005", "plane_max_x 0.5", "real_zoom 0", "x 1500"};
    const char *to[] = {"STATE_V2", "zoom nan", "pitch 1.6", "fx_pitch 0", "offset_z -1",
        "fov inf", "plane_max_x -1", "real_zoom 4294967296", "y 1500"};
    for (size_t i = 0; i < sizeof(from)/sizeof(*from); ++i) {
        std::string bad = text;
        const size_t at = bad.find(from[i]);
        CHECK(at != std::string::npos);
        if (at == std::string::npos) continue;
        bad.replace(at, std::strlen(from[i]), to[i]);
        auto loaded = source;
        CHECK(!readAnchor(loaded, bad));
        CHECK(std::memcmp(source.values, loaded.values, sizeof(source.values)) == 0);
    }
    DiagnosticCameraRoute::Anchor loaded;
    CHECK(!readAnchor(loaded, text + "trailing"));
    CHECK(!readAnchor(loaded, text.substr(0, text.find("offset_z"))));
}
