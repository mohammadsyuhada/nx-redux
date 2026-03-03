#include "scraper_api.h"
#include "http.h"
#include "cjson/cJSON.h"
#include "utils.h"
#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#define SS_API_BASE "https://api.screenscraper.fr/api2"
#define SS_SOFTNAME "nextui-scraper"
#define SS_RATE_LIMIT_MS 1200 // 1.2 seconds between requests

// Obfuscated dev credentials (XOR key 0x5A)
// Replace these arrays with your actual obfuscated credentials.
// To generate: for each char c in your string, store c ^ 0x5A
#define SS_XOR_KEY 0x5A
static const unsigned char ss_devid_enc[] = {
	0x31, 0x35, 0x2e, 0x3b, 0x31, 0x29, 0x3f, 0x37, 0x2a, 0x33, 0x2e};
static const unsigned char ss_devpass_enc[] = {
	0x12, 0x1e, 0x6f, 0x33, 0x19, 0x12, 0x3b, 0x39, 0x0a, 0x1b, 0x0d};

static void ss_decode(const unsigned char* enc, int len, char* out) {
	for (int i = 0; i < len; i++)
		out[i] = enc[i] ^ SS_XOR_KEY;
	out[len] = '\0';
}

static uint64_t last_request_time = 0;
static char ss_username[64] = "";
static char ss_password[128] = "";

// Shell-escape a string for safe use in system() calls (wraps in single quotes)
static char* shell_escape(const char* str) {
	if (!str)
		return strdup("''");
	size_t len = strlen(str);
	size_t quotes = 0;
	for (size_t i = 0; i < len; i++) {
		if (str[i] == '\'')
			quotes++;
	}
	char* escaped = malloc(len + quotes * 3 + 3);
	if (!escaped)
		return NULL;
	char* p = escaped;
	*p++ = '\'';
	for (size_t i = 0; i < len; i++) {
		if (str[i] == '\'') {
			*p++ = '\'';
			*p++ = '"';
			*p++ = '\'';
			*p++ = '"';
			*p++ = '\'';
		} else {
			*p++ = str[i];
		}
	}
	*p++ = '\'';
	*p = '\0';
	return escaped;
}

void ScraperAPI_init(void) {
	last_request_time = 0;
	ss_username[0] = '\0';
	ss_password[0] = '\0';
}

void ScraperAPI_setUserCredentials(const char* username, const char* password) {
	if (username)
		snprintf(ss_username, sizeof(ss_username), "%s", username);
	else
		ss_username[0] = '\0';
	if (password)
		snprintf(ss_password, sizeof(ss_password), "%s", password);
	else
		ss_password[0] = '\0';
}

bool ScraperAPI_hasUserCredentials(void) {
	return ss_username[0] != '\0' && ss_password[0] != '\0';
}

void ScraperAPI_rateLimit(void) {
	if (last_request_time == 0) {
		last_request_time = getMicroseconds();
		return;
	}
	uint64_t now = getMicroseconds();
	uint64_t elapsed_ms = (now - last_request_time) / 1000;
	if (elapsed_ms < SS_RATE_LIMIT_MS) {
		usleep((SS_RATE_LIMIT_MS - elapsed_ms) * 1000);
	}
	last_request_time = getMicroseconds();
}

bool ScraperAPI_isOnline(void) {
	return PWR_isOnline();
}

// Region priority: us > wor > eu > any
static const char* region_priority[] = {"us", "wor", "eu", NULL};

// Extract preferred media URL from medias array
// Tries types in preference order, then regions by priority
static void extractMediaURL(cJSON* medias, const char** types, int type_count,
							char* out_url, int url_size) {
	out_url[0] = '\0';

	for (int t = 0; t < type_count; t++) {
		// First pass: try each preferred region in priority order
		for (int r = 0; region_priority[r] != NULL; r++) {
			cJSON* media;
			cJSON_ArrayForEach(media, medias) {
				cJSON* type = cJSON_GetObjectItem(media, "type");
				if (!type || !cJSON_IsString(type))
					continue;
				if (strcmp(type->valuestring, types[t]) != 0)
					continue;
				cJSON* region = cJSON_GetObjectItem(media, "region");
				cJSON* url = cJSON_GetObjectItem(media, "url");
				if (!url || !cJSON_IsString(url))
					continue;
				if (region && cJSON_IsString(region) &&
					strcmp(region->valuestring, region_priority[r]) == 0) {
					snprintf(out_url, url_size, "%s", url->valuestring);
					return;
				}
			}
		}

		// Second pass: take any available for this type
		cJSON* media;
		cJSON_ArrayForEach(media, medias) {
			cJSON* type = cJSON_GetObjectItem(media, "type");
			if (!type || !cJSON_IsString(type))
				continue;
			if (strcmp(type->valuestring, types[t]) != 0)
				continue;
			cJSON* url = cJSON_GetObjectItem(media, "url");
			if (!url || !cJSON_IsString(url))
				continue;
			snprintf(out_url, url_size, "%s", url->valuestring);
			return;
		}
	}
}

static bool computeCRC32(const char* filepath, uint32_t* out_crc, long* out_size) {
	FILE* f = fopen(filepath, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	*out_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uLong crc = crc32(0L, Z_NULL, 0);
	unsigned char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		crc = crc32(crc, buf, n);
	fclose(f);
	*out_crc = (uint32_t)crc;
	return true;
}

// Parse game info from API JSON response
static bool parseSearchResponse(HTTP_Response* resp, ScraperGameInfo* info) {
	if (!resp)
		return false;
	if (resp->http_status != 200 || !resp->data) {
		HTTP_freeResponse(resp);
		return false;
	}

	cJSON* root = cJSON_Parse(resp->data);
	HTTP_freeResponse(resp);
	if (!root)
		return false;

	cJSON* response = cJSON_GetObjectItem(root, "response");
	if (!response) {
		cJSON_Delete(root);
		return false;
	}

	cJSON* jeu = cJSON_GetObjectItem(response, "jeu");
	if (!jeu) {
		cJSON_Delete(root);
		return false;
	}

	info->found = true;

	// Extract game name (prefer English, fallback to any)
	cJSON* noms = cJSON_GetObjectItem(jeu, "noms");
	if (noms && cJSON_IsArray(noms)) {
		cJSON* nom;
		bool got_name = false;
		cJSON_ArrayForEach(nom, noms) {
			cJSON* region = cJSON_GetObjectItem(nom, "region");
			cJSON* text = cJSON_GetObjectItem(nom, "text");
			if (!text || !cJSON_IsString(text))
				continue;
			if (!got_name) {
				snprintf(info->game_name, sizeof(info->game_name), "%s", text->valuestring);
				got_name = true;
			}
			if (region && cJSON_IsString(region) &&
				(strcmp(region->valuestring, "us") == 0 ||
				 strcmp(region->valuestring, "wor") == 0 ||
				 strcmp(region->valuestring, "eu") == 0)) {
				snprintf(info->game_name, sizeof(info->game_name), "%s", text->valuestring);
				break;
			}
		}
	}

	// Extract media URLs
	cJSON* medias = cJSON_GetObjectItem(jeu, "medias");
	if (medias && cJSON_IsArray(medias)) {
		const char* screenshot_types[] = {"ss", "sstitle"};
		extractMediaURL(medias, screenshot_types, 2,
						info->screenshot_url, sizeof(info->screenshot_url));

		const char* boxart_types[] = {"box-3D", "box-2D"};
		extractMediaURL(medias, boxart_types, 2,
						info->boxart_url, sizeof(info->boxart_url));

		const char* wheel_types[] = {"wheel-hd", "wheel"};
		extractMediaURL(medias, wheel_types, 2,
						info->wheel_url, sizeof(info->wheel_url));
	}

	LOG_info("Scraper: game='%s' ss='%s' box='%s' wheel='%s'\n",
			 info->game_name,
			 info->screenshot_url[0] ? info->screenshot_url : "(none)",
			 info->boxart_url[0] ? info->boxart_url : "(none)",
			 info->wheel_url[0] ? info->wheel_url : "(none)");

	cJSON_Delete(root);
	return true;
}

bool ScraperAPI_search(const char* rom_filename, const char* rom_path,
					   int system_id, ScraperGameInfo* info) {
	if (!rom_filename || !info)
		return false;

	memset(info, 0, sizeof(ScraperGameInfo));
	info->found = false;

	// URL-encode the filename (with extension for romnom)
	char* encoded_name = HTTP_urlEncode(rom_filename);
	if (!encoded_name)
		return false;

	// Decode dev credentials
	char devid[64], devpass[64];
	ss_decode(ss_devid_enc, sizeof(ss_devid_enc), devid);
	ss_decode(ss_devpass_enc, sizeof(ss_devpass_enc), devpass);

	// Build base API URL (filename only)
	char url[2048];
	int base_len = snprintf(url, sizeof(url),
							"%s/jeuInfos.php?devid=%s&devpassword=%s&softname=%s"
							"&romnom=%s&systemeid=%d&output=json",
							SS_API_BASE, devid, devpass, SS_SOFTNAME,
							encoded_name, system_id);

	// Append user credentials if configured
	if (base_len > 0 && base_len < (int)sizeof(url) &&
		ss_username[0] != '\0' && ss_password[0] != '\0') {
		char* enc_user = HTTP_urlEncode(ss_username);
		char* enc_pass = HTTP_urlEncode(ss_password);
		if (enc_user && enc_pass) {
			snprintf(url + base_len, sizeof(url) - base_len,
					 "&ssid=%s&sspassword=%s", enc_user, enc_pass);
		}
		free(enc_user);
		free(enc_pass);
	}
	free(encoded_name);

	// Save URL length before any CRC append
	int url_len = strlen(url);

	// Try 1: search by filename only
	ScraperAPI_rateLimit();
	HTTP_Response* resp = HTTP_get(url);
	if (parseSearchResponse(resp, info))
		return true;

	// Try 2: fallback with CRC32 + file size for non-standard filenames
	if (rom_path) {
		uint32_t crc_val;
		long file_size;
		if (computeCRC32(rom_path, &crc_val, &file_size)) {
			snprintf(url + url_len, sizeof(url) - url_len,
					 "&crc=%08x&romtaille=%ld", crc_val, file_size);
			ScraperAPI_rateLimit();
			resp = HTTP_get(url);
			if (parseSearchResponse(resp, info))
				return true;
		}
	}

	return false;
}

ScraperUserInfo ScraperAPI_fetchUserInfo(void) {
	ScraperUserInfo info = {0, 0, 0, false};

	if (ss_username[0] == '\0' || ss_password[0] == '\0')
		return info;

	// Decode dev credentials
	char devid[64], devpass[64];
	ss_decode(ss_devid_enc, sizeof(ss_devid_enc), devid);
	ss_decode(ss_devpass_enc, sizeof(ss_devpass_enc), devpass);

	char* enc_user = HTTP_urlEncode(ss_username);
	char* enc_pass = HTTP_urlEncode(ss_password);
	if (!enc_user || !enc_pass) {
		free(enc_user);
		free(enc_pass);
		return info;
	}

	char url[2048];
	snprintf(url, sizeof(url),
			 "%s/ssuserInfos.php?devid=%s&devpassword=%s&softname=%s"
			 "&ssid=%s&sspassword=%s&output=json",
			 SS_API_BASE, devid, devpass, SS_SOFTNAME,
			 enc_user, enc_pass);
	free(enc_user);
	free(enc_pass);

	ScraperAPI_rateLimit();

	HTTP_Response* resp = HTTP_get(url);
	if (!resp)
		return info;
	if (resp->http_status != 200 || !resp->data) {
		HTTP_freeResponse(resp);
		return info;
	}

	cJSON* root = cJSON_Parse(resp->data);
	HTTP_freeResponse(resp);
	if (!root)
		return info;

	cJSON* response = cJSON_GetObjectItem(root, "response");
	if (!response) {
		cJSON_Delete(root);
		return info;
	}

	cJSON* ssuser = cJSON_GetObjectItem(response, "ssuser");
	if (!ssuser) {
		cJSON_Delete(root);
		return info;
	}

	cJSON* req_today = cJSON_GetObjectItem(ssuser, "requeststoday");
	cJSON* max_req = cJSON_GetObjectItem(ssuser, "maxrequestsperday");
	cJSON* threads = cJSON_GetObjectItem(ssuser, "maxthreads");

	if (req_today && cJSON_IsString(req_today))
		info.requests_today = atoi(req_today->valuestring);
	else if (req_today && cJSON_IsNumber(req_today))
		info.requests_today = req_today->valueint;

	if (max_req && cJSON_IsString(max_req))
		info.max_requests_per_day = atoi(max_req->valuestring);
	else if (max_req && cJSON_IsNumber(max_req))
		info.max_requests_per_day = max_req->valueint;

	if (threads && cJSON_IsString(threads))
		info.threads = atoi(threads->valuestring);
	else if (threads && cJSON_IsNumber(threads))
		info.threads = threads->valueint;

	info.valid = true;
	cJSON_Delete(root);
	return info;
}

bool ScraperAPI_downloadFile(const char* url, const char* dest_path) {
	if (!url || !dest_path || url[0] == '\0')
		return false;

	// Shell-escape both arguments to prevent injection
	char* escaped_url = shell_escape(url);
	char* escaped_path = shell_escape(dest_path);
	if (!escaped_url || !escaped_path) {
		free(escaped_url);
		free(escaped_path);
		return false;
	}

	// Use curl for binary downloads since HTTP module is text-oriented
	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
			 "curl -s -k -L -o %s --connect-timeout 10 --max-time 30 %s",
			 escaped_path, escaped_url);
	free(escaped_url);
	free(escaped_path);

	LOG_info("Scraper download: %s\n", cmd);
	int ret = system(cmd);
	if (ret != 0) {
		LOG_error("Scraper download failed: curl exit=%d\n", ret);
		return false;
	}

	// Verify file exists and has content
	FILE* f = fopen(dest_path, "rb");
	if (!f) {
		LOG_error("Scraper download: file not created: %s\n", dest_path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);

	LOG_info("Scraper download: %s -> %ld bytes\n", dest_path, size);
	return size > 0;
}
