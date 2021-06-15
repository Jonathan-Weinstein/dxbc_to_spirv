RWBuffer<uint> uav : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 globalID : SV_DispatchThreadID)
{
	uint x = globalID.x;
	// All these bools can stay as bools in spirv...
	bool cond0 = x < 32u;
	bool cond1 = (x & 4) == 0;
	bool orResult = cond0 || cond1;
	uint val = orResult ? 1000 : 2000;
	val += x;
	// ...but also test using bools as {0, -1} int values work:
	val ^= -int((x & 2) == 0);
    uav[x] = val;
}

/*
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\fxc.exe" bool_and.hlsl -E main -T cs_5_0 -O3
Microsoft (R) Direct3D Shader Compiler 10.1 (using C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\D3DCOMPILER_47.dll)
Copyright (C) 2013 Microsoft. All rights reserved.
// ...
cs_5_0
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
// Approximately 9 instruction slots used
*/


/* current dxbc_to_spirv:

C:\VulkanSDK\1.2.148.1\Bin\spirv-dis.exe output.spv
; SPIR-V
; Version: 1.3
; Generator: Khronos; 0
; Bound: 46
; Schema: 0
               OpCapability Shader
               OpCapability SampledBuffer
               OpCapability ImageBuffer
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main" %ptr_vThreadID
               OpExecutionMode %main LocalSize 64 1 1
               OpName %main "main"
               OpName %ptr_vThreadID "ptr_vThreadID"
               OpName %vThreadID_xyz "vThreadID_xyz"
               OpName %vThreadID_x "vThreadID_x"
               OpName %ptr_uav0 "ptr_uav0"
               OpDecorate %ptr_vThreadID BuiltIn GlobalInvocationId
               OpDecorate %ptr_uav0 DescriptorSet 0
               OpDecorate %ptr_uav0 Binding 0
       %void = OpTypeVoid
          %4 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v2uint = OpTypeVector %uint 2
     %v3uint = OpTypeVector %uint 3
     %v4uint = OpTypeVector %uint 4
       %bool = OpTypeBool
    %uint_32 = OpConstant %uint 32
     %uint_4 = OpConstant %uint 4
     %uint_2 = OpConstant %uint 2
     %uint_0 = OpConstant %uint 0
  %uint_1000 = OpConstant %uint 1000
  %uint_2000 = OpConstant %uint 2000
%uint_4294967295 = OpConstant %uint 4294967295
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%ptr_vThreadID = OpVariable %_ptr_Input_v3uint Input
         %23 = OpTypeImage %uint Buffer 0 0 0 2 R32ui
%_ptr_UniformConstant_23 = OpTypePointer UniformConstant %23
   %ptr_uav0 = OpVariable %_ptr_UniformConstant_23 UniformConstant
       %main = OpFunction %void None %4
         %20 = OpLabel
%vThreadID_xyz = OpLoad %v3uint %ptr_vThreadID
%vThreadID_x = OpCompositeExtract %uint %vThreadID_xyz 0
         %27 = OpULessThan %bool %vThreadID_x %uint_32
         %29 = OpBitwiseAnd %uint %vThreadID_x %uint_4
         %31 = OpBitwiseAnd %uint %vThreadID_x %uint_2
         %33 = OpIEqual %bool %29 %uint_0
         %34 = OpIEqual %bool %31 %uint_0
         %35 = OpLogicalOr %bool %33 %27
         %38 = OpSelect %uint %35 %uint_1000 %uint_2000
         %39 = OpIAdd %uint %38 %vThreadID_x
         %41 = OpSelect %uint %34 %uint_4294967295 %uint_0 ; convert boolVal to intVal via: intVal = boolVal ? -1 : 0
         %42 = OpBitwiseXor %uint %41 %39
         %43 = OpLoad %23 %ptr_uav0
               OpImageWrite %43 %vThreadID_x %42
               OpReturn
               OpFunctionEnd
*/

