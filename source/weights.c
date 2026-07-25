/*
 * rofi
 *
 * MIT/X11 License
 * Copyright © 2013-2023 Qball Cow <qball@gmpclient.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/** Log domain for this module */
#define G_LOG_DOMAIN "Weights"

#include "config.h"
#include "weights.h"
#include "theme.h"
#include "settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Minimum weight to avoid division by zero / score inversion. */
#define WEIGHT_EPSILON 1e-6

/**
 * Pattern weight rule: compiled glob + multiplier.
 */
typedef struct {
  char *pattern;
  GPatternSpec *spec;
  double weight;
} WeightPattern;

/** Exact entry_id → heap-allocated gdouble. */
static GHashTable *exact_weights = NULL;

/** Ordered list of WeightPattern*; first match wins. */
static GList *pattern_weights = NULL;

/** Mode name → heap-allocated gdouble. */
static GHashTable *mode_weights = NULL;

static void weight_pattern_free(gpointer data) {
  WeightPattern *wp = (WeightPattern *)data;
  if (wp == NULL) {
    return;
  }
  g_free(wp->pattern);
  if (wp->spec) {
    g_pattern_spec_free(wp->spec);
  }
  g_slice_free(WeightPattern, wp);
}

static double clamp_weight(double weight) {
  if (weight <= 0.0 || !isfinite(weight)) {
    return WEIGHT_EPSILON;
  }
  return weight;
}

static double property_to_weight(const Property *p) {
  if (p == NULL) {
    return 1.0;
  }
  if (p->type == P_DOUBLE) {
    return clamp_weight(p->value.f);
  }
  if (p->type == P_INTEGER) {
    return clamp_weight((double)p->value.i);
  }
  if (p->type == P_STRING && p->value.s != NULL) {
    char *end = NULL;
    double v = g_ascii_strtod(p->value.s, &end);
    if (end != p->value.s) {
      return clamp_weight(v);
    }
  }
  g_warning("Ignoring non-numeric weight property '%s'", p->name);
  return 1.0;
}

static void load_properties_as_exact(ThemeWidget *wid) {
  if (wid == NULL || wid->properties == NULL) {
    return;
  }
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, wid->properties);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    Property *p = (Property *)value;
    if (p == NULL || p->name == NULL) {
      continue;
    }
    entry_weights_set_exact(p->name, property_to_weight(p));
  }
}

static void load_properties_as_patterns(ThemeWidget *wid) {
  if (wid == NULL || wid->properties == NULL) {
    return;
  }
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, wid->properties);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    Property *p = (Property *)value;
    if (p == NULL || p->name == NULL) {
      continue;
    }
    entry_weights_add_pattern(p->name, property_to_weight(p));
  }
}

static void load_properties_as_mode_weights(ThemeWidget *wid) {
  if (wid == NULL || wid->properties == NULL) {
    return;
  }
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, wid->properties);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    Property *p = (Property *)value;
    if (p == NULL || p->name == NULL) {
      continue;
    }
    mode_weights_set(p->name, property_to_weight(p));
  }
}

/**
 * Find a direct child of a ThemeWidget by name.
 */
static ThemeWidget *widget_child(ThemeWidget *parent, const char *name) {
  if (parent == NULL || name == NULL) {
    return NULL;
  }
  for (unsigned int i = 0; i < parent->num_widgets; i++) {
    ThemeWidget *w = parent->widgets[i];
    if (w && g_strcmp0(w->name, name) == 0) {
      return w;
    }
  }
  return NULL;
}

void entry_weights_clear(void) {
  if (exact_weights) {
    g_hash_table_remove_all(exact_weights);
  }
  if (pattern_weights) {
    g_list_free_full(pattern_weights, weight_pattern_free);
    pattern_weights = NULL;
  }
}

void mode_weights_clear(void) {
  if (mode_weights) {
    g_hash_table_remove_all(mode_weights);
  }
}

void entry_weights_destroy(void) {
  entry_weights_clear();
  mode_weights_clear();
  if (exact_weights) {
    g_hash_table_destroy(exact_weights);
    exact_weights = NULL;
  }
  if (mode_weights) {
    g_hash_table_destroy(mode_weights);
    mode_weights = NULL;
  }
}

void entry_weights_set_exact(const char *key, double weight) {
  if (key == NULL) {
    return;
  }
  if (exact_weights == NULL) {
    exact_weights =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  }
  double *val = g_malloc(sizeof(double));
  *val = clamp_weight(weight);
  g_hash_table_replace(exact_weights, g_strdup(key), val);
}

void entry_weights_add_pattern(const char *pattern, double weight) {
  if (pattern == NULL) {
    return;
  }
  WeightPattern *wp = g_slice_new0(WeightPattern);
  wp->pattern = g_strdup(pattern);
  wp->spec = g_pattern_spec_new(pattern);
  wp->weight = clamp_weight(weight);
  /* Append so configuration order is preserved; first match wins at lookup. */
  pattern_weights = g_list_append(pattern_weights, wp);
}

void mode_weights_set(const char *mode_name, double weight) {
  if (mode_name == NULL) {
    return;
  }
  if (mode_weights == NULL) {
    mode_weights =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  }
  double *val = g_malloc(sizeof(double));
  *val = clamp_weight(weight);
  g_hash_table_replace(mode_weights, g_strdup(mode_name), val);
}

void entry_weights_init(void) {
  entry_weights_clear();
  mode_weights_clear();

  ThemeWidget *exact = widget_child(rofi_configuration, "entry-weights");
  if (exact) {
    load_properties_as_exact(exact);
  }

  ThemeWidget *patterns = widget_child(rofi_configuration, "weight-patterns");
  if (patterns) {
    load_properties_as_patterns(patterns);
  }

  ThemeWidget *modes = widget_child(rofi_configuration, "mode-weights");
  if (modes) {
    load_properties_as_mode_weights(modes);
  }

  g_debug("Weights loaded: %u entry exact, %u patterns, %u modes "
          "(default_entry=%g)",
          exact_weights ? g_hash_table_size(exact_weights) : 0,
          g_list_length(pattern_weights),
          mode_weights ? g_hash_table_size(mode_weights) : 0,
          config.default_entry_weight);
}

double entry_weights_lookup(const char *entry_id) {
  double def = config.default_entry_weight;
  if (def <= 0.0 || !isfinite(def)) {
    def = 1.0;
  }

  if (entry_id == NULL) {
    return def;
  }

  if (exact_weights) {
    double *found = g_hash_table_lookup(exact_weights, entry_id);
    if (found) {
      return *found;
    }
  }

  for (GList *iter = pattern_weights; iter != NULL; iter = g_list_next(iter)) {
    WeightPattern *wp = (WeightPattern *)iter->data;
    if (wp && wp->spec && g_pattern_spec_match_string(wp->spec, entry_id)) {
      return wp->weight;
    }
  }

  return def;
}

double mode_weights_lookup(const char *mode_name) {
  if (mode_name != NULL && mode_weights != NULL) {
    double *found = g_hash_table_lookup(mode_weights, mode_name);
    if (found) {
      return *found;
    }
  }
  return 1.0;
}

double rofi_resolve_weight(const char *entry_id,
                           const char *fallback_mode_name) {
  const char *mode_name = fallback_mode_name;
  const char *entry_key = entry_id;
  char *owned_mode = NULL;

  if (entry_id != NULL) {
    const char *sep = strchr(entry_id, ROFI_WEIGHT_MODE_SEP);
    if (sep != NULL) {
      owned_mode = g_strndup(entry_id, (gsize)(sep - entry_id));
      mode_name = owned_mode;
      entry_key = sep + 1;
      /* Empty entry key after separator: treat as no entry id. */
      if (entry_key[0] == '\0') {
        entry_key = NULL;
      }
    }
  }

  double mode_w = mode_weights_lookup(mode_name);
  double entry_w = entry_weights_lookup(entry_key);
  g_free(owned_mode);
  return clamp_weight(mode_w * entry_w);
}

int rofi_apply_entry_weight(int raw_score, double weight,
                            RofiScorePolarity polarity) {
  if (weight == 1.0) {
    return raw_score;
  }

  weight = clamp_weight(weight);

  double scaled;
  if (polarity == ROFI_SCORE_CONFIDENCE) {
    /* Non-positive confidence: do not scale (would invert boost/demote). */
    if (raw_score <= 0) {
      return raw_score;
    }
    scaled = (double)raw_score * weight;
  } else {
    scaled = (double)raw_score / weight;
  }

  if (scaled >= (double)G_MAXINT) {
    return G_MAXINT;
  }
  if (scaled <= (double)G_MININT) {
    return G_MININT;
  }
  return (int)lround(scaled);
}
