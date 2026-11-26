#include "Translation/TranslationContext.hpp"
#include "RdnaDecoder/RdnaMemoryOpDecoder.hpp"
#include <array>
#include <stdexcept>
#include <source_location>
#include <string>

using namespace ShaderRecompiler;

namespace {

struct Case {
    std::uint32_t opcode;
    RdnaOpcode decoded;
    IrOpcode lowered;
};

constexpr std::array Cases{
    Case{0x40u, RdnaOpcode::SBufferAtomicSwap, IrOpcode::BufferAtomicSwap32},
    Case{0x42u, RdnaOpcode::SBufferAtomicAdd, IrOpcode::BufferAtomicIAdd32},
    Case{0x43u, RdnaOpcode::SBufferAtomicSub, IrOpcode::BufferAtomicISub32},
    Case{0x44u, RdnaOpcode::SBufferAtomicSmin, IrOpcode::BufferAtomicSMin32},
    Case{0x45u, RdnaOpcode::SBufferAtomicUmin, IrOpcode::BufferAtomicUMin32},
    Case{0x46u, RdnaOpcode::SBufferAtomicSmax, IrOpcode::BufferAtomicSMax32},
    Case{0x47u, RdnaOpcode::SBufferAtomicUmax, IrOpcode::BufferAtomicUMax32},
    Case{0x48u, RdnaOpcode::SBufferAtomicAnd, IrOpcode::BufferAtomicAnd32},
    Case{0x49u, RdnaOpcode::SBufferAtomicOr, IrOpcode::BufferAtomicOr32},
    Case{0x4au, RdnaOpcode::SBufferAtomicXor, IrOpcode::BufferAtomicXor32},
};

void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("scalar buffer atomic regression at line " + std::to_string(where.line()));
}

void Check(const Case& test, bool returnOld) {
    const std::uint32_t glc = returnOld ? 1u : 0u;
    const std::array code{0xf4000004u | (test.opcode << 18u) | (glc << 16u) | (1u << 6u), 8u};
    const auto instruction = DecodeRdnaSmem(0u, code, 0u);
    Require(instruction.op == test.decoded);
    Require(instruction.glc == returnOld);
    Require(instruction.destination.kind == RdnaOperandKind::ScalarRegister && instruction.destination.reg == 1u);
    Require(instruction.source0.kind == RdnaOperandKind::ScalarRegister && instruction.source0.reg == 8u);
    Require(instruction.source1.kind == RdnaOperandKind::ScalarRegister && instruction.source1.reg == 0u);
    Require(instruction.memoryOffset == 8u && instruction.dataDwordCount == 1u);

    IrProgram program;
    auto& block = program.CreateBlock();
    program.SetEntryBlock(block);
    TranslationContext context(program, block, 256u);
    context.TranslateInstruction(instruction);
    std::uint32_t atomics = 0u;
    std::uint32_t firstLanes = 0u;
    for (const auto* value : block.Instructions()) {
        if (value->Opcode() == IrOpcode::ReadFirstLane) ++firstLanes;
        if (value->Opcode() != test.lowered) continue;
        ++atomics;
        const auto& memory = program.Resources().memoryInfo.at(value->Flags<MemoryFlags>().index);
        Require(memory.kind == ResourceKind::Buffer && memory.resource == 2u && memory.offset == 8u);
        Require(value->Argument(5u)->Opcode() == IrOpcode::LogicalAnd);
    }
    Require(atomics == 1u && firstLanes == (returnOld ? 2u : 1u));
}

}

int main() {
    for (const auto& test : Cases) {
        Check(test, false);
        Check(test, true);
    }
}
