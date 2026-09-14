#ifndef __TEXT_WRAP_H__
#define __TEXT_WRAP_H__

// Pure word-wrap / ellipsis-truncate logic behind GFX_wrapText and
// GFX_truncateText. Header-only and font-agnostic (the caller supplies a
// width-measuring callback) so it can be unit-tested on the host without SDL:
// see tests/test_text_wrap.c.

#include <stddef.h>
#include <string.h>

// Returns the rendered pixel width of the NUL-terminated UTF-8 string.
typedef int (*TextWrap_measureFn)(void* ctx, const char* utf8);

#define TEXT_WRAP_LINE_MAX 512

// Copy in_name into out_name (out_size bytes), then shorten it with a trailing
// "..." until it measures <= max_width (padding is added to every measurement).
// Returns the final width including padding.
static int TextWrap_truncate(TextWrap_measureFn measure, void* ctx, const char* in_name, char* out_name,
							 size_t out_size, int max_width, int padding) {
	int text_width;
	// Bounded copy: the caller's tail can be arbitrarily long (e.g. release
	// notes wrapped to a few lines), so never trust it to fit. Cut on a UTF-8
	// character boundary so the ellipsis pass below only ever sees whole
	// characters.
	size_t len = strlen(in_name);
	if (len >= out_size) {
		len = out_size - 1;
		while (len > 0 && (in_name[len] & 0xC0) == 0x80)
			len--;
	}
	memcpy(out_name, in_name, len);
	out_name[len] = '\0';
	text_width = measure(ctx, out_name) + padding;

	while (text_width > max_width) {
		int len = strlen(out_name);
		if (len < 4) // can't append "..." without writing before out_name
			break;
		strcpy(&out_name[len - 4], "...\0");
		text_width = measure(ctx, out_name) + padding;
	}

	return text_width;
}

// Word-wrap str in place (spaces become '\n' at line breaks) to at most
// max_lines lines (0 = unlimited); the last line is ellipsis-truncated to fit.
// Returns the widest line's width.
static int TextWrap_wrap(TextWrap_measureFn measure, void* ctx, char* str, int max_width, int max_lines) {
	if (!str)
		return 0;

	int line_width;
	int max_line_width = 0;
	char* line = str;
	char buffer[TEXT_WRAP_LINE_MAX];

	line_width = measure(ctx, line);
	if (line_width <= max_width) {
		line_width = TextWrap_truncate(measure, ctx, line, buffer, sizeof(buffer), max_width, 0);
		strcpy(line, buffer);
		return line_width;
	}

	char* prev = NULL;
	char* tmp = line;
	int lines = 1;
	while (!max_lines || lines < max_lines) {
		tmp = strchr(tmp, ' ');
		if (!tmp) {
			if (prev) {
				line_width = measure(ctx, line);
				if (line_width >= max_width) {
					if (line_width > max_line_width)
						max_line_width = line_width;
					prev[0] = '\n';
					line = prev + 1;
				}
			}
			break;
		}
		tmp[0] = '\0';

		line_width = measure(ctx, line);

		if (line_width >= max_width) { // wrap
			if (line_width > max_line_width)
				max_line_width = line_width;
			tmp[0] = ' ';
			// No earlier space to break at (a single word wider than the line):
			// break here instead so the word becomes its own line.
			if (!prev)
				prev = tmp;
			tmp += 1;
			prev[0] = '\n';
			prev += 1;
			line = prev;
			lines += 1;
		} else { // continue
			tmp[0] = ' ';
			prev = tmp;
			tmp += 1;
		}
	}

	line_width = TextWrap_truncate(measure, ctx, line, buffer, sizeof(buffer), max_width, 0);
	strcpy(line, buffer);

	if (line_width > max_line_width)
		max_line_width = line_width;
	return max_line_width;
}

#endif // __TEXT_WRAP_H__
