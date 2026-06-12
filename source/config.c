/* config.c -- simple configuration parser
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <switch.h>

#include "config.h"

#define CONFIG_VARS \
  CONFIG_VAR_INT(screen_width); \
  CONFIG_VAR_INT(screen_height); \
  CONFIG_VAR_INT(language); \
  CONFIG_VAR_INT(show_fps);

Config config;

// actual screen size that is in use right now
int screen_width = 1280;
int screen_height = 720;

static inline void parse_var(const char *name, const char *value) {
  #define CONFIG_VAR_INT(var) if (!strcmp(name, #var)) { config.var = atoi(value); return; }
  CONFIG_VARS
  #undef CONFIG_VAR_INT
}

int read_config(const char *file) {
  char line[1024] = { 0 };

  memset(&config, 0, sizeof(Config));
  config.screen_width = -1;  // auto (picks 720p handheld / 1080p docked)
  config.screen_height = -1;
  config.language = -1;      // follow the console language
  config.show_fps = 0;       // small FPS counter in the top left corner

  FILE *f = fopen(file, "r");
  if (f == NULL)
    return -1;

  do {
    char *name = NULL, *value = NULL, *tmp = NULL;
    if (fgets(line, sizeof(line), f) != NULL) {
      name = line;
      while (*name && isspace((int)*name)) ++name;
      if (name[0] == '#') continue; // skip comments
      for (tmp = name; *tmp && !isspace((int)*tmp); ++tmp);
      if (*tmp != 0) {
        *tmp = 0;
        for (value = tmp + 1; *value && isspace((int)*value); ++value);
        for (tmp = value + strlen(value) - 1; isspace((int)*tmp); --tmp) *tmp = 0;
        parse_var(name, value);
      }
    }
  } while (!feof(f));

  fclose(f);

  return 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  #define CONFIG_VAR_INT(var) fprintf(f, "%s %d\n", #var, config.var)
  CONFIG_VARS
  #undef CONFIG_VAR_INT

  fclose(f);

  return 0;
}

// BCP-47 tags, indexed by config.language; order matches the game's expected
// locale strings (en first as the safe default)
static const char *const lang_tags[] = {
  "en-US", "fr-FR", "de-DE", "it-IT", "es-ES", "ja-JP", "nl-NL", "pt-PT",
  "ru-RU", "ko-KR", "zh-CN", "da-DK",
};

const char *config_locale_str(void) {
  if (config.language >= 0 &&
      config.language < (int)(sizeof(lang_tags) / sizeof(*lang_tags)))
    return lang_tags[config.language];

  // follow the console language
  u64 lcode = 0;
  SetLanguage sl = SetLanguage_ENUS;
  if (R_SUCCEEDED(setInitialize())) {
    if (R_SUCCEEDED(setGetSystemLanguage(&lcode)))
      setMakeLanguage(lcode, &sl);
    setExit();
  }
  switch (sl) {
    case SetLanguage_FR:
    case SetLanguage_FRCA: return "fr-FR";
    case SetLanguage_DE:   return "de-DE";
    case SetLanguage_IT:   return "it-IT";
    case SetLanguage_ES:
    case SetLanguage_ES419:return "es-ES";
    case SetLanguage_JA:   return "ja-JP";
    case SetLanguage_NL:   return "nl-NL";
    case SetLanguage_PT:
    case SetLanguage_PTBR: return "pt-PT";
    case SetLanguage_RU:   return "ru-RU";
    case SetLanguage_KO:   return "ko-KR";
    case SetLanguage_ZHCN:
    case SetLanguage_ZHHANS:return "zh-CN";
    default:               return "en-US";
  }
}
