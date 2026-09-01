#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"; kill $SRV 2>/dev/null || true' EXIT
# dummy server: /releases/latest 302-redirects to /releases/tag/v9.9.9
cat > "$TMP/srv.py" <<'EOF'
import http.server
class H(http.server.BaseHTTPRequestHandler):
    def do_HEAD(self):
        if self.path.endswith("/releases/latest"):
            self.send_response(302)
            self.send_header("Location", "http://127.0.0.1:8901/releases/tag/v9.9.9")
            self.end_headers()
        else:
            self.send_response(404); self.end_headers()
    do_GET = do_HEAD
    def log_message(self, *a): pass
http.server.HTTPServer(("127.0.0.1", 8901), H).serve_forever()
EOF
python3 "$TMP/srv.py" & SRV=$!
sleep 1
export NXREDUX_UPDATE_BASE=http://127.0.0.1:8901

OUT="$("$HERE/../check-update.sh" v1.0.0)" || { echo "FAIL: expected update"; exit 1; }
TAG="$(printf '%s' "$OUT" | cut -f1)"; URL="$(printf '%s' "$OUT" | cut -f2)"
[ "$TAG" = "v9.9.9" ] || { echo "FAIL: tag=$TAG"; exit 1; }
case "$URL" in
	*/releases/download/v9.9.9/NXRedux-v9.9.9-*) : ;;
	*) echo "FAIL: url=$URL"; exit 1 ;;
esac
set +e
"$HERE/../check-update.sh" v9.9.9; rc=$?
set -e
[ "$rc" -eq 1 ] || { echo "FAIL: same tag must exit 1 (got $rc)"; exit 1; }
echo "test_check_update: OK"
