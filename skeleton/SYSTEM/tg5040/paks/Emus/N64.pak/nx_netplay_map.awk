# nx_netplay_map.awk — rewrite [Input-SDL-Control1..4] for netplay.
# Args: -v P=<local player/port 1-4>  -v N=<total players 2-4>
# The local pad STAYS on Control1 (device 0 + its button map) on every device.
# mupen64plus netplay reads a device's local input via getKeys(netplay_get_
# controller(seat)), which resolves to local controller 0 (= Control1) for
# whatever seat this device controls (input_plugin_compat.c:71-79) — so the pad
# must NOT be moved to the assigned port. We only mark ports 1..N plugged=True
# so the game shows N controllers; the core routes Control1's input to this
# device's seat and injects the remote seats' input at runtime. Ports 2..N carry
# device=-1 (no local joystick) but are still plugged. P is unused here (it drives
# --netplay-player in launch.sh, not the controller config).
{ lines[NR] = $0 }
END {
    c1 = 0
    for (i = 1; i <= NR; i++)
        if (lines[i] ~ /^\[Input-SDL-Control1\]$/) { c1 = i; break }
    if (c1 == 0) { for (i = 1; i <= NR; i++) print lines[i]; exit }

    # capture Control1 body (up to next section) minus device/plugged = template
    tn = 0
    for (i = c1 + 1; i <= NR; i++) {
        if (lines[i] ~ /^\[/) break
        key = lines[i]; sub(/[ \t]*=.*/, "", key)
        if (key == "device" || key == "plugged") continue
        tmpl[++tn] = lines[i]
    }
    # control block = c1 .. first non-control section header
    bend = NR + 1
    for (i = c1; i <= NR; i++)
        if (lines[i] ~ /^\[/ && lines[i] !~ /^\[Input-SDL-Control[1-4]\]$/) { bend = i; break }

    for (i = 1; i < c1; i++) print lines[i]
    for (p = 1; p <= 4; p++) {
        printf "[Input-SDL-Control%d]\n", p
        printf "device = %d\n", (p == 1 ? 0 : -1)
        printf "plugged = %s\n", (p <= N ? "True" : "False")
        for (t = 1; t <= tn; t++) print tmpl[t]
    }
    for (i = bend; i <= NR; i++) print lines[i]
}
