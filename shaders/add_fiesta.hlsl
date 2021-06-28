struct ComputeShaderInput {
	uint3 globalID : SV_DispatchThreadID; // vThreadID, gl_GlobalInvocationID
	uint3 groupID : SV_GroupID; // vGroupID, gl_WorkGroupID
	uint3 localID : SV_GroupThreadID; // vThreadIDInGroup, gl_LocalInvocationID
	
	uint flatLocalIndex : SV_GroupIndex; // vThreadIDInGroupFlattened, gl_LocalInvocationIndex
};

// dispatch with (1,1,1) groups

RWBuffer<uint4> uav : register(u0);
[numthreads(4, 4, 4)] // gl_WorkGroupSize
void main(ComputeShaderInput input)
{
	int texelIndex = input.flatLocalIndex * 4; // ishl 2
	
	int4 ival = uav[texelIndex];
	
	ival.x =  ival.x +  ival.y;
	ival.y =  ival.y + -ival.z;
	ival.z = -ival.z +  ival.w;
	ival.w = -ival.w + -int(input.flatLocalIndex);
	
	ival.w = -int(input.globalID.x) + ival.w;
	
    uav[texelIndex++] = ival; 

	// integer instructions don't have an abs src modifier
	
	float4 fval	= float4(uav[texelIndex]);
	
	fval.x =  fval.x +  fval.y;
	fval.y =  fval.y + -fval.z;
	fval.z = -fval.z +  fval.w;
	fval.w = -fval.w + -float(input.flatLocalIndex);
	
	fval.w = -float(input.globalID.x) + fval.w;
	
	fval.x = abs(fval.x) + -abs(fval.y);
	
	uav[texelIndex++] = uint4(fval);
}
/*
cs_5_0
dcl_globalFlags refactoringAllowed
dcl_uav_typed_buffer (uint,uint,uint,uint) u0
dcl_input vThreadIDInGroupFlattened
dcl_input vThreadID.x
dcl_temps 3
dcl_thread_group 4, 4, 4
ishl r0.x, vThreadIDInGroupFlattened.x, l(2)
ld_uav_typed_indexable(buffer)(uint,uint,uint,uint) r1.xyzw, r0.xxxx, u0.xyzw
iadd r0.y, -r1.w, -vThreadIDInGroupFlattened.x
iadd r2.w, r0.y, -vThreadID.x
iadd r2.x, r1.y, r1.x
iadd r2.yz, -r1.zzzz, r1.yywy
store_uav_typed u0.xyzw, r0.xxxx, r2.xyzw
imad r0.x, vThreadIDInGroupFlattened.x, l(4), l(1)
ld_uav_typed_indexable(buffer)(uint,uint,uint,uint) r1.xyzw, r0.xxxx, u0.xyzw
utof r1.xyzw, r1.xyzw
add r0.y, r1.y, r1.x
add r0.zw, -r1.zzzz, r1.yyyw
add r0.y, -|r0.z|, r0.y
ftou r2.xyz, r0.yzwy
utof r0.y, vThreadIDInGroupFlattened.x
add r0.y, -r0.y, -r1.w
utof r0.z, vThreadID.x
add r0.y, r0.y, -r0.z
ftou r2.w, r0.y
store_uav_typed u0.xyzw, r0.xxxx, r2.xyzw
ret
// Approximately 21 instruction slots used
*/
