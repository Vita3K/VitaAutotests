// The function probe_lib exports in its library ProbeLib. The app imports it weakly, so it starts
// with no copy loaded, and never calls it.
#pragma once

int probe_lib_function(void);
