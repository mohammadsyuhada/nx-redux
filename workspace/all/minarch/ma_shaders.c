#include "ma_internal.h"
#include "ma_shaders.h"
#include "ma_config.h"
#include "ma_options.h"

void readShadersPreset(int i) {
	char shaderspath[MAX_PATH] = {0};
	sprintf(shaderspath, SHADERS_FOLDER "/%s", config.shaders.options[SH_SHADERS_PRESET].values[i]);
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

		if (!params[j].name || strlen(params[j].name) == 0) {
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
		if (!config.shaderpragmas[i].options[menucount].values || !config.shaderpragmas[i].options[menucount].labels) {
			free(config.shaderpragmas[i].options[menucount].values);
			free(config.shaderpragmas[i].options[menucount].labels);
			config.shaderpragmas[i].options[menucount].values = NULL;
			config.shaderpragmas[i].options[menucount].labels = NULL;
			continue;
		}
		for (int s = 0; s < steps; s++) {
			float val = params[j].min + s * params[j].step;
			char* str = malloc(16);
			snprintf(str, 16, "%.2f", val);
			config.shaderpragmas[i].options[menucount].values[s] = str;
			config.shaderpragmas[i].options[menucount].labels[s] = str;
			if (fabs(params[j].value - val) < 0.001f)
				config.shaderpragmas[i].options[menucount].value = s;
		}
		config.shaderpragmas[i].options[menucount].count = steps;
		config.shaderpragmas[i].options[menucount].values[steps] = NULL;
		config.shaderpragmas[i].options[menucount].labels[steps] = NULL;
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
	} else if (exactMatch(key, config.shaders.options[SH_SHADER1].key)) {
		char** shaderList = config.shaders.options[SH_SHADER1].values;
		if (shaderList) {
			int count = 0;
			while (shaderList && shaderList[count])
				count++;
			if (value >= 0 && value < count) {
				GFX_updateShader(0, shaderList[value], NULL, NULL, NULL, NULL);
				i = SH_SHADER1;
			}
		}
		loadShaderSettings(0);
	} else if (exactMatch(key, config.shaders.options[SH_SHADER1_FILTER].key)) {
		GFX_updateShader(0, NULL, NULL, &value, NULL, NULL);
		i = SH_SHADER1_FILTER;
	} else if (exactMatch(key, config.shaders.options[SH_SRCTYPE1].key)) {
		GFX_updateShader(0, NULL, NULL, NULL, NULL, &value);
		i = SH_SRCTYPE1;
	} else if (exactMatch(key, config.shaders.options[SH_SCALETYPE1].key)) {
		GFX_updateShader(0, NULL, NULL, NULL, &value, NULL);
		i = SH_SCALETYPE1;
	} else if (exactMatch(key, config.shaders.options[SH_UPSCALE1].key)) {
		GFX_updateShader(0, NULL, &value, NULL, NULL, NULL);
		i = SH_UPSCALE1;
	} else if (exactMatch(key, config.shaders.options[SH_SHADER2].key)) {
		char** shaderList = config.shaders.options[SH_SHADER2].values;
		if (shaderList) {
			int count = 0;
			while (shaderList && shaderList[count])
				count++;
			if (value >= 0 && value < count) {
				GFX_updateShader(1, shaderList[value], NULL, NULL, NULL, NULL);
				i = SH_SHADER2;
			}
		}
		loadShaderSettings(1);
	} else if (exactMatch(key, config.shaders.options[SH_SHADER2_FILTER].key)) {
		GFX_updateShader(1, NULL, NULL, &value, NULL, NULL);
		i = SH_SHADER2_FILTER;
	} else if (exactMatch(key, config.shaders.options[SH_SRCTYPE2].key)) {
		GFX_updateShader(1, NULL, NULL, NULL, NULL, &value);
		i = SH_SRCTYPE2;
	} else if (exactMatch(key, config.shaders.options[SH_SCALETYPE2].key)) {
		GFX_updateShader(1, NULL, NULL, NULL, &value, NULL);
		i = SH_SCALETYPE2;
	} else if (exactMatch(key, config.shaders.options[SH_UPSCALE2].key)) {
		GFX_updateShader(1, NULL, &value, NULL, NULL, NULL);
		i = SH_UPSCALE2;
	} else if (exactMatch(key, config.shaders.options[SH_SHADER3].key)) {
		char** shaderList = config.shaders.options[SH_SHADER3].values;
		if (shaderList) {
			int count = 0;
			while (shaderList && shaderList[count])
				count++;
			if (value >= 0 && value < count) {
				GFX_updateShader(2, shaderList[value], NULL, NULL, NULL, NULL);
				i = SH_SHADER3;
			}
		}
		loadShaderSettings(2);
	} else if (exactMatch(key, config.shaders.options[SH_SHADER3_FILTER].key)) {
		GFX_updateShader(2, NULL, NULL, &value, NULL, NULL);
		i = SH_SHADER3_FILTER;
	} else if (exactMatch(key, config.shaders.options[SH_SRCTYPE3].key)) {
		GFX_updateShader(2, NULL, NULL, NULL, NULL, &value);
		i = SH_SRCTYPE3;
	} else if (exactMatch(key, config.shaders.options[SH_SCALETYPE3].key)) {
		GFX_updateShader(2, NULL, NULL, NULL, &value, NULL);
		i = SH_SCALETYPE3;
	} else if (exactMatch(key, config.shaders.options[SH_UPSCALE3].key)) {
		GFX_updateShader(2, NULL, &value, NULL, NULL, NULL);
		i = SH_UPSCALE3;
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
