#ifndef __CA_BUNDLE_H__
#define __CA_BUNDLE_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "defines.h"

// Resolve the CA bundle to verify TLS against, or NULL when the card has none.
//
// The firmware ships no CA store (/etc/ssl/certs is empty), so a client that
// shells out to curl/wget must either be handed a bundle or skip verification
// (-k / --no-check-certificate). NX Redux ships a Mozilla bundle at
// $SHARED_SYSTEM_PATH/ssl/ca-certificates.crt (see the shared launcher helper
// skeleton/SYSTEM/shared/bin/nx_ca_bundle.sh, which exports CURL_CA_BUNDLE /
// SSL_CERT_FILE for pak launch scripts). When this returns a path the caller
// passes --cacert / --ca-certificate= for a verified connection; when it
// returns NULL the caller keeps the historical skip-verification fallback so
// behaviour on a card without any bundle is unchanged.
//
// Resolution order, first readable wins: the CURL_CA_BUNDLE env var (what the
// curl CLI itself honours), then SSL_CERT_FILE (OpenSSL-level consumers), then
// the shipped bundle path. Header-only + static so both http.c and
// wget_fetch.c share it without a new Makefile object; resolved once per
// translation unit and cached.
static inline const char* nx_ca_bundle_path(void) {
	static char cached[MAX_PATH];
	static int resolved = 0;
	if (resolved)
		return cached[0] ? cached : NULL;
	resolved = 1;
	cached[0] = '\0';

	const char* env;
	if ((env = getenv("CURL_CA_BUNDLE")) && *env && access(env, R_OK) == 0) {
		snprintf(cached, sizeof(cached), "%s", env);
		return cached;
	}
	if ((env = getenv("SSL_CERT_FILE")) && *env && access(env, R_OK) == 0) {
		snprintf(cached, sizeof(cached), "%s", env);
		return cached;
	}

	char shipped[MAX_PATH];
	snprintf(shipped, sizeof(shipped), "%s/ssl/ca-certificates.crt", SHARED_SYSTEM_PATH);
	if (access(shipped, R_OK) == 0) {
		snprintf(cached, sizeof(cached), "%s", shipped);
		return cached;
	}

	return NULL;
}

#endif // __CA_BUNDLE_H__
