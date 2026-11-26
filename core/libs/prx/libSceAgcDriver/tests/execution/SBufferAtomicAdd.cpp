#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

struct Case {
    std::uint32_t opcode;
    const char* name;
    std::uint32_t (*apply)(std::uint32_t, std::uint32_t);
};

struct Case64 {
    std::uint32_t opcode;
    const char* name;
    std::uint64_t (*apply)(std::uint64_t, std::uint64_t);
};

constexpr std::uint32_t Swap(std::uint32_t, std::uint32_t value) { return value; }
constexpr std::uint32_t Add(std::uint32_t old, std::uint32_t value) { return old + value; }
constexpr std::uint32_t Sub(std::uint32_t old, std::uint32_t value) { return old - value; }
constexpr std::uint32_t Smin(std::uint32_t old, std::uint32_t value) { return static_cast<std::int32_t>(old) < static_cast<std::int32_t>(value) ? old : value; }
constexpr std::uint32_t Umin(std::uint32_t old, std::uint32_t value) { return std::min(old, value); }
constexpr std::uint32_t Smax(std::uint32_t old, std::uint32_t value) { return static_cast<std::int32_t>(old) > static_cast<std::int32_t>(value) ? old : value; }
constexpr std::uint32_t Umax(std::uint32_t old, std::uint32_t value) { return std::max(old, value); }
constexpr std::uint32_t And(std::uint32_t old, std::uint32_t value) { return old & value; }
constexpr std::uint32_t Or(std::uint32_t old, std::uint32_t value) { return old | value; }
constexpr std::uint32_t Xor(std::uint32_t old, std::uint32_t value) { return old ^ value; }
constexpr std::uint32_t CompareSwap(std::uint32_t, std::uint32_t value) { return value; }
constexpr std::uint32_t Inc(std::uint32_t old, std::uint32_t limit) { return old >= limit ? 0u : old + 1u; }
constexpr std::uint32_t Dec(std::uint32_t old, std::uint32_t limit) { return old == 0u || old > limit ? limit : old - 1u; }
constexpr std::uint64_t Swap64(std::uint64_t, std::uint64_t value) { return value; }
constexpr std::uint64_t Add64(std::uint64_t old, std::uint64_t value) { return old + value; }
constexpr std::uint64_t Sub64(std::uint64_t old, std::uint64_t value) { return old - value; }
constexpr std::uint64_t Smin64(std::uint64_t old, std::uint64_t value) { return static_cast<std::int64_t>(old) < static_cast<std::int64_t>(value) ? old : value; }
constexpr std::uint64_t Umin64(std::uint64_t old, std::uint64_t value) { return std::min(old, value); }
constexpr std::uint64_t Smax64(std::uint64_t old, std::uint64_t value) { return static_cast<std::int64_t>(old) > static_cast<std::int64_t>(value) ? old : value; }
constexpr std::uint64_t Umax64(std::uint64_t old, std::uint64_t value) { return std::max(old, value); }
constexpr std::uint64_t And64(std::uint64_t old, std::uint64_t value) { return old & value; }
constexpr std::uint64_t Or64(std::uint64_t old, std::uint64_t value) { return old | value; }
constexpr std::uint64_t Xor64(std::uint64_t old, std::uint64_t value) { return old ^ value; }
constexpr std::uint64_t CompareSwap64(std::uint64_t, std::uint64_t value) { return value; }
constexpr std::uint64_t Inc64(std::uint64_t old, std::uint64_t limit) { return old >= limit ? 0ull : old + 1ull; }
constexpr std::uint64_t Dec64(std::uint64_t old, std::uint64_t limit) { return old == 0ull || old > limit ? limit : old - 1ull; }

constexpr std::array Cases{
    Case{0x40u, "swap", Swap}, Case{0x42u, "add", Add}, Case{0x43u, "sub", Sub},
    Case{0x44u, "smin", Smin}, Case{0x45u, "umin", Umin}, Case{0x46u, "smax", Smax},
    Case{0x47u, "umax", Umax}, Case{0x48u, "and", And}, Case{0x49u, "or", Or}, Case{0x4au, "xor", Xor},
    Case{0x41u, "cmpswap", CompareSwap}, Case{0x4bu, "inc", Inc}, Case{0x4cu, "dec", Dec},
};

constexpr std::array Cases64{
    Case64{0x60u, "swap_x2", Swap64}, Case64{0x62u, "add_x2", Add64}, Case64{0x63u, "sub_x2", Sub64},
    Case64{0x64u, "smin_x2", Smin64}, Case64{0x65u, "umin_x2", Umin64}, Case64{0x66u, "smax_x2", Smax64},
    Case64{0x67u, "umax_x2", Umax64}, Case64{0x68u, "and_x2", And64}, Case64{0x69u, "or_x2", Or64}, Case64{0x6au, "xor_x2", Xor64},
    Case64{0x61u, "cmpswap_x2", CompareSwap64}, Case64{0x6bu, "inc_x2", Inc64}, Case64{0x6cu, "dec_x2", Dec64},
};

alignas(256) std::array<std::uint32_t, 6> Code{};
alignas(256) std::array<std::uint32_t, 8> Output{};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

template <typename Device>
void Run(const Device& device, const Case& test, bool returnOld) {
    constexpr std::uint32_t oldValue = 0x80000010u;
    constexpr std::uint32_t operand = 0x7ffffff0u;
    Code = {0xf4000004u | (test.opcode << 18u) | ((returnOld ? 1u : 0u) << 16u) | (1u << 6u), 8u,
            0xf4000044u | (0x18u << 18u), 16u, 0xbf810000u, 0u};
    Output.fill(0xdeadbeefu);
    Output[2] = oldValue;
    std::vector<std::uint32_t> userData(12u, 0u);
    userData[1] = operand;
    if (test.opcode == 0x41u) userData[2] = oldValue;
    const auto descriptor = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(sizeof(Output)));
    std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 8);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(Code.data()), std::as_bytes(std::span(Code))}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(Code.data()), Code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, memory}, device->Target(), {0u, 0u, 0u, 128u}};
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device->Dispatch(result, 1u, 1u, 1u, {}, reinterpret_cast<std::uintptr_t>(Code.data()));
    device->WaitIdle();
    const std::uint32_t expectedMemory = test.apply(oldValue, operand);
    for (std::uint32_t index = 0u; index < Output.size(); ++index) {
        const std::uint32_t expected = index == 2u ? expectedMemory : index == 4u ? (returnOld ? oldValue : operand) : 0xdeadbeefu;
        Require(Output[index] == expected, "scalar buffer atomic " + std::string(test.name) + " GLC=" + std::to_string(returnOld) + ": word " + std::to_string(index) + " is " + std::to_string(Output[index]) + ", expected " + std::to_string(expected));
    }
}

template <typename Device>
void Run64(const Device& device, const Case64& test, bool returnOld) {
    constexpr std::uint64_t oldValue = 0x8000000100000010ull;
    constexpr std::uint64_t operand = 0x7fffffff000000f0ull;
    Code = {0xf4000084u | (test.opcode << 18u) | ((returnOld ? 1u : 0u) << 16u), 8u,
            0xf4000084u | (0x19u << 18u), 16u, 0xbf810000u, 0u};
    Output.fill(0xdeadbeefu);
    Output[2] = static_cast<std::uint32_t>(oldValue);
    Output[3] = static_cast<std::uint32_t>(oldValue >> 32u);
    std::vector<std::uint32_t> userData(12u, 0u);
    userData[2] = static_cast<std::uint32_t>(operand);
    userData[3] = static_cast<std::uint32_t>(operand >> 32u);
    if (test.opcode == 0x61u) {
        userData[4] = static_cast<std::uint32_t>(oldValue);
        userData[5] = static_cast<std::uint32_t>(oldValue >> 32u);
    }
    const auto descriptor = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(sizeof(Output)));
    std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 8);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(Code.data()), std::as_bytes(std::span(Code))}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(Code.data()), Code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, memory}, device->Target(), {0u, 0u, 0u, 128u}};
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device->Dispatch(result, 1u, 1u, 1u, {}, reinterpret_cast<std::uintptr_t>(Code.data()));
    device->WaitIdle();
    const std::uint64_t expectedMemory = test.apply(oldValue, operand);
    const std::uint64_t expectedReturn = returnOld ? oldValue : operand;
    const std::array<std::uint32_t, 8> expected{
        0xdeadbeefu, 0xdeadbeefu,
        static_cast<std::uint32_t>(expectedMemory), static_cast<std::uint32_t>(expectedMemory >> 32u),
        static_cast<std::uint32_t>(expectedReturn), static_cast<std::uint32_t>(expectedReturn >> 32u),
        0xdeadbeefu, 0xdeadbeefu};
    for (std::uint32_t index = 0u; index < Output.size(); ++index) {
        Require(Output[index] == expected[index], "scalar buffer atomic " + std::string(test.name) + " GLC=" + std::to_string(returnOld) + ": word " + std::to_string(index) + " is " + std::to_string(Output[index]) + ", expected " + std::to_string(expected[index]));
    }
}

template <typename Device>
void RunAddressAdd(const Device& device, std::uint32_t opcode, bool wide, bool returnOld) {
    constexpr std::uint64_t oldValue = 7u;
    constexpr std::uint64_t operand = 5u;
    const std::uint32_t storeOpcode = wide ? 0x19u : 0x18u;
    Code = {0xf4000000u | (opcode << 18u) | ((returnOld ? 1u : 0u) << 16u) | (1u << 6u) | 6u, 0u,
            0xf4000044u | (storeOpcode << 18u), 16u, 0xbf810000u, 0u};
    Output.fill(0xdeadbeefu);
    Output[2] = static_cast<std::uint32_t>(oldValue);
    Output[3] = static_cast<std::uint32_t>(oldValue >> 32u);
    std::vector<std::uint32_t> userData(16u, 0u);
    userData[1] = static_cast<std::uint32_t>(operand);
    userData[2] = static_cast<std::uint32_t>(operand >> 32u);
    const auto descriptor = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(sizeof(Output)));
    std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 8);
    const auto address = reinterpret_cast<std::uintptr_t>(&Output[2]);
    userData[12] = static_cast<std::uint32_t>(address);
    userData[13] = static_cast<std::uint32_t>(address >> 32u);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(Code.data()), std::as_bytes(std::span(Code))}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(Code.data()), Code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, memory}, device->Target(), {0u, 0u, 0u, 128u}};
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device->Dispatch(result, 1u, 1u, 1u, {}, reinterpret_cast<std::uintptr_t>(Code.data()));
    device->WaitIdle();
    const std::uint64_t expectedMemory = oldValue + operand;
    const std::uint64_t expectedReturn = returnOld ? oldValue : operand;
    Require(Output[2] == static_cast<std::uint32_t>(expectedMemory), "S_ATOMIC_ADD memory low word mismatch");
    if (wide) Require(Output[3] == static_cast<std::uint32_t>(expectedMemory >> 32u), "S_ATOMIC_ADD_X2 memory high word mismatch");
    Require(Output[4] == static_cast<std::uint32_t>(expectedReturn), "S_ATOMIC_ADD return low word mismatch");
    if (wide) Require(Output[5] == static_cast<std::uint32_t>(expectedReturn >> 32u), "S_ATOMIC_ADD_X2 return high word mismatch");
}

template <typename Device>
void RunAddressStore(const Device& device, std::uint32_t opcode, std::uint32_t dwords) {
    Code = {0xf4000000u | (opcode << 18u) | (1u << 6u) | 6u, 0u,
            0xf4000000u | (0x18u << 18u) | (5u << 6u) | 4u, 28u, 0xbf810000u, 0u};
    Output.fill(0xdeadbeefu);
    std::vector<std::uint32_t> userData(16u, 0u);
    constexpr std::array values{0x12345678u, 0x9abcdef0u, 0x0badc0deu, 0xfedcba98u};
    for (std::uint32_t index = 0u; index < dwords; ++index) userData[1u + index] = values[index];
    userData[5] = 0xfaceb00cu;
    const auto descriptor = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(sizeof(Output)));
    std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 8);
    const auto address = reinterpret_cast<std::uintptr_t>(&Output[2]);
    userData[12] = static_cast<std::uint32_t>(address);
    userData[13] = static_cast<std::uint32_t>(address >> 32u);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(Code.data()), std::as_bytes(std::span(Code))}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(Code.data()), Code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, memory}, device->Target(), {0u, 0u, 0u, 128u}};
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device->Dispatch(result, 1u, 1u, 1u, {}, reinterpret_cast<std::uintptr_t>(Code.data()));
    device->WaitIdle();
    for (std::uint32_t index = 0u; index < Output.size(); ++index) {
        const std::uint32_t expected = index >= 2u && index < 2u + dwords ? values[index - 2u] : index == 7u ? 0xfaceb00cu : 0xdeadbeefu;
        Require(Output[index] == expected, "S_STORE scalar address DWORD width check failed at word " + std::to_string(index));
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        for (const auto& test : Cases) {
            Run(device, test, false);
            Run(device, test, true);
        }
        for (const auto& test : Cases64) {
            Run64(device, test, false);
            Run64(device, test, true);
        }
        for (const bool returnOld : {false, true}) {
            RunAddressAdd(device, 0x82u, false, returnOld);
            RunAddressAdd(device, 0xa2u, true, returnOld);
        }
        RunAddressStore(device, 0x10u, 1u);
        RunAddressStore(device, 0x11u, 2u);
        RunAddressStore(device, 0x12u, 4u);
        std::puts("scalar buffer atomic tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
