# Benchmarks

`selector.lua` measures selector fragment renders of the dashboard scenario of the JavaScript version's benchmarks against the full render of the same template.

- `templates/` is a copy of `bench/templates/` of the JavaScript version. `dashboard.html` composes the other five panels with `x-include`.
- `data.lua` ports the data generators of `bench/data/*.js`. `Date.now()` is replaced by a fixed time, so that every run renders the same output, and JavaScript `null` is `cjson.null`.

## Run

Build the module, and install lua-cjson, then run:

```sh
lua bench/selector.lua [iterations]
```

It prints the size of each fragment and the mean CPU time of one render, with the selector as a string (resolved through the engine's selector cache) and as a compiled selector. Results depend on the machine and vary from run to run.
