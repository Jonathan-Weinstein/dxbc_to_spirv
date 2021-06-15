#pragma once

#include <stdint.h>

struct SpirvRunner;

SpirvRunner *NewSpirvRunner();
void DeleteSpirvRunner(SpirvRunner *runner);

const uint32_t *RunSimpleCompute(SpirvRunner *r, const void *spvCpde, unsigned numCodeBytes);