# JavaScript版との同等性対応表

Lua版の挙動の正本である JavaScript 版（`../js-reflow`、commit `b1f46947ac8516cf4f15070c6e01a5a3e1bb453c`）の試験と、Lua版の試験の対応を示す。JavaScript版を更新して追従するときは、この表とfixturesを同じcommitへ揃える。

- **対応**: 同じ入力と期待値をLua版の試験で確認している。Lua版の名前（snake_case）、`nil, err` の返却、エラーの付加情報を `err.meta` に持つことはLua固有の確定事項として読み替える。
- **承認済み差異**: DESIGN-09 または master-plan で承認したLua版との差異。期待値をLua版の挙動に改めて確認している。
- **対象外**: JavaScript版の内部部品を直接検査するなど、公開APIとLua版の参照IFから観測できない試験。理由を記す。
- 内部関数を直接呼ぶJavaScript版の試験は、同じ入力を描画（`x-text` の値を受け取る補助関数など）や参照IF（`ir:to_table()`、`reflow.selector.resolve`）で確認する。

## fixtures

JavaScript版の `test/fixtures` を `test/fixtures` へそのまま複製し、`helpers.js`・`setup.js` を `fixture.lua` に書き換えた。`test/fixture_test.lua` が `fixture-runner.js` と同じ手順で照合する。

| JavaScript版 | 件数 | Lua版の試験 |
|---|---|---|
| `test/fixtures/valid/*` | 23 | fixture_test.lua: `valid_fixtures_render_the_expected_output` |
| `test/fixtures/invalid/*` | 24 | fixture_test.lua: `invalid_fixtures_fail_as_expected` |

JavaScript版のリポジトリにある `example.html` は基準commitで追跡されていないローカルファイルのため、照合資産に含めない。

## `test/interpret.selector.test.js`（25件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | fragment: simple #id extraction from a static template | 対応 | render_fragment_test.lua: `simple_id_extraction_from_a_static_template` |
| 2 | fragment: renders x-text / x-bind inside the matched subtree | 対応 | render_fragment_test.lua: `renders_text_and_bind_inside_the_matched_subtree` |
| 3 | fragment: candidate inside x-data ancestor sees the scope | 対応 | render_fragment_test.lua: `candidate_inside_data_ancestor_sees_the_scope` |
| 4 | fragment: candidate inside x-if truthy branch renders | 対応 | render_fragment_test.lua: `candidate_inside_if_branch` |
| 5 | fragment: candidate inside x-if false branch is unreachable → no_match | 対応 | render_fragment_test.lua: `candidate_inside_if_branch` |
| 6 | fragment: chain evaluation picks the truthy branch | 対応 | render_fragment_test.lua: `chain_evaluation_picks_the_truthy_branch` |
| 7 | fragment: x-match selects the matching case | 対応 | render_fragment_test.lua: `match_selects_the_matching_case` |
| 8 | fragment: x-each with more than one iteration produces multiple_matches | 対応 | render_fragment_test.lua: `each_iterations_are_matches` |
| 9 | fragment: x-each with exactly one iteration is a single fragment | 対応 | render_fragment_test.lua: `each_iterations_are_matches` |
| 10 | fragment: x-each with zero iterations is no_match | 対応 | render_fragment_test.lua: `each_iterations_are_matches` |
| 11 | fragment: :nth-child selects the right runtime iteration | 対応 | render_fragment_test.lua: `nth_child_selects_the_runtime_iteration` |
| 12 | fragment: :nth-child respects mixed static + iterated siblings | 対応 | render_fragment_test.lua: `nth_child_respects_mixed_static_and_iterated_siblings` |
| 13 | fragment: :first-child / :last-child / :only-child | 対応 | render_fragment_test.lua: `first_last_and_only_child` |
| 14 | fragment: :first-of-type / :last-of-type / :nth-of-type | 対応 | render_fragment_test.lua: `first_last_and_nth_of_type` |
| 15 | fragment: x-match example verifying li:nth-child(2) picks the chosen case | 対応 | render_fragment_test.lua: `positional_condition_counts_the_selected_case` |
| 16 | fragment: cross-include finds a fragment defined in the included template | 対応 | render_fragment_test.lua: `include_is_searched_without_candidates` |
| 17 | fragment: not searched into includes when current template has candidates | 対応 | render_fragment_test.lua: `include_is_not_searched_with_candidates` |
| 18 | fragment: cross-include multi-match still errors | 対応 | render_fragment_test.lua: `matches_across_includes_are_counted_together` |
| 19 | fragment: cross-include propagates include errors (not_found) | 対応 | render_fragment_test.lua: `include_errors_propagate_from_the_search` |
| 20 | fragment: cross-include propagates cycle errors | 対応 | render_fragment_test.lua: `include_errors_propagate_from_the_search` |
| 21 | fragment: multiple id matches within one template is multiple_matches | 対応 | render_fragment_test.lua: `multiple_and_missing_matches` |
| 22 | fragment: no candidates anywhere is no_match | 対応 | render_fragment_test.lua: `multiple_and_missing_matches` |
| 23 | fragment: x-bind on the matched element is applied | 対応 | render_fragment_test.lua: `bind_and_helpers_apply_to_the_matched_element` |
| 24 | fragment: matched element with a helper reference in x-text | 対応 | render_fragment_test.lua: `bind_and_helpers_apply_to_the_matched_element` |
| 25 | fragment: runtime error inside the matched subtree surfaces as ReflowRuntimeError | 対応 | render_fragment_test.lua: `runtime_error_inside_the_matched_subtree` |

## `test/reflow.selector.test.js`（14件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | api: render() with selector returns matched fragment | 対応 | reflow_test.lua: `render_with_a_selector_returns_the_fragment` |
| 2 | api: Reflow.compileSelector produces a reusable CompiledSelector | 対応 | reflow_test.lua: `compile_selector_returns_a_compiled_selector` |
| 3 | api: raw string selector populates the cache | 対応 | reflow_test.lua: `string_selector_populates_the_cache` |
| 4 | api: selectorCacheSize=0 disables caching | 対応 | reflow_test.lua: `selector_cache_size_zero_disables_caching` |
| 5 | api: default cache size is 128 | 対応 | reflow_test.lua: `default_settings` |
| 6 | api: without selector, render behaves exactly as before | 対応 | reflow_test.lua: `render_with_a_selector_returns_the_fragment` |
| 7 | api: no_match error surfaces from public render | 対応 | reflow_test.lua: `selector_errors_surface_from_render` |
| 8 | api: multiple_matches error surfaces from public render | 対応 | reflow_test.lua: `selector_errors_surface_from_render` |
| 9 | api: syntax error from raw string surfaces as ReflowSelectorError | 対応 | reflow_test.lua: `selector_errors_surface_from_render` |
| 10 | api: Reflow.render (static one-shot) supports selector | 対応 | reflow_test.lua: `render_once_with_a_selector` |
| 11 | api: Reflow.renderFile supports selector via config | 対応 | reflow_test.lua: `render_file_once` |
| 12 | api: template not found error still fires when selector present | 対応 | reflow_test.lua: `template_not_found_comes_first_with_a_selector` |
| 13 | api: helper reachable from fragment render | 対応 | render_fragment_test.lua: `bind_and_helpers_apply_to_the_matched_element` |
| 14 | api: cross-include selector via public render | 対応 | reflow_test.lua: `include_searches_the_registered_templates` |

## `test/selector-coverage.test.js`（56件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | parse: "." followed by non-ident is rejected | 対応 | parity_selector_coverage_test.lua: `dot_followed_by_a_non_identifier_is_rejected` |
| 2 | parse: attribute selector missing operator or "]" is rejected | 対応 | parity_selector_coverage_test.lua: `attribute_missing_an_operator_or_bracket_is_rejected` |
| 3 | parse: ":" followed by non-ident is rejected | 対応 | parity_selector_coverage_test.lua: `colon_followed_by_a_non_identifier_is_rejected` |
| 4 | parse: string escape at EOF is rejected | 対応 | parity_selector_coverage_test.lua: `string_escape_at_the_end_is_rejected` |
| 5 | parse: trailing junk after a valid selector is rejected with "unexpected" | 対応 | parity_selector_coverage_test.lua: `trailing_content_after_a_selector_is_rejected` |
| 6 | parse: bare "#" without an identifier is rejected | 対応 | parity_selector_coverage_test.lua: `hash_without_an_identifier_is_rejected` |
| 7 | matchCompound: id compound rejects elements without the id | 対応 | parity_selector_coverage_test.lua: `id_condition_rejects_an_element_without_the_id` |
| 8 | matchCompound: multiple-class compound rejects missing class | 対応 | parity_selector_coverage_test.lua: `classes_reject_an_element_missing_one` |
| 9 | matchCompound: attribute with op rejects elements missing the attribute | 対応 | parity_selector_coverage_test.lua: `attribute_operator_rejects_an_element_missing_the_attribute` |
| 10 | matchCompound: attribute "=" exact match | 対応 | parity_selector_coverage_test.lua: `attribute_equal_is_an_exact_match` |
| 11 | matchCompound: attribute "~=" rejects when target contains whitespace or is empty | 対応 | parity_selector_coverage_test.lua: `includes_rejects_a_target_with_white_space` |
| 12 | resolve: descendant combinator returns empty when no ancestor matches | 対応 | parity_selector_coverage_test.lua: `descendant_combinator_without_a_matching_ancestor` |
| 13 | index: multi-space class attribute drops empty tokens | 対応 | parity_selector_coverage_test.lua: `index_drops_the_empty_class_names_of_repeated_spaces` |
| 14 | fragment: root-level positional candidate (parent === null) | 対応 | render_fragment_test.lua: `top_level_positional_candidate` |
| 15 | fragment: positional selector inside parent with x-data | 対応 | render_fragment_test.lua: `positional_candidate_under_a_parent_with_frames` |
| 16 | fragment: positional selector with parent x-for | 対応 | render_fragment_test.lua: `positional_candidate_under_a_parent_with_frames` |
| 17 | fragment: positional selector with parent x-each | 対応 | render_fragment_test.lua: `positional_candidate_under_a_parent_with_frames` |
| 18 | fragment: positional walk skips text/comment siblings | 対応 | render_fragment_test.lua: `positional_count_skips_text_and_comments` |
| 19 | fragment: positional walk counts chain sibling as 1 or 0 based on cond | 対応 | render_fragment_test.lua: `positional_count_of_unexecuted_siblings` |
| 20 | fragment: positional walk counts x-each sibling by iteration count | 対応 | render_fragment_test.lua: `positional_count_of_unexecuted_siblings` |
| 21 | fragment: positional walk counts x-for sibling by iteration count | 対応 | render_fragment_test.lua: `positional_count_of_unexecuted_siblings` |
| 22 | fragment: positional walk rejects x-each collection that is not an array | 対応 | render_fragment_test.lua: `positional_count_of_unexecuted_siblings` |
| 23 | fragment: targeted walk through ancestor x-for iterates | 対応 | render_fragment_test.lua: `ancestor_loops_are_executed` |
| 24 | fragment: targeted walk through ancestor x-each with rejected non-array collection | 対応 | render_fragment_test.lua: `ancestor_loops_are_executed` |
| 25 | fragment: cross-include propagates invalid include expression | 対応 | render_fragment_test.lua: `include_errors_propagate_from_the_search` |
| 26 | fragment: cross-include propagates depth exceeded | 対応 | render_fragment_test.lua: `include_errors_propagate_from_the_search` |
| 27 | fragment: positional pseudo on x-match child selects only when matching case is target | 対応 | render_fragment_test.lua: `positional_candidate_under_match` |
| 28 | fragment: positional pseudo on x-match child with no case selected is no_match | 対応 | render_fragment_test.lua: `positional_candidate_under_match` |
| 29 | fragment: candidate is x-each element with x-break-if inside | 対応 | render_fragment_test.lua: `candidate_loop_with_a_break_inside` |
| 30 | resolve: selector list matches same element only once | 対応 | selector_resolve_test.lua: `selector_list_unions_and_deduplicates` |
| 31 | fragment: direct-path candidate with own x-for iterating once | 対応 | render_fragment_test.lua: `candidate_with_its_own_loop` |
| 32 | fragment: direct-path candidate x-for with multiple iterations is multi-match | 対応 | render_fragment_test.lua: `candidate_with_its_own_loop` |
| 33 | fragment: direct-path candidate x-each with 0 items is no_match | 対応 | render_fragment_test.lua: `candidate_with_its_own_loop` |
| 34 | fragment: positional walk with target having x-each and single iteration | 対応 | render_fragment_test.lua: `candidate_with_its_own_loop` |
| 35 | fragment: positional walk with target having x-for iterating once | 対応 | render_fragment_test.lua: `candidate_with_its_own_loop` |
| 36 | fragment: positional parent x-each rejects non-array collection | 対応 | render_fragment_test.lua: `ancestor_loops_are_executed` |
| 37 | fragment: candidate is a non-selected x-case (checkOwnBranch matchBranch false) | 対応 | render_fragment_test.lua: `candidate_of_an_unselected_branch` |
| 38 | fragment: direct-path x-for candidate: break-if in body cuts further iterations | 承認済み差異 | DESIGN-09「部分描画の一致は通常描画で実際に出力される要素と同じ」（breakしたイテレーションも一致に数え `multiple_matches`。`render_fragment_test.lua: breaking_iteration_of_a_candidate_is_a_match`） |
| 39 | fragment: direct-path x-each candidate: break-if in body cuts further iterations | 承認済み差異 | DESIGN-09「部分描画の一致は通常描画で実際に出力される要素と同じ」（breakしたイテレーションも一致に数え `multiple_matches`。`render_fragment_test.lua: breaking_iteration_of_a_candidate_is_a_match`） |
| 40 | fragment: targeted walk through ancestor x-if (chainBranch) | 対応 | render_fragment_test.lua: `candidate_of_an_unselected_branch` |
| 41 | fragment: targeted walk through ancestor x-else branch | 対応 | render_fragment_test.lua: `candidate_of_an_unselected_branch` |
| 42 | fragment: targeted walk through ancestor x-match / x-case | 対応 | render_fragment_test.lua: `candidate_of_an_unselected_branch` |
| 43 | fragment: targeted walk through ancestor x-each iterating once | 対応 | render_fragment_test.lua: `ancestor_loops_are_executed` |
| 44 | fragment: targeted walk through ancestor x-each with zero iterations | 対応 | render_fragment_test.lua: `ancestor_loops_are_executed` |
| 45 | fragment: checkOwnBranch matchBranch returns false when no case matches | 対応 | render_fragment_test.lua: `candidate_of_an_unselected_branch` |
| 46 | fragment: direct-path x-each candidate rejects non-array collection | 対応 | render_fragment_test.lua: `errors_in_candidate_loops_surface` |
| 47 | fragment: direct-path x-for candidate rethrows non-BreakSignal errors | 対応 | render_fragment_test.lua: `errors_in_candidate_loops_surface` |
| 48 | fragment: direct-path x-each candidate rethrows non-BreakSignal errors | 対応 | render_fragment_test.lua: `errors_in_candidate_loops_surface` |
| 49 | fragment: positional walk with target x-for that hits BreakSignal in body | 対応 | render_fragment_test.lua: `positional_candidate_loop_with_a_break_inside` |
| 50 | fragment: positional walk with target x-each that hits BreakSignal in body | 対応 | render_fragment_test.lua: `positional_candidate_loop_with_a_break_inside` |
| 51 | fragment: positional walk with target x-for rethrows non-BreakSignal | 対応 | render_fragment_test.lua: `errors_in_candidate_loops_surface` |
| 52 | fragment: positional walk with target x-each rethrows non-BreakSignal | 対応 | render_fragment_test.lua: `errors_in_candidate_loops_surface` |
| 53 | fragment: targeted walk through ancestor x-nocase branch (matchBranch cond=null) | 対応 | render_fragment_test.lua: `candidate_of_an_unselected_branch` |
| 54 | fragment: direct-path x-each guard exits once enough matches are captured | 対応 | render_fragment_test.lua: `rendering_stops_at_the_second_match` |
| 55 | fragment: targeted walk through ancestor x-for guard exits once enough matches | 対応 | render_fragment_test.lua: `rendering_stops_at_the_second_match` |
| 56 | fragment: targeted walk through ancestor x-each guard exits once enough matches | 対応 | render_fragment_test.lua: `rendering_stops_at_the_second_match` |

## `test/selector/cache.test.js`（12件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | cache: default size 128 | 対応 | selector_cache_test.lua: `default_size_is_128` |
| 2 | cache: parses on miss and returns same instance on hit | 対応 | selector_cache_test.lua: `parses_on_miss_and_returns_same_selector_on_hit` |
| 3 | cache: separate strings produce separate entries | 対応 | selector_cache_test.lua: `separate_strings_produce_separate_entries` |
| 4 | cache: pre-compiled input bypasses cache | 対応 | selector_cache_test.lua: `compiled_selector_bypasses_cache` |
| 5 | cache: invalid selectors are NOT cached | 対応 | selector_cache_test.lua: `invalid_selectors_are_not_cached` |
| 6 | cache: LRU evicts oldest when size exceeds maxSize | 対応 | selector_cache_test.lua: `evicts_oldest_when_size_exceeds_max_size` |
| 7 | cache: hit refreshes recency (moves entry to newest) | 対応 | selector_cache_test.lua: `hit_refreshes_recency` |
| 8 | cache: size 0 disables caching entirely | 対応 | selector_cache_test.lua: `size_zero_disables_cache` |
| 9 | cache: constructor rejects negative or non-integer maxSize | 対応 | selector_cache_test.lua: `new_rejects_negative_or_non_integer_max_size` |
| 10 | cache: clear empties the cache | 対応 | selector_cache_test.lua: `new_accepts_integral_numbers` |
| 11 | cache: peek does not affect recency | 対応 | selector_cache_test.lua: `clear_empties_the_cache` |
| 12 | cache: non-string, non-compiled input delegates to parseSelector for a typed error | 対応 | selector_cache_test.lua: `peek_does_not_affect_recency` |

## `test/selector/index.test.js`（13件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | index: byTag / byId / byClass / byAttrName populate from static attrs | 対応 | dom_index_test.lua: `dom_index_lists_static_attributes_in_document_order` |
| 2 | index: annotates parent / depth / order on every element | 対応 | dom_index_test.lua: `dom_index_annotates_structure_like_the_javascript_version` |
| 3 | index: chain branches share the chain wrapper enclosing element as parent | 対応 | dom_index_test.lua: `dom_index_annotates_structure_like_the_javascript_version` |
| 4 | index: x-match cases are annotated as matchBranch and their parent is the x-match element | 対応 | dom_index_test.lua: `dom_index_annotates_structure_like_the_javascript_version` |
| 5 | index: x-include-bearing elements are collected | 対応 | dom_index_test.lua: `dom_index_ignores_dynamic_attributes_and_lists_includes` |
| 6 | computeControlPath: skips ancestors without control-flow | 対象外 | JS固有: 部分描画の制御経路走査の内部関数。Lua版は通常描画と同じ状態機械で実行する（IMPL-35）。観測できる効果は `render_fragment_test.lua` の分岐・反復・作用域の例 |
| 7 | computeControlPath: includes x-data / x-if / x-for / x-each / x-match ancestors | 対象外 | JS固有: 部分描画の制御経路走査の内部関数。Lua版は通常描画と同じ状態機械で実行する（IMPL-35）。観測できる効果は `render_fragment_test.lua` の分岐・反復・作用域の例 |
| 8 | elementRequiresExecution: returns true for control-flow, false for plain | 対象外 | JS固有: 部分描画の制御経路走査の内部関数。Lua版は通常描画と同じ状態機械で実行する（IMPL-35）。観測できる効果は `render_fragment_test.lua` の分岐・反復・作用域の例 |
| 9 | getStaticAttr / getStaticClassSet: read attribute values | 対応 | selector_resolve_test.lua: `static_conditions_read_the_first_attribute_of_a_name` |
| 10 | getStaticClassSet: empty for missing / empty class attribute | 対応 | parity_selector_coverage_test.lua: `classes_reject_an_element_missing_one` |
| 11 | sortByDocumentOrder: orders elements by their assigned order field | 対応 | selector_resolve_test.lua: `candidates_are_sorted_in_document_order` |
| 12 | buildTemplateIndex: ignores text and comment nodes | 対応 | dom_index_test.lua: `dom_index_annotates_structure_like_the_javascript_version` |
| 13 | buildTemplateIndex: handles empty id and class attribute values | 対応 | dom_index_test.lua: `dom_index_keeps_repeated_entries_and_skips_empty_values` |

## `test/selector/parse.test.js`（35件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | parse: single type selector | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 2 | parse: universal selector | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 3 | parse: id / class combinations | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 4 | parse: attribute selectors with all operators | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 5 | parse: attribute with ident value and single quotes | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 6 | parse: attribute existence (no value) | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 7 | parse: descendant combinator | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 8 | parse: child combinator | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 9 | parse: mixed combinators | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 10 | parse: selector list | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 11 | parse: positional pseudo-classes without arg | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 12 | parse: nth-* pseudo-classes with integer arg | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 13 | parse: complex selector with positional pseudo-class | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 14 | parse: case-insensitive pseudo-class names accepted | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 15 | parse: tag names lowercased | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 16 | parse: whitespace tolerance around commas and combinators | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 17 | parse: rejects empty selector | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 18 | parse: rejects non-string source | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 19 | parse: rejects sibling combinators | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 20 | parse: rejects column combinator | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 21 | parse: rejects pseudo-elements | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 22 | parse: rejects unsupported pseudo-classes | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 23 | parse: rejects formula args for nth-* | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 24 | parse: rejects positional pseudo-class with unexpected arg | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 25 | parse: rejects nth-* without arg | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 26 | parse: rejects nth-*(0) | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 27 | parse: rejects attribute namespaces | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 28 | parse: rejects attribute case-sensitivity flag | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 29 | parse: rejects multiple #ids in same compound | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 30 | parse: rejects trailing junk | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 31 | parse: rejects malformed attribute selectors | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 32 | parse: rejects malformed strings inside attributes | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 33 | parse: string escapes inside attribute values | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 34 | parse: result is frozen | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |
| 35 | isCompiledSelector: true for parseSelector output, false for strings/objects | 対応 | selector_test.lua: `compile_matches_the_javascript_version` |

## `test/selector/resolve.test.js`（22件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | resolve: #id seeds directly | 対応 | selector_resolve_test.lua: `id_seeds_directly` |
| 2 | resolve: .class seeds by class | 対応 | selector_resolve_test.lua: `class_seeds_by_class` |
| 3 | resolve: multiple classes require all to match | 対応 | selector_resolve_test.lua: `multiple_classes_require_all_to_match` |
| 4 | resolve: tag seed for bare type selector | 対応 | selector_resolve_test.lua: `tag_seed_for_bare_type_selector` |
| 5 | resolve: attribute-name seed for bare [attr] | 対応 | selector_resolve_test.lua: `attribute_name_seed_for_bare_attribute` |
| 6 | resolve: attribute value operators | 対応 | selector_resolve_test.lua: `attribute_value_operators` |
| 7 | resolve: \|= matches exact or hyphen-prefix | 対応 | selector_resolve_test.lua: `dash_matches_exact_or_hyphen_prefix` |
| 8 | resolve: descendant combinator matches any depth | 対応 | selector_resolve_test.lua: `descendant_combinator_matches_any_depth` |
| 9 | resolve: child combinator requires direct parent | 対応 | selector_resolve_test.lua: `child_combinator_requires_direct_parent` |
| 10 | resolve: multi-part complex selector | 対応 | selector_resolve_test.lua: `multi_part_complex_selector` |
| 11 | resolve: selector list unions and deduplicates | 対応 | selector_resolve_test.lua: `selector_list_unions_and_deduplicates` |
| 12 | resolve: candidates are sorted in document order | 対応 | selector_resolve_test.lua: `candidates_are_sorted_in_document_order` |
| 13 | resolve: chain branches are reachable and share the enclosing parent for combinators | 対応 | selector_resolve_test.lua: `chain_branches_share_the_enclosing_parent` |
| 14 | resolve: x-match cases are reachable as children of the x-match element | 対応 | selector_resolve_test.lua: `match_cases_are_children_of_the_match_element` |
| 15 | resolve: positional pseudo-classes flow to candidates on the rightmost compound | 対応 | selector_resolve_test.lua: `positional_conditions_flow_to_candidates` |
| 16 | resolve: rejects positional pseudos on non-rightmost compound | 対応 | selector_resolve_test.lua: `rejects_positional_conditions_on_non_rightmost_compound` |
| 17 | resolve: no seed bucket returns empty | 対応 | selector_resolve_test.lua: `no_seed_list_returns_empty` |
| 18 | resolve: universal / bare positional falls back to all elements | 対応 | selector_resolve_test.lua: `universal_falls_back_to_all_elements` |
| 19 | resolve: attribute intersection uses smallest bucket | 対応 | selector_resolve_test.lua: `attribute_intersection_uses_smallest_list` |
| 20 | evalPositional: first-child / last-child / only-child | 対応 | render_fragment_test.lua: `positional_conditions_follow_eval_positional` |
| 21 | evalPositional: *-of-type variants | 対応 | render_fragment_test.lua: `positional_conditions_follow_eval_positional` |
| 22 | evalPositional: nth-* forms | 対応 | render_fragment_test.lua: `positional_conditions_follow_eval_positional` |

## `test/unit/compile-runtime-edge.test.js`（18件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | x-for missing "=" fails | 対応 | parity_compile_runtime_edge_test.lua: `x_for_missing_equals_fails` |
| 2 | x-for invalid variable name fails | 対応 | parity_compile_runtime_edge_test.lua: `x_for_invalid_variable_name_fails` |
| 3 | x-for wrong arg count fails | 対応 | parity_compile_runtime_edge_test.lua: `x_for_wrong_argument_count_fails` |
| 4 | x-each missing in keyword fails | 対応 | parity_compile_runtime_edge_test.lua: `x_each_missing_in_keyword_fails` |
| 5 | x-each with item==index fails | 対応 | parity_compile_runtime_edge_test.lua: `x_each_with_item_equal_to_index_fails` |
| 6 | x-each invalid collection expression fails | 対応 | parity_compile_runtime_edge_test.lua: `x_each_invalid_collection_expression_fails` |
| 7 | x-elseif with empty value fails | 対応 | parity_compile_runtime_edge_test.lua: `x_elseif_with_empty_value_fails` |
| 8 | elseif after else fails | 対応 | parity_compile_runtime_edge_test.lua: `elseif_after_else_fails` |
| 9 | multiple x-else in chain fails | 対応 | parity_compile_runtime_edge_test.lua: `multiple_x_else_in_chain_fails` |
| 10 | x-match with mixed children (non-case element) fails | 対応 | parity_compile_runtime_edge_test.lua: `x_match_with_a_child_that_is_not_a_case_fails` |
| 11 | multiple x-nocase in same match fails | 対応 | parity_compile_runtime_edge_test.lua: `multiple_x_nocase_in_the_same_match_fails` |
| 12 | S+K same element fails | 対応 | parity_compile_runtime_edge_test.lua: `structural_and_control_on_the_same_element_fails` |
| 13 | duplicate x-data on same element is rejected | 対応 | parity_compile_runtime_edge_test.lua: `duplicate_x_data_on_the_same_element_is_rejected` |
| 14 | include not-found produces requested | 対応 | parity_compile_runtime_edge_test.lua: `include_not_found_has_the_requested_name` |
| 15 | include depth exceeded (via mutual, non-cycle) | 対応 | parity_compile_runtime_edge_test.lua: `include_depth_exceeded_without_a_cycle` |
| 16 | runtime error inside x-for iteration wraps non-BreakSignal | 対応 | parity_compile_runtime_edge_test.lua: `runtime_error_inside_x_for_iteration` |
| 17 | runtime error inside x-each iteration wraps non-BreakSignal | 対応 | parity_compile_runtime_edge_test.lua: `runtime_error_inside_x_each_iteration` |
| 18 | x-bind with array value fails | 対応 | parity_compile_runtime_edge_test.lua: `x_bind_with_an_array_value_fails` |

## `test/unit/coverage-fill.test.js`（12件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | <= evaluates true and false branches | 対応 | parity_coverage_fill_test.lua: `le_evaluates_true_and_false` |
| 2 | >= evaluates true and false branches | 対応 | parity_coverage_fill_test.lua: `ge_evaluates_true_and_false` |
| 3 | \|\| returns left when truthy | 対応 | parity_coverage_fill_test.lua: `or_returns_left_when_truthy` |
| 4 | ternary false branch | 対応 | parity_coverage_fill_test.lua: `ternary_false_branch` |
| 5 | parser: expect fails for missing closing paren | 対応 | parity_coverage_fill_test.lua: `missing_closing_paren_is_rejected` |
| 6 | parser: !=  at start of unary triggers guard path | 対応 | parity_coverage_fill_test.lua: `ne_without_left_operand_is_rejected` |
| 7 | parser: expect fails for ternary missing colon | 対応 | parity_coverage_fill_test.lua: `ternary_missing_colon_is_rejected` |
| 8 | parser: number literal negative exponent | 対応 | parity_coverage_fill_test.lua: `number_literal_negative_exponent` |
| 9 | x-match with nocase branch fires when no case matches | 対応 | parity_coverage_fill_test.lua: `x_match_nocase_branch_when_no_case_matches` |
| 10 | include-not-found emits requested and reason | 対応 | parity_coverage_fill_test.lua: `include_not_found_has_requested_and_reason` |
| 11 | static Reflow.renderFile without loader fails | 対応 | parity_coverage_fill_test.lua: `render_file_without_loader_fails` |
| 12 | parser: postfix breaks on . not followed by identifier | 対応 | parity_coverage_fill_test.lua: `member_access_with_a_trailing_dot_is_rejected` |

## `test/unit/defensive.test.js`（8件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | createEnv with undefined globals defaults to {} | 対応 | parity_defensive_test.lua: `nil_data_renders_with_empty_globals` |
| 2 | createEnv with null globals defaults to {} | 承認済み差異 | master-plan「描画入力はLua表または `nil`」（Lua固有の型契約。`null` は実行時エラー。`null_data_is_rejected`） |
| 3 | createEnv with provided globals preserves them | 対応 | parity_defensive_test.lua: `provided_data_is_kept_as_globals` |
| 4 | ReflowError meta does not overwrite reserved properties | 対応 | parity_defensive_test.lua: `meta_does_not_overwrite_the_error_fields` |
| 5 | ReflowError meta cause is exposed | 対応 | parity_defensive_test.lua: `cause_is_exposed` |
| 6 | all error classes have correct names | 対応 | parity_defensive_test.lua: `every_error_type_has_its_own_type` |
| 7 | collectHelperNames walks unary operand | 対応 | parity_defensive_test.lua: `helper_names_are_collected_from_a_unary_operand` |
| 8 | collectHelperNames on non-object returns empty set | 対象外 | JS固有: 内部関数 `collectHelperNames` へ `null` を直接渡す。公開APIでは起こらない |

## `test/unit/errors.test.js`（4件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | compile error includes template name, snippet, line, column, element | 対応 | parity_errors_test.lua: `compile_error_has_template_name_snippet_line_column_element` |
| 2 | runtime error includes include stack | 対応 | parity_errors_test.lua: `runtime_error_has_the_include_stack` |
| 3 | include cycle error carries reason and stack | 対応 | parity_errors_test.lua: `include_cycle_error_has_reason_and_stack` |
| 4 | include depth exceeded reports reason | 対応 | parity_errors_test.lua: `include_depth_exceeded_reports_reason` |

## `test/unit/escape.test.js`（5件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | escapeText escapes < > & " \' | 対応 | parity_escape_test.lua: `text_escapes_lt_gt_amp_quote_and_apostrophe` |
| 2 | escapeText passes plain ASCII through | 対応 | parity_escape_test.lua: `text_passes_plain_ascii_through` |
| 3 | escapeText coerces non-string values | 対応 | parity_escape_test.lua: `text_writes_non_string_values` |
| 4 | escapeAttr escapes < > & " but not \' | 対応 | parity_escape_test.lua: `attribute_escapes_lt_gt_amp_and_quote_but_not_apostrophe` |
| 5 | escapeText handles unicode | 対応 | parity_escape_test.lua: `text_handles_unicode` |

## `test/unit/expr-composite-literals.test.js`（29件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | object literal with identifier / string / number keys | 対応 | parity_expr_composite_literals_test.lua: `object_literal_with_identifier_string_and_number_keys` |
| 2 | object literal is empty when body is empty | 対応 | parity_expr_composite_literals_test.lua: `object_literal_is_empty_when_body_is_empty` |
| 3 | object literal accepts trailing comma | 対応 | parity_expr_composite_literals_test.lua: `object_literal_accepts_trailing_comma` |
| 4 | object literal computed key from scope reference | 対応 | parity_expr_composite_literals_test.lua: `computed_key_from_scope_reference` |
| 5 | object literal computed key from string / number literal | 対応 | parity_expr_composite_literals_test.lua: `computed_key_from_string_and_number_literal` |
| 6 | object literal computed key accepts member chain and optional chaining | 対応 | parity_expr_composite_literals_test.lua: `computed_key_accepts_member_chain_and_optional_chaining` |
| 7 | computed key rejects helper calls at parse time | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_helper_calls` |
| 8 | computed key rejects operators at parse time | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_operators` |
| 9 | computed key rejects nested object/array at parse time | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_nested_object_and_array` |
| 10 | computed key: bare $ without dot is rejected | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_bare_dollar` |
| 11 | computed key: bare @ without name is rejected | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_bare_at` |
| 12 | computed key: @name reference resolves | 対応 | parity_expr_composite_literals_test.lua: `computed_key_at_reference_resolves` |
| 13 | computed key: .name reference resolves | 対応 | parity_expr_composite_literals_test.lua: `computed_key_dot_reference_resolves` |
| 14 | computed key: bare . without name is rejected | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_bare_dot` |
| 15 | computed key: numeric literal | 対応 | parity_expr_composite_literals_test.lua: `computed_key_numeric_literal` |
| 16 | computed key: negative numeric literal | 対応 | parity_expr_composite_literals_test.lua: `computed_key_negative_numeric_literal` |
| 17 | computed key rejects unbracketed expression | 対応 | parity_expr_composite_literals_test.lua: `computed_key_rejects_unbracketed_expression` |
| 18 | computed key throws at runtime when result is not string or number | 対応 | parity_expr_composite_literals_test.lua: `computed_key_fails_when_the_result_is_not_a_string_or_number` |
| 19 | computed key throws for null / undefined / boolean / array | 対応 | parity_expr_composite_literals_test.lua: `computed_key_fails_for_null_undefined_boolean_and_array` |
| 20 | object literal duplicate keys — last write wins | 対応 | parity_expr_composite_literals_test.lua: `object_literal_duplicate_keys_last_write_wins` |
| 21 | object literal supports nested literals and helper values | 対応 | parity_expr_composite_literals_test.lua: `object_literal_supports_nested_literals_and_helper_values` |
| 22 | array literal with mixed expression items | 対応 | parity_expr_composite_literals_test.lua: `array_literal_with_mixed_expression_items` |
| 23 | array literal is empty when body is empty and accepts trailing comma | 対応 | parity_expr_composite_literals_test.lua: `array_literal_is_empty_when_body_is_empty_and_accepts_trailing_comma` |
| 24 | array literal nested inside object literal | 対応 | parity_expr_composite_literals_test.lua: `array_literal_nested_inside_object_literal` |
| 25 | object literal usable as expression in ternary | 対応 | parity_expr_composite_literals_test.lua: `object_literal_usable_in_ternary` |
| 26 | collectHelperNames descends into object and array literals | 対応 | parity_expr_composite_literals_test.lua: `helper_names_are_collected_from_object_and_array_literals` |
| 27 | unterminated object/array literals are rejected | 対応 | parity_expr_composite_literals_test.lua: `unterminated_object_and_array_literals_are_rejected` |
| 28 | missing colon after key is rejected | 対応 | parity_expr_composite_literals_test.lua: `missing_colon_after_key_is_rejected` |
| 29 | missing key before colon is rejected | 対応 | parity_expr_composite_literals_test.lua: `missing_key_before_colon_is_rejected` |

## `test/unit/expr-edge.test.js`（18件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | parses all string escape sequences | 対応 | parity_expr_edge_test.lua: `parses_all_string_escape_sequences` |
| 2 | rejects invalid escape sequences | 対応 | parity_expr_edge_test.lua: `rejects_invalid_escape_sequences` |
| 3 | rejects unterminated strings | 対応 | parity_expr_edge_test.lua: `rejects_unterminated_strings` |
| 4 | parses numbers with exponent | 対応 | parity_expr_edge_test.lua: `parses_numbers_with_exponent` |
| 5 | rejects unexpected end of expression | 対応 | parity_expr_edge_test.lua: `rejects_unexpected_end_of_expression` |
| 6 | rejects unexpected characters | 対応 | parity_expr_edge_test.lua: `rejects_unexpected_characters` |
| 7 | $ must be followed by dot | 対応 | parity_expr_edge_test.lua: `dollar_must_be_followed_by_dot` |
| 8 | @ must be followed by identifier | 対応 | parity_expr_edge_test.lua: `at_must_be_followed_by_identifier` |
| 9 | . must be followed by identifier | 対応 | parity_expr_edge_test.lua: `dot_must_be_followed_by_identifier` |
| 10 | ?. must be followed by identifier | 対応 | parity_expr_edge_test.lua: `optional_dot_must_be_followed_by_identifier` |
| 11 | rejects trailing content after expression | 対応 | parity_expr_edge_test.lua: `rejects_trailing_content_after_expression` |
| 12 | parses parenthesized expressions | 対応 | parity_expr_edge_test.lua: `parses_parenthesized_expressions` |
| 13 | helper call with zero args | 対応 | parity_expr_edge_test.lua: `helper_call_with_zero_args` |
| 14 | helper call with trailing comma tolerance behavior | 対応 | parity_expr_edge_test.lua: `helper_call_rejects_a_trailing_comma` |
| 15 | nested member access | 対応 | parity_expr_edge_test.lua: `nested_member_access` |
| 16 | mixed optional and non-optional chain | 対応 | parity_expr_edge_test.lua: `mixed_optional_and_non_optional_chain` |
| 17 | evaluate handles && short-circuit returning left value | 対応 | parity_expr_edge_test.lua: `and_short_circuit_returns_the_left_value` |
| 18 | evaluate handles \|\| returning right value when left falsy | 対応 | parity_expr_edge_test.lua: `or_returns_the_right_value_when_the_left_is_falsy` |

## `test/unit/expr.test.js`（12件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | parses literals | 対応 | parity_expr_test.lua: `parses_literals` |
| 2 | parses scope references | 対応 | parity_expr_test.lua: `parses_scope_references` |
| 3 | optional chaining short-circuits on null/undefined | 対応 | parity_expr_test.lua: `optional_chaining_short_circuits_on_null_and_undefined` |
| 4 | comparison and logical operators | 対応 | parity_expr_test.lua: `comparison_and_logical_operators` |
| 5 | ternary | 対応 | parity_expr_test.lua: `ternary` |
| 6 | null coalescing | 対応 | parity_expr_test.lua: `null_coalescing` |
| 7 | helper calls | 対応 | parity_expr_test.lua: `helper_calls` |
| 8 | collectHelperNames finds nested calls | 対応 | parity_expr_test.lua: `helper_names_are_collected_from_nested_calls` |
| 9 | rejects arithmetic operators | 対応 | parity_expr_test.lua: `rejects_arithmetic_operators` |
| 10 | rejects method call syntax | 対応 | parity_expr_test.lua: `rejects_method_call_syntax` |
| 11 | rejects bare identifier without call | 対応 | parity_expr_test.lua: `rejects_bare_identifier_without_call` |
| 12 | rejects reserved keyword literals only | 対応 | parity_expr_test.lua: `rejects_reserved_keyword_literals_only` |

## `test/unit/final-coverage.test.js`（17件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | x-match with non-whitespace text child fails | 対応 | parity_final_coverage_test.lua: `x_match_with_a_text_child_fails` |
| 2 | x-match with comments between cases is OK | 対応 | parity_final_coverage_test.lua: `x_match_with_comments_between_cases` |
| 3 | x-if chain with comments between chain members is OK | 対応 | parity_final_coverage_test.lua: `x_if_chain_with_comments_between_members` |
| 4 | compile error reconstructs open tag including regular attributes | 対応 | parity_final_coverage_test.lua: `compile_error_rebuilds_the_open_tag_with_regular_attributes` |
| 5 | compile error reconstructs tag with attribute containing special chars | 対応 | parity_final_coverage_test.lua: `compile_error_rebuilds_an_attribute_with_special_characters` |
| 6 | x-for start greater than stop with positive step fails | 対応 | parity_final_coverage_test.lua: `x_for_start_greater_than_stop_with_positive_step_fails` |
| 7 | x-each with only whitespace collection is rejected by regex | 対応 | parity_final_coverage_test.lua: `x_each_with_only_whitespace_collection_fails` |
| 8 | x-each with break-if hits break signal path | 対応 | parity_final_coverage_test.lua: `x_each_with_break_if` |
| 9 | x-include with null value throws describeValue null path | 対応 | parity_final_coverage_test.lua: `x_include_with_a_null_value_fails` |
| 10 | conflicting I×I (x-for + x-each) fails | 対応 | parity_final_coverage_test.lua: `conflicting_iteration_directives_fail` |
| 11 | conflicting K×K (x-break + x-break-if) fails | 対応 | parity_final_coverage_test.lua: `conflicting_control_directives_fail` |
| 12 | conflicting I×K on same element fails | 対応 | parity_final_coverage_test.lua: `iteration_and_control_on_the_same_element_fail` |
| 13 | bad expression in x-if triggers parseExprValue catch wrapping | 対応 | parity_final_coverage_test.lua: `bad_expression_in_x_if_fails` |
| 14 | x-bind: with empty attribute name fails | 対応 | parity_final_coverage_test.lua: `x_bind_with_an_empty_attribute_name_fails` |
| 15 | multiple structural directives on same element fails | 対応 | parity_final_coverage_test.lua: `multiple_structural_directives_on_the_same_element_fail` |
| 16 | chain scan breaks on element with unrelated directive | 対応 | parity_final_coverage_test.lua: `chain_ends_at_an_element_with_an_unrelated_directive` |
| 17 | orphan x-nocase outside x-match fails | 対応 | parity_final_coverage_test.lua: `orphan_x_nocase_outside_x_match_fails` |

## `test/unit/reflow-api.test.js`（13件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | duplicate compile throws ReflowCompileError | 対応 | reflow_test.lua: `duplicate_compile_is_a_compile_error` |
| 2 | clear(name) removes only that template | 対応 | reflow_test.lua: `clear_removes_templates` |
| 3 | clear(nonexistent) returns [] | 対応 | reflow_test.lua: `clear_removes_templates` |
| 4 | clear() with no args removes all | 対応 | reflow_test.lua: `clear_removes_templates` |
| 5 | re-compile after clear is allowed | 対応 | reflow_test.lua: `recompile_after_clear_is_allowed` |
| 6 | render nonexistent template throws ReflowRuntimeError | 対応 | reflow_test.lua: `render_of_an_unregistered_template_is_a_runtime_error` |
| 7 | compileFile without loader throws | 対応 | reflow_test.lua: `compile_file_requires_a_loader` |
| 8 | compileFile uses provided loader | 対応 | reflow_test.lua: `compile_file_uses_the_loader` |
| 9 | static Reflow.render one-shot | 対応 | reflow_test.lua: `render_once` |
| 10 | helpers passed to constructor are available | 対応 | reflow_test.lua: `helpers_are_available` |
| 11 | templates() returns registered names | 対応 | reflow_test.lua: `templates_lists_names_in_registration_order` |
| 12 | custom prefix works | 対応 | reflow_test.lua: `prefix_retargets_directives` |
| 13 | custom prefix rejects default x-* | 対応 | reflow_test.lua: `prefix_retargets_directives` |

## `test/unit/rendering-edge.test.js`（27件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | compile rejects non-string name | 対応 | parity_rendering_edge_test.lua: `compile_rejects_a_name_that_is_not_a_non_empty_string` |
| 2 | compile rejects non-string html | 対応 | parity_rendering_edge_test.lua: `compile_rejects_html_that_is_not_a_string` |
| 3 | x-bind rejects object value at render | 対応 | parity_rendering_edge_test.lua: `x_bind_rejects_an_object_value` |
| 4 | x-bind allows number 0 as value | 対応 | parity_rendering_edge_test.lua: `x_bind_allows_zero` |
| 5 | x-bind allows empty string as value | 対応 | parity_rendering_edge_test.lua: `x_bind_allows_an_empty_string` |
| 6 | x-html rejects non-string | 対応 | parity_rendering_edge_test.lua: `x_html_rejects_a_value_that_is_not_a_string` |
| 7 | x-html with null/undefined emits empty | 対応 | parity_rendering_edge_test.lua: `x_html_with_null_or_undefined_writes_nothing` |
| 8 | x-text with null/undefined emits empty | 対応 | parity_rendering_edge_test.lua: `x_text_with_undefined_writes_nothing` |
| 9 | x-text numeric value converted to string | 対応 | parity_rendering_edge_test.lua: `x_text_writes_a_number_as_a_string` |
| 10 | x-bind:name overrides existing attribute | 対応 | parity_rendering_edge_test.lua: `x_bind_overrides_an_existing_attribute` |
| 11 | x-bind:name omits when expr evaluates to undefined even if original attr exists | 対応 | parity_rendering_edge_test.lua: `x_bind_omits_the_attribute_when_undefined` |
| 12 | x-match with no matching case and no nocase emits nothing in body | 対応 | parity_rendering_edge_test.lua: `x_match_without_a_matching_case_writes_an_empty_body` |
| 13 | x-for descending inclusive | 対応 | parity_rendering_edge_test.lua: `x_for_descending_inclusive` |
| 14 | x-for start equals stop emits one iteration | 対応 | parity_rendering_edge_test.lua: `x_for_start_equal_to_stop_iterates_once` |
| 15 | unconditional x-break stops immediately | 対応 | parity_rendering_edge_test.lua: `unconditional_x_break_stops_immediately` |
| 16 | elseif chain evaluates first true | 対応 | parity_rendering_edge_test.lua: `elseif_chain_selects_the_first_true` |
| 17 | x-include with non-string throws | 対応 | parity_rendering_edge_test.lua: `x_include_with_a_value_that_is_not_a_string_fails` |
| 18 | render preserves comments in normal positions | 対応 | parity_rendering_edge_test.lua: `render_keeps_comments` |
| 19 | x-elseif before x-else consumes chain even across whitespace | 対応 | parity_rendering_edge_test.lua: `x_elseif_joins_the_chain_across_white_space` |
| 20 | x-data can be pushed under x-if | 対応 | parity_rendering_edge_test.lua: `x_data_under_x_if` |
| 21 | nested x-data creates inner shadow | 対応 | parity_rendering_edge_test.lua: `nested_x_data_shadows_the_outer_one` |
| 22 | helper throwing wraps into ReflowRuntimeError with cause | 対応 | parity_rendering_edge_test.lua: `a_helper_error_is_a_runtime_error_with_its_cause` |
| 23 | x-bind on void element (img) | 対応 | parity_rendering_edge_test.lua: `x_bind_on_a_void_element` |
| 24 | x-text on void element emits open only | 対応 | parity_rendering_edge_test.lua: `x_text_on_a_void_element` |
| 25 | static Reflow.render with helpers | 対応 | parity_rendering_edge_test.lua: `render_once_with_helpers` |
| 26 | static Reflow.renderFile uses loader | 対応 | parity_rendering_edge_test.lua: `render_file_once_uses_the_loader` |
| 27 | Reflow errors carry all documented properties on ReflowError base | 対象外 | 検査内容がない（`assert.ok(true)` だけ） |

## `test/unit/scanner-snippet-edge.test.js`（15件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | scanner: handles processing instructions | 対応 | parity_scanner_test.lua: `handles_processing_instructions` |
| 2 | scanner: handles stray < character | 対応 | parity_scanner_test.lua: `handles_stray_lt_character` |
| 3 | scanner: handles unterminated comment | 対応 | parity_scanner_test.lua: `handles_unterminated_comment_doctype_and_processing_instruction` |
| 4 | scanner: handles unterminated doctype | 対応 | parity_scanner_test.lua: `handles_unterminated_comment_doctype_and_processing_instruction` |
| 5 | scanner: handles unterminated processing instruction | 対応 | parity_scanner_test.lua: `handles_unterminated_comment_doctype_and_processing_instruction` |
| 6 | scanner: handles unterminated close tag | 対応 | parity_scanner_test.lua: `handles_unterminated_close_tag` |
| 7 | scanner: handles unterminated open tag | 対応 | parity_scanner_test.lua: `handles_unterminated_open_tag` |
| 8 | scanner: handles unterminated script content | 対応 | parity_scanner_test.lua: `handles_unterminated_script_content` |
| 9 | scanner: allows uppercase tag names normalized | 対応 | parity_scanner_test.lua: `normalizes_uppercase_tag_names` |
| 10 | snippet: offset before start clamps to 0 | 対象外 | JS固有: snippet関数へ範囲外の位置を直接渡す。公開APIでは位置が常に範囲内 |
| 11 | snippet: offset past end clamps to length | 対象外 | JS固有: snippet関数へ範囲外の位置を直接渡す。公開APIでは位置が常に範囲内 |
| 12 | snippet: makeSnippet with negative start | 対象外 | JS固有: snippet関数へ範囲外の位置を直接渡す。公開APIでは位置が常に範囲内 |
| 13 | snippet: makeSnippet with end beyond length | 対象外 | JS固有: snippet関数へ範囲外の位置を直接渡す。公開APIでは位置が常に範囲内 |
| 14 | snippet: makeSnippet with end < start collapses | 対象外 | JS固有: snippet関数へ範囲外の位置を直接渡す。公開APIでは位置が常に範囲内 |
| 15 | snippet: multi-line highlight extends past line end | 対応 | parity_scanner_test.lua: `snippet_highlight_of_a_range_spanning_lines` |

## `test/unit/scanner.test.js`（7件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | scans plain elements | 対応 | parity_scanner_test.lua: `scans_plain_elements` |
| 2 | skips comments | 対応 | parity_scanner_test.lua: `skips_comments` |
| 3 | skips doctype | 対応 | parity_scanner_test.lua: `skips_doctype` |
| 4 | respects quoted attribute values with > inside | 対応 | parity_scanner_test.lua: `respects_quoted_attribute_values_with_gt_inside` |
| 5 | skips script content | 対応 | parity_scanner_test.lua: `skips_script_content` |
| 6 | skips style content | 対応 | parity_scanner_test.lua: `skips_style_content` |
| 7 | handles nested elements | 対応 | parity_scanner_test.lua: `handles_nested_elements` |

## `test/unit/scope.test.js`（6件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | resolveDollar returns globals | 対応 | parity_scope_test.lua: `dollar_resolves_the_globals` |
| 2 | resolveDot returns undefined when not found | 対応 | parity_scope_test.lua: `dot_returns_undefined_when_not_found` |
| 3 | resolveDot searches innermost frame first | 対応 | parity_scope_test.lua: `dot_searches_the_innermost_frame_first` |
| 4 | resolveAt skips loop frames | 対応 | parity_scope_test.lua: `at_skips_loop_frames` |
| 5 | resolveAt returns undefined when only loop frames have it | 対応 | parity_scope_test.lua: `at_returns_undefined_when_only_loop_frames_have_it` |
| 6 | popFrame restores previous state | 対応 | parity_scope_test.lua: `closing_a_frame_restores_the_previous_one` |

## `test/unit/snippet.test.js`（4件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | offsetToLineCol at start | 対応 | parity_snippet_test.lua: `line_and_column_at_start` |
| 2 | offsetToLineCol within first line | 対応 | parity_snippet_test.lua: `line_and_column_within_first_line` |
| 3 | offsetToLineCol across lines | 対応 | parity_snippet_test.lua: `line_and_column_across_lines` |
| 4 | makeSnippet includes context lines and caret | 対応 | parity_snippet_test.lua: `snippet_includes_context_lines_and_caret` |

## `test/unit/x-with-parser.test.js`（18件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | parseWith: single binding with primitive | 対応 | parity_x_with_parser_test.lua: `single_binding_with_primitive` |
| 2 | parseWith: multiple bindings separated by commas | 対応 | parity_x_with_parser_test.lua: `multiple_bindings_separated_by_commas` |
| 3 | parseWith: commas inside object / array literals are not binding separators | 対応 | parity_x_with_parser_test.lua: `commas_inside_object_and_array_literals_are_not_separators` |
| 4 | parseWith: commas inside helper calls are not binding separators | 対応 | parity_x_with_parser_test.lua: `commas_inside_helper_calls_are_not_separators` |
| 5 | parseWith: commas inside string literals are not binding separators | 対応 | parity_x_with_parser_test.lua: `commas_inside_string_literals_are_not_separators` |
| 6 | parseWith: escaped quotes inside string literals do not terminate the string | 対応 | parity_x_with_parser_test.lua: `escaped_quotes_inside_string_literals_do_not_end_the_string` |
| 7 | parseWith: scope reference RHS | 対応 | parity_x_with_parser_test.lua: `scope_reference_values` |
| 8 | parseWith: rejects duplicate binding names | 対応 | parity_x_with_parser_test.lua: `rejects_duplicate_binding_names` |
| 9 | parseWith: rejects missing "=" | 対応 | parity_x_with_parser_test.lua: `rejects_missing_equals` |
| 10 | parseWith: rejects missing binding name | 対応 | parity_x_with_parser_test.lua: `rejects_missing_binding_name` |
| 11 | parseWith: rejects empty value | 対応 | parity_x_with_parser_test.lua: `rejects_empty_value` |
| 12 | parseWith: rejects binding with empty expression | 対応 | parity_x_with_parser_test.lua: `rejects_binding_with_empty_expression` |
| 13 | parseWith: rejects unbalanced brackets | 対応 | parity_x_with_parser_test.lua: `rejects_unbalanced_brackets` |
| 14 | parseWith: rejects unterminated string | 対応 | parity_x_with_parser_test.lua: `rejects_unterminated_string` |
| 15 | parseWith: rejects invalid expression | 対応 | parity_x_with_parser_test.lua: `rejects_invalid_expression` |
| 16 | parseWith: trailing comma at top level is tolerated | 対応 | parity_x_with_parser_test.lua: `trailing_comma_at_top_level_is_tolerated` |
| 17 | parseWith: tolerates whitespace around tokens | 対応 | parity_x_with_parser_test.lua: `tolerates_whitespace_around_tokens` |
| 18 | parseWith: newlines between bindings are allowed | 対応 | parity_x_with_parser_test.lua: `newlines_between_bindings_are_allowed` |

## `test/x-with-selector.test.js`（3件）

| # | JavaScript版の試験 | 区分 | Lua版の試験・理由 |
|---|---|---|---|
| 1 | selector fragment: x-with ancestor pushes bindings during control-path walk | 対応 | render_fragment_test.lua: `with_bindings_reach_the_candidate` |
| 2 | selector fragment: x-with on x-include element carries bindings into included template | 対応 | render_fragment_test.lua: `with_bindings_reach_the_candidate` |
| 3 | selector fragment: x-with combined with x-each on ancestor | 対応 | render_fragment_test.lua: `with_bindings_reach_the_candidate` |

## 集計

| 区分 | 件数 |
|---|---|
| 対応 | 380 |
| 承認済み差異 | 3 |
| 対象外 | 10 |
| 合計（fixtures除く） | 393 |
