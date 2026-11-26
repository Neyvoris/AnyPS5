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

bool TranslationContext::sBufferAtomic(const RdnaInstruction& inst, IrOpcode opcode) {
    if (inst.source0.kind != RdnaOperandKind::ScalarRegister) {
        throw std::runtime_error("scalar buffer atomic descriptor must be a scalar register");
    }
    MemoryInfo memory;
    memory.kind = ResourceKind::Buffer;
    memory.resource = inst.source0.reg / 4u;
    memory.offset = inst.memoryOffset;
    memory.dataDwords = inst.dataDwordCount;
    memory.componentCount = inst.dataDwordCount;
    IrValue* resource = getBufferResource(memory);
    IrValue& lane = ir.Emit(IrOpcode::LaneId, IrType::U32, {});
    IrValue& exec = ir.GetExec();
    IrValue& first = ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&lane, &exec});
    IrValue& leader = ir.LogicalAnd(exec, ir.IEqual(lane, first));
    const IrU32 offset(ir.Constant(0u));
    const IrU32 soffset = readU32(inst.source1);
    IrValue* atomicArgs[2]{};
    if (opcode == IrOpcode::BufferAtomicCmpSwap32) {
        atomicArgs[0] = &readU32(inst.destination).Value();
        atomicArgs[1] = &readU32(offsetOperand(inst.destination, 1u)).Value();
    } else if (opcode == IrOpcode::BufferAtomicCmpSwap64) {
        atomicArgs[0] = &readU64(inst.destination).Value();
        atomicArgs[1] = &readU64(offsetOperand(inst.destination, 2u)).Value();
    }
    IrValue& result = IrOpcodeType(opcode) == IrType::U64
        ? (atomicArgs[0] ? ir.Emit(opcode, IrType::U64, {resource, &ir.Constant(0u), &offset.Value(), &soffset.Value(), atomicArgs[0], atomicArgs[1], &leader}, addMemoryInfo(memory, inst.programCounter))
                         : ir.Emit(opcode, IrType::U64, {resource, &ir.Constant(0u), &offset.Value(), &soffset.Value(), &readU64(inst.destination).Value(), &leader}, addMemoryInfo(memory, inst.programCounter)))
        : (atomicArgs[0] ? ir.Emit(opcode, IrType::U32, {resource, &ir.Constant(0u), &offset.Value(), &soffset.Value(), atomicArgs[0], atomicArgs[1], &leader}, addMemoryInfo(memory, inst.programCounter))
                         : ir.Emit(opcode, IrType::U32, {resource, &ir.Constant(0u), &offset.Value(), &soffset.Value(), &readU32(inst.destination).Value(), &leader}, addMemoryInfo(memory, inst.programCounter)));
    if (inst.glc && IrOpcodeType(opcode) == IrType::U64) {
        const auto words = extractU64(IrU64(result));
        const IrU32 low(ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&words[0].Value(), &leader}));
        const IrU32 high(ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&words[1].Value(), &leader}));
        writeU32Pair(inst.destination, {low, high});
    } else if (inst.glc) {
        IrValue& returned = ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&result, &leader});
        writeOperand(inst.destination, &returned);
    }
    return true;
}

bool TranslationContext::sAtomicAddress(const RdnaInstruction& inst, IrOpcode opcode) {
    if (inst.source0.kind != RdnaOperandKind::ScalarRegister && inst.source0.kind != RdnaOperandKind::VccLo) {
        throw std::runtime_error("scalar address atomic base must be a scalar register pair");
    }
    MemoryInfo memory;
    memory.kind = ResourceKind::Global;
    memory.offset = inst.memoryOffset;
    memory.dataDwords = inst.dataDwordCount;
    memory.componentCount = inst.dataDwordCount;
    memory.coherent = inst.glc;
    const std::uint32_t base = inst.source0.kind == RdnaOperandKind::VccLo ? 106u : inst.source0.reg;
    IrValue* resource = getScalarAddressResource(base);
    IrValue& lane = ir.Emit(IrOpcode::LaneId, IrType::U32, {});
    IrValue& exec = ir.GetExec();
    IrValue& first = ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&lane, &exec});
    IrValue& leader = ir.LogicalAnd(exec, ir.IEqual(lane, first));
    const IrU32 offset = readU32(inst.source1);
    const IrU32 high(ir.Constant(0u));
    const MemoryFlags flags = addMemoryInfo(memory, inst.programCounter);
    const bool compare = opcode == IrOpcode::AddressAtomicCmpSwap32 || opcode == IrOpcode::AddressAtomicCmpSwap64;
    IrValue& result = IrOpcodeType(opcode) == IrType::U64
        ? (compare ? ir.Emit(opcode, IrType::U64, {resource, &offset.Value(), &high.Value(), &readU64(inst.destination).Value(), &readU64(offsetOperand(inst.destination, 2u)).Value(), &leader}, flags)
                   : ir.Emit(opcode, IrType::U64, {resource, &offset.Value(), &high.Value(), &readU64(inst.destination).Value(), &leader}, flags))
        : (compare ? ir.Emit(opcode, IrType::U32, {resource, &offset.Value(), &high.Value(), &readU32(inst.destination).Value(), &readU32(offsetOperand(inst.destination, 1u)).Value(), &leader}, flags)
                   : ir.Emit(opcode, IrType::U32, {resource, &offset.Value(), &high.Value(), &readU32(inst.destination).Value(), &leader}, flags));
    if (inst.glc) {
        if (IrOpcodeType(opcode) == IrType::U64) {
            const auto words = extractU64(IrU64(result));
            const IrU32 low(ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&words[0].Value(), &leader}));
            const IrU32 highWord(ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&words[1].Value(), &leader}));
            writeU32Pair(inst.destination, {low, highWord});
        } else {
            IrValue& returned = ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&result, &leader});
            writeOperand(inst.destination, &returned);
        }
    }
    return true;
}

bool TranslationContext::sAddressStore(const RdnaInstruction& inst) {
    if (inst.source0.kind != RdnaOperandKind::ScalarRegister && inst.source0.kind != RdnaOperandKind::VccLo) {
        throw std::runtime_error("scalar store base must be a scalar register pair");
    }
    MemoryInfo memory;
    memory.kind = ResourceKind::Global;
    memory.offset = inst.memoryOffset;
    memory.dataDwords = inst.dataDwordCount;
    memory.componentCount = inst.dataDwordCount;
    memory.coherent = inst.glc;
    const std::uint32_t base = inst.source0.kind == RdnaOperandKind::VccLo ? 106u : inst.source0.reg;
    IrValue* resource = getScalarAddressResource(base);
    IrValue& lane = ir.Emit(IrOpcode::LaneId, IrType::U32, {});
    IrValue& exec = ir.GetExec();
    IrValue& first = ir.Emit(IrOpcode::ReadFirstLane, IrType::U32, {&lane, &exec});
    IrValue& leader = ir.LogicalAnd(exec, ir.IEqual(lane, first));
    const IrU32 offset = readU32(inst.source1);
    const IrU32 high(ir.Constant(0u));
    std::array<IrValue*, 4> words{};
    for (std::uint32_t index = 0u; index < inst.dataDwordCount; ++index) {
        words[index] = &readU32(offsetOperand(inst.destination, index)).Value();
    }
    IrValue* value = words[0];
    IrOpcode opcode = IrOpcode::StoreAddressU32;
    if (inst.dataDwordCount == 2u) {
        opcode = IrOpcode::StoreAddressU32x2;
        value = &ir.Emit(IrOpcode::CompositeConstructU32x2, IrType::U32x2, {words[0], words[1]});
    } else if (inst.dataDwordCount == 4u) {
        opcode = IrOpcode::StoreAddressU32x4;
        value = &ir.Emit(IrOpcode::CompositeConstructU32x4, IrType::U32x4, {words[0], words[1], words[2], words[3]});
    } else if (inst.dataDwordCount != 1u) {
        return false;
    }
    (void)ir.Emit(opcode, IrType::Void, {resource, &offset.Value(), &high.Value(), value, &leader}, addMemoryInfo(memory, inst.programCounter));
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
