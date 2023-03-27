#ifndef BASEADDR_H
#define BASEADDR_H

#include "core.h"

///////////////////////////////////////////////////////////////
// Global Variables
///////////////////////////////////////////////////////////////

extern PVOID gNtosKrnlBase;
extern UINT32 gNtosKrnlSize;

///////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////

VOID
KmInitializeBaseAddresses(
  PDRIVER_OBJECT Driver);

#endif