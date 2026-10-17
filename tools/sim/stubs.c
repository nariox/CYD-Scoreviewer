/* Host stubs for the preview harness (tools/sim): the cards screen calls
   into the service on the refresh button, and the wifi status badge reads
   backend state. Neither runs offline. */
#include <stdbool.h>
#include "nba_service.h"

void nba_service_start(void) {}
void nba_service_request_refresh(void) {}
void nba_service_step_page(int delta) { (void)delta; }
int nba_service_page(void) { return 0; }
bool nba_service_is_running(void) { return false; }
