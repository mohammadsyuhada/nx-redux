#ifndef __gametime_db_h__
#define __gametime_db_h__

#include <sqlite3.h>
#include <stdbool.h>

typedef struct ROM ROM;
typedef struct PlayActivity PlayActivity;
typedef struct PlayActivities PlayActivities;

struct ROM {
	int id;
	char* type;
	char* name;
	char* file_path;
	char* image_path;
};
struct PlayActivity {
	ROM* rom;
	int play_count;
	int play_time_total;
	int play_time_average;
	char* first_played_at;
	char* last_played_at;
};
struct PlayActivities {
	PlayActivity** play_activity;
	int count;
	int play_time_total;
};

sqlite3* play_activity_db_open(void);
void play_activity_db_close(sqlite3* ctx);
void free_play_activities(PlayActivities* pa_ptr);

// Main interface functions for read access
PlayActivities* play_activity_find_all(void);
//int play_activity_get_play_time(const char *rom_path);

// Main interface functions for write access
// PortMaster's launcher (Roms/Ports/…Portmaster.sh, Tools/PortMaster.pak) isn't a game: never timed, and its older
// rows are left out of every list and total (an SQL condition on the `rom` table's file_path).
#define GAMETIME_NOT_EXCLUDED_SQL \
	"(lower(rom.file_path) NOT LIKE '%portmaster.sh' AND lower(rom.file_path) NOT LIKE '%portmaster.pak%')"
bool play_activity_is_excluded(const char* rom_file_path);
void play_activity_start(char* rom_file_path);
void play_activity_resume(void);
void play_activity_stop(char* rom_file_path);
void play_activity_stop_all(void);
void play_activity_delete(int rom_id);
void play_activity_list_all(void);

#endif // __gametime_db_h__