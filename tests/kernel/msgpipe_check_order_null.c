// The cases of msgpipe_check_order whose piece list or piece buffer is NULL. A wrong implementation
// may crash on those, so they run in a test of their own. The checks and printing are the same as
// in msgpipe_check_order.c, which this file includes with the NULL pointer cases selected.
#define CHECK_ORDER_WITH_NULL 1
#include "msgpipe_check_order.c"
