/*
 * rofi
 *
 * Unit tests for entry/mode weight lookup, config loading, and score scaling.
 *
 * Covers:
 *  - entry exact / pattern / default-entry-weight
 *  - mode-weights
 *  - combined resolve (mode × entry), including combi namespaced uids
 *  - loading from a synthetic configuration ThemeWidget tree (as entry_weights_init does)
 *  - ranking polarity (fzf confidence and levenshtein distance)
 *  - user-style combi scenario: boost script mode "p" over "drun"
 */

#include "config.h"
#include "rofi-types.h"
#include "settings.h"
#include "theme.h"
#include "weights.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real symbol from theme layer; tests build a synthetic tree when needed. */
ThemeWidget *rofi_configuration = NULL;

static int test = 0;

#define TASSERT(a)                                                             \
  {                                                                            \
    assert(a);                                                                 \
    printf("Test %i passed (%s)\n", ++test, #a);                               \
  }

#define TASSERTE(a, b)                                                         \
  {                                                                            \
    if ((a) == (b)) {                                                          \
      printf("Test %i passed (%s == %s) (%d == %d)\n", ++test, #a, #b,         \
             (int)(a), (int)(b));                                              \
    } else {                                                                   \
      printf("Test %i failed (%s == %s) (%d != %d)\n", ++test, #a, #b,         \
             (int)(a), (int)(b));                                              \
      abort();                                                                 \
    }                                                                          \
  }

#define TASSERTD(a, b)                                                         \
  {                                                                            \
    if (fabs((a) - (b)) < 1e-9) {                                              \
      printf("Test %i passed (%s == %s) (%g == %g)\n", ++test, #a, #b, (a),    \
             (b));                                                             \
    } else {                                                                   \
      printf("Test %i failed (%s == %s) (%g != %g)\n", ++test, #a, #b, (a),    \
             (b));                                                             \
      abort();                                                                 \
    }                                                                          \
  }

/* -------------------------------------------------------------------------- */
/* Synthetic configuration tree helpers (mirrors rasi configuration children) */
/* -------------------------------------------------------------------------- */

static void free_property(gpointer data) {
  Property *p = (Property *)data;
  if (p == NULL) {
    return;
  }
  g_free(p->name);
  if (p->type == P_STRING && p->value.s != NULL) {
    g_free(p->value.s);
  }
  g_free(p);
}

static void free_widget_tree(ThemeWidget *w) {
  if (w == NULL) {
    return;
  }
  for (unsigned int i = 0; i < w->num_widgets; i++) {
    free_widget_tree(w->widgets[i]);
  }
  g_free(w->widgets);
  if (w->properties) {
    g_hash_table_destroy(w->properties);
  }
  g_free(w->name);
  g_free(w);
}

static ThemeWidget *make_widget(const char *name) {
  ThemeWidget *w = g_malloc0(sizeof(ThemeWidget));
  w->name = g_strdup(name);
  w->properties =
      g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free_property);
  return w;
}

static void add_child(ThemeWidget *parent, ThemeWidget *child) {
  parent->widgets = g_realloc(parent->widgets,
                              sizeof(ThemeWidget *) * (parent->num_widgets + 1));
  parent->widgets[parent->num_widgets++] = child;
  child->parent = parent;
}

static void set_double_prop(ThemeWidget *w, const char *key, double v) {
  Property *p = g_malloc0(sizeof(Property));
  p->name = g_strdup(key);
  p->type = P_DOUBLE;
  p->value.f = v;
  g_hash_table_replace(w->properties, p->name, p);
}

static void set_int_prop(ThemeWidget *w, const char *key, int v) {
  Property *p = g_malloc0(sizeof(Property));
  p->name = g_strdup(key);
  p->type = P_INTEGER;
  p->value.i = v;
  g_hash_table_replace(w->properties, p->name, p);
}

static void set_string_prop(ThemeWidget *w, const char *key, const char *v) {
  Property *p = g_malloc0(sizeof(Property));
  p->name = g_strdup(key);
  p->type = P_STRING;
  p->value.s = g_strdup(v);
  g_hash_table_replace(w->properties, p->name, p);
}

/** Mirror view.c: combined weight then confidence (fzf) scaling. */
static int score_confidence(int base, const char *entry_id,
                            const char *fallback_mode) {
  double w = rofi_resolve_weight(entry_id, fallback_mode);
  return rofi_apply_entry_weight(base, w, ROFI_SCORE_CONFIDENCE);
}

/** Mirror view.c distance (levenshtein / normal) path. */
static int score_distance(int base, const char *entry_id,
                          const char *fallback_mode) {
  double w = rofi_resolve_weight(entry_id, fallback_mode);
  return rofi_apply_entry_weight(base, w, ROFI_SCORE_DISTANCE);
}

static char *ns_uid(const char *mode, const char *entry) {
  return g_strdup_printf("%s%c%s", mode, ROFI_WEIGHT_MODE_SEP, entry);
}

/* -------------------------------------------------------------------------- */

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv) {
  config.sort = TRUE;
  config.default_entry_weight = 1.0;
  entry_weights_clear();
  mode_weights_clear();

  /* ---- Lookup defaults when empty ---- */
  TASSERTD(entry_weights_lookup("firefox.desktop"), 1.0);
  TASSERTD(entry_weights_lookup(NULL), 1.0);
  TASSERTD(mode_weights_lookup("drun"), 1.0);
  TASSERTD(mode_weights_lookup(NULL), 1.0);
  TASSERTD(rofi_resolve_weight(NULL, NULL), 1.0);

  /* ---- Exact entry overrides ---- */
  entry_weights_set_exact("firefox.desktop", 1.5);
  entry_weights_set_exact("gimp.desktop", 0.8);
  TASSERTD(entry_weights_lookup("firefox.desktop"), 1.5);
  TASSERTD(entry_weights_lookup("gimp.desktop"), 0.8);
  TASSERTD(entry_weights_lookup("other.desktop"), 1.0);

  /* ---- Pattern overrides (first match wins) ---- */
  entry_weights_add_pattern("*.sh", 0.7);
  entry_weights_add_pattern("calc:*", 1.3);
  entry_weights_add_pattern("*down*", 2.0); /* later; first match should win */
  TASSERTD(entry_weights_lookup("script.sh"), 0.7);
  TASSERTD(entry_weights_lookup("calc:42"), 1.3);
  /* Exact still beats pattern for same key if registered as exact. */
  entry_weights_set_exact("special.sh", 2.0);
  TASSERTD(entry_weights_lookup("special.sh"), 2.0);
  /* First pattern wins over a later broader one. */
  entry_weights_clear();
  entry_weights_add_pattern("Shut*", 3.0);
  entry_weights_add_pattern("*", 0.1);
  TASSERTD(entry_weights_lookup("Shut down"), 3.0);
  TASSERTD(entry_weights_lookup("anything-else"), 0.1);

  /* ---- default-entry-weight fallback ---- */
  entry_weights_clear();
  config.default_entry_weight = 0.5;
  TASSERTD(entry_weights_lookup("no-match-entry"), 0.5);
  config.default_entry_weight = 1.0;
  /* Non-finite / non-positive default falls back to 1.0 */
  config.default_entry_weight = 0.0;
  TASSERTD(entry_weights_lookup("x"), 1.0);
  config.default_entry_weight = -2.0;
  TASSERTD(entry_weights_lookup("x"), 1.0);
  config.default_entry_weight = 1.0;

  /* ---- Mode weights (API) ---- */
  mode_weights_clear();
  mode_weights_set("p", 2.5);
  mode_weights_set("window", 1.2);
  mode_weights_set("drun", 0.5);
  TASSERTD(mode_weights_lookup("p"), 2.5);
  TASSERTD(mode_weights_lookup("window"), 1.2);
  TASSERTD(mode_weights_lookup("drun"), 0.5);
  TASSERTD(mode_weights_lookup("run"), 1.0);
  /* Replace existing mode weight */
  mode_weights_set("p", 10.0);
  TASSERTD(mode_weights_lookup("p"), 10.0);
  mode_weights_set("p", 2.5);

  /* ---- Resolve: mode × entry (standalone + fallback mode) ---- */
  entry_weights_set_exact("firefox.desktop", 1.5);
  TASSERTD(rofi_resolve_weight("firefox.desktop", "drun"), 1.5 * 0.5);
  TASSERTD(rofi_resolve_weight("other.desktop", "window"), 1.0 * 1.2);
  TASSERTD(rofi_resolve_weight(NULL, "p"), 1.0 * 2.5);
  TASSERTD(rofi_resolve_weight("firefox.desktop", NULL), 1.5 * 1.0);

  /* ---- Resolve: combi-namespaced "mode\x1fentry" ---- */
  {
    char *p_shut = ns_uid("p", "Shut down");
    char *drun_ff = ns_uid("drun", "firefox.desktop");
    char *p_other = ns_uid("p", "other.desktop");
    char *empty_entry = ns_uid("p", "");

    TASSERTD(rofi_resolve_weight(p_shut, "combi"), 2.5 * 1.0);
    TASSERTD(rofi_resolve_weight(drun_ff, "combi"), 0.5 * 1.5);
    /* Namespaced mode overrides fallback mode name. */
    TASSERTD(rofi_resolve_weight(p_other, "drun"), 2.5 * 1.0);
    /* Empty entry after separator uses default entry weight. */
    TASSERTD(rofi_resolve_weight(empty_entry, "combi"), 2.5 * 1.0);

    /* Markup-like power-menu row text still uses mode weight. */
    char *p_markup =
        ns_uid("p", "<span font_size=\"medium\">Shut down</span>");
    TASSERTD(rofi_resolve_weight(p_markup, "combi"), 2.5 * 1.0);

    g_free(p_shut);
    g_free(drun_ff);
    g_free(p_other);
    g_free(empty_entry);
    g_free(p_markup);
  }

  /* ---- User combi scenario: p boosted, drun demoted, same base score ---- */
  {
    /*
     * configuration {
     *   sort: true; sorting-method: "fzf";
     *   combi-modes: "window,p:rofi-power-menu --confirm='',drun";
     *   mode-weights { p: 2.5; drun: 0.5; }
     * }
     * Same raw fzf confidence → power-menu row must rank higher.
     */
    mode_weights_clear();
    entry_weights_clear();
    mode_weights_set("p", 2.5);
    mode_weights_set("drun", 0.5);
    mode_weights_set("window", 1.0);

    char *power = ns_uid("p", "Shut down");
    char *app = ns_uid("drun", "shutdown-helper.desktop");
    int base = 100;
    int score_p = score_confidence(base, power, "combi");
    int score_d = score_confidence(base, app, "combi");
    TASSERTE(score_p, 250); /* 100 * 2.5 */
    TASSERTE(score_d, 50);  /* 100 * 0.5 */
    TASSERT(score_p > score_d);

    /* Distance polarity: lower is better after boost. */
    int dist_p = score_distance(10, power, "combi");
    int dist_d = score_distance(10, app, "combi");
    TASSERTE(dist_p, 4);  /* lround(10/2.5) */
    TASSERTE(dist_d, 20); /* lround(10/0.5) */
    TASSERT(dist_p < dist_d);

    /* Extreme boost still wins against modest entry weight on drun. */
    entry_weights_set_exact("shutdown-helper.desktop", 3.0);
    score_p = score_confidence(base, power, "combi");
    score_d = score_confidence(base, app, "combi");
    /* p: 2.5 * 1.0 = 2.5 → 250; drun: 0.5 * 3.0 = 1.5 → 150 */
    TASSERTE(score_p, 250);
    TASSERTE(score_d, 150);
    TASSERT(score_p > score_d);

    g_free(power);
    g_free(app);
  }

  /* ---- entry_weights_init from synthetic configuration tree ---- */
  {
    entry_weights_destroy();
    config.default_entry_weight = 1.0;

    ThemeWidget *root = make_widget("configuration");
    ThemeWidget *modes = make_widget("mode-weights");
    ThemeWidget *entries = make_widget("entry-weights");
    ThemeWidget *patterns = make_widget("weight-patterns");

    /* Double, int, and string numeric values (rasi allows all three). */
    set_double_prop(modes, "p", 2.5);
    set_int_prop(modes, "window", 2);
    set_string_prop(modes, "drun", "0.5");

    set_double_prop(entries, "firefox.desktop", 1.5);
    set_double_prop(entries, "gimp.desktop", 0.8);

    set_double_prop(patterns, "*.sh", 0.7);
    set_double_prop(patterns, "calc:*", 1.3);

    add_child(root, modes);
    add_child(root, entries);
    add_child(root, patterns);

    rofi_configuration = root;
    entry_weights_init();

    TASSERTD(mode_weights_lookup("p"), 2.5);
    TASSERTD(mode_weights_lookup("window"), 2.0);
    TASSERTD(mode_weights_lookup("drun"), 0.5);
    TASSERTD(mode_weights_lookup("run"), 1.0);

    TASSERTD(entry_weights_lookup("firefox.desktop"), 1.5);
    TASSERTD(entry_weights_lookup("gimp.desktop"), 0.8);
    TASSERTD(entry_weights_lookup("script.sh"), 0.7);
    TASSERTD(entry_weights_lookup("calc:99"), 1.3);

    char *ns = ns_uid("p", "Shut down");
    TASSERTD(rofi_resolve_weight(ns, "combi"), 2.5);
    g_free(ns);
    ns = ns_uid("drun", "firefox.desktop");
    TASSERTD(rofi_resolve_weight(ns, "combi"), 0.5 * 1.5);
    g_free(ns);

    /* Re-init with empty config clears prior tables. */
    free_widget_tree(root);
    rofi_configuration = make_widget("configuration");
    entry_weights_init();
    TASSERTD(mode_weights_lookup("p"), 1.0);
    TASSERTD(entry_weights_lookup("firefox.desktop"), 1.0);

    free_widget_tree(rofi_configuration);
    rofi_configuration = NULL;

    /* init with NULL configuration is safe */
    entry_weights_init();
    TASSERTD(mode_weights_lookup("p"), 1.0);
  }

  /* ---- Confidence polarity (higher is better): final = raw * weight ---- */
  {
    int raw = 100;
    int weighted =
        rofi_apply_entry_weight(raw, 1.5, ROFI_SCORE_CONFIDENCE);
    TASSERTE(weighted, 150);

    weighted = rofi_apply_entry_weight(raw, 0.5, ROFI_SCORE_CONFIDENCE);
    TASSERTE(weighted, 50);

    TASSERTE(rofi_apply_entry_weight(raw, 1.0, ROFI_SCORE_CONFIDENCE), 100);
  }

  /* ---- Distance polarity (lower is better): final = raw / weight ---- */
  {
    int raw = 10;
    int weighted = rofi_apply_entry_weight(raw, 2.0, ROFI_SCORE_DISTANCE);
    TASSERTE(weighted, 5);

    weighted = rofi_apply_entry_weight(raw, 0.5, ROFI_SCORE_DISTANCE);
    TASSERTE(weighted, 20);

    TASSERTE(rofi_apply_entry_weight(0, 2.0, ROFI_SCORE_DISTANCE), 0);
  }

  /* ---- Non-positive confidence: weight must not invert direction ---- */
  {
    TASSERTE(rofi_apply_entry_weight(-20, 2.0, ROFI_SCORE_CONFIDENCE), -20);
    TASSERTE(rofi_apply_entry_weight(-20, 0.5, ROFI_SCORE_CONFIDENCE), -20);
    TASSERTE(rofi_apply_entry_weight(0, 2.0, ROFI_SCORE_CONFIDENCE), 0);
  }

  /* ---- Ranking semantics ---- */
  {
    int a = rofi_apply_entry_weight(50, 2.0, ROFI_SCORE_CONFIDENCE);
    int b = rofi_apply_entry_weight(50, 1.0, ROFI_SCORE_CONFIDENCE);
    TASSERT(a > b);

    int da = rofi_apply_entry_weight(8, 2.0, ROFI_SCORE_DISTANCE);
    int db = rofi_apply_entry_weight(8, 1.0, ROFI_SCORE_DISTANCE);
    TASSERT(da < db);
  }

  /* ---- Combined weight product applied to scores ---- */
  {
    mode_weights_clear();
    entry_weights_clear();
    mode_weights_set("drun", 2.0);
    entry_weights_set_exact("firefox.desktop", 1.5);
    double w = rofi_resolve_weight("firefox.desktop", "drun");
    TASSERTD(w, 3.0);
    TASSERTE(rofi_apply_entry_weight(100, w, ROFI_SCORE_CONFIDENCE), 300);
  }

  /* ---- Non-positive weight clamped (no crash / no zero div) ---- */
  {
    int d = rofi_apply_entry_weight(100, 0.0, ROFI_SCORE_DISTANCE);
    TASSERT(d > 0);
    int c = rofi_apply_entry_weight(100, -1.0, ROFI_SCORE_CONFIDENCE);
    TASSERT(c >= 0);
  }

  /* ---- NULL keys are no-ops on setters ---- */
  entry_weights_set_exact(NULL, 9.0);
  entry_weights_add_pattern(NULL, 9.0);
  mode_weights_set(NULL, 9.0);
  TASSERTD(entry_weights_lookup("never-set"), 1.0);

  /* ---- clear leaves destroy-safe module; destroy is idempotent enough ---- */
  entry_weights_clear();
  mode_weights_clear();
  entry_weights_destroy();
  entry_weights_destroy(); /* second destroy after null tables */

  printf("All %i weight tests passed.\n", test);
  return EXIT_SUCCESS;
}
