#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace WaterFftReference {

constexpr double Pi = 3.141592653589793238462643383279502884;
constexpr unsigned CanonicalResolution = 256;
constexpr unsigned Cascades = 3;
constexpr unsigned MapsPerCascade = 2;
constexpr unsigned Maps = Cascades * MapsPerCascade;
constexpr unsigned FieldCount = 8;
constexpr unsigned CanonicalMipLevels = 9;

struct Complex
{
    double real = 0.0;
    double imaginary = 0.0;
};

inline Complex Add(Complex a, Complex b)
{
    return {a.real + b.real, a.imaginary + b.imaginary};
}

inline Complex Multiply(Complex a, Complex b)
{
    return {a.real * b.real - a.imaginary * b.imaginary,
        a.real * b.imaginary + a.imaginary * b.real};
}

inline Complex Scale(Complex value, double scale)
{
    return {value.real * scale, value.imaginary * scale};
}

inline Complex Conjugate(Complex value)
{
    return {value.real, -value.imaginary};
}

inline Complex Unit(double angle)
{
    return {std::cos(angle), std::sin(angle)};
}

using ComplexGrid = std::vector<Complex>;
using RealGrid = std::vector<double>;
using PackedSpectra = std::array<ComplexGrid, FieldCount>;
using InitialTexel = std::array<double, 4>;
using InitialSpectrum = std::vector<InitialTexel>;

struct Image
{
    unsigned side = 0;
    std::vector<double> pixels;
};

using LayerPair = std::array<Image, MapsPerCascade>;
using Pyramid = std::vector<LayerPair>;
using ArrayPyramid = std::array<Pyramid, Cascades>;

inline bool IsPowerOfTwo(unsigned value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

inline void ValidateGrid(const ComplexGrid &grid, unsigned side)
{
    if (side == 0 || grid.size() != size_t(side) * side)
        throw std::invalid_argument("FFT reference grid extent does not match its side");
    for (const Complex value : grid)
        if (!std::isfinite(value.real) || !std::isfinite(value.imaginary))
            throw std::invalid_argument("FFT reference spectrum contains a non-finite value");
}

inline RealGrid DirectCenteredDftReal(const ComplexGrid &grid, unsigned side)
{
    ValidateGrid(grid, side);
    RealGrid result(size_t(side) * side);
    for (unsigned output_y = 0; output_y < side; ++output_y) {
        for (unsigned output_x = 0; output_x < side; ++output_x) {
            Complex sum;
            for (unsigned input_y = 0; input_y < side; ++input_y) {
                const int frequency_y = int(input_y) - int(side / 2);
                for (unsigned input_x = 0; input_x < side; ++input_x) {
                    const int frequency_x = int(input_x) - int(side / 2);
                    const double angle = 2.0 * Pi *
                        (double(frequency_x) * output_x + double(frequency_y) * output_y) / side;
                    sum = Add(sum, Multiply(grid[input_y * side + input_x], Unit(angle)));
                }
            }
            if (std::abs(sum.imaginary) > 1e-9 * (1.0 + std::abs(sum.real)))
                throw std::invalid_argument("FFT reference spectrum is not Hermitian");
            result[output_y * side + output_x] = sum.real;
        }
    }
    return result;
}

inline RealGrid SeparableCenteredDftReal(const ComplexGrid &grid, unsigned side)
{
    ValidateGrid(grid, side);
    ComplexGrid rows(size_t(side) * side);
    for (unsigned input_y = 0; input_y < side; ++input_y) {
        for (unsigned output_x = 0; output_x < side; ++output_x) {
            Complex sum;
            for (unsigned input_x = 0; input_x < side; ++input_x) {
                const int frequency_x = int(input_x) - int(side / 2);
                const double angle = 2.0 * Pi * double(frequency_x) * output_x / side;
                sum = Add(sum, Multiply(grid[input_y * side + input_x], Unit(angle)));
            }
            rows[input_y * side + output_x] = sum;
        }
    }
    RealGrid result(size_t(side) * side);
    for (unsigned output_y = 0; output_y < side; ++output_y) {
        for (unsigned output_x = 0; output_x < side; ++output_x) {
            Complex sum;
            for (unsigned input_y = 0; input_y < side; ++input_y) {
                const int frequency_y = int(input_y) - int(side / 2);
                const double angle = 2.0 * Pi * double(frequency_y) * output_y / side;
                sum = Add(sum, Multiply(rows[input_y * side + output_x], Unit(angle)));
            }
            if (std::abs(sum.imaginary) > 1e-9 * (1.0 + std::abs(sum.real)))
                throw std::invalid_argument("FFT reference spectrum is not Hermitian");
            result[output_y * side + output_x] = sum.real;
        }
    }
    return result;
}

inline RealGrid Scale(const RealGrid &grid, double scale)
{
    RealGrid result = grid;
    for (double &value : result)
        value *= scale;
    return result;
}

inline PackedSpectra MakePackedSpectra(unsigned side)
{
    if (side == 0)
        throw std::invalid_argument("FFT reference side must be nonzero");
    PackedSpectra result;
    for (ComplexGrid &field : result)
        field.resize(size_t(side) * side);
    return result;
}

inline LayerPair TransformPackedFields(const PackedSpectra &spectra, unsigned side)
{
    LayerPair result;
    for (Image &image : result) {
        image.side = side;
        image.pixels.resize(size_t(side) * side * 4);
    }
    for (unsigned field = 0; field < FieldCount; ++field) {
        const RealGrid transformed = SeparableCenteredDftReal(spectra[field], side);
        const unsigned map = field / 4;
        const unsigned channel = field % 4;
        for (size_t pixel = 0; pixel < transformed.size(); ++pixel)
            result[map].pixels[pixel * 4 + channel] = transformed[pixel];
    }
    return result;
}

inline PackedSpectra Evolve(const InitialSpectrum &initial, unsigned side, double period, double time)
{
    if (!std::isfinite(period) || period <= 0.0 || !std::isfinite(time) ||
        initial.size() != size_t(side) * side)
        throw std::invalid_argument("FFT reference evolution input is malformed");
    PackedSpectra fields = MakePackedSpectra(side);
    const Complex positive_i = {0.0, 1.0};
    const Complex negative_i = {0.0, -1.0};
    for (unsigned y = 0; y < side; ++y) {
        const double ky = 2.0 * Pi * (int(y) - int(side / 2)) / period;
        for (unsigned x = 0; x < side; ++x) {
            const InitialTexel &packed = initial[y * side + x];
            for (double value : packed)
                if (!std::isfinite(value))
                    throw std::invalid_argument("FFT reference initial spectrum contains a non-finite value");
            const double kx = 2.0 * Pi * (int(x) - int(side / 2)) / period;
            const double length = std::hypot(kx, ky);
            if (length <= 1e-12)
                continue;
            const Complex phase = Unit(std::sqrt(9.81 * length) * time);
            const Complex height = Add(Multiply({packed[0], packed[1]}, phase),
                Multiply({packed[2], packed[3]}, Conjugate(phase)));
            const size_t at = size_t(y) * side + x;
            fields[0][at] = x == 0 ? Complex{} : Scale(Multiply(negative_i, height), kx / length);
            fields[1][at] = height;
            fields[2][at] = y == 0 ? Complex{} : Scale(Multiply(negative_i, height), ky / length);
            fields[3][at] = x == 0 || y == 0 ? Complex{} : Scale(height, kx * ky / length);
            fields[4][at] = x == 0 ? Complex{} : Scale(Multiply(positive_i, height), kx);
            fields[5][at] = y == 0 ? Complex{} : Scale(Multiply(positive_i, height), ky);
            fields[6][at] = x == 0 ? Complex{} : Scale(height, kx * kx / length);
            fields[7][at] = y == 0 ? Complex{} : Scale(height, ky * ky / length);
        }
    }
    return fields;
}

inline std::array<double, FieldCount> EvaluatePoint(const InitialSpectrum &initial, unsigned side,
    double period, double time, unsigned output_x, unsigned output_y)
{
    if (output_x >= side || output_y >= side)
        throw std::out_of_range("FFT reference output point is out of range");
    const PackedSpectra fields = Evolve(initial, side, period, time);
    std::array<double, FieldCount> result = {};
    for (unsigned field = 0; field < FieldCount; ++field) {
        Complex sum;
        for (unsigned input_y = 0; input_y < side; ++input_y) {
            const int frequency_y = int(input_y) - int(side / 2);
            for (unsigned input_x = 0; input_x < side; ++input_x) {
                const int frequency_x = int(input_x) - int(side / 2);
                const double angle = 2.0 * Pi *
                    (double(frequency_x) * output_x + double(frequency_y) * output_y) / side;
                sum = Add(sum, Multiply(fields[field][input_y * side + input_x], Unit(angle)));
            }
        }
        if (std::abs(sum.imaginary) > 1e-9 * (1.0 + std::abs(sum.real)))
            throw std::invalid_argument("FFT reference point is not real");
        result[field] = sum.real;
    }
    return result;
}

inline Pyramid BuildMipPyramid(const LayerPair &base)
{
    if (!IsPowerOfTwo(base[0].side) || base[0].side != base[1].side ||
        base[0].pixels.size() != size_t(base[0].side) * base[0].side * 4 ||
        base[1].pixels.size() != size_t(base[1].side) * base[1].side * 4)
        throw std::invalid_argument("FFT reference base maps are malformed");
    Pyramid result;
    result.push_back(base);
    while (result.back()[0].side > 1) {
        const LayerPair &source = result.back();
        LayerPair destination;
        for (unsigned map = 0; map < MapsPerCascade; ++map) {
            destination[map].side = source[map].side / 2;
            destination[map].pixels.resize(size_t(destination[map].side) * destination[map].side * 4);
            for (unsigned y = 0; y < destination[map].side; ++y)
                for (unsigned x = 0; x < destination[map].side; ++x)
                    for (unsigned channel = 0; channel < 4; ++channel) {
                        double sum = 0.0;
                        for (unsigned offset_y = 0; offset_y < 2; ++offset_y)
                            for (unsigned offset_x = 0; offset_x < 2; ++offset_x)
                                sum += source[map].pixels[
                                    (((2 * y + offset_y) * source[map].side + 2 * x + offset_x) * 4) + channel];
                        destination[map].pixels[(y * destination[map].side + x) * 4 + channel] = sum * 0.25;
                    }
        }
        result.push_back(destination);
    }
    return result;
}

inline Pyramid TransformCascade(const InitialSpectrum &initial, unsigned side, double period, double time)
{
    return BuildMipPyramid(TransformPackedFields(Evolve(initial, side, period, time), side));
}

inline ArrayPyramid TransformAll(const std::array<InitialSpectrum, Cascades> &initial, unsigned side,
    const std::array<double, Cascades> &periods, double time)
{
    ArrayPyramid result;
    for (unsigned cascade = 0; cascade < Cascades; ++cascade)
        result[cascade] = TransformCascade(initial[cascade], side, periods[cascade], time);
    return result;
}

inline unsigned ArrayLayer(unsigned cascade, unsigned map)
{
    if (cascade >= Cascades || map >= MapsPerCascade)
        throw std::out_of_range("FFT reference array layer is out of range");
    return cascade * MapsPerCascade + map;
}

inline bool WithinFp32(double actual, double expected)
{
    return std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= 1e-4 + 1e-3 * std::abs(expected);
}

inline bool WithinFp16(double actual, double expected)
{
    return std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= 2e-3 + 4e-3 * std::abs(expected);
}

}
