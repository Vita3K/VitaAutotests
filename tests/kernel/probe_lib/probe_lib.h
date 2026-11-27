// The functions probe_lib exports in its library ProbeLib and probe_two_libs in ProbeLibA. The app
// imports them weakly, so it starts with no copy loaded, and never calls them.
#pragma once

#include <psp2/types.h>

int probe_lib_function(void);
int probe_lib_a_function(void);

// The runs of the start and stop functions, counted through a pointer in their arguments
typedef struct LibRuns {
    int starts;
    int stops;
} LibRuns;

typedef struct LibArgs {
    LibRuns *out;
} LibArgs;
