#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "vp_defines.h"
#include "api.h"
#include "config.h"
#include "msettings.h"
#include "audio_manager.h"
#include "display_helper.h"
#include "ffplay_engine.h"

// PID of the currently running ffplay child process (0 = none).
// volatile sig_atomic_t: read by FfplayEngine_signalStop() from the app's
// SIGINT/SIGTERM handler, so it must be async-signal-safe to access.
static volatile sig_atomic_t ffplay_pid = 0;

// Set from the signal handler when the app is quitting: prevents audio-change
// restarts and lets the wait loop kill a child forked in the window before
// ffplay_pid was published.
static volatile sig_atomic_t ffplay_stop = 0;

// Handoff file where the patched ffplay reports playback position (tmpfs)
#define FFPLAY_POS_FILE "/tmp/ffplay.pos"

// Captured ffplay stderr (tmpfs). ffplay exits 0 even when it fails to OPEN the
// input, so the exit code alone can't tell a real failure from a normal quit;
// we scan this file for the fatal-open error line instead.
#define FFPLAY_ERR_FILE "/tmp/ffplay.err"

// Set by FfplayEngine_play when the last run failed to open/play its input.
static bool last_playback_failed = false;

// Audio device change detection during ffplay playback
static volatile bool audio_device_changed = false;

static void on_ffplay_audio_changed(int sink_type) {
	(void)sink_type;
	audio_device_changed = true;
}

// Build argv for ffplay and exec in a forked child. Returns exit status.
// Escape a value for inclusion inside single quotes in an ffmpeg filtergraph.
// Inside '...' only the closing quote is special, so a literal ' must be written
// as '\'' (close-quote, backslash-quote, reopen-quote). Without this, a subtitle
// path containing an apostrophe (e.g. "my'movie.srt") ends the quoted value early
// and the rest of the filename is parsed as filtergraph syntax, breaking playback.
static void escape_filter_squote(const char* in, char* out, size_t out_size) {
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 4 < out_size; i++) {
		if (in[i] == '\'') {
			out[j++] = '\'';
			out[j++] = '\\';
			out[j++] = '\'';
			out[j++] = '\'';
		} else {
			out[j++] = in[i];
		}
	}
	out[j] = '\0';
}

// ffplay/ffmpeg exits 0 even when it fails to OPEN the input (e.g. "Invalid
// data found when processing input", "Server returned 403 Forbidden"). On a
// fatal open failure it prints a line beginning with the input URL followed by
// ':'. Detect that in the captured stderr — distinct from benign mid-stream
// warnings like "[https @ ...] Stream ends prematurely" which a working stream
// may print. Returns true if a fatal open error was found.
static bool ffplay_stderr_has_fatal(const char* url) {
	FILE* f = fopen(FFPLAY_ERR_FILE, "r");
	if (!f)
		return false;
	size_t urllen = strlen(url);
	char line[1024];
	bool fatal = false;
	while (fgets(line, sizeof(line), f)) {
		if (urllen > 0 && strncmp(line, url, urllen) == 0 && line[urllen] == ':') {
			LOG_error("ffplay input error: %s", line);
			fatal = true;
			break;
		}
	}
	fclose(f);
	return fatal;
}

static int ffplay_exec(FfplayConfig* config, int use_subs) {
	char* argv[64];
	int argc = 0;

	// Fresh capture file per run; if the child's redirect fails to open it, the
	// scanner simply finds no file (no false positive).
	unlink(FFPLAY_ERR_FILE);

	argv[argc++] = FFPLAY_PATH;
	argv[argc++] = "-fs";		// Fullscreen
	argv[argc++] = "-autoexit"; // Exit when video ends
	argv[argc++] = "-loglevel";
	argv[argc++] = "error";

	// Resample inside ffplay (swresample) to the sink's preferred rate so the
	// audio device opens at a rate ALSA plug passes through — otherwise plug
	// linear-resamples the stream rate into dmix (same artifact class fixed in
	// the music player and emulators). Rate re-picks automatically on sink
	// changes because this engine restarts ffplay for them (see run loop).
	static char af_str[32];
	snprintf(af_str, sizeof(af_str), "aresample=%d", AudioMgr_pickRate(48000));
	argv[argc++] = "-af";
	argv[argc++] = af_str;

	// Seek position
	char seek_str[32];
	if (config->start_position_sec > 0) {
		snprintf(seek_str, sizeof(seek_str), "%d", config->start_position_sec);
		argv[argc++] = "-ss";
		argv[argc++] = seek_str;
	}

	// Downscale decoded frames to screen width — reduces renderer workload by
	// avoiding pushing more pixels than the display can show.
	// Note: the decoder still works at full resolution; this is a post-decode scale.
	// min(w,iw) is a no-op for content already <= screen width.
	char scale_filter[128] = "";
	if (config->screen_width > 0) {
		snprintf(scale_filter, sizeof(scale_filter),
				 "scale='min(%d,iw)':-2:flags=fast_bilinear", config->screen_width);
	}

	// Subtitle filters
	// When multiple external subs are available, each becomes a separate -vf entry
	// plus one empty -vf for "subtitles off". D-pad DOWN cycles through them in ffplay.
	char vf_strs[MAX_SUBTITLE_FILES + 1][1024];
	char vf_str[1024];

	const char* sub_fontname = (CFG_getFontId() == 1) ? "MiSans Semibold" : "Rounded Mplus 1c Bold";

	if (use_subs && config->subtitle_count > 0) {
		// Disable embedded subtitle streams — external vfilters handle subtitles instead.
		// Without this, embedded subs render on top and hide external subtitle changes.
		argv[argc++] = "-sn";
		// Multiple external subtitle files — one -vf per file + empty for "off"
		for (int i = 0; i < config->subtitle_count; i++) {
			char sub_esc[2048];
			escape_filter_squote(config->subtitle_paths[i], sub_esc, sizeof(sub_esc));
			if (scale_filter[0]) {
				snprintf(vf_strs[i], sizeof(vf_strs[0]),
						 "%s,subtitles='%s':fontsdir='%s':force_style='Fontname=%s,FontSize=32'",
						 scale_filter, sub_esc, RES_PATH, sub_fontname);
			} else {
				snprintf(vf_strs[i], sizeof(vf_strs[0]),
						 "subtitles='%s':fontsdir='%s':force_style='Fontname=%s,FontSize=32'",
						 sub_esc, RES_PATH, sub_fontname);
			}
			argv[argc++] = "-vf";
			argv[argc++] = vf_strs[i];
		}
		// "Subtitles off" entry — still apply scale filter if set
		if (scale_filter[0]) {
			snprintf(vf_strs[config->subtitle_count], sizeof(vf_strs[0]), "%s", scale_filter);
			argv[argc++] = "-vf";
			argv[argc++] = vf_strs[config->subtitle_count];
		} else {
			vf_strs[config->subtitle_count][0] = '\0';
			argv[argc++] = "-vf";
			argv[argc++] = vf_strs[config->subtitle_count];
		}
	} else if (use_subs && config->subtitle_path[0] != '\0') {
		// Single subtitle (legacy path: embedded or single external)
		// fontsdir: system fontconfig has no fonts, so point to our bundled font
		// force_style: only for external subs (SRT has no styling); skip for embedded
		//              ASS/SSA which have their own fonts and positioning
		char sub_esc[2048];
		escape_filter_squote(config->subtitle_path, sub_esc, sizeof(sub_esc));
		if (config->subtitle_is_external) {
			snprintf(vf_str, sizeof(vf_str),
					 "%s%ssubtitles='%s':fontsdir='%s':force_style='Fontname=%s,FontSize=32'",
					 scale_filter, scale_filter[0] ? "," : "",
					 sub_esc, RES_PATH, sub_fontname);
		} else {
			snprintf(vf_str, sizeof(vf_str),
					 "%s%ssubtitles='%s':fontsdir='%s'",
					 scale_filter, scale_filter[0] ? "," : "",
					 sub_esc, RES_PATH);
		}
		argv[argc++] = "-vf";
		argv[argc++] = vf_str;
	} else {
		// No subtitle filters — disable ffplay's built-in subtitle stream decoder
		// so it doesn't auto-render embedded subs (saves CPU, especially for HEVC)
		argv[argc++] = "-sn";
		if (scale_filter[0]) {
			snprintf(vf_str, sizeof(vf_str), "%s", scale_filter);
			argv[argc++] = "-vf";
			argv[argc++] = vf_str;
		}
	}

	// Window title
	if (config->title[0] != '\0') {
		argv[argc++] = "-window_title";
		argv[argc++] = config->title;
	}

	// Common playback options for all sources
	argv[argc++] = "-framedrop"; // Drop frames if decoding too slow
	argv[argc++] = "-fast";		 // Enable speed-optimized decoding
	// Skip expensive decode steps — critical for HEVC on this ARM chip,
	// negligible quality impact for H.264 on a small screen.
	argv[argc++] = "-skip_loop_filter"; // Skip deblocking/SAO filter
	argv[argc++] = "all";
	argv[argc++] = "-skip_idct"; // Skip IDCT on non-reference frames
	argv[argc++] = "noref";		 // note: "noref" not "nonref"

	// Position reporting for resume (local files only — streams have no position)
	if (!config->is_stream) {
		argv[argc++] = "-posfile";
		argv[argc++] = FFPLAY_POS_FILE;
	}

	// Stream-specific buffering options
	if (config->is_stream) {
		argv[argc++] = "-infbuf"; // Disable buffer size limit for live streams
		argv[argc++] = "-probesize";
		argv[argc++] = "5000000"; // 5MB probe size
		argv[argc++] = "-analyzeduration";
		argv[argc++] = "5000000";	  // 5 seconds analysis
		argv[argc++] = "-user_agent"; // YouTube CDN requires a browser User-Agent
		argv[argc++] = "Mozilla/5.0";
		argv[argc++] = "-reconnect";
		argv[argc++] = "1";
		argv[argc++] = "-reconnect_streamed";
		argv[argc++] = "1";
		argv[argc++] = "-reconnect_delay_max";
		argv[argc++] = "5"; // Retry up to 5s before giving up
	}

	// ClearKey decryption for DASH DRM streams (CENC)
	if (config->decryption_key[0] != '\0') {
		argv[argc++] = "-cenc_decryption_key";
		argv[argc++] = config->decryption_key;
	}

	// Input file (must be last)
	argv[argc++] = "-i";
	argv[argc++] = config->path;
	argv[argc] = NULL;


	// Mute speaker amp before ffplay opens audio device to prevent pop
	PLAT_overrideMute(1);

	// Fork and exec ffplay
	pid_t child = fork();
	if (child < 0) {
		LOG_error("fork() failed: %s\n", strerror(errno));
		SetVolume(GetVolume());
		return -1;
	}

	if (child == 0) {
		// Child process: ensure ALSA finds .asoundrc managed by audiomon
		setenv("HOME", USERDATA_PATH, 1);
		// Generate a minimal fontconfig pointing to system fonts directory
		// so fontconfig doesn't scan the entire filesystem (~13s startup delay)
		if (use_subs) {
			FILE* fc = fopen("/tmp/ffplay-fonts.conf", "w");
			if (fc) {
				fprintf(fc,
						"<?xml version=\"1.0\"?>\n"
						"<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
						"<fontconfig>\n"
						"\t<dir>" RES_PATH "</dir>\n"
						"\t<cachedir>/tmp/fontconfig-cache</cachedir>\n"
						"</fontconfig>\n");
				fclose(fc);
				setenv("FONTCONFIG_FILE", "/tmp/ffplay-fonts.conf", 1);
			}
		}
		// Close inherited file descriptors (especially DRM) before exec.
		// Prevents the child's ffplay from sharing the parent's DRM fd,
		// which would cause DRM master conflicts on TG5050.
		for (int fd = 3; fd < 256; fd++)
			close(fd);
		// Redirect ffplay's stderr to a file so the parent can detect fatal
		// open errors (ffplay masks them with a 0 exit code).
		int efd = open(FFPLAY_ERR_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (efd >= 0) {
			dup2(efd, STDERR_FILENO);
			close(efd);
		}
		execv(FFPLAY_PATH, argv);
		_exit(127);
	}

	// Publish the child pid for the quit-signal handler. A signal arriving
	// between fork() and this store is covered by the ffplay_stop check in
	// the wait loop below.
	ffplay_pid = child;

	// Parent process: restore volume after ffplay opens audio device
	// ffplay needs time to probe media, open codecs, and open ALSA device
	usleep(2000000); // 2s for ffplay to fully initialize audio
	SetVolume(GetVolume());

	// Start monitoring for audio device changes (BT connect/disconnect, USB DAC)
	audio_device_changed = false;
	AudioMgr_setCallback(on_ffplay_audio_changed);

	// Wait for ffplay to exit, polling for audio device changes
	int status = 0;
	int result;
	while (1) {
		result = waitpid(child, &status, WNOHANG);
		if (result > 0) {
			ffplay_pid = 0; // reaped — the quit handler must not signal it anymore
			break;			// ffplay exited
		}
		if (result == -1 && errno != EINTR) {
			ffplay_pid = 0;
			break; // error
		}

		AudioMgr_pollEvents();

		if (ffplay_stop) {
			// Quitting (SIGINT/SIGTERM) — make sure the child dies even if the
			// handler ran before ffplay_pid was published (fork race)
			kill(child, SIGTERM);
		}

		if (audio_device_changed) {
			// Audio output changed — kill ffplay so it can be restarted
			// with the new ALSA device (audiomon has updated .asoundrc)
			kill(child, SIGTERM);
			usleep(100000);
			kill(child, SIGKILL);
			ffplay_pid = 0; // clear before reaping so the handler never signals a reaped pid
			waitpid(child, NULL, 0);
			AudioMgr_setCallback(NULL);
			return FFPLAY_EXIT_AUDIO_CHANGED;
		}

		usleep(100000); // 100ms between polls
	}

	AudioMgr_setCallback(NULL);

	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code != 0) {
			LOG_error("ffplay exited with code %d, url: %s\n", code, config->path);
		}
		return code;
	}
	return -1;
}

bool FfplayEngine_lastFailed(void) {
	return last_playback_failed;
}

void FfplayEngine_signalStop(void) {
	// Async-signal-safe: only touches sig_atomic_t values and calls kill(2);
	// errno is preserved so an interrupted syscall's error check isn't clobbered
	ffplay_stop = 1;
	pid_t pid = (pid_t)ffplay_pid;
	if (pid > 0) {
		int saved_errno = errno;
		kill(pid, SIGTERM);
		errno = saved_errno;
	}
}

int FfplayEngine_play(FfplayConfig* config) {
	if (!config || config->path[0] == '\0') {
		return -1;
	}

	// Check if ffplay binary exists before doing anything
	if (access(FFPLAY_PATH, X_OK) != 0) {
		LOG_error("ffplay binary not found: %s\n", FFPLAY_PATH);
		return -1;
	}

	LOG_info("ffplay: playing %s\n", config->path);

	last_playback_failed = false;

	// Release joysticks so ffplay can use them for input.
	PAD_quit();

	// TG5050: release display before ffplay so only one process uses KMSDRM
	DisplayHelper_prepareForExternal();

	int has_subs = (config->subtitle_path[0] != '\0') || (config->subtitle_count > 0);
	int exit_code;

	// Track playback time so we can approximate seek position on restart
	struct timespec play_start;
	clock_gettime(CLOCK_MONOTONIC, &play_start);
	int original_start = config->start_position_sec;

	// Remove any stale position report from a previous playback
	unlink(FFPLAY_POS_FILE);

	do {
		exit_code = ffplay_exec(config, has_subs);

		if (exit_code == FFPLAY_EXIT_AUDIO_CHANGED) {
			// Resume near where we left off (local files only). Prefer the
			// exact position ffplay reported — correct across pause/seek —
			// and fall back to elapsed wall-clock time.
			if (!config->is_stream) {
				int pos_sec, dur_sec;
				if (FfplayEngine_getLastPosition(&pos_sec, &dur_sec)) {
					config->start_position_sec = pos_sec;
				} else {
					struct timespec now;
					clock_gettime(CLOCK_MONOTONIC, &now);
					int elapsed = (int)(now.tv_sec - play_start.tv_sec);
					config->start_position_sec = original_start + elapsed;
				}
			}

			// Brief pause for new audio device to settle
			usleep(500000);
			LOG_info("ffplay: restarting for audio device change\n");
		}
	} while (exit_code == FFPLAY_EXIT_AUDIO_CHANGED && !ffplay_stop);

	// Restore original start position
	config->start_position_sec = original_start;

	// ffplay may exit 0 despite failing to open the input; check its stderr for
	// the fatal-open line so callers can report a real playback failure.
	if (ffplay_stderr_has_fatal(config->path))
		last_playback_failed = true;

	// TG5050: restore display after ffplay exits
	DisplayHelper_recoverDisplay();

	// Re-initialize input and clear stale button states
	PAD_init();
	PAD_reset();

	return exit_code;
}

bool FfplayEngine_getLastPosition(int* pos_sec, int* dur_sec) {
	FILE* f = fopen(FFPLAY_POS_FILE, "r");
	if (!f)
		return false;

	int p = 0, d = 0;
	int n = fscanf(f, "%d %d", &p, &d);
	fclose(f);

	if (n < 1 || p < 0)
		return false;
	if (pos_sec)
		*pos_sec = p;
	if (dur_sec)
		*dur_sec = (n >= 2 && d > 0) ? d : 0;
	return true;
}
