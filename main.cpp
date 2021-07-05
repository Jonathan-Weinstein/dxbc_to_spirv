/*

    GetSrcValueWithType() and SimpleCodegenBasicBlock() are the interesting functions.


    Can be built with something like:
    windows:
        g++ -std=c++11 -I %VULKAN_SDK%\Include -I %VULKAN_SDK%\Include\spirv-headers *.cpp
    linux:
        g++ -std=c++11 -I $VULKAN_SDK/Include -I $VULKAN_SDK$/Include/spirv-headers *.cpp -ldl

**/

#include <stdio.h>

#include "common.h"

#include <vector>

#include "DxbcTextScanner.h"

#include "Array.h"

#include "SpirvRunner.h"

#include <spirv.h>
#include <GLSL.std.450.h>

#define MAIN_U32 0x6E00696Du

typedef Array<uint32_t> SpirvDynamicArray;

// does not support removal
// pointers are not stable
class ConstantsMapScaler32 {
public:
    struct Element {
        uint32_t constkey; // key of table
        SpvId id;
    };
    typedef std::vector<Element>::const_iterator iter;

    struct FindElseInsertResult {
        SpvId *pId; // caller fills this in after knowing insrt was succesful
        bool inserted;
    };

private:
    std::vector<Element> v;
public:
    uint size() const { return uint(v.size()); }
    iter begin() const { return v.cbegin(); }
    iter end() const { return v.cend(); }

    FindElseInsertResult FindElseInsert(uint32_t key)
    {
        for (Element& elem : v) {
            if (elem.constkey == key) {
                return { &elem.id, false };
            }
        }
        v.push_back(Element{ key, 0 });
        return { &v.back().id, true };
    }
};

/*
Note: The "Gen" or "G" in int means generic, and means
OpTypeInt with a "signedness" of 0.

It is undesired for sint and uint to be two distinct types,
rather just have 32-bits and have the semantics be defined by each
instruction. Spirv supposedly supports this. The spec says
one way to think of signdess=0 is "no sign".

I see some stuff some ops in the spec that say signedness must be 0,
but I haven't seen anything saying must be 1. So I guess "G"-int
can be used for everything, except maybe the sample-type of image decls,
or when spirv 1.4's SignExtend and ZeroExtend image operands aren't available.

*/
enum {
    StaticSpvId_ExtInst_GLSL_std = 1,

    StaticSpvId_EntryFunction = 2,
    StaticSpvId_TypeVoid = 3,
    StaticSpvId_TypeVoidFunction = 4,
    
    StaticSpvId_TypeBool = 8, // handy to start this at a multiple of 4?
    StaticSpvId_TypeV2Bool,
    StaticSpvId_TypeV3Bool,
    StaticSpvId_TypeV4Bool,
    StaticSpvId_TypeGenInt32,
    StaticSpvId_TypeV2GenInt32,
    StaticSpvId_TypeV3GenInt32,
    StaticSpvId_TypeV4GenInt32,
    StaticSpvId_TypeFloat32,
    StaticSpvId_TypeV2Float32,
    StaticSpvId_TypeV3Float32,
    StaticSpvId_TypeV4Float32,

    // might only need "signed" int32 for the sampled-type for OpTypeImage?

    StaticSpvId_End
};


struct BasicBlock {
    SpvId spvId = 0;
    Array<uint32_t> code;
};

struct Function {
    // Descriptor values and input-file values (not pointers) are
    // handled like constant IDs, but these
    // are per-function, instead of in the module,
    // because OpLoad must be in a funtion body.
    //
    // I thinks its nice if these ops appear once at the start of the function,
    // instead of potentially multiple times.
    SpvId vThreadID_xyx_id = 0;
    SpvId vThreadID_c_id[3] = {}; // OpCompositeExtract of the above.
    // SpvId uav_ids[64] = {};

    SpvId vThreadIDInGroupFlattened_id = 0;
};


struct Module {
    DxbcHeaderInfo dxbcHeaderInfo;

    ConstantsMapScaler32 gint32Constants;

    //bound = idinfo.size();
    // std::vector<SpirvIdInfo> idInfo;
    SpvId _bound = StaticSpvId_End;

    SpvId ptr_vThreadID_id = 0;
    SpvId ptr_uav_ids[64] = { };
    SpvId uav_image_type_ids[64] = { };

    SpvId ptr_vThreadIDInGroupFlattened_id = 0;

    SpvId GetBound() const { return _bound; }


    SpvId AllocId() { return _bound++; }

    SpvId Get_vThreadID_c_id(Function *fn, uint comp)
    {
        ASSERT(comp < 3u);
        SpvId id = fn->vThreadID_c_id[comp];
        if (!id) {
            if (!fn->vThreadID_xyx_id) {
                fn->vThreadID_xyx_id = this->_bound++;
            }
            id = this->_bound++;
            fn->vThreadID_c_id[comp] = id;
        }
        return id;
    }

    SpvId Get_vThreadIDInGroupFlattened_id(Function *fn)
    {
        SpvId id = fn->vThreadIDInGroupFlattened_id;
        if (!id) {
            id = this->AllocId();
            fn->vThreadIDInGroupFlattened_id = id;
        }
        return id;
    }

    SpvId GetGIntConstantId(uint32_t key)
    {
        ConstantsMapScaler32::FindElseInsertResult res = this->gint32Constants.FindElseInsert(key);
        if (res.inserted) {
            SpvId id = this->_bound++;
            *res.pId = id;
            return id;
        }
        else {
            return *res.pId;
        }
    }
};

// ------------------------------------------------------------------------------------------------
// Module spirv emit utility

static uint32_t *
PutWordHeaderAndString(Array<uint32_t>& code, SpvOp op, uint numMiddleArgs, char_view str)
{
    uint numWords = 1 + numMiddleArgs + (str.length + 1 + 3) / 4u; // header + middle + str + 1+ zeros
    uint32_t *p = code.uninitialized_push_n(numWords);
    char *const pByteEnd = reinterpret_cast<char *>(p + numWords);
    *p++ = op | numWords << 16; // advance to any middle args

    char *copyDst = reinterpret_cast<char *>(p + numMiddleArgs);
    memcpy(copyDst, str.ptr, str.length);
    copyDst += str.length;
    ASSERT(copyDst < pByteEnd);
    do *copyDst = 0; while (++copyDst != pByteEnd);
    
    return p;
}

static void EmitOpEntryPoint(Array<uint32_t>& code, SpvExecutionModel execModel, SpvId functionId, char_view name)
{
    uint32_t *p = PutWordHeaderAndString(code, SpvOpEntryPoint, 2, name);
    p[0] = execModel; // e.g: SpvExecutionModelGLCompute
    p[1] = functionId;
}

static void EmitOpExtInstrImport(Array<uint32_t>& code, SpvId resultId, char_view name)
{
    uint32_t *p = PutWordHeaderAndString(code, SpvOpExtInstImport, 1, name);
    p[0] = resultId;
}

static void EmitOpName(Array<uint32_t>& code, SpvId id, char_view name)
{
    uint32_t *p = PutWordHeaderAndString(code, SpvOpName, 1, name);
    p[0] = id;
}

static void EmitOpList(Array<uint32_t>& code, SpvOp op, array_span<const uint32_t> list)
{
    uint nTail = list.size();
    uint nTotal = 1 + nTail;
    uint32_t *p = code.uninitialized_push_n(nTotal);
    *p++ = op | nTotal << 16;
    for (uint i = 0; i < nTail; ++i) {
        p[i] = list[i];
    }
}

static void EmitDecorateBuiltin(Array<uint32_t>& code, SpvId id, SpvBuiltIn builtinEnum)
{
    code.push_initlist({ SpvOpDecorate | 4 << 16, id, SpvDecorationBuiltIn, uint32_t(builtinEnum) });
}

static SpvId
EmitBinOp(Module& m, Array<uint32_t>& code, SpvOp op, SpvId resultTypeId, SpvId a, SpvId b)
{
    uint32_t *p = code.uninitialized_push_n(5);
    SpvId dstValueId = m.AllocId();
    p[0] = op | 5 << 16;
    p[1] = resultTypeId;
    p[2] = dstValueId;
    p[3] = a;
    p[4] = b;
    return dstValueId;
}

static void EmitScalarConstants(Array<uint32_t>& code, const ConstantsMapScaler32& constants, SpvId typeId)
{
    uint32_t *p = code.uninitialized_push_n(constants.size() * 4);
    for (const ConstantsMapScaler32::Element& e : constants) {
        p[0] = SpvOpConstant | 4 << 16;
        p[1] = typeId;
        p[2] = e.id;
        p[3] = e.constkey;
        p += 4;
    }
}

static void EmitDecorateSetAndBinding(Array<uint32_t>& code, SpvId id, uint set, uint binding)
{
    uint32_t *p = code.uninitialized_push_n(8);
    p[0] = SpvOpDecorate | 4 << 16; p[1] = id; p[2] = SpvDecorationDescriptorSet; p[3] = set;
    p[4] = SpvOpDecorate | 4 << 16; p[5] = id; p[6] = SpvDecorationBinding; p[7] = binding;
}

static SpvId EmitBitcast(Module& m, Array<uint32_t>& code, SpvId dstTypeId, SpvId srcValueId)
{
    SpvId resultId = m.AllocId();
    code.push4(SpvOpBitcast | 4 << 16, dstTypeId, resultId, srcValueId);
    return resultId;
}

static SpvId EmitSelect(Module& m, Array<uint32_t>& code, SpvId dstTypeId, SpvId cond, SpvId t, SpvId f)
{
    SpvId resultId = m.AllocId();
    code.push_initlist({ SpvOpSelect | 6 << 16, dstTypeId, resultId, cond, t, f });
    return resultId;
}

// ---------------------------------------------------------------------------------------------
// Info on spirv opcodes and dxbc->spirv mappings:

enum class SpirvOpClass : uint8_t {
    misc,
    int_common,
    float_common,
    int_cmp,
    float_cmp,
    select_generic
};

struct SpirvOpInfo {
    uint16_t similarSpvOp;
    SpirvOpClass opClass;

    bool IsFloatArithmeticOrCompare() const
    { 
        return opClass == SpirvOpClass::float_cmp || opClass == SpirvOpClass::float_common;
    }
    
    SpvOp BoolLogicOpOfBitwiseIntOp() const
    {
        switch (similarSpvOp) {
        case SpvOpNot: return SpvOpLogicalNot; // unary
        case SpvOpBitwiseOr: return SpvOpLogicalOr;
        case SpvOpBitwiseXor: return SpvOpLogicalNotEqual;
        case SpvOpBitwiseAnd: return SpvOpLogicalAnd;
        default: return SpvOpNop; // 0;
        }
    }

    SpvId DstComponentTypeId() const
    {
        switch (opClass) {
        case SpirvOpClass::int_cmp:
        case SpirvOpClass::float_cmp:
            return StaticSpvId_TypeBool;
        case SpirvOpClass::int_common:
            return StaticSpvId_TypeGenInt32;
        case SpirvOpClass::float_common:
            return StaticSpvId_TypeFloat32;
        default:
            return 0;
        }
    }

    bool IsBitShift() const
    {

        switch (similarSpvOp) {
        case SpvOpShiftLeftLogical:
        case SpvOpShiftRightLogical:
        case SpvOpShiftRightArithmetic:
            return true;
        }
        return false;
    }
};

static SpirvOpInfo GetSpirvOpInfo(DxbcInstrTag dxbcTag)
{
    switch (dxbcTag) {
    case DxbcInstrTag::iand: return { SpvOpBitwiseAnd, SpirvOpClass::int_common };
    case DxbcInstrTag::ior: return { SpvOpBitwiseOr, SpirvOpClass::int_common };
    case DxbcInstrTag::ixor: return { SpvOpBitwiseXor, SpirvOpClass::int_common };
    case DxbcInstrTag::inot: return { SpvOpNot, SpirvOpClass::int_common };
    case DxbcInstrTag::iadd: return { SpvOpIAdd, SpirvOpClass::int_common };
    case DxbcInstrTag::add: return { SpvOpFAdd, SpirvOpClass::float_common };
    case DxbcInstrTag::movc: return { SpvOpSelect, SpirvOpClass::select_generic };
    case DxbcInstrTag::ult: return { SpvOpULessThan, SpirvOpClass::int_cmp };
    case DxbcInstrTag::ieq: return { SpvOpIEqual, SpirvOpClass::int_cmp };
    case DxbcInstrTag::ishl: return { SpvOpShiftLeftLogical, SpirvOpClass::int_common };
    default:
        return {};
    }
}

//--------------------------------------------------------------------------------------------------------------




struct DxbcComponentVarInfo {
    unsigned spvValueId : 24, spvStaticTypeId : 8;
};

// Local Value Numbering
//
// Haven't got to the hashtable/previous value reuse part yet.
// Think that could be nice to potentially reduce bitcasts, int<->bool conversions, and abs/neg.
// May not be worth doing it for much else.
//
// The more helpful part is associating dxbc vars with a value, so don't have to do
// a bunch of OpLoad/OpStore in the middle of a basic block.
struct LvnContext {
    DxbcComponentVarInfo dxbcVarInfo[256][4] = { }; // 4 components per reg. XXX: max temp regs is 4096

    SpvId CurrentTypeIdOfTempVar(uint writeMaskComp, const DxbcOperand& src) const
    {
        ASSERT(src.file == DxbcFile::temp);
        uint comp = src.srcSwizzle[writeMaskComp];
        return dxbcVarInfo[src.slotInFile][comp].spvStaticTypeId;
    }
};

struct ValueAndType {
    SpvId valueId, typeId;
};

static ValueAndType
GetCurrentValueNoAbsNeg(Module& m, Function& function, SpirvDynamicArray& code, LvnContext& lvn,
    uint writeMaskComp, const DxbcOperand& src, SpvId immediateTypeId = StaticSpvId_TypeGenInt32)
{
    ASSERT(immediateTypeId < (uint)StaticSpvId_End);

    const uint srcComponentIndex = src.srcSwizzle[writeMaskComp];

    SpvId valueId;
    SpvId typeId;

    if (src.file == DxbcFile::temp) {
        DxbcComponentVarInfo var = lvn.dxbcVarInfo[src.slotInFile][srcComponentIndex];
        ASSERT(var.spvValueId);
        valueId = var.spvValueId;
        typeId = var.spvStaticTypeId;
    }
    else if (src.file == DxbcFile::vThreadID) {
        valueId = m.Get_vThreadID_c_id(&function, srcComponentIndex);
        typeId = StaticSpvId_TypeGenInt32;
    }
    else if (src.file == DxbcFile::vThreadIDInGroupFlattened) {
        valueId = m.Get_vThreadIDInGroupFlattened_id(&function);
        typeId = StaticSpvId_TypeGenInt32;
    }
    else {
        ASSERT(src.file == DxbcFile::immediate);
        if (immediateTypeId == StaticSpvId_TypeGenInt32) {
            valueId = m.GetGIntConstantId(src.immediateValue.u[srcComponentIndex]);
            typeId = StaticSpvId_TypeGenInt32;
        }
        else {
            ASSERT(0); // TODO: flt constants
            return {};
        }
    }

    return { valueId, typeId };
}


static SpvId
GetSrcValueWithType(Module& m, Function& function, SpirvDynamicArray& code, LvnContext& lvn,
                    uint writeMaskComp, const DxbcOperand& src, SpvId desiredTypeId)
{
    ValueAndType const current = GetCurrentValueNoAbsNeg(m, function, code, lvn, writeMaskComp, src, desiredTypeId);
    SpvId typeId = current.typeId;
    SpvId valueId = current.valueId;

    if (typeId != desiredTypeId) {
        if (typeId == StaticSpvId_TypeBool) {
            if (desiredTypeId == StaticSpvId_TypeGenInt32) {
                SpvId a = m.GetGIntConstantId(uint32_t(-1));
                SpvId b = m.GetGIntConstantId(0);
                valueId = EmitSelect(m, code, desiredTypeId, valueId, a, b);
            }
            else {
                ASSERT(desiredTypeId == StaticSpvId_TypeFloat32);
                ASSERT(0); // TODO
            }
        }
        else if (desiredTypeId == StaticSpvId_TypeBool) {
            // can get here from movc
            if (typeId == StaticSpvId_TypeGenInt32) {
                valueId = EmitBinOp(m, code, SpvOpINotEqual, StaticSpvId_TypeBool,
                    valueId, m.GetGIntConstantId(0));
            }
            else {
                ASSERT(typeId == StaticSpvId_TypeFloat32);
                ASSERT(0); // TODO
            }
        }
        else {
            valueId = EmitBitcast(m, code, desiredTypeId, valueId);
        }
    }

    if (src.flags & DxbcOperandFlag_SrcAbs) {
        ASSERT(desiredTypeId == StaticSpvId_TypeFloat32);
        SpvId const srcValId = valueId;
        valueId = m.AllocId();
        /* 6 words for { dst = unary src... } */
        code.push_initlist({ SpvOpExtInst | 6 << 16, desiredTypeId, valueId, StaticSpvId_ExtInst_GLSL_std, GLSLstd450FAbs, srcValId });
    }
    if (src.flags & DxbcOperandFlag_SrcNeg) {
        ASSERT(desiredTypeId == StaticSpvId_TypeFloat32 || desiredTypeId == StaticSpvId_TypeGenInt32);
        SpvOp const negOp = (desiredTypeId == StaticSpvId_TypeFloat32) ? SpvOpFNegate : SpvOpSNegate;
        SpvId const srcValId = valueId;
        valueId = m.AllocId();
        code.push4(negOp | 4 << 16u, desiredTypeId, valueId, srcValId);
    }

    return valueId;
}


static bool
IsCurrentTypeBool(const LvnContext& lvn, uint writeCompIndex, const DxbcOperand& src)
{
    return src.file == DxbcFile::temp && lvn.CurrentTypeIdOfTempVar(writeCompIndex, src) == StaticSpvId_TypeBool;
}

/*
    Recursive codegen dxbc->spirv until { EOF/EndFunction, else, endif, endloop }.
    Returns said DXBC instruction, XXX: may want the instr before that, like if was ret or break
**/
static void
Codegen(Module& m, Function& function, SpirvDynamicArray& code,
        DxbcTextScanner *scanner, DxbcInstruction& dxbcInstr)
{
    LvnContext lvn;

    while (!DxbcText_ScanIsEof(scanner)) {    
        DxbcTextScanResult scanResult = DxbcText_ScanInstrInFuncBody(scanner, &dxbcInstr);
        if (scanResult != DxbcTextScanResult::Okay) {
            puts("bad dxbc text :(");
            return;
        }

        const SpirvOpInfo spvOpInfo = GetSpirvOpInfo(dxbcInstr.tag);

        if (dxbcInstr.tag == DxbcInstrTag::ret) {
            code.push(SpvOpReturn | 1u << 16);
            break;
        }
        else if (dxbcInstr.tag == DxbcInstrTag::store_uav_typed) {
            puts("TODO: store_uav_typed without assuming RWBuffer<uint>:register(u0)");
            SpvId imageId = m.AllocId();
            code.push4(SpvOpLoad | 4 << 16, m.uav_image_type_ids[0], imageId, m.ptr_uav_ids[0]);
            SpvId coordId = GetSrcValueWithType(m, function, code, lvn,
                0, dxbcInstr.operands[1], StaticSpvId_TypeGenInt32);
            SpvId texelValueId = GetSrcValueWithType(m, function, code, lvn,
                0, dxbcInstr.operands[2], StaticSpvId_TypeGenInt32);
            code.push4(SpvOpImageWrite | 4 << 16, imageId, coordId, texelValueId);
        }
        else if (dxbcInstr.tag == DxbcInstrTag::ld_uav_typed) {
            // very similar to "ld" (SRVs)
            puts("TODO: ld_uav_typed");
            const uint numDests = 1;
            const uint numSrcs = 2; // coord, uav
            const DxbcOperand *srcs = dxbcInstr.operands + numDests;
            const DxbcOperand& dst = dxbcInstr.operands[0];
            const uint writeMask = dst.dstWritemask;
            ASSERT(writeMask);
            for (uint writeCompIndex = 0; writeCompIndex < 4u; ++writeCompIndex) {
                if (!(writeMask & 1u << writeCompIndex)) {
                    continue;
                }
                lvn.dxbcVarInfo[uint(dst.slotInFile)][writeCompIndex] = { m.GetGIntConstantId(880 + writeCompIndex), StaticSpvId_TypeGenInt32 };
            }
        }
        else if (dxbcInstr.tag == DxbcInstrTag::imad) {
            // can also remove an int negation here by using SpvOpISub when applicable
            puts("TODO: imad");
            const uint numDests = 1;
            const uint numSrcs = 3; // a*b+c
            const DxbcOperand *srcs = dxbcInstr.operands + numDests;
            const DxbcOperand& dst = dxbcInstr.operands[0];
            const uint writeMask = dst.dstWritemask;
            ASSERT(writeMask);
            for (uint writeCompIndex = 0; writeCompIndex < 4u; ++writeCompIndex) {
                if (!(writeMask & 1u << writeCompIndex)) {
                    continue;
                }
                lvn.dxbcVarInfo[uint(dst.slotInFile)][writeCompIndex] = { m.GetGIntConstantId(770 + writeCompIndex), StaticSpvId_TypeGenInt32 };
            }
        }
        else if (dxbcInstr.tag == DxbcInstrTag::movc) {
            const DxbcOperand *srcs = dxbcInstr.operands + 1;
            const DxbcOperand& dst = dxbcInstr.operands[0];
            uint wm = dst.dstWritemask;
            ASSERT(wm);
            do {
                uint const writeCompIndex = bsf(wm);
                uint const k = (srcs[1].file != DxbcFile::immediate) ? 0 : 1; // src[k + 1] decides the dest type
                ValueAndType const src_k = GetCurrentValueNoAbsNeg(m, function, code, lvn, writeCompIndex, srcs[k+1]);
                SpvId srcValueIds[3];
                srcValueIds[0] = GetSrcValueWithType(m, function, code, lvn, writeCompIndex, srcs[0], StaticSpvId_TypeBool);
                srcValueIds[k+1] = src_k.valueId;
                uint const otherSrcIndex = (k ^ 1) + 1;
                srcValueIds[otherSrcIndex] = GetSrcValueWithType(m, function, code, lvn, writeCompIndex, srcs[otherSrcIndex], src_k.typeId);
                SpvId dstValueId = EmitSelect(m, code, src_k.typeId, srcValueIds[0], srcValueIds[1], srcValueIds[2]);
                lvn.dxbcVarInfo[uint(dst.slotInFile)][writeCompIndex] = { dstValueId, src_k.typeId };
            } while ((wm &= wm - 1) != 0);
        } else {
            const uint numDests = dxbcInstr.NumDstRegs();
            const uint numSrcs = dxbcInstr.NumSrcRegs();
            const DxbcOperand& dst = dxbcInstr.operands[0];
            ASSERT(numDests == 1);
            ASSERT(numSrcs == 2); // TODO: handle other stuff
            const uint writeMask = dst.dstWritemask;
            ASSERT(writeMask);
            for (uint writeCompIndex = 0; writeCompIndex < 4u; ++writeCompIndex) {
                if (!(writeMask & 1u << writeCompIndex)) {
                    continue;
                }

                const DxbcOperand *srcs = dxbcInstr.operands + numDests; // can mut for add ans shift stuff
                DxbcOperand aTmpSrcs[3]; // for add/sub order and flags.

                // ult, ieq can have diff dst type id
                SpvId srcTypeSpvId = spvOpInfo.IsFloatArithmeticOrCompare() ? StaticSpvId_TypeFloat32 : StaticSpvId_TypeGenInt32;
                SpvId dstTypeSpvId = spvOpInfo.DstComponentTypeId();

                SpvOp op = SpvOp(spvOpInfo.similarSpvOp);
                SpvId srcValueIds[2];

                /* pre src fetch special handling, some of this is the same for each comp and could be moved up */
                if (op == SpvOpIAdd || op == SpvOpFAdd) {
                    aTmpSrcs[0] = srcs[0];
                    aTmpSrcs[1] = srcs[1];
                    SpvOp const subOp = (op == SpvOpIAdd) ? SpvOpISub : SpvOpFSub;
                    if (aTmpSrcs[0].flags & DxbcOperandFlag_SrcNeg) {
                        op = subOp;
                        if (aTmpSrcs[1].flags & DxbcOperandFlag_SrcNeg) {
                            // dst = -a + -b :: -a - b
                            aTmpSrcs[1].flags &= ~DxbcOperandFlag_SrcNeg;
                        }
                        else {
                            // dst = -a + b :: b - a
                            aTmpSrcs[0].flags &= ~DxbcOperandFlag_SrcNeg;
                            DxbcOperand const tmp0 = aTmpSrcs[0]; // swap em
                            aTmpSrcs[0] = aTmpSrcs[1];
                            aTmpSrcs[1] = tmp0;
                        }
                        srcs = aTmpSrcs;
                    }
                    else if (aTmpSrcs[1].flags & DxbcOperandFlag_SrcNeg) {
                        // dst =  a + -b :: a - b
                        op = subOp;
                        aTmpSrcs[1].flags &= ~DxbcOperandFlag_SrcNeg;
                        srcs = aTmpSrcs;
                    }
                }
                else if (spvOpInfo.IsBitShift()) {
                    if (srcs[1].file == DxbcFile::immediate) {
                        aTmpSrcs[0] = srcs[0];
                        aTmpSrcs[1] = srcs[1];
                        aTmpSrcs[1].immediateValue.u[aTmpSrcs[1].srcSwizzle[writeCompIndex]] &= 31u;
                        srcs = aTmpSrcs;
                    }
                }
                else if (const SpvOp boolSpvOp = spvOpInfo.BoolLogicOpOfBitwiseIntOp()) { 
                    /* Prefer leaving stuff in bools if the next op can be done using bools: */
                    if (IsCurrentTypeBool(lvn, writeCompIndex, srcs[0]) && 
                        IsCurrentTypeBool(lvn, writeCompIndex, srcs[1])) {
                        
                        dstTypeSpvId = StaticSpvId_TypeBool;
                        srcTypeSpvId = StaticSpvId_TypeBool;
                        op = boolSpvOp;
                    }
                }

                /* fetch srcs: */
                for (uint srcIndex = 0; srcIndex < numSrcs; ++srcIndex) {
                    srcValueIds[srcIndex] = GetSrcValueWithType(m, function, code, lvn, writeCompIndex,
                                                                srcs[srcIndex], srcTypeSpvId);
                }

                /* post src fetch special handling: */
                if (spvOpInfo.IsBitShift() && srcs[1].file != DxbcFile::immediate) {
                    /* undefined in spirv if shift exceeds bitwidth of type, dxbc says low 5 bits are used (for uint32_t). */
                    srcValueIds[1] = EmitBinOp(m, code, SpvOpBitwiseAnd, StaticSpvId_TypeGenInt32, srcValueIds[1], m.GetGIntConstantId(31));
                }

                SpvId dstValueId = EmitBinOp(m, code, op, dstTypeSpvId,
                                             srcValueIds[0], srcValueIds[1]);
                ASSERT(dst.file == DxbcFile::temp); // TODO: graphics shaders have outputs
                ASSERT(uint(dst.slotInFile) < lengthof(lvn.dxbcVarInfo));
                lvn.dxbcVarInfo[uint(dst.slotInFile)][writeCompIndex] = { dstValueId, dstTypeSpvId };
            }
        }
    }
}

void DxbcTextToSpirvFile(const char *szDxbcText, const char *filename, SpvImageFormat uav0Format = SpvImageFormatUnknown) // okay if write-only
{
    DxbcTextScanner scanner = { szDxbcText };
    
    Module m;
    // assuming just a single func and basic block now...
    BasicBlock basicblock = {};
    basicblock.spvId = m.AllocId();
    Function fn = {};

    if (DxbcText_ScanHeader(&scanner, &m.dxbcHeaderInfo) != DxbcTextScanResult::Okay) {
        puts("bad dxbc header");
        return;
    }
    
    if (m.dxbcHeaderInfo.vThreadID_usedMask) {
        m.ptr_vThreadID_id = m.AllocId();
    }
    if (m.dxbcHeaderInfo.vThreadIDInGroupFlattened) {
        m.ptr_vThreadIDInGroupFlattened_id = m.AllocId();
    }
    if (m.dxbcHeaderInfo.uavDeclMask) {
        m.ptr_uav_ids[0] = m.AllocId();
        m.uav_image_type_ids[0] = m.AllocId(); // XXX: reuse
    }

    DxbcInstruction dxbcInstr;
    dxbcInstr.tag = DxbcInstrTag::ret;
    Codegen(m, fn, basicblock.code, &scanner, dxbcInstr);
    if (dxbcInstr.tag != DxbcInstrTag::ret) {
        puts("should end in ret");
        return;
    }

    Array<uint32_t> code;
    code.reserve(1024);

    // Section 0: Header ------------------------------------------------------------- 
    uint32_t* p = code.uninitialized_push_n(5);
    p[0] = 0x07230203; // magic
    p[1] = 1u << 16 | 3u << 8; // version 1.3; 1.4 makes OpEntryPoint more annoying and spirv-val will complain
    p[2] = 0; // generators magic number
    p[3] = 0; // bound, will assign this near the end
    p[4] = 0; // reserved for schema


    // Section 1: caps ------------------------------------------------------------- 
    static const SpvCapability spvCaps[] = {
        SpvCapabilityShader,
        SpvCapabilitySampledBuffer,
        SpvCapabilityImageBuffer,
        SpvCapabilityStorageImageWriteWithoutFormat
    };
    p = code.uninitialized_push_n(lengthof(spvCaps) * 2);
    for (SpvCapability cap : spvCaps) {
        p[0] = SpvOpCapability | 2 << 16;
        p[1] = cap;
        p += 2;
    }

    // Section 2: declare exts -------------------------------------------------------------
    // none

    // Section 3: import ext instrs -------------------------------------------------------------
    EmitOpExtInstrImport(code, StaticSpvId_ExtInst_GLSL_std, "GLSL.std.450"_view);

    // Section 4: memory model -------------------------------------------------------------
    code.push_initlist({ SpvOpMemoryModel | 3<<16, SpvAddressingModelLogical, SpvMemoryModelGLSL450 }); // use Vulkan memory model in 1.5?

    // Section 5: entry point --------------------------------------------------------------
    {
        const uint index = code.size();
        EmitOpEntryPoint(code, SpvExecutionModelGLCompute, StaticSpvId_EntryFunction, "main"_view);
        if (m.ptr_vThreadID_id) {
            code[index] += 1u << 16;
            code.push(m.ptr_vThreadID_id);
        }
        if (m.ptr_vThreadIDInGroupFlattened_id) {
            code[index] += 1u << 16;
            code.push(m.ptr_vThreadIDInGroupFlattened_id);
        }
    }
    // Section 6: execution modes: --------------------------------------------------------------------------
    const uint32_t workGroupSizeStuff[] = {
        StaticSpvId_EntryFunction, SpvExecutionModeLocalSize,
        (uint)m.dxbcHeaderInfo.workgroupSize.x, (uint)m.dxbcHeaderInfo.workgroupSize.y, (uint)m.dxbcHeaderInfo.workgroupSize.z
    };
    EmitOpList(code, SpvOpExecutionMode, { workGroupSizeStuff, lengthof(workGroupSizeStuff) });

#if 1
    // Section 7: debug --------------------------------------------------------------------------
    EmitOpName(code, StaticSpvId_EntryFunction, "main");
    if (m.ptr_vThreadID_id) {
        EmitOpName(code, m.ptr_vThreadID_id, "ptr_vThreadID");
        if (fn.vThreadID_xyx_id) {
            EmitOpName(code, fn.vThreadID_xyx_id, "vThreadID");
            char strbuf[] = "vThreadID_";
            for (int comp = 0; comp < 3; ++comp) {
                const SpvId id = fn.vThreadID_c_id[comp];
                if (id) {
                    strbuf[(sizeof strbuf) - 1] = 'x' + comp;
                    EmitOpName(code, id, strbuf);
                }
            }
        }
    }
    if (m.ptr_vThreadIDInGroupFlattened_id) {
        EmitOpName(code, m.ptr_vThreadIDInGroupFlattened_id, "ptr_vThreadIDInGroupFlattened");
        if (fn.vThreadIDInGroupFlattened_id) {
            EmitOpName(code, fn.vThreadIDInGroupFlattened_id, "vThreadIDInGroupFlattened");
        }
    }

    if (m.ptr_uav_ids[0]) {
        EmitOpName(code, m.ptr_uav_ids[0], "ptr_uav0");
    }
#endif

    // Section 8: annotations/decorations --------------------------------------------------------------------------
    if (m.ptr_vThreadID_id) {
        EmitDecorateBuiltin(code, m.ptr_vThreadID_id, SpvBuiltInGlobalInvocationId);
    }
    if (m.ptr_vThreadIDInGroupFlattened_id) {
        // multiple arrays, for less branches?
        EmitDecorateBuiltin(code, m.ptr_vThreadIDInGroupFlattened_id, SpvBuiltInLocalInvocationIndex);
    }
    if (m.ptr_uav_ids[0]) {
        EmitDecorateSetAndBinding(code, m.ptr_uav_ids[0], 0, 0);
    }

    // Section 9: types, constants, global-variables --------------------------------------------------------------------------
    code.push_initlist({ SpvOpTypeVoid | 2 << 16, StaticSpvId_TypeVoid });
    code.push_initlist({ SpvOpTypeFunction | 3 << 16, StaticSpvId_TypeVoidFunction, StaticSpvId_TypeVoid });

    code.push_initlist({ SpvOpTypeInt | 4 << 16, StaticSpvId_TypeGenInt32, 32, 0 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, StaticSpvId_TypeV2GenInt32, StaticSpvId_TypeGenInt32, 2 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, StaticSpvId_TypeV3GenInt32, StaticSpvId_TypeGenInt32, 3 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, StaticSpvId_TypeV4GenInt32, StaticSpvId_TypeGenInt32, 4 });
    
    code.push_initlist({ SpvOpTypeFloat | 3 << 16, StaticSpvId_TypeFloat32, 32 });
    //code.push_initlist({ SpvOpTypeVector | 4 << 16, StaticSpvId_TypeV2Float32, StaticSpvId_TypeFloat32, 2 });
    //code.push_initlist({ SpvOpTypeVector | 4 << 16, StaticSpvId_TypeV3Float32, StaticSpvId_TypeFloat32, 3 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, StaticSpvId_TypeV4Float32, StaticSpvId_TypeFloat32, 4 });

    code.push_initlist({ SpvOpTypeBool | 2 << 16, StaticSpvId_TypeBool });

    EmitScalarConstants(code, m.gint32Constants, StaticSpvId_TypeGenInt32);

    if (m.ptr_vThreadID_id) {
        const SpvStorageClass storageClass = SpvStorageClassInput;
        const SpvId ptr_input_v3gint_type_id = m.AllocId();
        code.push_initlist({ SpvOpTypePointer | 4 << 16, ptr_input_v3gint_type_id, uint32_t(storageClass), StaticSpvId_TypeV3GenInt32 });
        code.push_initlist({ SpvOpVariable | 4 << 16, ptr_input_v3gint_type_id, m.ptr_vThreadID_id, uint32_t(storageClass) });
    }
    if (m.ptr_vThreadIDInGroupFlattened_id) {
        const SpvStorageClass storageClass = SpvStorageClassInput;
        const SpvId ptr_input_gint_type_id = m.AllocId(); // misc hash table
        code.push_initlist({ SpvOpTypePointer | 4 << 16, ptr_input_gint_type_id, uint32_t(storageClass), StaticSpvId_TypeGenInt32 });
        code.push_initlist({ SpvOpVariable | 4 << 16, ptr_input_gint_type_id, m.ptr_vThreadIDInGroupFlattened_id, uint32_t(storageClass) });
    }
    if (m.ptr_uav_ids[0]) {
        const SpvStorageClass storageClass = SpvStorageClassUniformConstant;
        SpvId const uavImageTypeId = m.uav_image_type_ids[0];
        ASSERT(uavImageTypeId);
        code.push_initlist({
            SpvOpTypeImage | 9 << 16, m.uav_image_type_ids[0], StaticSpvId_TypeGenInt32,
            SpvDimBuffer, 0, 0, // dim, "depth", arrayed
            0, 2, uint32_t(uav0Format), // MS, "sampled", SpvImageFormat*
        });
        const SpvId ptr_type_id = m.AllocId();
        code.push_initlist({ SpvOpTypePointer | 4 << 16, ptr_type_id, uint32_t(storageClass), uavImageTypeId });
        code.push_initlist({ SpvOpVariable | 4 << 16, ptr_type_id, m.ptr_uav_ids[0], uint32_t(storageClass) });
    }

    // section 10: function decls -----------------------------------------------------
    // none

    // section 11: functions defs -----------------------------------------------------
    code.push_initlist({ SpvOpFunction | 5 << 16, StaticSpvId_TypeVoid, StaticSpvId_EntryFunction, SpvFunctionControlMaskNone, StaticSpvId_TypeVoidFunction });
    
    //this is the function's entry basic block:
    code.push_initlist({SpvOpLabel | 2<<16, basicblock.spvId});
    if (fn.vThreadID_xyx_id) {
        code.push4(SpvOpLoad | 4 << 16, StaticSpvId_TypeV3GenInt32, fn.vThreadID_xyx_id, m.ptr_vThreadID_id);
        for (uint comp = 0; comp < 3; ++comp) {
            const SpvId id = fn.vThreadID_c_id[comp];
            if (id) {
                code.push_initlist({ SpvOpCompositeExtract | 5 << 16, StaticSpvId_TypeGenInt32, id, fn.vThreadID_xyx_id, comp });
            }
        }
    }
    if (fn.vThreadIDInGroupFlattened_id) {
        code.push4(SpvOpLoad | 4 << 16, StaticSpvId_TypeGenInt32, fn.vThreadIDInGroupFlattened_id, m.ptr_vThreadIDInGroupFlattened_id);
    }
    code.push_n(basicblock.code.data(), basicblock.code.size()); // should push a return too
    code.push(SpvOpFunctionEnd | 1 << 16);

    code[3] = m.GetBound();

    {
        printf("\nwriting spirv to [file]=[%s]\n", filename);
        FILE *fp = fopen(filename, "wb");
        if (fp) {
            const size_t nbytes = code.size() * sizeof(uint32_t);
            if (fwrite(code.data(), 1, code.size() * sizeof(uint32_t), fp) != nbytes) {
                perror("fwrite");
            }
            fclose(fp);
        }
        else {
            perror("fopen");
        }
    }

#if 0
    {
        auto const GetValue = [](uint x) -> uint {
	        // All these bools can stay as bools in spirv...
	        bool cond0 = x < 32u;
	        bool cond1 = (x & 4) == 0;
	        bool orResult = cond0 || cond1;
	        uint val = orResult ? 1000 : 2000;
	        val += x;
	        // ...but also test using bools as {0, -1} int values work:
	        val ^= -int((x & 2) == 0);
            return val;
        };

        puts("\n\n  running the spirv...\n");
        if (SpirvRunner *runner = NewSpirvRunner()) {
            const uint32_t *data = RunSimpleCompute(runner, code.data(), code.size() * sizeof(uint32_t));

            bool okay = true;
            for (uint x = 0; x < 64; ++x) {
                uint32_t const got = data[x];
                uint32_t const expected = GetValue(x);
                if (got != expected) {
                    printf("x=%d, got=0x%X, expected=0x%X\n", x, got, expected);
                    okay = false;
                }
            }

            DeleteSpirvRunner(runner);
            puts(okay ? "\n\nhooray, got expected data" : "\n\ngot wrong values :(");
        }
        else {

        }
    }
#endif
}


/* Some interseting tools:

%VULKAN_SDK% = C:\VulkanSDK\1.2.148.1

C:\VulkanSDK\1.2.148.1\Bin\spirv-val.exe generated.spv
C:\VulkanSDK\1.2.148.1\Bin\spirv-dis.exe generated.spv

C:\VulkanSDK\1.2.148.1\Bin\dxc.exe -spirv some_file.hlsl -T cs_5_0 -E main -O3
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\fxc.exe" some_file.hlsl -E main -T cs_5_0 -O3

set PATH=%PATH%;C:\VulkanSDK\1.2.176.1\Bin

**/
int main(void)
{
#if 1
    static const char LogicalOrDxbcText[] = R"(cs_5_0
dcl_globalFlags refactoringAllowed
dcl_uav_typed_buffer (uint,uint,uint,uint) u0
dcl_input vThreadID.x
dcl_temps 1
dcl_thread_group 64, 1, 1
ult r0.x, vThreadID.x, l(32)
and r0.yz, vThreadID.xxxx, l(0, 4, 2, 0)
ieq r0.yz, r0.yyzy, l(0, 0, 0, 0)
or r0.x, r0.y, r0.x
movc r0.x, r0.x, l(1000), l(2000)
iadd r0.x, r0.x, vThreadID.x
xor r0.x, r0.z, r0.x
store_uav_typed u0.xyzw, vThreadID.xxxx, r0.xxxx
ret
)";

    DxbcTextToSpirvFile(LogicalOrDxbcText, "LogicalOr.spv");
#endif

#if 1
    static const char AddFiestaDxbcText[] = R"(cs_5_0
dcl_globalFlags refactoringAllowed
dcl_uav_typed_buffer (uint,uint,uint,uint) u0
dcl_input vThreadIDInGroupFlattened
dcl_input vThreadID.x
dcl_temps 2
dcl_thread_group 4, 4, 4
ld_uav_typed_indexable(buffer)(uint,uint,uint,uint) r0.xyzw, vThreadIDInGroupFlattened.xxxx, u0.xyzw
iadd r1.x, -r0.w, -vThreadIDInGroupFlattened.x
iadd r1.w, r1.x, -vThreadID.x
iadd r1.x, r0.y, r0.x
iadd r1.yz, -r0.zzzz, r0.yywy
store_uav_typed u0.xyzw, vThreadIDInGroupFlattened.xxxx, r1.xyzw
ret
)";

    DxbcTextToSpirvFile(AddFiestaDxbcText, "AddInts.spv", SpvImageFormatRgba32ui);
#endif

#if 1
    static const char FltDxbcText[] = R"(cs_5_0
dcl_globalFlags refactoringAllowed
dcl_uav_typed_buffer (uint,uint,uint,uint) u0
dcl_input vThreadIDInGroupFlattened
dcl_input vThreadID.x
dcl_temps 2
dcl_thread_group 4, 4, 4
ld_uav_typed_indexable(buffer)(uint,uint,uint,uint) r0.xyzw, vThreadIDInGroupFlattened.xxxx, u0.xyzw
add r0.y, -|r0.z|, r0.y
ret
)";

    DxbcTextToSpirvFile(FltDxbcText, "Flt.spv");
#endif
    return 0;
}