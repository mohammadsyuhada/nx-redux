#include "ma_internal.h"
#include "utils.h"
#include "config.h"
#include "ma_game.h"
#include "ma_saves.h"
#include <zip.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <sys/wait.h>
#include <SDL2/SDL_image.h>

#define SEVENZIP_PATH SDCARD_PATH "/.system/shared/bin/7zzs.aarch64"

void Game_open(char* path) {
	int skipzip = 0;
	memset(&game, 0, sizeof(game));

	strcpy((char*)game.path, path);
	strcpy((char*)game.name, baseName(path));
	strcpy((char*)game.alt_name, game.name); // default it

	// check first if the rom already is alive in tmp folder if so skip unzipping shit
	char tmpfldr[255];
	snprintf(tmpfldr, sizeof(tmpfldr), "/tmp/nextarch/%s", core.tag);
	char* tmppath = findFileInDir(tmpfldr, game.name);
	if (tmppath) {
		// Verify the file exists and has non-zero size (not being written or truncated)
		struct stat st;
		if (stat(tmppath, &st) == 0 && st.st_size > 0) {
			printf("File exists skipping unzipping and setting game.tmp_path: %s\n", tmppath);
			strcpy((char*)game.tmp_path, tmppath);
			skipzip = 1;
			// Update the game name to the extracted file name instead of the zip name
			if (CFG_getUseExtractedFileName())
				strcpy((char*)game.alt_name, strrchr(game.tmp_path, '/') + 1);
		} else {
			printf("File exists but is empty or inaccessible, will re-extract: %s\n", tmppath);
		}
		free(tmppath);
	} else {
		printf("File does not exist in %s\n", tmpfldr);
	}

	// if we have an archive
	const char* archive_ext = NULL;
	if (suffixMatch(".zip", game.path))
		archive_ext = "zip";
	else if (suffixMatch(".7z", game.path))
		archive_ext = "7z";

	if (archive_ext && !skipzip) {
		int supports_archive = 0;
		int i = 0;
		char* ext;
		char exts[128];
		char* extensions[32];
		strcpy(exts, core.extensions);
		while (i < 31 && (ext = strtok(i ? NULL : exts, "|"))) {
			extensions[i++] = ext;
			if (!strcmp(archive_ext, ext)) {
				supports_archive = 1;
				break;
			}
		}
		extensions[i] = NULL;

		// if the core doesn't support this archive type natively
		if (!supports_archive) {
			// extract the rom inside game.path into /tmp/nextarch, setting game.tmp_path
			int extracted = !strcmp("zip", archive_ext) ? extract_zip(extensions) : extract_7z(extensions);
			if (!extracted)
				return;
			// Update the game name to the extracted file name instead of the archive name
			if (CFG_getUseExtractedFileName())
				strcpy((char*)game.alt_name, strrchr(game.tmp_path, '/') + 1);
		}
	}

	// some cores handle opening files themselves, eg. pcsx_rearmed
	// if the frontend tries to load a 500MB file itself bad things happen
	if (!core.need_fullpath) {
		path = game.tmp_path[0] == '\0' ? game.path : game.tmp_path;

		FILE* file = fopen(path, "r");
		if (file == NULL) {
			LOG_error("Error opening game: %s\n\t%s\n", path, strerror(errno));
			return;
		}

		fseek(file, 0, SEEK_END);
		long file_size = ftell(file);
		if (file_size < 0) {
			LOG_error("Couldn't get size of file: %s\n", path);
			fclose(file);
			return;
		}
		game.size = file_size;

		rewind(file);
		game.data = malloc(game.size);
		if (game.data == NULL) {
			LOG_error("Couldn't allocate memory for file: %s\n", path);
			fclose(file);
			return;
		}

		if (fread(game.data, sizeof(uint8_t), game.size, file) != game.size) {
			LOG_error("Error reading game data: %s\n", path);
			free(game.data);
			game.data = NULL;
			fclose(file);
			return;
		}

		fclose(file);
	}

	char m3u_path[MAX_PATH];
	if (M3U_findForRom(game.path, m3u_path, sizeof(m3u_path))) {
		strcpy(game.m3u_path, m3u_path);
		strcpy((char*)game.name, strrchr(m3u_path, '/') + 1);
		strcpy((char*)game.alt_name, game.name); // default it
	}

	game.is_open = 1;
}
void Game_close(void) {
	if (game.data)
		free(game.data);
	// why delete tempfile? keep it for next time when loading the game its much faster from /tmp ram folder
	// if (game.tmp_path[0]) remove(game.tmp_path);
	game.is_open = 0;
	VIB_setStrength(0); // just in case
}

struct retro_disk_control_ext_callback disk_control_ext;
int Game_changeDisc(char* path) {
	// only populated when the core registered a disk-control interface; a stray
	// folder-named .m3u next to a non-disc core's roms can get us here without one
	if (!disk_control_ext.replace_image_index) {
		LOG_warn("Game_changeDisc: core has no disk control interface\n");
		return 0;
	}
	if (exactMatch(game.path, path))
		return 1; // requested disc is already loaded
	if (!exists(path))
		return 0;

	Game_close();
	Game_open(path);

	struct retro_game_info game_info = {};
	game_info.path = game.path;
	game_info.data = game.data;
	game_info.size = game.size;

	disk_control_ext.replace_image_index(0, &game_info);
	putFile(CHANGE_DISC_PATH, path); // NextUI still needs to know this to update recents.txt

	// The snapshot belongs to the disc that was just ejected.
	State_invalidateUndo();
	return 1;
}

int extract_zip(char** extensions) {
	// only runs on the main thread at game load; a big buffer keeps the
	// read/write syscall count sane for multi-MB roms
	static char buf[65536];
	struct zip* za;
	int ze;
	int result = 0;
	if ((za = zip_open(game.path, 0, &ze)) == NULL) {
		zip_error_t error;
		zip_error_init_with_code(&error, ze);
		LOG_error("can't open zip archive `%s': %s\n", game.path, zip_error_strerror(&error));
		return 0;
	}

	mkdir("/tmp/nextarch", 0777);
	char tmp_dirname[255];
	snprintf(tmp_dirname, sizeof(tmp_dirname), "%s/%s", "/tmp/nextarch", core.tag);
	mkdir(tmp_dirname, 0777);

	int i, len;
	int fd;
	struct zip_file* zf;
	struct zip_stat sb;
	long long sum;
	for (i = 0; i < zip_get_num_entries(za, 0); i++) {
		if (zip_stat_index(za, i, 0, &sb) == 0) {
			// POSIX basename() may modify its argument, so never hand it
			// libzip's internal entry-name buffer directly
			char entry_name[MAX_PATH];
			snprintf(entry_name, sizeof(entry_name), "%s", sb.name);
			len = strlen(sb.name);
			if (len > 0 && sb.name[len - 1] == '/') {
				snprintf(game.tmp_path, sizeof(game.tmp_path), "%s/%s", tmp_dirname, basename(entry_name));
			} else {
				int found = 0;
				char extension[34];
				for (int e = 0; extensions[e]; e++) {
					snprintf(extension, sizeof(extension), ".%s", extensions[e]);
					if (suffixMatch(extension, sb.name)) {
						found = 1;
						break;
					}
				}
				if (!found)
					continue;

				snprintf(game.tmp_path, sizeof(game.tmp_path), "%s/%s", tmp_dirname, basename(entry_name));

				// Check if file already exists and has the correct size
				struct stat st;
				if (stat(game.tmp_path, &st) == 0 && st.st_size == sb.size) {
					// File already exists with correct size, skip extraction
					result = 1;
					goto done;
				}

				zf = zip_fopen_index(za, i, 0);
				if (!zf) {
					LOG_error("zip_fopen_index failed\n");
					goto done;
				}

				// Try to create file exclusively first to avoid race condition
				fd = open(game.tmp_path, O_RDWR | O_CREAT | O_EXCL, 0644);
				if (fd < 0) {
					if (errno == EEXIST) {
						// File was created by another process, verify it's complete
						zip_fclose(zf);
						if (stat(game.tmp_path, &st) == 0 && st.st_size == sb.size) {
							result = 1;
							goto done;
						}
						// File exists but wrong size, try to truncate and rewrite
						fd = open(game.tmp_path, O_RDWR | O_TRUNC, 0644);
						if (fd < 0) {
							LOG_error("open failed after EEXIST: %s\n", strerror(errno));
							goto done;
						}
						zf = zip_fopen_index(za, i, 0);
						if (!zf) {
							LOG_error("zip_fopen_index failed on retry\n");
							close(fd);
							goto done;
						}
					} else {
						LOG_error("open failed: %s\n", strerror(errno));
						zip_fclose(zf);
						goto done;
					}
				}

				sum = 0;
				while (sum != sb.size) {
					zip_int64_t n = zip_fread(zf, buf, sizeof(buf));
					if (n < 0) {
						LOG_error("zip_fread failed\n");
						close(fd);
						zip_fclose(zf);
						goto done;
					}
					if (n == 0) {
						// EOF before the header-declared size: corrupt archive
						LOG_error("zip entry truncated: %s\n", sb.name);
						close(fd);
						zip_fclose(zf);
						goto done;
					}
					if (write(fd, buf, n) != n) {
						LOG_error("write failed: %s\n", strerror(errno));
						close(fd);
						zip_fclose(zf);
						goto done;
					}
					sum += n;
				}
				close(fd);
				zip_fclose(zf);
				result = 1;
				goto done;
			}
		}
	}

done:
	if (zip_close(za) == -1) {
		LOG_error("can't close zip archive `%s'\n", game.path);
	}

	return result;
}

int extract_7z(char** extensions) {
	mkdir("/tmp/nextarch", 0777);
	char tmp_dirname[255];
	snprintf(tmp_dirname, sizeof(tmp_dirname), "%s/%s", "/tmp/nextarch", core.tag);
	mkdir(tmp_dirname, 0777);

	// extract into a private staging dir first so this archive's files can be
	// told apart from previously cached ones in the shared core folder
	char staging[255];
	snprintf(staging, sizeof(staging), "%s/7zXXXXXX", tmp_dirname);
	if (!mkdtemp(staging)) {
		LOG_error("mkdtemp failed: %s\n", strerror(errno));
		return 0;
	}

	char out_arg[300];
	snprintf(out_arg, sizeof(out_arg), "-o%s", staging);
	static char sevenzip_bin[] = SEVENZIP_PATH;
	char* argv[] = {sevenzip_bin, "e", (char*)game.path, out_arg, "-y", NULL};

	int ok = 0;
	pid_t pid = fork();
	if (pid == 0) {
		freopen("/dev/null", "w", stdout);
		execv(argv[0], argv);
		_exit(127);
	}
	if (pid > 0) {
		int status = 0;
		waitpid(pid, &status, 0);
		ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}
	if (!ok)
		LOG_error("can't extract 7z archive `%s'\n", game.path);

	// move the first file matching a core extension up into the shared cache
	// folder, then drop the staging leftovers
	int result = 0;
	DIR* dir = opendir(staging);
	if (dir) {
		struct dirent* entry;
		char src[MAX_PATH];
		while ((entry = readdir(dir))) {
			if (entry->d_name[0] == '.')
				continue;
			snprintf(src, sizeof(src), "%s/%s", staging, entry->d_name);
			if (ok && !result) {
				int found = 0;
				char extension[34];
				for (int e = 0; extensions[e]; e++) {
					snprintf(extension, sizeof(extension), ".%s", extensions[e]);
					if (suffixMatch(extension, entry->d_name)) {
						found = 1;
						break;
					}
				}
				if (found) {
					snprintf(game.tmp_path, sizeof(game.tmp_path), "%s/%s", tmp_dirname, entry->d_name);
					if (rename(src, game.tmp_path) == 0) {
						result = 1;
						continue;
					}
					LOG_error("rename failed: %s\n", strerror(errno));
				}
			}
			unlink(src);
		}
		closedir(dir);
	}
	rmdir(staging);

	if (ok && !result)
		LOG_error("no file matching core extensions in `%s'\n", game.path);
	return result;
}
