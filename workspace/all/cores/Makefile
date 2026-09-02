###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

###############################

# Apple's make 3.81 parses "define TEMPLATE=" as a variable named "TEMPLATE="
# and silently generates no per-core rules. Fail loudly instead.
ifneq (3.82,$(firstword $(sort $(MAKE_VERSION) 3.82)))
$(error GNU make >= 3.82 required for core builds; on macOS run gmake (brew install make))
endif

###############################

# overrides shared by all cores

# commits after this one break save states (as of 20231025)
# gpsp_HASH = 1d1c719

###############################

# this logic was broken out from picoarch's all-in-one makefile

PATCH = git apply
PROCS = -j4

###############################

# Clone/object and artifact roots. Device platforms use the flat defaults;
# desktop overrides these before including (src/<os-arch>, output/<os-arch>)
# so macOS and Linux (docker) builds in the same checkout never share compiled
# objects or clobber each other's .so files.
SRC_ROOT ?= src
OUTPUT_ROOT ?= output

BUILD_ROOT=../../$(PLATFORM)/cores

define TEMPLATE=

$1_REPO ?= https://github.com/libretro/$(1)
$1_MAKE ?= make $(and $($1_MAKEFILE),-f $($1_MAKEFILE)) platform=$(PLATFORM) $($(1)_FLAGS)
$1_BUILD_PATH ?= $(1)
$1_CORE ?= $(1)_libretro.so

$(SRC_ROOT)/$(1):
	mkdir -p $(SRC_ROOT)
	cd $(SRC_ROOT) && git clone --depth 1 $(if $($1_BRANCH),--branch $($1_BRANCH),) $$($(1)_REPO) $(1)
	$(if $($1_HASH),cd $(SRC_ROOT)/$$($1_BUILD_PATH) && git fetch --depth=1 origin $($1_HASH) && git checkout $($1_HASH) && echo $($1_HASH),)
	$(if $($1_SUBMODULES),cd $(SRC_ROOT)/$(1) && git submodule init $($1_SUBMODULES) && git submodule update $($1_SUBMODULES),cd $(SRC_ROOT)/$(1) && git submodule update --init --recursive)

# The markers must exist even when a core has no patch to apply — a missing
# marker file makes the .so target permanently stale, so the core rebuilds on
# every invocation. Create it once (guarded: re-touching an existing marker
# would make it newer than the built .so and cascade a rebuild).
$(SRC_ROOT)/$(1)/.patched: $(SRC_ROOT)/$(1)
	(test ! -f patches/$(1).patch) || (test -f $(SRC_ROOT)/$(1)/.patched) || (cd $(SRC_ROOT)/$(1) && $(PATCH) -p1 < $(CURDIR)/patches/$(1).patch)
	test -f $(SRC_ROOT)/$(1)/.patched || touch $(SRC_ROOT)/$(1)/.patched

$(SRC_ROOT)/$(1)/.patched-all:
	(test ! -d ../../all/cores/patches/$(1)) || (test -f $(SRC_ROOT)/$(1)/.patched-all) || (cd $(SRC_ROOT)/$(1) && $(foreach patch, $(sort $(wildcard ../../all/cores/patches/$(1)/*.patch)), $(PATCH) -p1 < $(CURDIR)/$(patch) &&) true)
	test -f $(SRC_ROOT)/$(1)/.patched-all || touch $(SRC_ROOT)/$(1)/.patched-all


# The target is the real artifact filename ($1_CORE, e.g. stella2014_libretro.so
# for a2600) — targeting a nonexistent $(1)_libretro.so made every renamed core
# perpetually stale and relinked on each run.
$(OUTPUT_ROOT)/$$($1_CORE): $(SRC_ROOT)/$(1)/.patched $(SRC_ROOT)/$(1)/.patched-all
	mkdir -p $(OUTPUT_ROOT)
	cd $(SRC_ROOT)/$$($1_BUILD_PATH) && $$($1_MAKE) $(PROCS)
	mv $(SRC_ROOT)/$$($1_BUILD_PATH)/$$($1_CORE) ./$(OUTPUT_ROOT)

clone-$(1): $(SRC_ROOT)/$(1)

status-$(1): $(SRC_ROOT)/$(1)
	cd $(SRC_ROOT)/$(1) && git show --oneline -s

patch-$(1): $(SRC_ROOT)/$(1)/.patched

clean-$(1):
	test ! -d $(SRC_ROOT)/$(1) || cd $(SRC_ROOT)/$$($1_BUILD_PATH) && $$($1_MAKE) clean
	rm -f $(OUTPUT_ROOT)/$$($1_CORE)

$(1): $(OUTPUT_ROOT)/$$($1_CORE)

endef

###############################

all: cores

$(foreach CORE,$(CORES),$(eval $(call TEMPLATE,$(CORE))))

cores: $(foreach CORE,$(CORES),$(CORE))
	
clean: $(foreach CORE,$(CORES),clean-$(CORE))
	
nuke:
	rm -rf ./$(OUTPUT_ROOT)
	rm -rf ./$(SRC_ROOT)