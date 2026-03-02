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

#define SS_API_BASE "https://www.screenscraper.fr/api2"
#define SS_SOFTNAME "nextui-scraper"
#define SS_RATE_LIMIT_MS 1200 // 1.2 seconds between requests

// Obfuscated dev credentials (XOR key 0x5A)
// Replace these arrays with your actual obfuscated credentials.
// To generate: for each char c in your string, store c ^ 0x5A
#define SS_XOR_KEY 0x5A
static const unsigned char ss_devid_enc[] = {
	// "CHANGEME" XOR 0x5A -> placeholder
	0x19, 0x32, 0x3b, 0x34, 0x3d, 0x3f, 0x37, 0x3f};
static const unsigned char ss_devpass_enc[] = {
	// "CHANGEME" XOR 0x5A -> placeholder
	0x19, 0x32, 0x3b, 0x34, 0x3d, 0x3f, 0x37, 0x3f};

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

// Extract preferred media URL from medias array
// Tries types in preference order, returns first match
static void extractMediaURL(cJSON* medias, const char** types, int type_count,
							const char* region_pref, char* out_url, int url_size) {
	out_url[0] = '\0';

	for (int t = 0; t < type_count; t++) {
		cJSON* media;
		cJSON_ArrayForEach(media, medias) {
			cJSON* type = cJSON_GetObjectItem(media, "type");
			if (!type || !cJSON_IsString(type))
				continue;
			if (strcmp(type->valuestring, types[t]) != 0)
				continue;

			// Prefer region match
			cJSON* region = cJSON_GetObjectItem(media, "region");
			cJSON* url = cJSON_GetObjectItem(media, "url");
			if (!url || !cJSON_IsString(url))
				continue;

			if (region_pref && region && cJSON_IsString(region) &&
				strcmp(region->valuestring, region_pref) == 0) {
				snprintf(out_url, url_size, "%s", url->valuestring);
				return;
			}

			// Take first available if no region match yet
			if (out_url[0] == '\0') {
				snprintf(out_url, url_size, "%s", url->valuestring);
			}
		}
		// If we found any URL for this type, use it
		if (out_url[0] != '\0')
			return;
	}
}

bool ScraperAPI_search(const char* rom_filename, int system_id, ScraperGameInfo* info) {
	if (!rom_filename || !info)
		return false;

	memset(info, 0, sizeof(ScraperGameInfo));
	info->found = false;

	// Strip extension from filename
	char name_no_ext[512];
	snprintf(name_no_ext, sizeof(name_no_ext), "%s", rom_filename);
	char* dot = strrchr(name_no_ext, '.');
	if (dot)
		*dot = '\0';

	// URL-encode the filename (with extension for romnom)
	char* encoded_name = HTTP_urlEncode(rom_filename);
	if (!encoded_name)
		return false;

	// Decode dev credentials
	char devid[64], devpass[64];
	ss_decode(ss_devid_enc, sizeof(ss_devid_enc), devid);
	ss_decode(ss_devpass_enc, sizeof(ss_devpass_enc), devpass);

	// Build API URL
	char url[2048];
	int len = snprintf(url, sizeof(url),
					   "%s/jeuInfos.php?devid=%s&devpassword=%s&softname=%s"
					   "&romnom=%s&systemeid=%d&output=json",
					   SS_API_BASE, devid, devpass, SS_SOFTNAME,
					   encoded_name, system_id);

	// Append user credentials if configured
	if (len > 0 && len < (int)sizeof(url) &&
		ss_username[0] != '\0' && ss_password[0] != '\0') {
		char* enc_user = HTTP_urlEncode(ss_username);
		char* enc_pass = HTTP_urlEncode(ss_password);
		if (enc_user && enc_pass) {
			snprintf(url + len, sizeof(url) - len,
					 "&ssid=%s&sspassword=%s", enc_user, enc_pass);
		}
		free(enc_user);
		free(enc_pass);
	}
	free(encoded_name);

	// Rate limit before request
	ScraperAPI_rateLimit();

	// Make request
	HTTP_Response* resp = HTTP_get(url);
	if (!resp)
		return false;
	if (resp->http_status != 200 || !resp->data) {
		HTTP_freeResponse(resp);
		return false;
	}

	// Parse JSON
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
		extractMediaURL(medias, screenshot_types, 2, "us",
						info->screenshot_url, sizeof(info->screenshot_url));

		const char* boxart_types[] = {"box-2D", "box-3D"};
		extractMediaURL(medias, boxart_types, 2, "us",
						info->boxart_url, sizeof(info->boxart_url));

		const char* wheel_types[] = {"wheel-hd", "wheel"};
		extractMediaURL(medias, wheel_types, 2, "us",
						info->wheel_url, sizeof(info->wheel_url));
	}

	cJSON_Delete(root);
	return info->found;
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
			 "curl -s -L -o %s --connect-timeout 10 --max-time 30 %s",
			 escaped_path, escaped_url);
	free(escaped_url);
	free(escaped_path);

	int ret = system(cmd);
	if (ret != 0)
		return false;

	// Verify file exists and has content
	FILE* f = fopen(dest_path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);

	return size > 0;
}
