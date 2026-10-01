#ifndef HOME_STATS_H
#define HOME_STATS_H

#include <stdbool.h>

#include "home_stats_model.h"

void HomeStats_init(void);	  // starts the worker; computes nothing yet
void HomeStats_quit(void);	  // stops and joins the worker
void HomeStats_request(void); // (re)compute in the background (menu show, return, pin change); requests coalesce
// The latest result. False before the first one (out->ready false).
bool HomeStats_get(HomeStats* out);
// True once after the worker published a new result (main loop: redraw).
bool HomeStats_checkAsyncLoaded(void);

#endif // HOME_STATS_H
