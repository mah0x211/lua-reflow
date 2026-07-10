/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
// project
#include "error.h"
// system
#include <stdlib.h>
#include <string.h>

reflow_error *reflow_error_new(const char *type, const char *message)
{
    reflow_error *err = (reflow_error *)calloc(1, sizeof(reflow_error));
    if (err == NULL) {
        return NULL;
    }
    err->type    = type;
    err->message = message;
    err->line    = 0;
    err->column  = 0;
    return err;
}

void reflow_error_free(reflow_error *err)
{
    if (err == NULL) {
        return;
    }
    reflow_error_free(err->cause);
    free(err);
}
