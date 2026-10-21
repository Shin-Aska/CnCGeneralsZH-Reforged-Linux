#include "WaterFftReference.h"
#include "Platform/WaterFftSpectrum.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool near(double actual, double expected, double absolute = 1e-11, double relative = 1e-11)
{
    return std::abs(actual - expected) <= absolute + relative * std::abs(expected);
}

void check_analytic_dft()
{
    using namespace WaterFftReference;
    const unsigned n = 4;
    ComplexGrid zero(n * n);
    const RealGrid zero_result = DirectCenteredDftReal(zero, n);
    for (double value : zero_result)
        check(value == 0.0, "zero spectrum must transform to exact zero");

    ComplexGrid dc(n * n);
    dc[(n / 2) * n + n / 2] = {2.5, 0.0};
    const RealGrid dc_result = DirectCenteredDftReal(dc, n);
    for (double value : dc_result)
        check(near(value, 2.5), "centered DC must be spatially constant without normalization");

    ComplexGrid cosine(n * n);
    cosine[(n / 2) * n + n / 2 + 1] = {0.5, 0.0};
    cosine[(n / 2) * n + n / 2 - 1] = {0.5, 0.0};
    const RealGrid cosine_result = DirectCenteredDftReal(cosine, n);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x)
            check(near(cosine_result[y * n + x], std::cos(2.0 * Pi * x / n)),
                "single-frequency sign/axis fixture must be cos(+2*pi*x/N)");

    const RealGrid wrongly_normalized = Scale(cosine_result, 1.0 / double(n * n));
    check(!WithinFp32(wrongly_normalized[0], cosine_result[0]),
        "negative control must reject stale 1/N^2 normalization");

    ComplexGrid sine(n * n);
    sine[(n / 2) * n + n / 2 + 1] = {0.0, -0.5};
    sine[(n / 2) * n + n / 2 - 1] = {0.0, 0.5};
    const double signed_expected[4] = {0.0, 1.0, 0.0, -1.0};
    const RealGrid direct_sine = DirectCenteredDftReal(sine, n);
    PackedSpectra packed_sine = MakePackedSpectra(n);
    packed_sine[1] = sine;
    const LayerPair transformed_sine = TransformPackedFields(packed_sine, n);
    bool reversed_sign_matches = true;
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            const double expected = signed_expected[x];
            check(near(direct_sine[y * n + x], expected),
                "direct DFT must match the hand-derived signed sine fixture");
            check(near(transformed_sine[0].pixels[(y * n + x) * 4 + 1], expected),
                "separable packed DFT must match the hand-derived signed sine fixture");
            reversed_sign_matches = reversed_sign_matches && near(-direct_sine[y * n + x], expected);
        }
    check(!reversed_sign_matches, "reversed transform sign mutation must fail signed sine expectations");
}

void check_six_fields_and_mips()
{
    using namespace WaterFftReference;
    const unsigned n = 4;
    PackedSpectra spectra = MakePackedSpectra(n);
    for (unsigned field = 0; field < FieldCount; ++field) {
        const double amplitude = double(field + 1);
        spectra[field][(n / 2) * n + n / 2 + 1] = {0.5 * amplitude, 0.0};
        spectra[field][(n / 2) * n + n / 2 - 1] = {0.5 * amplitude, 0.0};
    }
    const LayerPair base = TransformPackedFields(spectra, n);
    for (unsigned field = 0; field < FieldCount; ++field) {
        const RealGrid direct = DirectCenteredDftReal(spectra[field], n);
        const unsigned map = field / 4;
        const unsigned channel = field % 4;
        for (unsigned pixel = 0; pixel < n * n; ++pixel)
            check(near(base[map].pixels[pixel * 4 + channel], direct[pixel]),
                "separable oracle must agree with independent direct tiny DFT");
    }
    for (unsigned field = 0; field < FieldCount; ++field) {
        const unsigned map = field < 4 ? 0 : 1;
        const unsigned channel = field % 4;
        check(near(base[map].pixels[channel], double(field + 1)),
            "asymmetric field fixture must preserve map/channel order");
    }
    LayerPair permuted = base;
    for (unsigned pixel = 0; pixel < n * n; ++pixel)
        std::swap(permuted[0].pixels[pixel * 4], permuted[0].pixels[pixel * 4 + 1]);
    bool permutation_matches_contract = true;
    for (unsigned field = 0; field < FieldCount; ++field) {
        const unsigned map = field / 4;
        const unsigned channel = field % 4;
        permutation_matches_contract = permutation_matches_contract &&
            near(permuted[map].pixels[channel], double(field + 1));
    }
    check(!permutation_matches_contract,
        "deliberately permuted channels must fail the known field-to-RGBA contract");
    const Pyramid mips = BuildMipPyramid(base);
    check(mips.size() == 3 && mips[0][0].side == 4 && mips[1][0].side == 2 && mips[2][0].side == 1,
        "tiny pyramid must contain log2(N)+1 levels");
    for (unsigned map = 0; map < MapsPerCascade; ++map)
        for (unsigned channel = 0; channel < 4; ++channel)
            check(near(mips.back()[map].pixels[channel], 0.0, 1e-10, 0.0),
                "cosine fixture final mip must be the four-row/four-column mean");
}

void check_production_seed_goldens()
{
    using namespace WaterFftReference;
    const int modes[Cascades][2] = {{17, 11}, {19, 11}, {9, 7}};
    const double coefficients[Cascades][2] = {
        {-5.401565067586489e-05, 0.0026124909054487944},
        {-0.00042250446858815849, -0.00016978083294816315},
        {-0.00010452910646563396, -5.9811340179294348e-05}
    };
    const double periods[Cascades] = {450.0, 103.0, 13.0};
    const double expected[Cascades][FieldCount] = {
        {0.00065885191333790402, 0.0051668437714458837, 0.00042631594392452646, 0.00066625788655881753,
            -0.00018627179276297821, -0.00012052880708192707, 0.0010296712792272631, 0.00043110804424394063},
        {-0.00049049232057022567, 0.00071282605455872641, -0.00028396923822486748, 0.0004139510379536943,
            0.0006568986585193565, 0.00038030974966910106, 0.00071500633828365404, 0.00023965586407845464},
        {0.00018963913502438869, 1.7219887584225382e-05, 0.00014749710501896896, 4.5987065900698383e-05,
            -0.0010450478063477652, -0.00081281496049270612, 5.9126227586612265e-05, 3.5767717922765424e-05}
    };
    const std::vector<float> production = WaterFftSpectrum::InitialSpectrum();
    check(production.size() == size_t(Cascades) * CanonicalResolution * CanonicalResolution * 4,
        "production spectrum extent must match the frozen fixture contract");
    for (unsigned cascade = 0; cascade < Cascades; ++cascade) {
        InitialSpectrum initial(size_t(CanonicalResolution) * CanonicalResolution);
        const unsigned x = unsigned(int(CanonicalResolution / 2) + modes[cascade][0]);
        const unsigned y = unsigned(int(CanonicalResolution / 2) + modes[cascade][1]);
        const unsigned opposite_x = (CanonicalResolution - x) % CanonicalResolution;
        const unsigned opposite_y = (CanonicalResolution - y) % CanonicalResolution;
        const float *production_positive =
            &production[((cascade * CanonicalResolution + y) * CanonicalResolution + x) * 4];
        const float *production_opposite =
            &production[((cascade * CanonicalResolution + opposite_y) * CanonicalResolution + opposite_x) * 4];
        check(double(production_positive[0]) == coefficients[cascade][0] &&
                double(production_positive[1]) == coefficients[cascade][1] &&
                production_positive[2] == 0.0f && production_positive[3] == 0.0f,
            "current production selected coefficient must match the frozen input fixture");
        check(production_opposite[0] == 0.0f && production_opposite[1] == 0.0f &&
                double(production_opposite[2]) == coefficients[cascade][0] &&
                double(production_opposite[3]) == -coefficients[cascade][1],
            "current production opposite coefficient must match the frozen conjugate fixture");
        initial[y * CanonicalResolution + x] = {coefficients[cascade][0], coefficients[cascade][1], 0.0, 0.0};
        initial[opposite_y * CanonicalResolution + opposite_x] =
            {0.0, 0.0, coefficients[cascade][0], -coefficients[cascade][1]};
        const auto actual = EvaluatePoint(initial, CanonicalResolution, periods[cascade], 1.375, 17, 53);
        for (unsigned field = 0; field < FieldCount; ++field)
            check(near(actual[field], expected[cascade][field], 1e-13, 1e-12),
                "frozen production-seed point must preserve phase/axis/field convention");
        check(!near(actual[0], expected[cascade][2], 1e-8, 1e-8),
            "axis-swap negative control must be armed for every cascade");
    }
}

void check_canonical_array_mips()
{
    using namespace WaterFftReference;
    for (unsigned cascade = 0; cascade < Cascades; ++cascade) {
        LayerPair base;
        for (unsigned map = 0; map < MapsPerCascade; ++map) {
            base[map].side = CanonicalResolution;
            base[map].pixels.resize(size_t(CanonicalResolution) * CanonicalResolution * 4);
            for (unsigned y = 0; y < CanonicalResolution; ++y)
                for (unsigned x = 0; x < CanonicalResolution; ++x)
                for (unsigned channel = 0; channel < 4; ++channel)
                    base[map].pixels[(y * CanonicalResolution + x) * 4 + channel] =
                        100.0 * cascade + 10.0 * map + channel + 0.01 * x + 0.001 * y;
        }
        const Pyramid mips = BuildMipPyramid(base);
        check(mips.size() == CanonicalMipLevels, "canonical map must contain nine mip levels");
        for (unsigned mip = 0; mip < CanonicalMipLevels; ++mip)
            for (unsigned map = 0; map < MapsPerCascade; ++map) {
                check(ArrayLayer(cascade, map) == cascade * 2 + map,
                    "six-layer array order must remain cascade-major/map-minor");
                check(mips[mip][map].side == (CanonicalResolution >> mip),
                    "canonical mip extent must halve through 1x1");
                const double block_mean = 0.011 * ((1u << mip) - 1) * 0.5;
                for (unsigned channel = 0; channel < 4; ++channel) {
                    const double expected = 100.0 * cascade + 10.0 * map + channel + block_mean;
                    check(near(mips[mip][map].pixels[channel], expected, 1e-11, 1e-12),
                        "all nine mips and six array layers must preserve independent box averages");
                    if (mip != 0)
                        check(!near(mips[mip][map].pixels[channel], 100.0 * cascade + 10.0 * map + channel,
                                  1e-8, 1e-8),
                            "wrong base-texel mip negative control must be armed");
                }
            }
    }
}

void check_layout_and_rejection()
{
    using namespace WaterFftReference;
    check(CanonicalResolution == 256 && Cascades == 3 && Maps == 6 && CanonicalMipLevels == 9,
        "canonical contract must be 256, three spectra, six maps, nine mips");
    check(ArrayLayer(0, 0) == 0 && ArrayLayer(1, 0) == 2 && ArrayLayer(2, 1) == 5,
        "array layer contract must be cascade*2+map");
    check(!WithinFp32(0.0, 0.0051668437714458837),
        "all-zero false-pass control must fail the fixed production-seed height");
    check(WithinFp32(1.00109, 1.0) && !WithinFp32(1.00111, 1.0),
        "FP32 gate must remain abs 1e-4 plus rel 1e-3");
    check(WithinFp16(1.0059, 1.0) && !WithinFp16(1.0061, 1.0),
        "FP16 gate must remain abs 2e-3 plus rel 4e-3");
    bool bad_size = false;
    try {
        (void)DirectCenteredDftReal(ComplexGrid(9), 4);
    } catch (const std::invalid_argument &) {
        bad_size = true;
    }
    check(bad_size, "malformed grid size must throw invalid_argument");
    ComplexGrid nan_grid(4);
    nan_grid[0].real = std::numeric_limits<double>::quiet_NaN();
    bool bad_nan = false;
    try {
        (void)DirectCenteredDftReal(nan_grid, 2);
    } catch (const std::invalid_argument &) {
        bad_nan = true;
    }
    check(bad_nan, "non-finite spectrum must throw invalid_argument");
}
}

int main()
{
    check_analytic_dft();
    check_six_fields_and_mips();
    check_production_seed_goldens();
    check_canonical_array_mips();
    check_layout_and_rejection();
    if (failures) {
        std::fprintf(stderr, "water_fft_reference: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("water_fft_reference: PASS zero/DC/cosine/signed-sine, live production-seed binding and independent goldens, direct/separable sign parity, 8 fields, 6 layers x 9 mips, malformed/NaN rejection, FP32/FP16 bounds, zero/normalization/sign/axis/channel-permutation/mip negative controls\n");
    return 0;
}
