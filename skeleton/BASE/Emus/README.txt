Put your own paks here (e.g. MyEmu.pak), either directly in this folder or in
a platform subfolder (tg5040/MyEmu.pak on a Brick, Brick Pro or Smart Pro,
tg5050/MyEmu.pak on a Smart Pro S). Community paks usually need the platform
subfolder: they hardcode that path internally.

The paks NX Redux ships live in /.system/paks/ and are replaced on every
update, so don't edit them there. A shipped pak always wins: a pak here with
the same name as a shipped one is ignored, so give yours its own name.
