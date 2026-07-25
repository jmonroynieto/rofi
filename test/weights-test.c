/*
 * rofi
 *
 * Unit tests for entry weight lookup and score scaling.
 */

#include "config.h"
#include "rofi-types.h"
#include "settings.h"
#include "weights.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stub: weights.c reads config children at init; tests drive tables directly. */
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
      printf("Test %i passed (%s == %s) (%d == %d)\n", ++test, #a, #b, (int)(a),\
             (int)(b));                                                        \
    } else {                                                                   \
      printf("Test %i failed (%s == %s) (%d != %d)\n", ++test, #a, #b, (int)(a),\
             (int)(b));                                                        \
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

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv) {
  /* Ensure clean defaults for isolation. */
  config.sort = TRUE;
  config.default_entry_weight = 1.0;
  entry_weights_clear();

  /* ---- Lookup: default when empty ---- */
  TASSERTD(entry_weights_lookup("firefox.desktop"), 1.0);
  TASSERTD(entry_weights_lookup(NULL), 1.0);

  /* ---- Exact overrides ---- */
  entry_weights_set_exact("firefox.desktop", 1.5);
  entry_weights_set_exact("gimp.desktop", 0.8);
  TASSERTD(entry_weights_lookup("firefox.desktop"), 1.5);
  TASSERTD(entry_weights_lookup("gimp.desktop"), 0.8);
  TASSERTD(entry_weights_lookup("other.desktop"), 1.0);

  /* ---- Pattern overrides (first match wins) ---- */
  entry_weights_add_pattern("*.sh", 0.7);
  entry_weights_add_pattern("calc:*", 1.3);
  TASSERTD(entry_weights_lookup("script.sh"), 0.7);
  TASSERTD(entry_weights_lookup("calc:42"), 1.3);
  /* Exact still beats pattern for same key if registered as exact. */
  entry_weights_set_exact("special.sh", 2.0);
  TASSERTD(entry_weights_lookup("special.sh"), 2.0);

  /* ---- default-entry-weight fallback ---- */
  config.default_entry_weight = 0.5;
  TASSERTD(entry_weights_lookup("no-match-entry"), 0.5);
  config.default_entry_weight = 1.0;

  /* ---- Confidence polarity (higher is better): final = raw * weight ---- */
  {
    int raw = 100;
    int weighted =
        rofi_apply_entry_weight(raw, 1.5, ROFI_SCORE_CONFIDENCE);
    TASSERTE(weighted, 150);

    /* Weight < 1 demotes confidence scores. */
    weighted = rofi_apply_entry_weight(raw, 0.5, ROFI_SCORE_CONFIDENCE);
    TASSERTE(weighted, 50);

    /* Identity at weight 1.0 */
    TASSERTE(rofi_apply_entry_weight(raw, 1.0, ROFI_SCORE_CONFIDENCE), 100);
  }

  /* ---- Distance polarity (lower is better): final = raw / weight ---- */
  {
    int raw = 10;
    int weighted = rofi_apply_entry_weight(raw, 2.0, ROFI_SCORE_DISTANCE);
    TASSERTE(weighted, 5);

    /* Weight < 1 increases distance (demotes). */
    weighted = rofi_apply_entry_weight(raw, 0.5, ROFI_SCORE_DISTANCE);
    TASSERTE(weighted, 20);

    TASSERTE(rofi_apply_entry_weight(0, 2.0, ROFI_SCORE_DISTANCE), 0);
  }

  /* ---- Non-positive confidence: weighting must not invert direction ---- */
  {
    /* A below-baseline (but accepted) fuzzy match: raw <= 0. Multiplying
     * would flip promote/demote, so the weight must be a no-op here. */
    TASSERTE(rofi_apply_entry_weight(-20, 2.0, ROFI_SCORE_CONFIDENCE), -20);
    TASSERTE(rofi_apply_entry_weight(-20, 0.5, ROFI_SCORE_CONFIDENCE), -20);
    TASSERTE(rofi_apply_entry_weight(0, 2.0, ROFI_SCORE_CONFIDENCE), 0);
  }

  /* ---- Ranking semantics: higher weight must improve order ---- */
  {
    /* Confidence: larger score is better after weighting. */
    int a = rofi_apply_entry_weight(50, 2.0, ROFI_SCORE_CONFIDENCE);
    int b = rofi_apply_entry_weight(50, 1.0, ROFI_SCORE_CONFIDENCE);
    TASSERT(a > b);

    /* Distance: smaller score is better after weighting. */
    int da = rofi_apply_entry_weight(8, 2.0, ROFI_SCORE_DISTANCE);
    int db = rofi_apply_entry_weight(8, 1.0, ROFI_SCORE_DISTANCE);
    TASSERT(da < db);
  }

  /* ---- Non-positive weight clamped to epsilon (no crash / no zero div) ---- */
  {
    int d = rofi_apply_entry_weight(100, 0.0, ROFI_SCORE_DISTANCE);
    TASSERT(d > 0);
    /* Weight clamps to WEIGHT_EPSILON (~1e-6); 100 * epsilon rounds to 0. */
    int c = rofi_apply_entry_weight(100, -1.0, ROFI_SCORE_CONFIDENCE);
    TASSERT(c >= 0);
  }

  entry_weights_destroy();
  printf("All %i weight tests passed.\n", test);
  return EXIT_SUCCESS;
}
