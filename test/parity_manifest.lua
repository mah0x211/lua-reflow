-- Machine-readable map from the approved JavaScript reference behavior to
-- Lua regression tests or explicitly approved compatibility rules. Reference
-- test names are identifiers only; this file never loads the JavaScript tree.
return {
    source = {
        url = 'https://github.com/mah0x211/reflow.js.git',
        commit = 'b1f46947ac8516cf4f15070c6e01a5a3e1bb453c',
    },
    categories = {
        {
            id = 'public-api-and-coordinator',
            reference = { 'unit/reflow-api', 'reflow.selector' },
            lua = {
                { file = 'test/reflow_test.lua', cases = {
                    'compile_and_render_basic', 'compile_returns_self',
                    'templates_lists_registered', 'clear_all',
                } },
                { file = 'test/coordinator_test.lua', cases = {
                    'compile_file_delegates_to_loader',
                    'static_render_selector_extracts_fragment',
                    'module_exposes_expected_functions',
                } },
            },
        },
        {
            id = 'html-parser-and-source-model',
            reference = { 'unit/scanner', 'unit/scanner-snippet-edge' },
            lua = {
                { file = 'test/parser_test.lua', cases = {
                    'element_open_close', 'void_element_no_endtag',
                    'text_entities_not_decoded', 'comment', 'source_offsets',
                    'reject_mismatched_close',
                } },
            },
        },
        {
            id = 'directive-compilation-and-validation',
            reference = { 'unit/compile-runtime-edge', 'unit/x-with-parser' },
            lua = {
                { file = 'test/compile_directives_test.lua', cases = {
                    'data_stored_raw_and_validated', 'with_bindings_recorded',
                    'each_with_index', 'unknown_directive_rejected',
                    'reject_structural_with_iteration',
                    'helper_unknown_rejected',
                } },
                { file = 'test/compile_postprocess_test.lua', cases = {
                    'if_elseif_else_forms_chain', 'match_nocase_last',
                    'break_outside_loop_rejected',
                } },
            },
        },
        {
            id = 'scope-data-and-with',
            reference = { 'unit/scope', 'unit/x-with-parser', 'x-with-selector' },
            lua = {
                { file = 'test/directives_test.lua', cases = {
                    'parse_data_json5_sugar', 'parse_with_multiple',
                    'parse_with_bracket_nesting', 'parse_with_reject_duplicate',
                } },
                { file = 'test/render_test.lua', cases = {
                    'with_binding_visible_via_dot', 'data_scope',
                } },
            },
        },
        {
            id = 'conditionals-and-match',
            reference = { 'unit/rendering-edge', 'unit/final-coverage' },
            lua = {
                { file = 'test/render_test.lua', cases = {
                    'if_true_branch', 'elseif_middle_branch', 'match_case',
                    'match_nocase_fallback',
                } },
                { file = 'test/compile_postprocess_test.lua', cases = {
                    'reject_elseif_after_else', 'reject_case_after_nocase',
                    'reject_match_without_case',
                } },
            },
        },
        {
            id = 'iteration-and-break',
            reference = { 'unit/rendering-edge', 'unit/coverage-fill' },
            lua = {
                { file = 'test/render_test.lua', cases = {
                    'for_ascending', 'for_descending', 'each_array',
                    'each_with_numeric_index', 'each_object_iteration',
                    'break_if_inside_loop',
                } },
                { file = 'test/directives_test.lua', cases = {
                    'parse_for_reject_step_zero',
                    'parse_for_reject_direction_desc',
                    'parse_each_reject_same_name',
                } },
            },
        },
        {
            id = 'content-binding-and-escaping',
            reference = { 'unit/escape', 'unit/rendering-edge' },
            lua = {
                { file = 'test/render_test.lua', cases = {
                    'text_escapes_html', 'html_directive_raw',
                    'html_non_string_rejected', 'bind_escapes_attr',
                    'bind_boolean_false_omits',
                } },
                { file = 'test/escape_test.lua', cases = {
                    'esct_basic', 'esca_basic',
                } },
            },
        },
        {
            id = 'includes-and-template-loading',
            reference = { 'unit/rendering-edge', 'unit/errors' },
            lua = {
                { file = 'test/include_test.lua', cases = {
                    'include_basic', 'include_reads_globals',
                    'include_ignores_outer_scope', 'include_cycle_indirect',
                    'include_depth_exceeded',
                } },
                { file = 'test/template_test.lua', cases = {
                    'render_with_x_include_via_templates_table',
                } },
            },
        },
        {
            id = 'expression-parse-and-evaluation',
            reference = {
                'unit/expr', 'unit/expr-composite-literals', 'unit/expr-edge',
            },
            lua = {
                { file = 'test/expr_parse_test.lua', cases = {
                    'optional_chaining', 'coalesce', 'helper_nested',
                    'object_computed_key', 'array_nested',
                    'reject_arithmetic', 'reject_method_call',
                } },
                { file = 'test/expr_eval_test.lua', cases = {
                    'optional_chaining', 'coalesce', 'helper_multi_args',
                    'nested_literals', 'exponent',
                } },
            },
        },
        {
            id = 'structured-errors-and-snippets',
            reference = { 'unit/errors', 'unit/snippet', 'unit/scanner-snippet-edge' },
            lua = {
                { file = 'test/structured_error_test.lua', cases = {
                    'compile_error_shape', 'render_error_shape',
                    'render_include_not_found_reason',
                    'include_error_reports_included_template_name',
                } },
                { file = 'test/selector_parse_test.lua', cases = {
                    'error_has_position_and_source',
                    'error_position_after_newline',
                } },
            },
        },
        {
            id = 'selector-parse-match-index-resolve',
            reference = {
                'interpret.selector', 'reflow.selector',
                'selector/index', 'selector/parse', 'selector/resolve',
                'selector-coverage',
            },
            lua = {
                { file = 'test/selector_parse_test.lua', cases = {
                    'id_class_attr_combined', 'nth_pseudos',
                    'chained_combinators', 'selector_list',
                    'err_sibling_combinators',
                } },
                { file = 'test/selector_match_test.lua', cases = {
                    'tag_plus_class_plus_attr',
                    'attr_name_lookup_case_insensitive',
                } },
                { file = 'test/selector_index_test.lua', cases = {
                    'buckets_preserve_document_order',
                    'includes_lists_only_x_include_elements',
                } },
                { file = 'test/selector_resolve_test.lua', cases = {
                    'selector_list_document_order',
                    'positional_nth_carries_argument',
                    'err_ancestor_positional_pseudo',
                } },
            },
        },
        {
            id = 'fragment-rendering-and-positional-selection',
            reference = { 'reflow.selector', 'x-with-selector' },
            lua = {
                { file = 'test/render_fragment_test.lua', cases = {
                    'selector_returns_outer_html',
                    'positional_pseudo_first_child',
                    'cross_template_fragment_via_include',
                    'ancestor_x_data_pushes_scope',
                    'repeated_selector_render_is_stable',
                } },
            },
        },
        {
            id = 'golden-reference-fixtures',
            reference = { 'fixtures' },
            fixture_manifest = 'test/fixtures/manifest.lua',
            fixture_runner = 'test/fixture_test.lua',
            expected = { valid = 23, invalid = 24 },
        },
    },
    compatibility = {
        {
            id = 'error-transport',
            rule = 'Lua APIs return nil plus a structured error table instead of throwing JavaScript Error subclasses.',
        },
        {
            id = 'json5-input-boundary',
            rule = 'Lua accepts a JSON5 string or nil where the JavaScript API accepts an already materialized value.',
        },
        {
            id = 'lua-naming',
            rule = 'Lua public method names use snake_case equivalents of JavaScript camelCase names.',
        },
        {
            id = 'synchronous-api',
            rule = 'Lua compile, load, and render operations are synchronous; JavaScript Promise scheduling is not part of the port contract.',
        },
        {
            id = 'x-each-object-extension',
            rule = 'Arrays bind a zero-based numeric index; objects are additionally supported and bind their string key.',
            lua = {
                file = 'test/render_test.lua',
                cases = { 'each_with_numeric_index', 'each_object_iteration' },
            },
        },
    },
    exclusions = {
        {
            id = 'node-html-rewriter-adapter',
            reason = 'Node HtmlRewriter integration is a platform adapter, not platform-independent template semantics.',
        },
        {
            id = 'workerd-html-rewriter-adapter',
            reason = 'Workerd HtmlRewriter integration is a platform adapter unavailable in the Lua runtime.',
        },
        {
            id = 'javascript-module-and-promise-mechanics',
            reason = 'ES module loading, JavaScript object identity, and Promise timing are language-runtime mechanics covered by synchronous-api.',
        },
    },
}
