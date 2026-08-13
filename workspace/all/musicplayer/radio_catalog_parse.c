#include "radio_catalog_parse.h"
#include <stdio.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include "parson/parson.h"

// Copy src into dst (bounded), replacing the persistence delimiters '|'/CR/LF
// with a space. Catalog strings are persisted to a '|'-delimited, newline-
// terminated store (stations.txt); those bytes in a field would corrupt it.
static void copy_delim_safe(char* dst, int dst_sz, const char* src) {
	int j = 0;
	for (int i = 0; src && src[i] && j < dst_sz - 1; i++) {
		char c = src[i];
		if (c == '|' || c == '\r' || c == '\n')
			c = ' ';
		dst[j++] = c;
	}
	dst[j] = '\0';
}

void RadioCatalog_sanitizeCode(const char* in, char* out, int out_sz) {
	int j = 0;
	for (int i = 0; in && in[i] && j < out_sz - 1; i++) {
		char ch = in[i];
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9'))
			out[j++] = ch;
	}
	out[j] = '\0';
}

void RadioCatalog_sortCountriesHomeFirst(CuratedCountry* arr, int n,
										 const char* home_code) {
	// Insertion sort: A-Z by name (case-insensitive).
	for (int i = 1; i < n; i++) {
		CuratedCountry key = arr[i];
		int j = i - 1;
		while (j >= 0 && strcasecmp(arr[j].name, key.name) > 0) {
			arr[j + 1] = arr[j];
			j--;
		}
		arr[j + 1] = key;
	}
	// Pull the home country to the front, if present.
	if (home_code && home_code[0]) {
		for (int i = 0; i < n; i++) {
			if (strcasecmp(arr[i].code, home_code) == 0) {
				CuratedCountry home = arr[i];
				for (int k = i; k > 0; k--)
					arr[k] = arr[k - 1];
				arr[0] = home;
				break;
			}
		}
	}
}

int RadioCatalog_parseCountries(const char* json, CuratedCountry* out, int max,
								const char* home_code) {
	JSON_Value* root = json_parse_string(json);
	if (!root)
		return -1;
	JSON_Array* arr = json_value_get_array(root);
	if (!arr) {
		json_value_free(root);
		return -1;
	}
	int count = 0;
	size_t total = json_array_get_count(arr);
	for (size_t i = 0; i < total && count < max; i++) {
		JSON_Object* o = json_array_get_object(arr, i);
		if (!o)
			continue;
		const char* name = json_object_get_string(o, "name");
		const char* code = json_object_get_string(o, "iso_3166_1");
		int sc = (int)json_object_get_number(o, "stationcount");
		if (!name || !code || sc <= 0)
			continue;
		CuratedCountry* c = &out[count++];
		snprintf(c->name, sizeof(c->name), "%s", name);
		snprintf(c->code, sizeof(c->code), "%s", code);
	}
	json_value_free(root);
	RadioCatalog_sortCountriesHomeFirst(out, count, home_code);
	return count;
}

// Copy the first comma-separated token of `tags` into `dst`, mapping the
// persistence delimiters '|'/CR/LF to a space (genre is persisted too).
static void first_tag(const char* tags, char* dst, int dst_sz) {
	int j = 0;
	for (int i = 0; tags && tags[i] && tags[i] != ',' && j < dst_sz - 1; i++) {
		char c = tags[i];
		if (c == '|' || c == '\r' || c == '\n')
			c = ' ';
		dst[j++] = c;
	}
	dst[j] = '\0';
}

int RadioCatalog_parseStations(const char* json, CuratedStation* out, int max,
							   const char* code) {
	JSON_Value* root = json_parse_string(json);
	if (!root)
		return -1;
	JSON_Array* arr = json_value_get_array(root);
	if (!arr) {
		json_value_free(root);
		return -1;
	}
	int count = 0;
	size_t total = json_array_get_count(arr);
	for (size_t i = 0; i < total && count < max; i++) {
		JSON_Object* o = json_array_get_object(arr, i);
		if (!o)
			continue;
		const char* name = json_object_get_string(o, "name");
		const char* resolved = json_object_get_string(o, "url_resolved");
		const char* url = json_object_get_string(o, "url");
		const char* use_url = (resolved && resolved[0]) ? resolved : url;
		if (!name || !use_url || !use_url[0])
			continue;
		const char* codec = json_object_get_string(o, "codec");
		const char* tags = json_object_get_string(o, "tags");
		int bitrate = (int)json_object_get_number(o, "bitrate");

		CuratedStation* s = &out[count];
		copy_delim_safe(s->url, sizeof(s->url), use_url);

		// Dedup by URL: radio-browser lists the same stream under multiple rows
		// (sometimes with different names). The saved store is URL-keyed
		// (Radio_stationExists / add / remove all match by URL), so duplicate-URL
		// rows would share one [+] state and look broken. Keep only the first
		// occurrence — the server pre-sorts by votes, so that's the top row.
		bool dup = false;
		for (int k = 0; k < count; k++) {
			if (strcmp(out[k].url, s->url) == 0) {
				dup = true;
				break;
			}
		}
		if (dup)
			continue;

		copy_delim_safe(s->name, sizeof(s->name), name);
		first_tag(tags, s->genre, sizeof(s->genre));
		if (strcasecmp(s->genre, "unknown") == 0)
			s->genre[0] = '\0'; // treat an "unknown" tag as no genre
		if (codec && codec[0] && bitrate > 0) {
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "%s \xC2\xB7 %dkbps", codec, bitrate);
			copy_delim_safe(s->slogan, sizeof(s->slogan), tmp);
		} else if (codec && codec[0])
			copy_delim_safe(s->slogan, sizeof(s->slogan), codec);
		else
			s->slogan[0] = '\0';
		snprintf(s->country_code, sizeof(s->country_code), "%s", code);
		count++;
	}
	json_value_free(root);
	return count;
}
