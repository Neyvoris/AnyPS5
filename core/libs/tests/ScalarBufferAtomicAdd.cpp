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
    std::uint32_t dataDwords;
};

constexpr std::array Cases{
    Case{0x40u, RdnaOpcode::SBufferAtomicSwap, IrOpcode::BufferAtomicSwap32, 1u},
    Case{0x42u, RdnaOpcode::SBufferAtomicAdd, IrOpcode::BufferAtomicIAdd32, 1u},
    Case{0x43u, RdnaOpcode::SBufferAtomicSub, IrOpcode::BufferAtomicISub32, 1u},
    Case{0x44u, RdnaOpcode::SBufferAtomicSmin, IrOpcode::BufferAtomicSMin32, 1u},
    Case{0x45u, RdnaOpcode::SBufferAtomicUmin, IrOpcode::BufferAtomicUMin32, 1u},
    Case{0x46u, RdnaOpcode::SBufferAtomicSmax, IrOpcode::BufferAtomicSMax32, 1u},
    Case{0x47u, RdnaOpcode::SBufferAtomicUmax, IrOpcode::BufferAtomicUMax32, 1u},
    Case{0x48u, RdnaOpcode::SBufferAtomicAnd, IrOpcode::BufferAtomicAnd32, 1u},
    Case{0x49u, RdnaOpcode::SBufferAtomicOr, IrOpcode::BufferAtomicOr32, 1u},
    Case{0x4au, RdnaOpcode::SBufferAtomicXor, IrOpcode::BufferAtomicXor32, 1u},
    Case{0x41u, RdnaOpcode::SBufferAtomicCmpswap, IrOpcode::BufferAtomicCmpSwap32, 1u},
    Case{0x60u, RdnaOpcode::SBufferAtomicSwapX2, IrOpcode::BufferAtomicSwap64, 2u},
    Case{0x62u, RdnaOpcode::SBufferAtomicAddX2, IrOpcode::BufferAtomicIAdd64, 2u},
    Case{0x63u, RdnaOpcode::SBufferAtomicSubX2, IrOpcode::BufferAtomicISub64, 2u},
    Case{0x64u, RdnaOpcode::SBufferAtomicSminX2, IrOpcode::BufferAtomicSMin64, 2u},
    Case{0x65u, RdnaOpcode::SBufferAtomicUminX2, IrOpcode::BufferAtomicUMin64, 2u},
    Case{0x66u, RdnaOpcode::SBufferAtomicSmaxX2, IrOpcode::BufferAtomicSMax64, 2u},
    Case{0x67u, RdnaOpcode::SBufferAtomicUmaxX2, IrOpcode::BufferAtomicUMax64, 2u},
    Case{0x68u, RdnaOpcode::SBufferAtomicAndX2, IrOpcode::BufferAtomicAnd64, 2u},
    Case{0x69u, RdnaOpcode::SBufferAtomicOrX2, IrOpcode::BufferAtomicOr64, 2u},
    Case{0x6au, RdnaOpcode::SBufferAtomicXorX2, IrOpcode::BufferAtomicXor64, 2u},
    Case{0x61u, RdnaOpcode::SBufferAtomicCmpswapX2, IrOpcode::BufferAtomicCmpSwap64, 2u},
    Case{0x4bu, RdnaOpcode::SBufferAtomicInc, IrOpcode::BufferAtomicInc32, 1u},
    Case{0x4cu, RdnaOpcode::SBufferAtomicDec, IrOpcode::BufferAtomicDec32, 1u},
    Case{0x6bu, RdnaOpcode::SBufferAtomicIncX2, IrOpcode::BufferAtomicInc64, 2u},
    Case{0x6cu, RdnaOpcode::SBufferAtomicDecX2, IrOpcode::BufferAtomicDec64, 2u},
};

struct AddressCase { std::uint32_t encoding; RdnaOpcode decoded; IrOpcode lowered; std::uint32_t dwords; bool compare; };
constexpr std::array AddressCases{
    AddressCase{0x80u, RdnaOpcode::SAtomicSwap, IrOpcode::AddressAtomicSwap32, 1u, false},
    AddressCase{0x81u, RdnaOpcode::SAtomicCmpswap, IrOpcode::AddressAtomicCmpSwap32, 1u, true},
    AddressCase{0x82u, RdnaOpcode::SAtomicAdd, IrOpcode::AddressAtomicIAdd32, 1u, false},
    AddressCase{0x83u, RdnaOpcode::SAtomicSub, IrOpcode::AddressAtomicISub32, 1u, false},
    AddressCase{0x84u, RdnaOpcode::SAtomicSmin, IrOpcode::AddressAtomicSMin32, 1u, false},
    AddressCase{0x85u, RdnaOpcode::SAtomicUmin, IrOpcode::AddressAtomicUMin32, 1u, false},
    AddressCase{0x86u, RdnaOpcode::SAtomicSmax, IrOpcode::AddressAtomicSMax32, 1u, false},
    AddressCase{0x87u, RdnaOpcode::SAtomicUmax, IrOpcode::AddressAtomicUMax32, 1u, false},
    AddressCase{0x88u, RdnaOpcode::SAtomicAnd, IrOpcode::AddressAtomicAnd32, 1u, false},
    AddressCase{0x89u, RdnaOpcode::SAtomicOr, IrOpcode::AddressAtomicOr32, 1u, false},
    AddressCase{0x8au, RdnaOpcode::SAtomicXor, IrOpcode::AddressAtomicXor32, 1u, false},
    AddressCase{0x8bu, RdnaOpcode::SAtomicInc, IrOpcode::AddressAtomicInc32, 1u, false},
    AddressCase{0x8cu, RdnaOpcode::SAtomicDec, IrOpcode::AddressAtomicDec32, 1u, false},
    AddressCase{0xa0u, RdnaOpcode::SAtomicSwapX2, IrOpcode::AddressAtomicSwap64, 2u, false},
    AddressCase{0xa1u, RdnaOpcode::SAtomicCmpswapX2, IrOpcode::AddressAtomicCmpSwap64, 2u, true},
    AddressCase{0xa2u, RdnaOpcode::SAtomicAddX2, IrOpcode::AddressAtomicIAdd64, 2u, false},
    AddressCase{0xa3u, RdnaOpcode::SAtomicSubX2, IrOpcode::AddressAtomicISub64, 2u, false},
    AddressCase{0xa4u, RdnaOpcode::SAtomicSminX2, IrOpcode::AddressAtomicSMin64, 2u, false},
    AddressCase{0xa5u, RdnaOpcode::SAtomicUminX2, IrOpcode::AddressAtomicUMin64, 2u, false},
    AddressCase{0xa6u, RdnaOpcode::SAtomicSmaxX2, IrOpcode::AddressAtomicSMax64, 2u, false},
    AddressCase{0xa7u, RdnaOpcode::SAtomicUmaxX2, IrOpcode::AddressAtomicUMax64, 2u, false},
    AddressCase{0xa8u, RdnaOpcode::SAtomicAndX2, IrOpcode::AddressAtomicAnd64, 2u, false},
    AddressCase{0xa9u, RdnaOpcode::SAtomicOrX2, IrOpcode::AddressAtomicOr64, 2u, false},
    AddressCase{0xaau, RdnaOpcode::SAtomicXorX2, IrOpcode::AddressAtomicXor64, 2u, false},
    AddressCase{0xabu, RdnaOpcode::SAtomicIncX2, IrOpcode::AddressAtomicInc64, 2u, false},
    AddressCase{0xacu, RdnaOpcode::SAtomicDecX2, IrOpcode::AddressAtomicDec64, 2u, false},
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
    Require(instruction.memoryOffset == 8u && instruction.dataDwordCount == test.dataDwords);

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
        Require(memory.kind == ResourceKind::Buffer && memory.resource == 2u && memory.offset == 8u && memory.dataDwords == test.dataDwords);
        if (test.decoded == RdnaOpcode::SBufferAtomicCmpswap || test.decoded == RdnaOpcode::SBufferAtomicCmpswapX2) {
            Require(value->ArgumentCount() == 7u);
            Require(value->Argument(4u)->Type() == (test.dataDwords == 1u ? IrType::U32 : IrType::U64));
            Require(value->Argument(5u)->Type() == (test.dataDwords == 1u ? IrType::U32 : IrType::U64));
            Require(value->Argument(6u)->Opcode() == IrOpcode::LogicalAnd);
        } else {
            Require(value->ArgumentCount() == 6u);
            Require(value->Argument(5u)->Opcode() == IrOpcode::LogicalAnd);
        }
    }
    Require(atomics == 1u && firstLanes == (returnOld ? 1u + test.dataDwords : 1u));
}

void CheckAddressAtomic(const AddressCase& test, bool returnOld) {
    const std::uint32_t glc = returnOld ? 1u : 0u;
    const std::array code{0xf4000000u | (test.encoding << 18u) | (glc << 16u) | (1u << 6u) | 6u, 8u};
    const auto instruction = DecodeRdnaSmem(0u, code, 0u);
    Require(instruction.op == test.decoded && instruction.glc == returnOld);
    Require(instruction.destination.kind == RdnaOperandKind::ScalarRegister && instruction.destination.reg == 1u);
    Require(instruction.source0.kind == RdnaOperandKind::ScalarRegister && instruction.source0.reg == 12u);
    Require(instruction.source1.kind == RdnaOperandKind::ScalarRegister && instruction.source1.reg == 0u);
    Require(instruction.memoryOffset == 8u && instruction.dataDwordCount == test.dwords);

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
        Require(memory.kind == ResourceKind::Global && memory.offset == 8u && memory.dataDwords == test.dwords);
        Require(value->ArgumentCount() == (test.compare ? 6u : 5u));
        Require(value->Argument(value->ArgumentCount() - 1u)->Opcode() == IrOpcode::LogicalAnd);
    }
    Require(atomics == 1u && firstLanes == (returnOld ? 1u + test.dwords : 1u));
}

void CheckAddressStore(std::uint32_t opcode, RdnaOpcode decoded, IrOpcode lowered, std::uint32_t dataDwords) {
    const std::array code{0xf4000000u | (opcode << 18u) | (1u << 6u) | 6u, 8u};
    const auto instruction = DecodeRdnaSmem(0u, code, 0u);
    Require(instruction.op == decoded && instruction.dataDwordCount == dataDwords);
    Require(instruction.destination.reg == 1u && instruction.source0.reg == 12u && instruction.memoryOffset == 8u);
    IrProgram program;
    auto& block = program.CreateBlock();
    program.SetEntryBlock(block);
    TranslationContext context(program, block, 256u);
    context.TranslateInstruction(instruction);
    std::uint32_t stores = 0u;
    for (const auto* value : block.Instructions()) {
        if (value->Opcode() != lowered) continue;
        ++stores;
        const auto& memory = program.Resources().memoryInfo.at(value->Flags<MemoryFlags>().index);
        Require(memory.kind == ResourceKind::Global && memory.offset == 8u && memory.dataDwords == dataDwords);
        Require(value->ArgumentCount() == 5u && value->Argument(4u)->Opcode() == IrOpcode::LogicalAnd);
    }
    Require(stores == 1u);
}

}

int main() {
    for (const auto& test : Cases) {
        Check(test, false);
        Check(test, true);
    }
    for (const auto& test : AddressCases) {
        CheckAddressAtomic(test, false);
        CheckAddressAtomic(test, true);
    }
    CheckAddressStore(0x10u, RdnaOpcode::SStoreDword, IrOpcode::StoreAddressU32, 1u);
    CheckAddressStore(0x11u, RdnaOpcode::SStoreDwordX2, IrOpcode::StoreAddressU32x2, 2u);
    CheckAddressStore(0x12u, RdnaOpcode::SStoreDwordX4, IrOpcode::StoreAddressU32x4, 4u);
}
