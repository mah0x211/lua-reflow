/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_JSON5_H
#define REFLOW_JSON5_H

#include <stddef.h>
#include "arena.h"
#include "error.h"
#include "value.h"

/*
 * Parse x-data value (wrapped in "{}" then JSON5-parsed via yyjson).
 * Returns a reflow_value tree (arena-owned), or NULL on error (err set).
 */
reflow_value *json5_parse_data(const char *value, size_t len,
                               arena_t *arena, reflow_error *err);

/*
 * Parse a JSON5/JSON string via yyjson.
 * Returns a reflow_value tree (arena-owned), or NULL on error (err set).
 */
reflow_value *json5_parse(const char *src, size_t len,
                          arena_t *arena, reflow_error *err);

#endif
