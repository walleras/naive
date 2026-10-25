/* sm_server.h  --  a mock "web server" for Naive routes.
 *
 * There is no real network here (design doc S13 is candid that 1995 transport
 * couldn't sustain it). This stands in for an HTTP server that answers a route
 * with a Naive document: known routes return .nve source, everything else is
 * a 404. Routes map to files under a server root directory (default "www").
 */
#ifndef SM_SERVER_H
#define SM_SERVER_H

#include "sm_common.h"

/* Set the directory routes are served from (default "www"). */
void sm_server_set_root(const char *dir);

/* Fetch the Naive body for a route. On success returns SM_OK and *out_body is a
 * freshly allocated NUL-terminated document (free with sm_free). On an unknown
 * route or unreadable file, returns SM_ERR_NOT_FOUND and fills err. */
SmStatus sm_server_get(const char *route, char **out_body, char *err, size_t errcap);

/* POST an application/x-www-form-urlencoded body to a route (the 1995 form
 * encoding). For /submit the server acts CGI-style: it parses the body and
 * returns a page echoing the received fields. Other routes behave like GET. */
SmStatus sm_server_post(const char *route, const char *body,
                        char **out_body, char *err, size_t errcap);

#endif /* SM_SERVER_H */
