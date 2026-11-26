#include "Translation/TranslationContext.hpp"
#include "RdnaDecoder/RdnaMemoryOpDecoder.hpp"
#include <array>
#include <stdexcept>
#include <source_location>
#include <string>

using namespace ShaderRecompiler;

namespace {

void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("scalar buffer atomic add regression at line " + std::to_string(where.line()));
}

void Check(bool returnOld) {
    const std::uint32_t glc = returnOld ? 1u : 0u;
    const std::array code{0xf4000004u | (0x42u << 18u) | (glc << 16u) | (1u << 6u), 8u};
    const auto instruction = DecodeRdnaSmem(0u, code, 0u);
    Require(instruction.op == RdnaOpcode::SBufferAtomicAdd);
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
        if (value->Opcode() != IrOpcode::BufferAtomicIAdd32) continue;
        ++atomics;
        const auto& memory = program.Resources().memoryInfo.at(value->Flags<MemoryFlags>().index);
        Require(memory.kind == ResourceKind::Buffer && memory.resource == 2u && memory.offset == 8u);
        Require(value->Argument(5u)->Opcode() == IrOpcode::LogicalAnd);
    }
    Require(atomics == 1u && firstLanes == (returnOld ? 2u : 1u));
}

}

int main() {
    Check(false);
    Check(true);
}
