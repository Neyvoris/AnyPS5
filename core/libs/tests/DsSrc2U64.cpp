#include "Translation/TranslationContext.hpp"
#include "RdnaDecoder/RdnaMemoryOpDecoder.hpp"
#include <array>
#include <stdexcept>
#include <source_location>
#include <string>

using namespace ShaderRecompiler;

namespace {

struct Case { std::uint32_t encoding; RdnaOpcode opcode; IrOpcode irOpcode; };
constexpr std::array Cases{
    Case{0xc0u, RdnaOpcode::DsAddSrc2U64, IrOpcode::SharedAtomicIAdd64},
    Case{0xc1u, RdnaOpcode::DsSubSrc2U64, IrOpcode::SharedAtomicISub64},
    Case{0xc2u, RdnaOpcode::DsRsubSrc2U64, IrOpcode::SharedAtomicRsub64},
    Case{0xc3u, RdnaOpcode::DsIncSrc2U64, IrOpcode::SharedAtomicInc64},
    Case{0xc4u, RdnaOpcode::DsDecSrc2U64, IrOpcode::SharedAtomicDec64},
    Case{0xc5u, RdnaOpcode::DsMinSrc2I64, IrOpcode::SharedAtomicSMin64},
    Case{0xc6u, RdnaOpcode::DsMaxSrc2I64, IrOpcode::SharedAtomicSMax64},
    Case{0xc7u, RdnaOpcode::DsMinSrc2U64, IrOpcode::SharedAtomicUMin64},
    Case{0xc8u, RdnaOpcode::DsMaxSrc2U64, IrOpcode::SharedAtomicUMax64},
    Case{0xc9u, RdnaOpcode::DsAndSrc2B64, IrOpcode::SharedAtomicAnd64},
    Case{0xcau, RdnaOpcode::DsOrSrc2B64, IrOpcode::SharedAtomicOr64},
    Case{0xcbu, RdnaOpcode::DsXorSrc2B64, IrOpcode::SharedAtomicXor64},
    Case{0xcdu, RdnaOpcode::DsWriteSrc2B64, IrOpcode::WriteSharedU32x2},
    Case{0xd2u, RdnaOpcode::DsMinSrc2F64, IrOpcode::SharedAtomicFMin64},
    Case{0xd3u, RdnaOpcode::DsMaxSrc2F64, IrOpcode::SharedAtomicFMax64},
};

void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("DS SRC2 u64 regression at line " + std::to_string(where.line()));
}

RdnaInstruction Decode(const Case& test) {
    const std::uint32_t offset0 = 2u;
    const std::uint32_t offset1 = 0u;
    const std::uint32_t word0 = (0x36u << 26u) | (test.encoding << 18u) | (offset1 << 8u) | offset0;
    const std::uint32_t word1 = (4u << 24u) | (7u << 16u) | (6u << 8u) | 5u;
    const std::array code{word0, word1};
    const auto instruction = DecodeRdnaDs(0u, code, 0u);
    Require(instruction.op == test.opcode);
    Require(instruction.dataDwordCount == 2u && instruction.dataBits == 32u);
    Require(instruction.memoryOffset == 0u && instruction.secondaryOffset == 8u);
    Require(instruction.sourceCount == 1u);
    return instruction;
}

void CheckTranslation(const Case& test) {
    const auto instruction = Decode(test);
    IrProgram program;
    auto& block = program.CreateBlock();
    program.SetEntryBlock(block);
    TranslationContext context(program, block, 256u);
    context.TranslateInstruction(instruction);
    std::uint32_t loads = 0u;
    std::uint32_t operations = 0u;
    for (auto* value : block.Instructions()) {
        if (value->Opcode() == IrOpcode::LoadSharedU32x2) {
            ++loads;
            const auto& memory = program.Resources().memoryInfo.at(value->Flags<MemoryFlags>().index);
            Require(memory.kind == ResourceKind::Lds && memory.offset == 8u && memory.dataDwords == 2u);
        }
        if (value->Opcode() == test.irOpcode) {
            ++operations;
            const auto& memory = program.Resources().memoryInfo.at(value->Flags<MemoryFlags>().index);
            Require(memory.kind == ResourceKind::Lds && memory.offset == 0u && memory.dataDwords == 2u);
        }
    }
    Require(loads == 1u && operations == 1u);
}

}

int main() {
    for (const auto& test : Cases) CheckTranslation(test);
}
