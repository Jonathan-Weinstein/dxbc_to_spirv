/*
    This was supposed to bed quick and dirty.

    Could cleanup a bit.

    If a bad input string is detected, will crash with a message.
    XXX: Some of those are asserts?, but should be in non-debug builds too.

    Should add line number info to messages, and should handle at least // comments.
*/
#include "common.h"

#include "DxbcTextScanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ByteView {
    const char *pbegin;
    const char *pend;
    uint Length() const { return uint(pend - pbegin); }

    const char *begin() const { return pbegin; }
    const char *end() const { return pend; }
};

static bool StartsWith(ByteView str, char_view pfx)
{
    return str.Length() >= pfx.length && memcmp(str.pbegin, pfx.ptr, pfx.length) == 0;
}


static bool EqualStrZ(ByteView view, const char *sz)
{
    const uint len = view.Length();
    return strncmp(view.pbegin, sz, len) == 0 && sz[len] == 0;
}

static void
Println(FILE *fp, const char *s, const ByteView view)
{
    fputs(s, fp);
    fwrite(view.pbegin, 1, view.Length(), stdout);
    fputc('\n', fp);
}


struct DxbcInstrStringInfo {
    const char *name;
    // uint8_t nameLength;
    DxbcInstrTag instrTag;
    DxbcInstrClass instrClass;
};

// todo: use hashmap
static const DxbcInstrStringInfo StringTable[] = {
    { "dcl_globalFlags",        DxbcInstrTag::dcl_globalFlags,       DxbcInstrClass::misc_outside_function_body },
    { "dcl_uav_typed_buffer",   DxbcInstrTag::dcl_uav_typed_buffer,  DxbcInstrClass::misc_outside_function_body },
    { "dcl_input",              DxbcInstrTag::dcl_input,             DxbcInstrClass::misc_outside_function_body },
    { "dcl_temps",              DxbcInstrTag::dcl_temps,             DxbcInstrClass::misc_outside_function_body },
    { "dcl_thread_group",       DxbcInstrTag::dcl_thread_group,      DxbcInstrClass::misc_outside_function_body },
    // ...
    { "ret",                    DxbcInstrTag::ret,                   DxbcInstrClass::misc_in_function_body },
    { "endif",                  DxbcInstrTag::endif,                 DxbcInstrClass::misc_in_function_body },
    { "else",                   DxbcInstrTag::_else,                 DxbcInstrClass::misc_in_function_body },
    { "ld_uav_typed",           DxbcInstrTag::ld_uav_typed,          DxbcInstrClass::misc_in_function_body },
    { "ld_uav_typed_indexable", DxbcInstrTag::ld_uav_typed,          DxbcInstrClass::misc_in_function_body },
    // ...
    { "mov",                    DxbcInstrTag::mov,                   DxbcInstrClass::dst0_assign_unary_op },
    { "not",/*no 'i' in str*/   DxbcInstrTag::inot,                  DxbcInstrClass::dst0_assign_unary_op },
    // ...
    { "and", /*no 'i' in str*/  DxbcInstrTag::iand,                  DxbcInstrClass::dst0_assign_binary_op },
    { "xor", /*no 'i' in str*/  DxbcInstrTag::ixor,                  DxbcInstrClass::dst0_assign_binary_op },
    { "or", /*no 'i' in str*/   DxbcInstrTag::ior,                   DxbcInstrClass::dst0_assign_binary_op },
    { "ishl",                   DxbcInstrTag::ishl,                  DxbcInstrClass::dst0_assign_binary_op },
    { "iadd",                   DxbcInstrTag::iadd,                  DxbcInstrClass::dst0_assign_binary_op },
    { "add",                    DxbcInstrTag::add,                   DxbcInstrClass::dst0_assign_binary_op },
    { "store_uav_typed",        DxbcInstrTag::store_uav_typed,       DxbcInstrClass::dst0_assign_binary_op },
    { "ult",                    DxbcInstrTag::ult,                   DxbcInstrClass::dst0_assign_binary_op },
    { "uge",                    DxbcInstrTag::uge,                   DxbcInstrClass::dst0_assign_binary_op },
    { "ieq",                    DxbcInstrTag::ieq,                   DxbcInstrClass::dst0_assign_binary_op },
    // ...
    { "movc",                   DxbcInstrTag::movc,                  DxbcInstrClass::dst0_assign_tri_op },
    { "imad",                   DxbcInstrTag::imad,                  DxbcInstrClass::dst0_assign_tri_op },
};

static const DxbcInstrStringInfo *
LookupInstrInfo(ByteView view)
{
    for (const DxbcInstrStringInfo& info : StringTable) {
        if (EqualStrZ(view, info.name)) {
            return &info;
        }
    }
    return nullptr;
}


static const char *
SkipWs(const char *p)
{
    for (;; ++p) {
        char const c = *p;
        if (!(c == ' ' || c == '\n' || c == '\r' || c == '\t'))
            break;
    }
    return p;
}

static void
SkipWs(DxbcTextScanner *scanner)
{
    scanner->pSrc = SkipWs(scanner->pSrc);
}

static bool IsAlphaOrUnderscore(uint c)
{
    return ((c | 32u) - 'a') < 26u || c == '_';
}

static uint LetterToCompIndex(uint ch)
{
    return ((ch | 32u) - 'w' - 1) & 3u;
}

static DxbcTextScanResult
ScanCName(DxbcTextScanner *scanner, ByteView *out)
{
    const char *p = SkipWs(scanner->pSrc);
    out->pbegin = p;
    out->pend = p;
    scanner->pSrc = p;
    uint c = *p;
    if (c == 0) {
        return DxbcTextScanResult::Eof;
    }
    if (!IsAlphaOrUnderscore(c)) {
        return DxbcTextScanResult::ExpectedAlpha;
    }
    do c = *++p; while (IsAlphaOrUnderscore(c) || c - '0' < 10u);
    out->pend = p;
    scanner->pSrc = p;
    return DxbcTextScanResult::Okay;
}

static uint
ScanChar(DxbcTextScanner *scanner)
{
    const char *p = SkipWs(scanner->pSrc);
    scanner->pSrc = *p == 0 ? p : (p + 1);
    return *p;
}

static void
Verify(bool cond, const char *str="")
{
    if (!cond) {
        fprintf(stderr, "VerifyFailed: %s\n", str);
#ifdef _MSC_VER
        __debugbreak();
#else
        exit(43);
#endif
    }
}

static void attrib_noreturn Panic(const char *str, int line)
{
    fprintf(stderr, "line:%d, failed VERIFY(%s)\n", line, str);
#ifdef _MSC_VER
        __debugbreak();
#else
        exit(43);
#endif
}

#define VERIFY(e) ((e) ? (void)0 : Panic(#e, __LINE__));

static uint
ScanDotWriteOrInputMask(DxbcTextScanner *scanner)
{
    char const shouldBeDot = ScanChar(scanner);
    Verify(shouldBeDot == '.', "should have dot before writemask/sizzle");

    ByteView maskStr;
    DxbcTextScanResult maskRes = ScanCName(scanner, &maskStr);
    VERIFY(maskRes == DxbcTextScanResult::Okay);
    VERIFY(maskStr.Length() - 1u < 4u);

    uint writeMask = 0;
    for (char ch : maskStr) {
        uint comp = LetterToCompIndex(ch);
        VERIFY(comp < 4u);
        Verify(!(writeMask & 1u << comp), "bad writemask");
        writeMask |= 1u << comp;
    }
    return writeMask;
}

DxbcTextScanResult
DxbcText_ScanHeader(DxbcTextScanner *scanner, DxbcHeaderInfo *headerInfo)
{
    *headerInfo = { };

    {
        ByteView shaderModelString;
        DxbcTextScanResult result = ScanCName(scanner, &shaderModelString);
        if (result != DxbcTextScanResult::Okay) {
            return result;
        }
        VERIFY(EqualStrZ(shaderModelString, "cs_5_0"));
    }

    for (;;) {
        ByteView firstStr;
        VERIFY(ScanCName(scanner, &firstStr) == DxbcTextScanResult::Okay);
        const DxbcInstrStringInfo *const info = LookupInstrInfo(firstStr);
        if (!info) {
            Println(stderr, "unknown instruction: ", firstStr);
            return DxbcTextScanResult::UnknownInstruction;
        }
        if (info->instrClass != DxbcInstrClass::misc_outside_function_body) {
            scanner->pSrc = firstStr.pbegin; // backup
            break;
        }
        switch (info->instrTag) {
            case DxbcInstrTag::dcl_globalFlags: { // dcl_globalFlags refactoringAllowed
                ByteView name; ScanCName(scanner, &name);
                VERIFY(EqualStrZ(name, "refactoringAllowed"));
                headerInfo->globalFlags |= DXBC_GLOBAL_FLAG_REFACTORING_ALLOWED;
            } break;
            case DxbcInstrTag::dcl_temps: { // dcl_temps 1
                unsigned long numV4s = strtol(scanner->pSrc, const_cast<char **>(&scanner->pSrc), 0);
                ASSERT(errno == 0);
                ASSERT(numV4s - 1u < 4096u);
                headerInfo->numTemps = uint16_t(numV4s);
            } break;
            case DxbcInstrTag::dcl_input: { // dcl_input vThreadID.x
                ByteView name; ScanCName(scanner, &name);
                if (EqualStrZ(name, "vThreadIDInGroupFlattened")) {
                    headerInfo->vThreadIDInGroupFlattened = true;
                }
                else {
                    uint mask = ScanDotWriteOrInputMask(scanner);
                    if (EqualStrZ(name, "vThreadID")) {
                        headerInfo->vThreadID_usedMask |= mask;
                    }
                    else {
                        VERIFY(0);
                    }
                }
            } break;
            case DxbcInstrTag::dcl_thread_group: { // dcl_thread_group 64, 1, 1
                int nBytesAdvance = 0;
                int nIntsGot = sscanf(scanner->pSrc, "%d,%d,%d%n", &headerInfo->workgroupSize.x, &headerInfo->workgroupSize.y, &headerInfo->workgroupSize.z, &nBytesAdvance);
                VERIFY(nIntsGot == 3);
                scanner->pSrc += nBytesAdvance;
            } break;
            case DxbcInstrTag::dcl_uav_typed_buffer: { // dcl_uav_typed_buffer (sint,sint,sint,sint) u0
                puts("TODO: skippiong to end of line, assuming this is like { RWBuffer<int> myUav : register(u0); }");
                const char *p = scanner->pSrc;
                while (*p != '\n' && *p != 0) ++p;
                scanner->pSrc = p;
            } break;
            default: {
                ASSERT(0);
                unreachable;
            } break;
        }
    }

    return DxbcTextScanResult::Okay;
}

DxbcTextScanResult
DxbcText_ScanInstrInFuncBody(DxbcTextScanner *scanner, DxbcInstruction *instr)
{
    ByteView firstStr;
    DxbcTextScanResult result = ScanCName(scanner, &firstStr);
    if (result != DxbcTextScanResult::Okay) {
        return result;
    }

    *instr = {};

    int numDests;
    int numSrcs;

    {
        if (StartsWith(firstStr, "if_"_view)) {
            if (EqualStrZ(firstStr, "if_nz")) {
                instr->flags |= DxbcInstrFlag_nz;
            }
            else if (EqualStrZ(firstStr, "if_z")) {
                // ...
            }
            else {
                VERIFY(0);
            }

            numDests = 0;
            numSrcs = 1;

            instr->tag = DxbcInstrTag::if_;
            instr->instrClass = DxbcInstrClass::misc_in_function_body;
        }
        else {
            const DxbcInstrStringInfo * info = LookupInstrInfo(firstStr);

            if (!info) {
                Println(stderr, "unknown instruction: ", firstStr);
                return DxbcTextScanResult::UnknownInstruction;
            }

            numDests = 1;
            numSrcs = (int)info->instrClass - (int)DxbcInstrClass::dst0_assign_unary_op + 1;

            instr->tag = info->instrTag;
            instr->instrClass = info->instrClass;

            if (info->instrTag == DxbcInstrTag::ld_uav_typed) {
                numSrcs = 2;
                // ld_uav_typed_indexable(buffer)(uint,uint,uint,uint) r1.xyzw, vThreadID.xxxx, u0.xyzw
                if (EqualStrZ(firstStr, "ld_uav_typed_indexable")) {
                    // TODO:
                    const char *p = scanner->pSrc;
                    while (*p != '\0' && *p != ')') { ++p; } if (*p == ')') { ++p; }
                    while (*p != '\0' && *p != ')') { ++p; } if (*p == ')') { ++p; }
                    scanner->pSrc = p;
                }
            }

            switch (info->instrTag) {
            case DxbcInstrTag::ret:
            case DxbcInstrTag::_else:
            case DxbcInstrTag::endif:
                return DxbcTextScanResult::Okay;
            default:
                break;
            }
        }
    }

    const int numTotalOperands = numDests + numSrcs;

    int srcSwizzleCharLen = -1;
    int writeMaskCharLen = -1;
    uint writeMaskBits = 0;

    for (int argIndex = 0;;) {
        // debug:
        for (uint c = 0; c < 4; ++c) {
            instr->operands[argIndex].immediateValue.u[c] = 0xdead0000u | c;
        }

        uint operandFlags = 0;
        SkipWs(scanner);
        if (scanner->pSrc[0] == '-') {
            scanner->pSrc++;
            operandFlags |= DxbcOperandFlag_SrcNeg;
        }
        if (scanner->pSrc[0] == '|') {
            scanner->pSrc++;
            operandFlags |= DxbcOperandFlag_SrcAbs;
        }

        ByteView argstr;
        result = ScanCName(scanner, &argstr);
        if (result != DxbcTextScanResult::Okay) {
            fprintf(stderr, "bad arg string[%d], err=%d\n", argIndex, int(result));
        }

        int immSrcComponents = -1;
        int slot = -1;
        DxbcFile file;

        uint operandFirstChar = *argstr.pbegin++;
        if (operandFirstChar == 'l') {
            file = DxbcFile::immediate;
            Verify(argIndex >= numDests, "imm can't be dst");
            operandFirstChar = ScanChar(scanner);
            Verify(operandFirstChar == '(', "should have paren at start of immediate vec<{float, int}, {1, 2, 3, 4}>");
            int comp = 0;
            for (;; ++comp) {
                Verify(comp < 4u, "imm vec too many comps");
                bool hasDot = false;
                for (const char *p = scanner->pSrc;;) {
                    if (*p == '.') {
                        hasDot = true;
                        break;
                    }
                    else if (*p - '0' >= 10u) {
                        break;
                    }
                    ++p;
                }
                if (hasDot) {
                    float fval = strtof(scanner->pSrc, const_cast<char **>(&scanner->pSrc));
                    Verify(errno == 0, "bad float immediate");
                    instr->operands[argIndex].immediateValue.f[comp] = fval;
                }
                else {
                    long long ival = strtoll(scanner->pSrc, const_cast<char **>(&scanner->pSrc), 0);
                    Verify(errno == 0, "bad int immediate");
                    Verify(ival >= INT32_MIN && ival <= INT32_MAX, "int immediate out of range");
                    instr->operands[argIndex].immediateValue.u[comp] = int32_t(ival);
                }
                operandFirstChar = ScanChar(scanner);
                if (operandFirstChar == ',') {
                    continue;
                }
                else if (operandFirstChar == ')') {
                    break;
                }
                else {
                    Verify(0, "validation code is not fun to write at the moment");
                }
            }
            immSrcComponents = comp + 1;
        }
        else if (operandFirstChar == 'v') {
            if (EqualStrZ(argstr, "ThreadID")) {
                Verify(argIndex >= numDests, "vThreadID can't be dst");
                file = DxbcFile::vThreadID;
                slot = 0;
            }
            else if (EqualStrZ(argstr, "ThreadIDInGroupFlattened")) {
                Verify(argIndex >= numDests, "v* can't be dst");
                file = DxbcFile::vThreadIDInGroupFlattened;
                slot = 0;
            }
            else {
                Verify(0, "TODO: implement v* input besides vThreadID");
            }
        }
        else {
            switch (operandFirstChar) {
            case 'u':
            case 'r': {
                file = operandFirstChar == 'u' ? DxbcFile::uav : DxbcFile::temp;
                slot = 0;
                const char *p = argstr.pbegin;
                VERIFY(*p - '0' < 10u);
                for (;;) {
                    uint d = *p - '0';
                    if (d >= 10u) {
                        break;
                    }
                    p++;
                    slot = slot*10u + d;
                    VERIFY(slot < 4096u);
                }
                scanner->pSrc = p;
            } break;
            default: {
                // could be EOF here
                fprintf(stderr, "bad arg[%d] first char: %c", argIndex, argstr.pbegin[0]);
                ASSERT(0);
                return DxbcTextScanResult::Other;
            } break;
            }
        }

        instr->operands[argIndex].file = file;
        instr->operands[argIndex].slotInFile = slot;

        ByteView maskStr;
        if (file != DxbcFile::immediate) {
            char const shouldBeDot = ScanChar(scanner);
            Verify(shouldBeDot == '.', "should have dot before writemask/sizzle");
            DxbcTextScanResult maskRes = ScanCName(scanner, &maskStr);
            VERIFY(maskRes == DxbcTextScanResult::Okay);
            VERIFY(maskStr.Length() - 1u < 4u);
        }
        else {
            static const char az_xyz[] = "xyzw";
            maskStr = { az_xyz, az_xyz + immSrcComponents };
        }

        if (argIndex < numDests) {
            // parse dst writemask and saturate
            uint writeMask = 0;
            for (char ch : maskStr) {
                uint comp = LetterToCompIndex(ch);
                VERIFY(comp < 4u);
                Verify(!(writeMask & 1u << comp), "bad writemask");
                writeMask |= 1u << comp;
            }
            instr->operands[argIndex].dstWritemask = writeMask;
            writeMaskCharLen = maskStr.Length();
            writeMaskBits = writeMask;
            VERIFY((operandFlags & ~DxbcOperandFlag_DstSat) == 0);
        }
        else {
            // parse src swizzle and abs/neg
            if (numDests) {
                VERIFY(maskStr.Length());
                VERIFY(srcSwizzleCharLen < 0 || srcSwizzleCharLen == maskStr.Length());
                srcSwizzleCharLen = maskStr.Length();
            }

            ASSERT((writeMaskBits != 0) ^ (numDests == 0));
            /*
                The textual representation of DXBC seems to have 2+ ways of doing writemasks/swizzles.
                1. Is the "linedup" way, which is the layout in the DxbcInstruction, e.g: { dst._yz_ = src.*??* }
                    I think even non-contiguous writemasks are allowed in this mode, like { dst.x_z_ = src.?*?* }
                2. glsl/hlsl style, e.g: dst.y = src.x

                To distinguish between these 2 modes in the dsbc text, my current idea is to choose mode 2 (glsl)
                when the string-length of the writemask is the same as the string-length of the swizzles.
            */
            bool const bModeGLSL = (writeMaskCharLen == srcSwizzleCharLen) && numDests;
            uint swizzle = 0;
            if (bModeGLSL) {
                uint tmpWriteMask = writeMaskBits;
                // 5 (0101), 9 (1001), 10 (1010) are not contiguous:
                // ................................fedcba9876543210
                VERIFY(writeMaskBits < 0x10u && (0b1111100111011110u & 1u << writeMaskBits));
                const char *str = maskStr.pbegin;
                do {
                    uint const writeCompIndex = bsf(tmpWriteMask);
                    tmpWriteMask ^= 1u << writeCompIndex; // clear bit since it was 1
                    uint const srcCompIndex = LetterToCompIndex(*str++);
                    VERIFY(srcCompIndex < 4u);
                    swizzle |= srcCompIndex << (writeCompIndex * 2);
                } while (tmpWriteMask);
            } else {
                // example: and r0.yz, vThreadID.xxxx, l(0, 4, 2, 0)
                // if_nz r0.y
                uint i = 0;
                VERIFY(numDests == 0 || maskStr.Length() == 4); // hmm, think this should be the case.
                for (char ch : maskStr) {
                    uint comp = LetterToCompIndex(ch);
                    VERIFY(comp < 4u);
                    swizzle |= comp << (i++ * 2);
                }
            }
            instr->operands[argIndex].srcSwizzle.bits = swizzle;
            if (operandFlags & DxbcOperandFlag_SrcAbs) {
                // closing absolute-value bar: "add r0.y, -|r0.z|, r0.y"
                if (scanner->pSrc[0] == '|') {
                    scanner->pSrc++;
                }
                else {
                    VERIFY(0);
                }
            }
        }
        instr->operands[argIndex].flags = operandFlags;

        if (++argIndex == numTotalOperands) {
            break;
        }
        char const shouldBeComma = ScanChar(scanner);
        Verify(shouldBeComma == ',', "should have comma after operand");
    }

    return DxbcTextScanResult::Okay;
}


bool
DxbcText_ScanIsEof(DxbcTextScanner *scanner)
{
    scanner->pSrc = SkipWs(scanner->pSrc);
    return scanner->pSrc[0] == 0;
}

#if 0 // likely not interesting anymore
void
Scanner_Test()
{
    static const char TestStr0[] = R"(cs_5_0
dcl_globalFlags refactoringAllowed
dcl_uav_typed_buffer (sint,sint,sint,sint) u0
dcl_input vThreadID.x
dcl_temps 1
dcl_thread_group 64, 1, 1
ult r0.x, vThreadID.x, l(32)
movc r0.x, r0.x, l(1000), l(2000)
and r0.y, vThreadID.x, l(15)
ult r0.y, r0.y, l(8)
xor r0.x, r0.y, r0.x
store_uav_typed u0.xyzw, vThreadID.xxxx, r0.xxxx
ret
)";
    DxbcTextScanner scanner;
    scanner.pSrc = TestStr0;

    DxbcTextScanResult result;

    DxbcHeaderInfo header;
    result = DxbcText_ScanHeader(&scanner, &header);
    VERIFY(result == DxbcTextScanResult::Okay);


    DxbcInstruction instr;
    for (;;) {
        if (DxbcText_ScanIsEof(&scanner)) {
            puts("got eof");
            break;
        }

        result = DxbcText_ScanInstrInFuncBody(&scanner, &instr);
        if (result != DxbcTextScanResult::Okay) {
            puts("not okay");
            break;
        }
    }
}
#endif
