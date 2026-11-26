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

constexpr std::array Cases{
    Case{0x40u, "swap", Swap}, Case{0x42u, "add", Add}, Case{0x43u, "sub", Sub},
    Case{0x44u, "smin", Smin}, Case{0x45u, "umin", Umin}, Case{0x46u, "smax", Smax},
    Case{0x47u, "umax", Umax}, Case{0x48u, "and", And}, Case{0x49u, "or", Or}, Case{0x4au, "xor", Xor},
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

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        for (const auto& test : Cases) {
            Run(device, test, false);
            Run(device, test, true);
        }
        std::puts("scalar buffer atomic tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
