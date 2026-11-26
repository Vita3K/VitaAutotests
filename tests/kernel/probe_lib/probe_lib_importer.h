// The function probe_lib_importer exports in its library ProbeImporter. It returns the address of
// the module's own ProbeLib import stub. The app imports it weakly.
#pragma once

#include <psp2/types.h>

SceUIntPtr probe_importer_stub(void);
