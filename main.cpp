/*

    GetSrcValueWithType() and SimpleCodegenBasicBlock() are the interesting functions.

**/

#include <stdio.h>

#include "common.h"

#include <vector>

#include "DxbcTextScanner.h"

#include "Array.h"

// VULKAN_SDK=C:\VulkanSDK\1.2.148.1
// %VULKAN_SDK%\Include\spirv-headers

#include <spirv.h>
#include <GLSL.std.450.h>

#define MAIN_U32 0x6E00696Du

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
    FixedSpvId_ExtInst_GLSL_std = 1,

    FixedSpvId_EntryFunction = 2,
    FixedSpvId_TypeVoid = 3,
    FixedSpvId_TypeVoidFunction = 4,
    
    FixedSpvId_TypeBool = 8, // handy to start this at a multiple of 4?
    FixedSpvId_TypeV2Bool,
    FixedSpvId_TypeV3Bool,
    FixedSpvId_TypeV4Bool,
    FixedSpvId_TypeGenInt32,
    FixedSpvId_TypeV2GenInt32,
    FixedSpvId_TypeV3GenInt32,
    FixedSpvId_TypeV4GenInt32,
    FixedSpvId_TypeFloat32,
    FixedSpvId_TypeV2Float32,
    FixedSpvId_TypeV3Float32,
    FixedSpvId_TypeV4Float32,

    // might only need "signed" int32 for the sampled-type for OpTypeImage?

    FixedSpvId_End
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
};


struct Module {
    DxbcHeaderInfo dxbcHeaderInfo;

    ConstantsMapScaler32 gint32Constants;

    //bound = idinfo.size();
    // std::vector<SpirvIdInfo> idInfo;
    SpvId _bound = FixedSpvId_End;

    SpvId ptr_vThreadID_id = 0;
    SpvId ptr_uav_ids[64] = { };
    SpvId uav_image_type_ids[64] = { };

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
            return FixedSpvId_TypeBool;
        case SpirvOpClass::int_common:
            return FixedSpvId_TypeGenInt32;
        case SpirvOpClass::float_common:
            return FixedSpvId_TypeFloat32;
        default:
            return 0;
        }
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
    case DxbcInstrTag::movc: return { SpvOpSelect, SpirvOpClass::select_generic };
    case DxbcInstrTag::ult: return { SpvOpULessThan, SpirvOpClass::int_cmp };
    case DxbcInstrTag::ieq: return { SpvOpIEqual, SpirvOpClass::int_cmp };
    default:
        return {};
    }
}

//--------------------------------------------------------------------------------------------------------------




struct DxbcComponentVarInfo {
    unsigned spvValueId : 24, spvFixedTypeId : 8;
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

    SpvId TypeIdOfCurrentValue(uint writeMaskComp, const DxbcOperand& src) const
    {
        ASSERT(src.file == DxbcFile::temp);
        uint comp = src.comps.srcSwizzle[writeMaskComp];
        return dxbcVarInfo[src.slotInFile][comp].spvFixedTypeId;
    }
};

static SpvId
GetSrcValueWithType(Module& m, Function& function, BasicBlock& bb, LvnContext& lvn,
                    uint writeMaskComp, const DxbcOperand& src, SpvId desiredTypeId)
{
    ASSERT(desiredTypeId < (uint)FixedSpvId_End);

    const uint srcComponentIndex = src.comps.srcSwizzle[writeMaskComp];

    SpvId valueId;
    SpvId typeId;

    if (src.file == DxbcFile::temp) {
        DxbcComponentVarInfo var = lvn.dxbcVarInfo[src.slotInFile][srcComponentIndex];
        ASSERT(var.spvValueId);
        valueId = var.spvValueId;
        typeId = var.spvFixedTypeId;
    }
    else if (src.file == DxbcFile::vThreadID) {
        valueId = m.Get_vThreadID_c_id(&function, srcComponentIndex);
        typeId = FixedSpvId_TypeGenInt32;
    }
    else {
        ASSERT(src.file == DxbcFile::immediate);
        if (desiredTypeId == FixedSpvId_TypeGenInt32) {
            valueId = m.GetGIntConstantId(src.immediateValue.u[srcComponentIndex]);
            typeId = FixedSpvId_TypeGenInt32;
        }
        else {
            ASSERT(0); // TODO: flt constants
            return 0;
        }
    }

    if (typeId != desiredTypeId) {
        if (typeId == FixedSpvId_TypeBool) {
            if (desiredTypeId == FixedSpvId_TypeGenInt32) {
                SpvId a = m.GetGIntConstantId(uint32_t(-1));
                SpvId b = m.GetGIntConstantId(0);
                valueId = EmitSelect(m, bb.code, desiredTypeId, valueId, a, b);
            }
            else {
                ASSERT(desiredTypeId == FixedSpvId_TypeFloat32);
                ASSERT(0); // TODO
            }
        }
        else if (desiredTypeId == FixedSpvId_TypeBool) {
            // can get here from movc
            if (typeId == FixedSpvId_TypeGenInt32) {
                valueId = EmitBinOp(m, bb.code, SpvOpINotEqual, FixedSpvId_TypeBool,
                    valueId, m.GetGIntConstantId(0));
            }
            else {
                ASSERT(typeId == FixedSpvId_TypeFloat32);
                ASSERT(0); // TODO
            }
        }
        else {
            valueId = EmitBitcast(m, bb.code, desiredTypeId, valueId);
        }
    }

    return valueId;
}


/*
    Codegen dxbc->spirv until something like a ret or if/else/endif is encountered.

    Fills in the dxbc instr that ends the basic block.
**/
static void
SimpleCodegenBasicBlock(Module& m, Function& function, BasicBlock& bb, bool bEmitOpLabel,
                        DxbcTextScanner *scanner, DxbcInstruction& dxbcInstr)
{
    /* 
     * It is desired to load vThreadID, descriptors, cbuffer values and similar at the top of a function's entry
     * BasicBlock. If this is it's entry basic block, don't emit the label so the uniform input loads can be inserted
     * easier when assembling the final spirv code.
     */
    if (bEmitOpLabel) {
        bb.code.push2(SpvOpLabel | 2 << 16, bb.spvId);
    }

    LvnContext lvn;

    while (!DxbcText_ScanIsEof(scanner)) {    
        DxbcTextScanResult scanResult = DxbcText_ScanInstrInFuncBody(scanner, &dxbcInstr);
        if (scanResult != DxbcTextScanResult::Okay) {
            puts("bad dxbc text :(");
            return;
        }

        const SpirvOpInfo spvOpInfo = GetSpirvOpInfo(dxbcInstr.tag);

        if (dxbcInstr.tag == DxbcInstrTag::ret) {
            bb.code.push(SpvOpReturn | 1u << 16);
            break;
        }
        else if (dxbcInstr.tag == DxbcInstrTag::store_uav_typed) {
            puts("TODO: store_uav_typed without assuming RWBuffer<uint>:register(u0)");
            SpvId imageId = m.AllocId();
            bb.code.push4(SpvOpLoad | 4 << 16, m.uav_image_type_ids[0], imageId, m.ptr_uav_ids[0]);
            SpvId coordId = GetSrcValueWithType(m, function, bb, lvn,
                0, dxbcInstr.operands[1], FixedSpvId_TypeGenInt32);
            SpvId texelValueId = GetSrcValueWithType(m, function, bb, lvn,
                0, dxbcInstr.operands[2], FixedSpvId_TypeGenInt32);
            bb.code.push4(SpvOpImageWrite | 4 << 16, imageId, coordId, texelValueId);
        }
        else if (dxbcInstr.tag == DxbcInstrTag::movc) {
            puts("todo: movc, try avoiding bitcasts for floats");
            const DxbcOperand *srcs = dxbcInstr.operands + 1;
            const DxbcOperand& dst = dxbcInstr.operands[0];
            for (uint writeCompIndex = 0; writeCompIndex < 4u; ++writeCompIndex) {
                if (!(dst.comps.dstWritemask & 1u << writeCompIndex)) {
                    continue;
                }
                const SpvId dstTypeSpvId = FixedSpvId_TypeGenInt32;
                SpvId srcValueIds[3];
                for (uint srcIndex = 0; srcIndex < lengthof(srcValueIds); ++srcIndex) {
                    srcValueIds[srcIndex] = GetSrcValueWithType(m, function, bb, lvn, writeCompIndex,
                        srcs[srcIndex], srcIndex == 0 ? SpvId(FixedSpvId_TypeBool) : dstTypeSpvId);
                }
                SpvId dstValueId = EmitSelect(m, bb.code, dstTypeSpvId,
                                                srcValueIds[0], srcValueIds[1], srcValueIds[2]);
                lvn.dxbcVarInfo[uint(dst.slotInFile)][writeCompIndex] = { dstValueId, dstTypeSpvId };
            }
        } else {
            const uint numDests = dxbcInstr.NumDstRegs();
            const uint numSrcs = dxbcInstr.NumSrcRegs();
            const DxbcOperand *srcs = dxbcInstr.operands + numDests;
            const DxbcOperand& dst = dxbcInstr.operands[0];
            ASSERT(numDests == 1); // TODO: handle other stuff
            ASSERT(numSrcs == 2); // TODO: handle other stuff
            const uint writeMask = dst.comps.dstWritemask;
            ASSERT(writeMask);
            for (uint writeCompIndex = 0; writeCompIndex < 4u; ++writeCompIndex) {
                if (!(writeMask & 1u << writeCompIndex)) {
                    continue;
                }

                // ult, ieq can have diff dst type id
                SpvId srcTypeSpvId = spvOpInfo.IsFloatArithmeticOrCompare() ? FixedSpvId_TypeFloat32 : FixedSpvId_TypeGenInt32;
                SpvId dstTypeSpvId = spvOpInfo.DstComponentTypeId();

                SpvOp op = SpvOp(spvOpInfo.similarSpvOp);
                SpvId srcValueIds[2];

                /* Prefer leaving stuff in bools if the next op can be done using bools: */
                const SpvOp boolSpvOp = spvOpInfo.BoolLogicOpOfBitwiseIntOp();
                if (boolSpvOp && 
                    srcs[0].file == DxbcFile::temp && 
                    srcs[1].file == DxbcFile::temp &&
                    lvn.TypeIdOfCurrentValue(writeCompIndex, srcs[0]) == FixedSpvId_TypeBool && 
                    lvn.TypeIdOfCurrentValue(writeCompIndex, srcs[1]) == FixedSpvId_TypeBool) { 
                    dstTypeSpvId = FixedSpvId_TypeBool;
                    srcTypeSpvId = FixedSpvId_TypeBool;
                    op = boolSpvOp;
                }

                for (uint srcIndex = 0; srcIndex < numSrcs; ++srcIndex) {
                    srcValueIds[srcIndex] = GetSrcValueWithType(m, function, bb, lvn, writeCompIndex,
                                                                srcs[srcIndex], srcTypeSpvId);
                }

                SpvId dstValueId = EmitBinOp(m, bb.code, op, dstTypeSpvId,
                                             srcValueIds[0], srcValueIds[1]);
                ASSERT(dst.file == DxbcFile::temp); // TODO: graphics shaders have outputs
                ASSERT(uint(dst.slotInFile) < lengthof(lvn.dxbcVarInfo));
                lvn.dxbcVarInfo[uint(dst.slotInFile)][writeCompIndex] = { dstValueId, dstTypeSpvId };
            }
        }
    }
}

void DxbcTextToSpirvFile(const char *szDxbcText, const char *filename)
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
    if (m.dxbcHeaderInfo.uavDeclMask) {
        m.ptr_uav_ids[0] = m.AllocId();
        m.uav_image_type_ids[0] = m.AllocId(); // XXX: reuse
    }

    DxbcInstruction dxbcInstr;
    dxbcInstr.tag = DxbcInstrTag::ret;
    SimpleCodegenBasicBlock(m, fn, basicblock, false, &scanner, dxbcInstr);
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
    EmitOpExtInstrImport(code, FixedSpvId_ExtInst_GLSL_std, "GLSL.std.450"_view);

    // Section 4: memory model -------------------------------------------------------------
    code.push_initlist({ SpvOpMemoryModel | 3<<16, SpvAddressingModelLogical, SpvMemoryModelGLSL450 }); // use Vulkan memory model in 1.5?

    // Section 5: entry point --------------------------------------------------------------
    {
        const uint index = code.size();
        EmitOpEntryPoint(code, SpvExecutionModelGLCompute, FixedSpvId_EntryFunction, "main"_view);
        if (m.ptr_vThreadID_id) {
            code[index] += 1u << 16;
            code.push(m.ptr_vThreadID_id);
        }
    }
    // Section 6: execution modes: --------------------------------------------------------------------------
    const uint32_t workGroupSizeStuff[] = {
        FixedSpvId_EntryFunction, SpvExecutionModeLocalSize,
        (uint)m.dxbcHeaderInfo.workgroupSize.x, (uint)m.dxbcHeaderInfo.workgroupSize.y, (uint)m.dxbcHeaderInfo.workgroupSize.z
    };
    EmitOpList(code, SpvOpExecutionMode, { workGroupSizeStuff, lengthof(workGroupSizeStuff) });

#if 1
    // Section 7: debug --------------------------------------------------------------------------
    EmitOpName(code, FixedSpvId_EntryFunction, "main");
    if (m.ptr_vThreadID_id) {
        EmitOpName(code, m.ptr_vThreadID_id, "ptr_vThreadID");
        if (fn.vThreadID_xyx_id) {
            EmitOpName(code, fn.vThreadID_xyx_id, "vThreadID_xyz");
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

    if (m.ptr_uav_ids[0]) {
        EmitOpName(code, m.ptr_uav_ids[0], "ptr_uav0");
    }
#endif

    // Section 8: annotations/decorations --------------------------------------------------------------------------
    if (m.ptr_vThreadID_id) {
        EmitDecorateBuiltin(code, m.ptr_vThreadID_id, SpvBuiltInGlobalInvocationId);
    }
    if (m.ptr_uav_ids[0]) {
        EmitDecorateSetAndBinding(code, m.ptr_uav_ids[0], 0, 0);
    }

    // Section 9: types, constants, global-variables --------------------------------------------------------------------------
    code.push_initlist({ SpvOpTypeVoid | 2 << 16, FixedSpvId_TypeVoid });
    code.push_initlist({ SpvOpTypeFunction | 3 << 16, FixedSpvId_TypeVoidFunction, FixedSpvId_TypeVoid });

    code.push_initlist({ SpvOpTypeInt | 4 << 16, FixedSpvId_TypeGenInt32, 32, 0 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, FixedSpvId_TypeV2GenInt32, FixedSpvId_TypeGenInt32, 2 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, FixedSpvId_TypeV3GenInt32, FixedSpvId_TypeGenInt32, 3 });
    code.push_initlist({ SpvOpTypeVector | 4 << 16, FixedSpvId_TypeV4GenInt32, FixedSpvId_TypeGenInt32, 4 });
    
    code.push_initlist({ SpvOpTypeBool | 2 << 16, FixedSpvId_TypeBool });

    EmitScalarConstants(code, m.gint32Constants, FixedSpvId_TypeGenInt32);

    if (m.ptr_vThreadID_id) {
        const SpvStorageClass storageClass = SpvStorageClassInput;
        const SpvId ptr_input_v3gint_type_id = m.AllocId();
        code.push_initlist({ SpvOpTypePointer | 4 << 16, ptr_input_v3gint_type_id, uint32_t(storageClass), FixedSpvId_TypeV3GenInt32 });
        code.push_initlist({ SpvOpVariable | 4 << 16, ptr_input_v3gint_type_id, m.ptr_vThreadID_id, uint32_t(storageClass) });
    }
    if (m.ptr_uav_ids[0]) {
        const SpvStorageClass storageClass = SpvStorageClassUniformConstant;
        SpvId const uavImageTypeId = m.uav_image_type_ids[0];
        ASSERT(uavImageTypeId);
        code.push_initlist({
            SpvOpTypeImage | 9 << 16, m.uav_image_type_ids[0], FixedSpvId_TypeGenInt32,
            SpvDimBuffer, 0, 0, // dim, "depth", arrayed
            0, 2, SpvImageFormatUnknown, // MS, "sampled", actual VkImageViewFormat
            // need capability kernal for operand[9] of OpTypeImage, so can't explicitly say SpvAccessQualifierWriteOnly, but maybe don't need.
        });
        const SpvId ptr_type_id = m.AllocId();
        code.push_initlist({ SpvOpTypePointer | 4 << 16, ptr_type_id, uint32_t(storageClass), uavImageTypeId });
        code.push_initlist({ SpvOpVariable | 4 << 16, ptr_type_id, m.ptr_uav_ids[0], uint32_t(storageClass) });
    }

    // section 10: function decls -----------------------------------------------------
    // none

    // section 11: functions defs -----------------------------------------------------
    code.push_initlist({ SpvOpFunction | 5 << 16, FixedSpvId_TypeVoid, FixedSpvId_EntryFunction, SpvFunctionControlMaskNone, FixedSpvId_TypeVoidFunction });
    
    //this is the function's entry basic block:
    code.push_initlist({SpvOpLabel | 2<<16, basicblock.spvId});
    if (fn.vThreadID_xyx_id) {
        code.push4(SpvOpLoad | 4 << 16, FixedSpvId_TypeV3GenInt32, fn.vThreadID_xyx_id, m.ptr_vThreadID_id);
        for (uint comp = 0; comp < 3; ++comp) {
            const SpvId id = fn.vThreadID_c_id[comp];
            if (id) {
                code.push_initlist({ SpvOpCompositeExtract | 5 << 16, FixedSpvId_TypeGenInt32, id, fn.vThreadID_xyx_id, comp });
            }
        }
    }
    code.push_n(basicblock.code.data(), basicblock.code.size()); // should push a return too
    code.push(SpvOpFunctionEnd | 1 << 16);

    code[3] = m.GetBound();

    {
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
}


/* Some interseting tools:

%VULKAN_SDK% = C:\VulkanSDK\1.2.148.1

C:\VulkanSDK\1.2.148.1\Bin\spirv-val.exe generated.spv
C:\VulkanSDK\1.2.148.1\Bin\spirv-dis.exe generated.spv

C:\VulkanSDK\1.2.148.1\Bin\dxc.exe -spirv some_file.hlsl -T cs_5_0 -E main -O3
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\fxc.exe" some_file.hlsl -E main -T cs_5_0 -O3

**/
int main(void)
{
    static const char DxbcText[] = R"(cs_5_0
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

    DxbcTextToSpirvFile(DxbcText, "output.spv");

    return 0;
}