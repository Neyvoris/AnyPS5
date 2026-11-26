#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Inputs = 4;
constexpr std::uint32_t Results = 16;
constexpr std::uint32_t ExtendedResults = 32;
alignas(256) std::array<std::uint32_t, Threads * Inputs> Input{};
alignas(256) std::array<std::uint32_t, Threads * ExtendedResults> Output{};

alignas(256) constexpr std::array<std::uint32_t, 171> Code{
    0x34020084, 0xe0381000, 0x80000201, 0x340c0082, 0xbf8c3f70, 0x4a0e0cff, 0x00001000, 0x4a100c80,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda007c00, 0x00000007, 0xd8d80000, 0x0a000007,
    0x4a0e0cff, 0x00001080, 0x4a100cff, 0x00002080, 0xd8340000, 0x00000207, 0xd8340000, 0x00000308,
    0xda040400, 0x00000007, 0xd8d80000, 0x0b000007, 0x4a0e0cff, 0x00001100, 0x4a100cff, 0x00000100,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda087c00, 0x00000007, 0xd8d80000, 0x0c000007,
    0x4a0e0cff, 0x00001180, 0x4a100cff, 0x00002180, 0xd8340000, 0x00000207, 0xd8340000, 0x00000308,
    0xda0c0400, 0x00000007, 0xd8d80000, 0x0d000007, 0x4a0e0cff, 0x00001200, 0x4a100cff, 0x00000200,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda107c00, 0x00000007, 0xd8d80000, 0x0e000007,
    0x4a0e0cff, 0x00001280, 0x4a100cff, 0x00002280, 0xd8340000, 0x00000207, 0xd8340000, 0x00000308,
    0xda140400, 0x00000007, 0xd8d80000, 0x0f000007, 0x4a0e0cff, 0x00001300, 0x4a100cff, 0x00000300,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda187c00, 0x00000007, 0xd8d80000, 0x10000007,
    0x4a0e0cff, 0x00001380, 0x4a100cff, 0x00002380, 0xd8340000, 0x00000207, 0xd8340000, 0x00000308,
    0xda1c0400, 0x00000007, 0xd8d80000, 0x11000007, 0x4a0e0cff, 0x00001400, 0x4a100cff, 0x00000400,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda207c00, 0x00000007, 0xd8d80000, 0x12000007,
    0x4a0e0cff, 0x00001480, 0x4a100cff, 0x00002480, 0xd8340000, 0x00000207, 0xd8340000, 0x00000308,
    0xda240400, 0x00000007, 0xd8d80000, 0x13000007, 0x4a0e0cff, 0x00001500, 0x4a100cff, 0x00000500,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda287c00, 0x00000007, 0xd8d80000, 0x14000007,
    0x4a0e0cff, 0x00001580, 0x4a100cff, 0x00002580, 0xd8340000, 0x00000207, 0xd8340000, 0x00000308,
    0xda2c0400, 0x00000007, 0xd8d80000, 0x15000007, 0x4a0e0cff, 0x00001600, 0x4a100cff, 0x00000600,
    0xd8340000, 0x00000207, 0xd8340000, 0x00000308, 0xda347c00, 0x00000007, 0xd8d80000, 0x16000007,
    0x34020086, 0xbf8cc07f, 0xe0781000, 0x80010a01, 0xe0781010, 0x80010e01, 0xe0781020, 0x80011201,
    0xe0781030, 0x80011601, 0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 3> AddressOffsetCode{0xda008000, 0x00000000, 0xbf810000};

alignas(256) constexpr std::array<std::uint32_t, 85> FloatCode{
    0x34020086, 0x340c0082, 0x7e040281, 0x7e060282, 0x4a0e0cff, 0x00000100, 0x4a100cff, 0x00000200,
    0x4a120cff, 0x00000300, 0xd8340000, 0x00000308, 0xd8340000, 0x00000207, 0xbf8cc07f, 0xda480040,
    0x00000007, 0xbf8cc07f, 0xd8d80000, 0x0a000007, 0xbf8cc07f, 0xd8340000, 0x00000209, 0xbf8cc07f,
    0xd8480000, 0x00000309, 0xbf8cc07f, 0xd8d80000, 0x0d000009, 0x4a0e0cff, 0x00000400, 0x4a100cff,
    0x00000500, 0x4a120cff, 0x00000600, 0xd8340000, 0x00000308, 0xd8340000, 0x00000207, 0xbf8cc07f,
    0xda4c0040, 0x00000007, 0xbf8cc07f, 0xd8d80000, 0x0b000007, 0xbf8cc07f, 0xd8340000, 0x00000209,
    0xbf8cc07f, 0xd84c0000, 0x00000309, 0xbf8cc07f, 0xd8d80000, 0x0e000009, 0x4a0e0cff, 0x00000700,
    0x4a100cff, 0x00000800, 0x4a120cff, 0x00000900, 0xd8340000, 0x00000308, 0xd8340000, 0x00000207,
    0xbf8cc07f, 0xda540040, 0x00000007, 0xbf8cc07f, 0xd8d80000, 0x0c000007, 0xbf8cc07f, 0xd8340000,
    0x00000209, 0xbf8cc07f, 0xd8540000, 0x00000309, 0xbf8cc07f, 0xd8d80000, 0x0f000009, 0xbf8cc07f,
    0xe0781000, 0x80010a01, 0xe0781010, 0x80010e01, 0xbf810000,
};

void Fill(std::uint32_t tid, std::uint32_t* words) {
    const std::uint64_t seed = (tid + 1u) * 0x9e3779b97f4a7c15ull;
    const auto mixed = seed * 0xbf58476d1ce4e5b9ull;
    words[0] = tid % 4u == 0u ? tid % 7u : static_cast<std::uint32_t>(seed);
    words[1] = tid % 4u == 0u ? (tid * 3u) % 7u : tid % 4u == 1u ? words[0] : static_cast<std::uint32_t>(mixed >> 32u);
    words[2] = std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::int32_t>(tid % 9u) - 4) * 1.75f + 0.5f);
    words[3] = std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::int32_t>((tid * 5u) % 11u) - 5) * 0.625f + 0.25f);
}

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

ShaderRecompiler::RecompileResult Compile(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, const std::optional<ShaderRecompiler::ShaderFloatMode>& floatMode = std::nullopt) {
    std::vector<std::uint32_t> userData(8, 0u);
    const auto input = BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size() * 4u));
    const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size() * 4u));
    std::copy(input.begin(), input.end(), userData.begin());
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 10240u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    request.context.floatMode = floatMode;
    return ShaderRecompiler::Recompile(request);
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) Fill(tid, &Input[tid * Inputs]);
    Output.fill(0xdeadbeefu);
    const auto result = Compile(device, Code);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(Code.data()));
    device.WaitIdle();
}

void CheckLanes() {
    constexpr std::array<const char*, 13> names{
        "ds_add_src2_u32", "ds_sub_src2_u32", "ds_rsub_src2_u32", "ds_inc_src2_u32", "ds_dec_src2_u32", "ds_min_src2_i32", "ds_max_src2_i32", "ds_min_src2_u32",
        "ds_max_src2_u32", "ds_and_src2_b32", "ds_or_src2_b32", "ds_xor_src2_b32", "ds_write_src2_b32",
    };
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const std::uint32_t* in = &Input[tid * Inputs];
        const std::uint32_t a = in[0];
        const std::uint32_t b = in[1];
        const auto sa = static_cast<std::int32_t>(a);
        const auto sb = static_cast<std::int32_t>(b);
        const std::array<std::uint32_t, 13> expected{
            a + b, a - b, b - a, a >= b ? 0u : a + 1u, (a == 0u || a > b) ? b : a - 1u,
            static_cast<std::uint32_t>(std::min(sa, sb)), static_cast<std::uint32_t>(std::max(sa, sb)), std::min(a, b), std::max(a, b),
            a & b, a | b, a ^ b, b,
        };
        for (std::uint32_t op = 0; op < expected.size(); ++op) {
            const std::uint32_t actual = Output[tid * Results + op];
            Require(actual == expected[op], "lds src2: lane " + std::to_string(tid) + " " + names[op] + " is " + Hex(actual) + ", expected " + Hex(expected[op]));
        }
    }
}

void CheckFloats(AgcDriver::VulkanDevice& device) {
    constexpr std::array<const char*, 3> names{"min", "max", "add"};
    constexpr std::array<std::uint32_t, 2> modes{0x00u, 0x30u};
    std::array<std::array<std::array<std::uint32_t, Results>, Threads>, 2> results{};
    for (std::uint32_t mode = 0; mode < modes.size(); ++mode) {
        Output.fill(0xdeadbeefu);
        const auto result = Compile(device, FloatCode, ShaderRecompiler::ShaderFloatMode{modes[mode], false, true, false});
        device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(FloatCode.data()));
        device.WaitIdle();
        for (std::uint32_t tid = 0; tid < Threads; ++tid) std::copy_n(&Output[tid * Results], Results, results[mode][tid].begin());
    }
    for (std::uint32_t mode = 0; mode < modes.size(); ++mode) {
        for (std::uint32_t op = 0; op < names.size(); ++op) {
            for (std::uint32_t tid = 0; tid < Threads; ++tid) {
                Require(results[mode][tid][op] == results[mode][tid][op + names.size()],
                    std::string("lds src2: ") + names[op] + " differs from ds_" + names[op] + "_f32 in mode " + Hex(modes[mode]));
            }
        }
    }
    const bool keepsDenormals = device.Target().float32DenormPreserve;
    for (std::uint32_t op = 0; op < names.size(); ++op) {
        if (op == 2u && !keepsDenormals) continue;
        for (std::uint32_t tid = 0; tid < Threads; ++tid) {
            Require(results[0][tid][op] != results[1][tid][op], std::string("lds src2: ") + names[op] + " ignores the f32 denormal mode");
        }
    }
}

void CheckAddressOffsetRejected(AgcDriver::VulkanDevice& device) {
    try {
        (void)Compile(device, AddressOffsetCode);
    } catch (const std::exception&) {
        return;
    }
    Require(false, "lds src2: an offset taken from the address was accepted");
}

struct U64Case {
    std::uint32_t encoding;
    const char* name;
    std::uint64_t (*expected)(std::uint64_t, std::uint64_t);
    bool floating;
};

std::uint64_t Add64(std::uint64_t a, std::uint64_t b) { return a + b; }
std::uint64_t Sub64(std::uint64_t a, std::uint64_t b) { return a - b; }
std::uint64_t Rsub64(std::uint64_t a, std::uint64_t b) { return b - a; }
std::uint64_t Inc64(std::uint64_t a, std::uint64_t b) { return a >= b ? 0u : a + 1u; }
std::uint64_t Dec64(std::uint64_t a, std::uint64_t b) { return a == 0u || a > b ? b : a - 1u; }
std::uint64_t MinI64(std::uint64_t a, std::uint64_t b) { return std::bit_cast<std::uint64_t>(std::min(std::bit_cast<std::int64_t>(a), std::bit_cast<std::int64_t>(b))); }
std::uint64_t MaxI64(std::uint64_t a, std::uint64_t b) { return std::bit_cast<std::uint64_t>(std::max(std::bit_cast<std::int64_t>(a), std::bit_cast<std::int64_t>(b))); }
std::uint64_t MinU64(std::uint64_t a, std::uint64_t b) { return std::min(a, b); }
std::uint64_t MaxU64(std::uint64_t a, std::uint64_t b) { return std::max(a, b); }
std::uint64_t And64(std::uint64_t a, std::uint64_t b) { return a & b; }
std::uint64_t Or64(std::uint64_t a, std::uint64_t b) { return a | b; }
std::uint64_t Xor64(std::uint64_t a, std::uint64_t b) { return a ^ b; }
std::uint64_t Write64(std::uint64_t, std::uint64_t b) { return b; }
std::uint64_t MinF64(std::uint64_t a, std::uint64_t b) { return std::bit_cast<std::uint64_t>(std::min(std::bit_cast<double>(a), std::bit_cast<double>(b))); }
std::uint64_t MaxF64(std::uint64_t a, std::uint64_t b) { return std::bit_cast<std::uint64_t>(std::max(std::bit_cast<double>(a), std::bit_cast<double>(b))); }

constexpr std::array<U64Case, 15> U64Cases{{
    {0xc0u, "ds_add_src2_u64", Add64, false},
    {0xc1u, "ds_sub_src2_u64", Sub64, false},
    {0xc2u, "ds_rsub_src2_u64", Rsub64, false},
    {0xc3u, "ds_inc_src2_u64", Inc64, false},
    {0xc4u, "ds_dec_src2_u64", Dec64, false},
    {0xc5u, "ds_min_src2_i64", MinI64, false},
    {0xc6u, "ds_max_src2_i64", MaxI64, false},
    {0xc7u, "ds_min_src2_u64", MinU64, false},
    {0xc8u, "ds_max_src2_u64", MaxU64, false},
    {0xc9u, "ds_and_src2_b64", And64, false},
    {0xcau, "ds_or_src2_b64", Or64, false},
    {0xcbu, "ds_xor_src2_b64", Xor64, false},
    {0xcdu, "ds_write_src2_b64", Write64, false},
    {0xd2u, "ds_min_src2_f64", MinF64, true},
    {0xd3u, "ds_max_src2_f64", MaxF64, true},
}};

std::pair<std::uint64_t, std::uint64_t> U64Inputs(std::uint32_t tid, bool floating) {
    if (floating) {
        const double lhs = static_cast<double>(static_cast<std::int32_t>(tid) - 11) * 1.25 + 0.5;
        const double rhs = static_cast<double>(13u - tid) * 0.375 - 2.0;
        return {std::bit_cast<std::uint64_t>(lhs), std::bit_cast<std::uint64_t>(rhs)};
    }
    switch (tid % 4u) {
    case 0u: return {0xffff'ffff'0000'0010ull + tid, 0x0000'0001'0000'0021ull + tid};
    case 1u: return {tid % 9u, (tid * 3u) % 11u};
    case 2u: return {std::bit_cast<std::uint64_t>(-17ll - tid), std::bit_cast<std::uint64_t>(23ll + tid)};
    default: return {0x8000'0000'0000'0010ull + tid, 0x7fff'ffff'ffff'fff0ull - tid};
    }
}

std::vector<std::uint32_t> U64Shader(const U64Case& test, std::uint32_t group) {
    std::vector<std::uint32_t> code(Code.begin(), Code.end());
    code[3] = 0x340c0083u;
    code[160] = 0x34020087u;
    code[162] = 0xe0741000u;
    code[163] = 0x80011801u;
    const std::uint32_t write0 = 8u + group * 12u;
    const std::uint32_t write1 = write0 + 2u;
    const std::uint32_t operation = write0 + 4u;
    const std::uint32_t read = write0 + 6u;
    code[write0] = 0xd9340000u;
    code[write0 + 1u] = 0x00000207u;
    code[write1] = 0xd9340000u;
    code[write1 + 1u] = 0x00000408u;
    code[operation] = (code[operation] & ~(0xffu << 18u)) | (test.encoding << 18u);
    code[read] = (code[read] & ~(0xffu << 18u)) | (0x76u << 18u);
    code[read + 1u] = 0x18000007u;
    return code;
}

void CheckU64Cases(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t operation = 0; operation < U64Cases.size(); ++operation) {
        const auto& test = U64Cases[operation];
        for (std::uint32_t tid = 0; tid < Threads; ++tid) {
            const auto [a, b] = U64Inputs(tid, test.floating);
            Input[tid * Inputs + 0u] = static_cast<std::uint32_t>(a);
            Input[tid * Inputs + 1u] = static_cast<std::uint32_t>(a >> 32u);
            Input[tid * Inputs + 2u] = static_cast<std::uint32_t>(b);
            Input[tid * Inputs + 3u] = static_cast<std::uint32_t>(b >> 32u);
        }
        Output.fill(0xdeadbeefu);
        const auto code = U64Shader(test, operation % 13u);
        const auto result = Compile(device, code);
        device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
        device.WaitIdle();
        for (std::uint32_t tid = 0; tid < Threads; ++tid) {
            const auto [a, b] = U64Inputs(tid, test.floating);
            const std::uint32_t low = Output[tid * ExtendedResults + 0u];
            const std::uint32_t high = Output[tid * ExtendedResults + 1u];
            const std::uint64_t actual = low | (static_cast<std::uint64_t>(high) << 32u);
            const std::uint64_t expected = test.expected(a, b);
            Require(actual == expected, std::string("lds src2 u64: lane ") + std::to_string(tid) + " " + test.name + " is " + std::to_string(actual) + ", expected " + std::to_string(expected));
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Run(*device);
        CheckLanes();
        CheckAddressOffsetRejected(*device);
        CheckFloats(*device);
        CheckU64Cases(*device);
        std::puts("lds src2 tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
