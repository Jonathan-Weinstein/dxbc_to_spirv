struct ComputeShaderInput {
	uint3 globalID : SV_DispatchThreadID; // vThreadID, gl_GlobalInvocationID
	uint3 groupID : SV_GroupID; // vGroupID, gl_WorkGroupID
	uint3 localID : SV_GroupThreadID; // vThreadIDInGroup, gl_LocalInvocationID
	
	uint flatLocalIndex : SV_GroupIndex; // vThreadIDInGroupFlattened, gl_LocalInvocationIndex
};
// Note: HLSL/dxbc/dxil does not have a gl_NumWorkGroups (arguments of dispatch command) equivalent


// -------------------------------------------------------------------------------


// dispatch with (1,1,1) groups

RWBuffer<uint4> uav : register(u0);
[numthreads(4, 4, 4)] // gl_WorkGroupSize
void main(ComputeShaderInput input)
{
	int4 ival = uav[input.flatLocalIndex];
	
	ival.x =  ival.x +  ival.y;
	ival.y =  ival.y + -ival.z;
	ival.z = -ival.z +  ival.w;
	ival.w = -ival.w + -int(input.flatLocalIndex);
	
	ival.w = -int(input.globalID.x) + ival.w;
	
    uav[input.flatLocalIndex] = ival; 

	// integer instructions don't have an abs src modifier
}

/* "C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\fxc.exe" hello_absneg.hlsl -E main -T cs_5_0 -O3

cs_5_0
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
*/

