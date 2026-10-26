#ifndef WS_VIEWS_H
#define WS_VIEWS_H
#include "ws_java.h"
/* Android view geometry/lifecycle for Apportable's native UIKit bridge.
 * All mutations and traversal run on the game's main thread. */
int ws_views_init(ws_java *java);
void ws_views_destroy(ws_java *java);
int ws_views_layout(ws_java *java);
#endif
