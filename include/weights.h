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

#ifndef ROFI_WEIGHTS_H
#define ROFI_WEIGHTS_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * @defgroup WEIGHTS Entry and Mode Weights
 * @ingroup CONFIGURATION
 *
 * Scale match scores after matching, when sorting is enabled.
 * See CONFIG.md for the RASI syntax (`entry-weights`, `weight-patterns`,
 * `mode-weights`).
 *
 * Combined multiplier: @c mode_weight × entry_weight.
 *
 * @{
 */

/**
 * Separator embedded by combi between child mode name and entry id in
 * #mode_get_uid results so #rofi_resolve_weight can recover both parts.
 * ASCII unit separator; not used in mode names.
 */
#define ROFI_WEIGHT_MODE_SEP '\x1f'

/**
 * Score polarity for #rofi_apply_entry_weight.
 */
typedef enum {
  /** Higher raw score is better (fzf, fzf-v2). */
  ROFI_SCORE_CONFIDENCE = 0,
  /** Lower raw score is better (levenshtein / normal). */
  ROFI_SCORE_DISTANCE = 1,
} RofiScorePolarity;

/**
 * Load weight tables from #rofi_configuration.
 * Safe to call again; replaces prior tables.
 */
void entry_weights_init(void);

/** Free weight tables. */
void entry_weights_destroy(void);

/**
 * @param entry_id Entry key (e.g. desktop id or completion string)
 *
 * Resolve entry weight: exact match, then first matching glob, else
 * #Settings::default_entry_weight. Does not include mode weight.
 *
 * @returns multiplier (always > 0)
 */
double entry_weights_lookup(const char *entry_id);

/**
 * @param mode_name Mode name (e.g. "drun", "window", script alias "p")
 *
 * Resolve mode weight from `mode-weights`; default 1.0 when unset.
 *
 * @returns multiplier (always > 0)
 */
double mode_weights_lookup(const char *mode_name);

/**
 * @param entry_id            Entry uid/completion; may be namespaced
 *                            @c "mode" ROFI_WEIGHT_MODE_SEP "entry" (combi)
 * @param fallback_mode_name  Used when @p entry_id is not namespaced
 *                            (typically #mode_get_name of the active mode)
 *
 * Combined weight: @c mode_weights_lookup × entry_weights_lookup.
 *
 * @returns multiplier (always > 0)
 */
double rofi_resolve_weight(const char *entry_id, const char *fallback_mode_name);

/**
 * @param raw_score Score from the matcher
 * @param weight    From #rofi_resolve_weight / #entry_weights_lookup
 * @param polarity  Confidence vs distance
 *
 * Apply @p weight to @p raw_score.
 * Call only when sorting is enabled (`config.sort`); the filter path already
 * guards this. Confidence scores `<= 0` are left unchanged so a boost cannot
 * invert into a demotion (and vice versa).
 *
 * @returns scaled score in the same polarity as @p raw_score
 */
int rofi_apply_entry_weight(int raw_score, double weight,
                            RofiScorePolarity polarity);

/**
 * @param key    Lookup key (copied)
 * @param weight Multiplier
 *
 * Insert or replace an exact-match entry weight (also used by tests).
 */
void entry_weights_set_exact(const char *key, double weight);

/**
 * @param pattern Glob (`*`, `?`; GPatternSpec)
 * @param weight  Multiplier
 *
 * Append a pattern rule; first match wins at lookup (also used by tests).
 */
void entry_weights_add_pattern(const char *pattern, double weight);

/**
 * @param mode_name Mode name key (copied)
 * @param weight    Multiplier
 *
 * Insert or replace a mode weight (also used by tests).
 */
void mode_weights_set(const char *mode_name, double weight);

/** Drop exact and pattern entry rules; keep module allocated. */
void entry_weights_clear(void);

/** Drop mode weight rules; keep module allocated. */
void mode_weights_clear(void);

/** @} */

G_END_DECLS

#endif // ROFI_WEIGHTS_H
