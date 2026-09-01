# Desktop-Linux build env for the AppImage. ubuntu:22.04 = glibc 2.35 floor
# so the artifact runs on most current distros. Nothing here is hosted by
# us: base image + a pinned appimagetool are the only external pulls.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential make cmake git ca-certificates pkg-config wget file curl \
	python3 \
	libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
	libsamplerate0-dev libsqlite3-dev liblz4-dev libzip-dev zlib1g-dev \
	xvfb xdotool imagemagick \
	&& rm -rf /var/lib/apt/lists/*
# appimagetool, pinned. Release 13 is upstream's last AppImageKit tag; it was
# later marked obsolete and its assets renamed with an "obsolete-" prefix
# (upstream now points new consumers at AppImage/appimagetool's "continuous"
# build instead), so the download URL below targets the renamed asset while
# still resolving to the same pinned release/binary the plan called for.
# Fill SHA256 on first fetch (see plan Task 10 Step 2) and treat any later
# mismatch as a supply-chain stop-the-line.
ARG AIT_URL=https://github.com/AppImage/AppImageKit/releases/download/13/obsolete-appimagetool-x86_64.AppImage
ARG AIT_SHA256=df3baf5ca5facbecfc2f3fa6713c29ab9cefa8fd8c1eac5d283b79cab33e4acb
RUN wget -q "$AIT_URL" -O /usr/local/bin/appimagetool \
	&& echo "$AIT_SHA256  /usr/local/bin/appimagetool" | sha256sum -c - \
	&& chmod +x /usr/local/bin/appimagetool
WORKDIR /work
