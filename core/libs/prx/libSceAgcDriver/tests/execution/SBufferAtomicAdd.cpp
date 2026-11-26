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

alignas(256) std::array<std::uint32_t, 6> Code{};
alignas(256) std::array<std::uint32_t, 8> Output{};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

template <typename Device>
void Run(const Device& device, bool returnOld) {
    Code = {0xf4000004u | (0x42u << 18u) | ((returnOld ? 1u : 0u) << 16u) | (1u << 6u), 8u,
            0xf4000044u | (0x18u << 18u), 16u, 0xbf810000u, 0u};
    Output.fill(0xdeadbeefu);
    Output[2] = 100u;
    std::vector<std::uint32_t> userData(12u, 0u);
    userData[0] = 0u;
    userData[1] = 5u;
    const auto descriptor = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(sizeof(Output)));
    std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 8);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(Code.data()), std::as_bytes(std::span(Code))}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    ShaderRecompiler::RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(Code.data()), Code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, memory}, device->Target(), {0u, 0u, 0u, 128u}};
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device->Dispatch(result, 1u, 1u, 1u, {}, reinterpret_cast<std::uintptr_t>(Code.data()));
    device->WaitIdle();
    for (std::uint32_t index = 0u; index < Output.size(); ++index) {
        const std::uint32_t expected = index == 2u ? 105u : index == 4u ? (returnOld ? 100u : 5u) : 0xdeadbeefu;
        Require(Output[index] == expected, "scalar buffer atomic add: word " + std::to_string(index) + " is " + std::to_string(Output[index]) + ", expected " + std::to_string(expected));
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Run(device, false);
        Run(device, true);
        std::puts("scalar buffer atomic add tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
