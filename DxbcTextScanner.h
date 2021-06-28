#pragma once

#include <stdint.h>

enum {
    DXBC_GLOBAL_FLAG_REFACTORING_ALLOWED = 1u << 0
};

struct DxbcHeaderInfo {
    uint8_t globalFlags; // dcl_globalFlags refactoringAllowed
    uint8_t vThreadID_usedMask;
    bool vThreadIDInGroupFlattened;
    uint16_t numTemps; // up to 4096 32bitx4 temps, r0 through rN-1
    struct { int x, y, z; } workgroupSize;

    enum : uint64_t { uavDeclMask = 1 }; // TODO
};

struct DxbcSourceSwizzle {
    uint8_t bits;

    uint operator[](uint i) const { return (bits >> (i * 2)) & 0x3; }

    DxbcSourceSwizzle() = default;

    constexpr DxbcSourceSwizzle(uint x, uint y, uint z, uint w)
        : bits(w<<6 | z<<4 | y<<2 | x)
    {

    }
};

enum class DxbcFile : uint8_t {
    immediate,
    temp,
    uav,
    vThreadID,
    vThreadIDInGroupFlattened,
};

enum class DxbcInstrClass : uint8_t {
    misc_in_function_body,
    misc_outside_function_body,
    dst0_assign_unary_op,
    dst0_assign_binary_op,
    dst0_assign_tri_op,
};

enum class DxbcInstrTag : uint8_t {
    dcl_globalFlags,
    dcl_uav_typed_buffer,
    dcl_input,
    dcl_temps,
    dcl_thread_group,
    ret,
    ult,
    ieq,
    movc,
    ior,
    iand,
    ixor,
    inot,
    ishl,
    iadd,
    imad,
    add, // flt
    store_uav_typed,
    ld_uav_typed,
};

enum {
    DxbcOperandFlag_SrcAbs = 1 << 0,
    DxbcOperandFlag_SrcNeg = 1 << 1,
    DxbcOperandFlag_DstSat = 1 << 2
};

struct DxbcOperand {
    DxbcFile file;
    uint8_t flags;
    uint8_t dstWritemask;
    DxbcSourceSwizzle srcSwizzle;
    int16_t slotInFile;
    union {
        uint32_t u[4];
        float f[4];
    } immediateValue; // maybe shouldn't be here, the slot could be an index into an array.
};

struct DxbcInstruction {
    DxbcInstrTag tag;
    DxbcInstrClass instrClass;
    DxbcOperand operands[4];

    uint NumDstRegs() const
    {
        switch (instrClass) {
        case DxbcInstrClass::dst0_assign_unary_op:
        case DxbcInstrClass::dst0_assign_binary_op:
        case DxbcInstrClass::dst0_assign_tri_op:
            return 1;
        default: return 0;
        }
    }

    uint NumSrcRegs() const
    {
        switch (instrClass) {
        case DxbcInstrClass::dst0_assign_unary_op: return 1;
        case DxbcInstrClass::dst0_assign_binary_op: return 2;
        case DxbcInstrClass::dst0_assign_tri_op: return 3;
        default: return 0;
        }
    }
};

enum class DxbcTextScanResult {
    Okay,
    Eof,
    ExpectedAlpha,
    UnknownInstruction,
    Other,
};

struct DxbcTextScanner {
    const char *pSrc; // must be 0-terminated

    // line number

    // Split up multi-dest macro functions, have state here for those?
    // Then would be less like dxbc.
};

// Collects info about everything until the first function definition:
DxbcTextScanResult
DxbcText_ScanHeader(DxbcTextScanner *scanner, DxbcHeaderInfo *headerInfo);

DxbcTextScanResult
DxbcText_ScanInstrInFuncBody(DxbcTextScanner *scanner, DxbcInstruction *instr);

// Name this better? may advance in the string:
bool
DxbcText_ScanIsEof(DxbcTextScanner *scanner);

