#pragma once

#include "runtime.h"

/* Parse a JSON string into an AglVal.
 * Returns a VAL_RESULT: ok(value) on success, err("message") on failure.
 * Strings are allocated from arena, containers from gc. */
AglVal agl_json_parse(const char *input, int length, AglArena *arena, AglGc *gc);

/* Stringify an AglVal into compact JSON.
 * Returns an arena-allocated string. *out_len receives the byte length. */
const char *agl_json_stringify(AglVal val, int *out_len, AglArena *arena);
