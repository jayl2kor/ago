#pragma once

#include "runtime.h"

/* Execute a command with arguments.
 * Returns a Result<map, string> where the map has keys:
 *   "stdout" (string), "stderr" (string), "status" (int).
 * On error, returns err("message"). */
AglVal agl_exec(const char *cmd, int cmd_len,
                AglArrayVal *args,
                AglArena *arena, AglGc *gc);
