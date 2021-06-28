RWBuffer<uint> uav : register(u0);
[numthreads(8, 4, 2)]
void main(uint3 globalID : SV_DispatchThreadID, uint flattenedLocalIndex : SV_GroupIndex)
{
	uint a;
	[branch] if (globalID.x >= 4) {
		[branch] if (globalID.z == 0) {
			a = globalID.x - globalID.y;
			[branch] if (globalID.y < 2) {
				a <<= 2;
			}
		} else {
			a = globalID.x ^ globalID.y;
		}
		a += 5;
		[branch] if (globalID.y == 0) {
			a += 7;
		} else {
			a ^= 255;
		}
	} else [branch] if (globalID.y < 2) {
		a = globalID.x;
		[branch] if ((globalID.x & 1) == 0) {
			a += 100;
		}
	} else {
		a = 42;
	}
    uav[flattenedLocalIndex] = a;
}

/*
cs_5_0
dcl_globalFlags refactoringAllowed
dcl_uav_typed_buffer (uint,uint,uint,uint) u0
dcl_input vThreadIDInGroupFlattened
dcl_input vThreadID.xyz
dcl_temps 1
dcl_thread_group 8, 4, 2
uge r0.x, vThreadID.x, l(4)
if_nz r0.x
  if_z vThreadID.z
    iadd r0.x, -vThreadID.y, vThreadID.x
    ult r0.y, vThreadID.y, l(2)
    if_nz r0.y
      ishl r0.x, r0.x, l(2)
    endif
  else
    xor r0.x, vThreadID.y, vThreadID.x
  endif
  iadd r0.y, r0.x, l(5)
  if_z vThreadID.y
    iadd r0.x, r0.x, l(12)
  else
    xor r0.x, r0.y, l(255)
  endif
else
  ult r0.y, vThreadID.y, l(2)
  if_nz r0.y
    and r0.y, vThreadID.x, l(1)
    if_z r0.y
      iadd r0.x, vThreadID.x, l(100)
    else
      mov r0.x, vThreadID.x
    endif
  else
    mov r0.x, l(42)
  endif
endif
store_uav_typed u0.xyzw, vThreadIDInGroupFlattened.xxxx, r0.xxxx
ret
*/

