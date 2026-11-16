#pragma once
#include <stdint.h>
struct IdentityTestFicr {uint32_t DEVICEID[2];};
static IdentityTestFicr identityTestFicr{{0x289430bd,0x0e8a89b7}};
#define NRF_FICR (&identityTestFicr)
