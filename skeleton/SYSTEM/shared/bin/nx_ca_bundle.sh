#!/bin/sh
# nx_ca_bundle.sh - resolve the NX Redux CA bundle and export it for TLS trust.
#
# The firmware ships no CA store (/etc/ssl/certs is empty), so every HTTPS
# client on the card fails certificate verification unless pointed at a bundle.
# NX Redux ships a Mozilla bundle at $SHARED_SYSTEM_PATH/ssl/ca-certificates.crt
# (PortMaster's Xtras install drops a byte-identical copy under its own tree).
# This helper finds it and hands it to the tools that need it.
#
# Source (never exec) it - it only sets variables, has no side effects, and is
# safe under `set -u` / `set -e`:
#   pak launchers:  . "$SHARED_SYSTEM_PATH/bin/nx_ca_bundle.sh"
#   Xtras scripts:  . "${SDCARD_PATH:-/mnt/SDCARD}/.system/shared/bin/nx_ca_bundle.sh"
# Guard the source with `[ -f "$helper" ] &&` so a very old card missing the
# file (or an unset path) never aborts the sourcing script.
#
# It sets NX_CA_BUNDLE to the resolved path (empty string when no bundle is
# found) and, when found, exports:
#   CURL_CA_BUNDLE - read directly by the curl CLI (--cacert equivalent).
#   SSL_CERT_FILE  - honoured by OpenSSL-level consumers (e.g. GNU wget's
#                    OpenSSL backend, and NX Redux's flycast: its patched
#                    core/oslib/http_client.cpp reads SSL_CERT_FILE first, then
#                    CURL_CA_BUNDLE, into CURLOPT_CAINFO).
# Callers that want to branch on presence (e.g. deriving a wget TLS flag) read
# NX_CA_BUNDLE.

NX_CA_BUNDLE=""
for _nx_ca in "${SDCARD_PATH:-/mnt/SDCARD}/.system/shared/ssl/ca-certificates.crt" \
    "${SDCARD_PATH:-/mnt/SDCARD}/Emus/shared/PortMaster/ssl/certs/ca-certificates.crt"; do
    if [ -f "$_nx_ca" ]; then
        NX_CA_BUNDLE="$_nx_ca"
        export CURL_CA_BUNDLE="$_nx_ca"
        export SSL_CERT_FILE="$_nx_ca"
        break
    fi
done
unset _nx_ca
