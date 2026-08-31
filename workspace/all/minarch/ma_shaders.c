#include "ma_internal.h"
#include "utils.h"
#include "ma_shaders.h"
#include "ma_config.h"

void readShadersPreset(int i) {
	char shaderspath[MAX_PATH] = {0};
	sprintf(shaderspath, "%s/Shaders/%s", SDCARD_PATH, config.shaders.options[SH_SHADERS_PRESET].values[i]);
	// free the previous preset buffer before replacing it
	if (config.shaders_preset) {
		free(config.shaders_preset);
		config.shaders_preset = NULL;
	}
	if (exists(shaderspath)) {
		config.shaders_preset = allocFile(shaderspath);
		Config_readOptionsString(config.shaders_preset);
	}
}

// free a pragma option set built by loadShaderSettings (values/labels share
// the same strings and key/name/desc point into platform-owned ShaderParams)
static void freeShaderSettings(int i) {
	if (config.shaderpragmas[i].options) {
		for (int j = 0; config.shaderpragmas[i].options[j].key; j++) {
			Option* o = &config.shaderpragmas[i].options[j];
			if (o->values) {
				for (int s = 0; o->values[s]; s++)
					free(o->values[s]);
				free(o->values);
			}
			free(o->labels);
		}
		free(config.shaderpragmas[i].options);
		config.shaderpragmas[i].options = NULL;
	}
	config.shaderpragmas[i].count = 0;
}

void loadShaderSettings(int i) {
	int menucount = 0;
	freeShaderSettings(i); // called on every shader change; drop the old set
	config.shaderpragmas[i].options = calloc(32 + 1, sizeof(Option));
	if (!config.shaderpragmas[i].options)
		return;
	ShaderParam* params = PLAT_getShaderPragmas(i);
	if (params == NULL)
		return;
	for (int j = 0; j < 32; j++) {
		if (params[j].step == 0.0f) {
			// Prevent division by zero; skip this parameter or set steps to 1
			continue;
		}

		if (params[j].name[0] == '\0') {
			// Skip invalid parameter names
			continue;
		}

		int steps = (int)((params[j].max - params[j].min) / params[j].step) + 1;
		if (steps <= 0 || steps > 1024) {
			// malformed #pragma parameter (min > max, or absurd step) — a
			// negative steps would index values[steps] out of bounds below
			continue;
		}

		config.shaderpragmas[i].options[menucount].key = params[j].name;
		config.shaderpragmas[i].options[menucount].name = params[j].name;
		config.shaderpragmas[i].options[menucount].desc = params[j].name;
		config.shaderpragmas[i].options[menucount].default_value = params[j].def;

		config.shaderpragmas[i].options[menucount].values = malloc(sizeof(char*) * (steps + 1));
		config.shaderpragmas[i].options[menucount].labels = malloc(sizeof(char*) * (steps + 1));
		int filled = 0;
		if (config.shaderpragmas[i].options[menucount].values && config.shaderpragmas[i].options[menucount].labels) {
			for (int s = 0; s < steps; s++) {
				float val = params[j].min + s * params[j].step;
				char* str = malloc(16);
				if (!str)
					break; // OOM — keep the values filled so far, NULL-terminated below
				snprintf(str, 16, "%.2f", val);
				config.shaderpragmas[i].options[menucount].values[s] = str;
				config.shaderpragmas[i].options[menucount].labels[s] = str;
				filled = s + 1;
				if (fabs(params[j].value - val) < 0.001f)
					config.shaderpragmas[i].options[menucount].value = s;
			}
		}
		if (!filled) {
			// array alloc failed, or OOM before the first step filled
			free(config.shaderpragmas[i].options[menucount].values);
			free(config.shaderpragmas[i].options[menucount].labels);
			config.shaderpragmas[i].options[menucount].values = NULL;
			config.shaderpragmas[i].options[menucount].labels = NULL;
			continue;
		}
		config.shaderpragmas[i].options[menucount].count = filled;
		config.shaderpragmas[i].options[menucount].values[filled] = NULL;
		config.shaderpragmas[i].options[menucount].labels[filled] = NULL;
		menucount++;
	}
	config.shaderpragmas[i].count = menucount;
}

void Config_syncShaders(char* key, int value) {
	int i = -1;
	if (exactMatch(key, config.shaders.options[SH_SHADERS_PRESET].key)) {
		readShadersPreset(value);
		i = SH_SHADERS_PRESET;
	} else if (exactMatch(key, config.shaders.options[SH_NROFSHADERS].key)) {
		GFX_setShaders(value);
		i = SH_NROFSHADERS;
	}

	static const struct {
		int shader, filter, srctype, scaletype, upscale;
	} SH_PASS[3] = {
		{SH_SHADER1, SH_SHADER1_FILTER, SH_SRCTYPE1, SH_SCALETYPE1, SH_UPSCALE1},
		{SH_SHADER2, SH_SHADER2_FILTER, SH_SRCTYPE2, SH_SCALETYPE2, SH_UPSCALE2},
		{SH_SHADER3, SH_SHADER3_FILTER, SH_SRCTYPE3, SH_SCALETYPE3, SH_UPSCALE3},
	};
	for (int p = 0; i == -1 && p < 3; p++) {
		if (exactMatch(key, config.shaders.options[SH_PASS[p].shader].key)) {
			char** shaderList = config.shaders.options[SH_PASS[p].shader].values;
			if (shaderList) {
				int count = 0;
				while (shaderList[count])
					count++;
				if (value >= 0 && value < count) {
					GFX_updateShader(p, shaderList[value], NULL, NULL, NULL, NULL);
					i = SH_PASS[p].shader;
				}
			}
			loadShaderSettings(p);
		} else if (exactMatch(key, config.shaders.options[SH_PASS[p].filter].key)) {
			GFX_updateShader(p, NULL, NULL, &value, NULL, NULL);
			i = SH_PASS[p].filter;
		} else if (exactMatch(key, config.shaders.options[SH_PASS[p].srctype].key)) {
			GFX_updateShader(p, NULL, NULL, NULL, NULL, &value);
			i = SH_PASS[p].srctype;
		} else if (exactMatch(key, config.shaders.options[SH_PASS[p].scaletype].key)) {
			GFX_updateShader(p, NULL, NULL, NULL, &value, NULL);
			i = SH_PASS[p].scaletype;
		} else if (exactMatch(key, config.shaders.options[SH_PASS[p].upscale].key)) {
			GFX_updateShader(p, NULL, &value, NULL, NULL, NULL);
			i = SH_PASS[p].upscale;
		}
	}

	if (i == -1)
		return;
	Option* option = &config.shaders.options[i];
	option->value = value;
}

////////

void applyShaderSettings() {
	for (int y = 0; y < config.shaders.options[SH_NROFSHADERS].value; y++) {
		ShaderParam* params = PLAT_getShaderPragmas(y);
		if (params == NULL) {
			break;
		}
		for (int i = 0; i < config.shaderpragmas[y].count; i++) {
			for (int j = 0; j < 32; j++) {
				if (exactMatch(params[j].name, config.shaderpragmas[y].options[i].key)) {
					params[j].value = strtof(config.shaderpragmas[y].options[i].values[config.shaderpragmas[y].options[i].value], NULL);
				}
			}
		}
	}
}
void initShaders() {
	for (int i = 0; config.shaders.options[i].key; i++) {
		if (i != SH_SHADERS_PRESET) {
			Option* option = &config.shaders.options[i];
			;
			Config_syncShaders(option->key, option->value);
		}
	}
}
