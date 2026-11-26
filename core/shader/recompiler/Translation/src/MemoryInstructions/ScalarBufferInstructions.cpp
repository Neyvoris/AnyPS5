#include "Translation/TranslationContext.hpp"
#include <array>
#include <stdexcept>

namespace ShaderRecompiler {

namespace {

MemoryInfo scalarMemoryInfoFromInstruction(const RdnaInstruction& inst, bool raw) {
    if (inst.family != RdnaInstructionFamily::SMEM) {
        throw std::runtime_error("scalarMemoryInfoFromInstruction requires an SMEM instruction");
    }
    // Raw address loads may take their 64-bit base from VCC; descriptor loads need four SGPRs.
    if (inst.source0.kind != RdnaOperandKind::ScalarRegister && !(raw && inst.source0.kind == RdnaOperandKind::VccLo)) {
        throw std::runtime_error("scalar memory base must be a scalar register");
    }
    MemoryInfo memory;
    memory.kind = raw ? ResourceKind::ScalarAddress : ResourceKind::ScalarBuffer;
    memory.offset = inst.memoryOffset;
    memory.dataDwords = inst.dataDwordCount;
    memory.componentCount = inst.dataDwordCount;
    if (!raw) {
        memory.resource = inst.source0.reg / 4u;
    }
    return memory;
}

}

bool TranslationContext::sLoad(const RdnaInstruction& inst, bool raw) {
    const MemoryInfo memory = scalarMemoryInfoFromInstruction(inst, raw);
    const std::uint32_t baseCode = inst.source0.kind == RdnaOperandKind::VccLo ? 106u : inst.source0.reg;
    IrValue* resource = raw ? getScalarAddressResource(baseCode) : getBufferResource(memory);
    const IrU32 offset = readU32(inst.source1);
    std::array<IrValue*, 16u> loaded{};
    for (std::uint32_t component = 0u; component < memory.dataDwords; ++component) {
        MemoryInfo scalar = memory;
        scalar.offset += component * 4u;
        scalar.dataDwords = 1u;
        scalar.componentIndex = component;
        const MemoryFlags flags = addMemoryInfo(scalar, inst.programCounter);
        if (raw) {
            loaded[component] = &ir.Emit(IrOpcode::LoadAddressU32, IrOpcodeType(IrOpcode::LoadAddressU32), {resource, &offset.Value(), &ir.Constant(0u), &ir.ConstantBool(true)}, flags);
        } else {
            loaded[component] = &ir.Emit(IrOpcode::ReadConstBuffer, IrOpcodeType(IrOpcode::ReadConstBuffer), {resource, &offset.Value()}, flags);
        }
    }
    for (std::uint32_t component = 0u; component < memory.dataDwords; ++component) {
        writeOperand(scalarDestinationOperand(inst.destination, component), loaded[component]);
    }
    return true;
}

bool TranslationContext::sBufferStore(const RdnaInstruction& inst) {
    if (inst.source0.kind != RdnaOperandKind::ScalarRegister) {
        throw std::runtime_error("scalar buffer store descriptor must be a scalar register");
    }
    MemoryInfo memory;
    memory.kind = ResourceKind::Buffer;
    memory.resource = inst.source0.reg / 4u;
    memory.offset = inst.memoryOffset;
    memory.dataDwords = inst.dataDwordCount;
    memory.componentCount = inst.dataDwordCount;
    memory.coherent = inst.glc;
    IrValue* resource = getBufferResource(memory);
    IrValue& lane = ir.Emit(IrOpcode::LaneId, IrType::U32, {});
    IrValue& exec = ir.GetExec();
    IrValue& first = ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&lane, &exec});
    IrValue& leader = ir.LogicalAnd(exec, ir.IEqual(lane, first));
    const IrU32 offset(ir.Constant(0u));
    const IrU32 soffset = readU32(inst.source1);
    std::array<IrValue*, 4> values{};
    for (std::uint32_t component = 0u; component < inst.dataDwordCount; ++component) {
        values[component] = &readU32(offsetOperand(inst.destination, component)).Value();
    }
    IrValue* value = values[0];
    IrOpcode opcode = IrOpcode::StoreBufferU32;
    if (inst.dataDwordCount == 2u) {
        opcode = IrOpcode::StoreBufferU32x2;
        value = &ir.Emit(IrOpcode::CompositeConstructU32x2, IrType::U32x2, {values[0], values[1]});
    } else if (inst.dataDwordCount == 4u) {
        opcode = IrOpcode::StoreBufferU32x4;
        value = &ir.Emit(IrOpcode::CompositeConstructU32x4, IrType::U32x4, {values[0], values[1], values[2], values[3]});
    } else if (inst.dataDwordCount != 1u) {
        return false;
    }
    (void)ir.Emit(opcode, IrType::Void,
                  {resource, &ir.Constant(0u), &offset.Value(), &soffset.Value(), value, &leader},
                  addMemoryInfo(memory, inst.programCounter));
    return true;
}

bool TranslationContext::sScratchLoad(const RdnaInstruction& /*inst*/) {
    throw std::runtime_error("s_scratch_load not yet implemented: wave-uniform or swizzled scratch layout required");
}

void TranslationContext::TranslateCodeTableLoad(const RdnaInstruction& instruction, const ControlFlowGraph::CodeTableLoad& table) {
    if (table.values.empty()) throw std::runtime_error("empty shader code table");
    const IrU32 index = readRawU32(instruction.source1);
    IrU32 low(ir.Constant(static_cast<std::uint32_t>(table.values.back())));
    IrU32 high(ir.Constant(static_cast<std::uint32_t>(table.values.back() >> 32u)));
    for (std::size_t entry = table.values.size() - 1u; entry-- > 0u;) {
        auto& matches = ir.IEqual(index.Value(), ir.Constant(static_cast<std::uint32_t>(entry * instruction.dataDwordCount * 4u)));
        low = IrU32(ir.Select(matches, ir.Constant(static_cast<std::uint32_t>(table.values[entry])), low.Value()));
        if (instruction.dataDwordCount == 2u) high = IrU32(ir.Select(matches, ir.Constant(static_cast<std::uint32_t>(table.values[entry] >> 32u)), high.Value()));
    }
    if (instruction.dataDwordCount == 1u) {
        writeRawU32(instruction.destination, low);
        return;
    }
    writeU32Pair(instruction.destination, {low, high});
}

}
