CORES= a2600
CORES+= a5200
CORES+= a7800
CORES+= handy
CORES+= c64
CORES+= c128
CORES+= fake-08
CORES+= fbneo
CORES+= fceumm
CORES+= gambatte
CORES+= gpsp
CORES+= libretro-cap32
CORES+= libretro-uae
CORES+= mednafen_pce_fast
CORES+= mednafen_supafaust
CORES+= mednafen_vb
CORES+= mgba
CORES+= pcsx_rearmed
CORES+= pet
CORES+= picodrive
CORES+= plus4
CORES+= pokemini
CORES+= race
CORES+= snes9x
CORES+= vic
CORES+= prboom
CORES+= bluemsx
CORES+= gearcoleco

###############################

# optional core vars
# *_REPO=
# *_HASH=
# *_CORE=
# *_FLAGS=
# *_MAKEFILE=
# *_BUILD_PATH=
# *_SUBMODULES=

a2600_REPO = https://github.com/libretro/stella2014-libretro
a2600_CORE = stella2014_libretro.so
a2600_HASH = 3cc89f0d316d6c924a5e3f4011d17421df58e615

a5200_CORE = a5200_libretro.so
a5200_HASH = 526404072821bb2021fab16f8c5dbbca300512c8

a7800_CORE = prosystem_libretro.so
a7800_REPO = https://github.com/libretro/prosystem-libretro
a7800_HASH = acae250da8d98b8b9707cd499e2a0bf6d8500652

handy_REPO = https://github.com/libretro/libretro-handy
handy_HASH = fca239207e9c111da3e85d2faf0b1b9d7524e498

fbneo_REPO = https://github.com/libretro/FBNeo
fbneo_CORE = fbneo_libretro.so
fbneo_BUILD_PATH = fbneo/src/burner/libretro
fbneo_MAKE = make
fbneo_HASH = 6a5cc250c2db8d874a06ee86e302a2a78918b4c1

mednafen_pce_fast_REPO = https://github.com/libretro/beetle-pce-fast-libretro
mednafen_pce_fast_HASH = 52675734da114a19b3705f03906b1455f3d76644

mednafen_vb_REPO = https://github.com/libretro/beetle-vb-libretro
mednafen_vb_HASH = 65debc7c4c7b85e2fd988d2be53496c2cf0b5f44

fake-08_REPO = https://github.com/jtothebell/fake-08
fake-08_CORE = fake08_libretro.so
fake-08_BUILD_PATH = fake-08/platform/libretro
fake-08_HASH = f6bab5a7ba521ce440e45d1aeef6122674be6ee9

fceumm_REPO = https://github.com/libretro/libretro-fceumm
fceumm_HASH = 5cd4a43e16a7f3cd35628d481c347a0a98cfdfa2

gambatte_REPO = https://github.com/libretro/gambatte-libretro
gambatte_HASH = 9fe223d9c4b615c55840170c6e85e6e9fa4bd1d2

gpsp_HASH = 602512d7c687acb84cd56265cbcf2d7b7c75fb37

mednafen_supafaust_REPO = https://github.com/libretro/supafaust
mednafen_supafaust_HASH = e25f66765938d33f9ad5850e8d6cd597e55b7299

mgba_HASH = c758314a639aa0066e7b65a8341448181b73c804

pcsx_rearmed_MAKEFILE = Makefile.libretro
pcsx_rearmed_HASH = 6eb679d064a1efe92af79fe9970c0591c8da02d4
# pcsx has a disabled submodule for gnulib, which takes forever to check out and is unused - skip checkout
pcsx_rearmed_SUBMODULES = frontend/libpicofe

picodrive_REPO = https://github.com/irixxxx/picodrive
picodrive_MAKEFILE = Makefile.libretro
# No background Audio on Sega CD
# https://github.com/LoveRetro/NextUI/issues/68
picodrive_HASH = b0be121b7d58d6ee1ee2809974e62893c80a8264

pokemini_REPO = https://github.com/libretro/PokeMini
pokemini_MAKEFILE = Makefile.libretro
# Our patchfile doesn't work on latest Pokemon mini commits, pinning to last working hash for now
pokemini_HASH = 78656d4615691c393e65e48672ce5fff5b1f97d3

prboom_REPO = https://github.com/libretro/libretro-prboom
prboom_HASH = 93c8e7a2074e4fd8410398e3d571c6d9afec1d84

race_HASH = 171950ea64c96ead503340ed60fa7ba18f56325c

snes9x2005_plus_REPO = https://github.com/libretro/snes9x2005
snes9x2005_plus_FLAGS = USE_BLARGG_APU=1

snes9x_REPO = https://github.com/libretro/snes9x
snes9x_CORE = snes9x_libretro.so
snes9x_BUILD_PATH = snes9x/libretro
snes9x_HASH = 5a40cd5514e63e691e39141d64267798357a1424

libretro-uae_REPO = https://github.com/libretro/libretro-uae
libretro-uae_BRANCH = 2.6.1
libretro-uae_CORE = puae2021_libretro.so
libretro-uae_HASH = 2826e67ac623974f3b8ab15951f2f8407d81fc17

libretro-cap32_REPO = https://github.com/libretro/libretro-cap32
libretro-cap32_CORE = cap32_libretro.so
libretro-cap32_HASH = 54eabbb210a6149496b1d023bebddb3f0120bc33

c64_REPO  = https://github.com/libretro/vice-libretro
c64_CORE  = vice_x64_libretro.so
c64_FLAGS = EMUTYPE=x64
c64_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

c128_REPO  = https://github.com/libretro/vice-libretro
c128_CORE  = vice_x128_libretro.so
c128_FLAGS = EMUTYPE=x128
c128_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

vic_REPO  = https://github.com/libretro/vice-libretro
vic_CORE  = vice_xvic_libretro.so
vic_FLAGS = EMUTYPE=xvic
vic_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

pet_REPO  = https://github.com/libretro/vice-libretro
pet_CORE  = vice_xpet_libretro.so
pet_FLAGS = EMUTYPE=xpet
pet_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

plus4_REPO  = https://github.com/libretro/vice-libretro
plus4_CORE  = vice_xplus4_libretro.so
plus4_FLAGS = EMUTYPE=xplus4
plus4_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

bluemsx_REPO = https://github.com/libretro/blueMSX-libretro
bluemsx_CORE = bluemsx_libretro.so
bluemsx_MAKEFILE = Makefile.libretro
bluemsx_HASH = 036376d6679c9e153712dbbb3fdca774afc49706

gearcoleco_REPO = https://github.com/drhelius/Gearcoleco.git
gearcoleco_CORE = gearcoleco_libretro.so
gearcoleco_BUILD_PATH = gearcoleco/platforms/libretro
gearcoleco_HASH = ed5095b3ce158ff0cf82395a61c2476505634894

###############################

include ../../all/cores/Makefile
