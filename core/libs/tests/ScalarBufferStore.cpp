#include "Translation/TranslationContext.hpp"
#include "RdnaDecoder/RdnaMemoryOpDecoder.hpp"
#include <array>
#include <stdexcept>
#include <source_location>
#include <string>

using namespace ShaderRecompiler;

namespace {

void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("scalar buffer store regression at line " + std::to_string(where.line()));
}

void Check(std::uint32_t opcode, RdnaOpcode expectedOpcode, std::uint32_t count) {
    const std::array code{0xf4000044u | (opcode << 18u), 4u};
    const auto instruction = DecodeRdnaSmem(0u, code, 0u);
    Require(instruction.op == expectedOpcode);
    Require(instruction.destination.kind == RdnaOperandKind::ScalarRegister && instruction.destination.reg == 1u);
    Require(instruction.source0.kind == RdnaOperandKind::ScalarRegister && instruction.source0.reg == 8u);
    Require(instruction.source1.kind == RdnaOperandKind::ScalarRegister && instruction.source1.reg == 0u);
    Require(instruction.memoryOffset == 4u && instruction.dataDwordCount == count);

    IrProgram program;
    auto& block = program.CreateBlock();
    program.SetEntryBlock(block);
    TranslationContext context(program, block, 256u);
    context.TranslateInstruction(instruction);
    std::uint32_t stores = 0u;
    std::uint32_t firstLanes = 0u;
    IrOpcode expectedStore = IrOpcode::StoreBufferU32;
    if (count == 2u) expectedStore = IrOpcode::StoreBufferU32x2;
    if (count == 4u) expectedStore = IrOpcode::StoreBufferU32x4;
    for (const auto* value : block.Instructions()) {
        if (value->Opcode() == IrOpcode::ReadFirstLane) ++firstLanes;
        if (value->Opcode() != expectedStore) continue;
        ++stores;
        const auto& memory = program.Resources().memoryInfo.at(value->Flags<MemoryFlags>().index);
        Require(memory.kind == ResourceKind::Buffer && memory.resource == 2u && memory.offset == 4u);
        Require(memory.dataDwords == count && memory.componentCount == count);
        Require(value->Argument(5u)->Opcode() == IrOpcode::LogicalAnd);
    }
    Require(stores == 1u && firstLanes == 1u);
}

}

int main() {
    Check(0x18u, RdnaOpcode::SBufferStoreDword, 1u);
    Check(0x19u, RdnaOpcode::SBufferStoreDwordx2, 2u);
    Check(0x1au, RdnaOpcode::SBufferStoreDwordx4, 4u);
}
