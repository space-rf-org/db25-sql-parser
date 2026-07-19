# AST Notes

Working notes on `ASTNode` field conventions and their intended lifetime /
ownership. See `include/db25/ast/ast_node.hpp` for the layout (fixed at 128
bytes, `static_assert`-enforced).

## String lifetime (arena ownership)

Every `std::string_view` stored on an AST node (`primary_text`, `schema_name`,
`catalog_name`, and any future string field) is copied into the parser's arena
via `Parser::copy_to_arena()`. AST strings therefore do **not** alias:

- the caller-supplied SQL text buffer passed to `parse()` / `parse_script()`, or
- the parser's internal tokenizer token buffer.

Because of this, `parse()` and `parse_script()` release the tokenizer before
returning (matching the error paths). A returned AST is valid for the lifetime
of the arena — i.e. until the `Parser` is destroyed or `reset()` / a subsequent
`parse()` / `parse_script()` recycles the arena. Regression coverage lives in
`tests/test_ast_lifetime.cpp`.

## `schema_name` overloads

`ASTNode::schema_name` is used for more than a literal schema qualifier. Current
uses, and their status:

- **Name / schema qualifiers** (identifiers, table refs, `CREATE`, `ANALYZE`,
  `REINDEX`, `PRAGMA`, ...): legitimate use — an actual namespace qualifier.
- **Table / derived-table aliases** (`NodeFlags::HasAlias`): the alias is stored
  in `schema_name`. **This is intentional and left as-is** — downstream
  consumers depend on this convention.
- **Window frame bound direction** (`FrameBound` nodes): *previously* stored the
  `"PRECEDING"` / `"FOLLOWING"` keyword in `schema_name`. This was clearly not a
  name qualifier and has been **moved out**:
  - direction is now recorded in `semantic_flags` via `FrameBoundFlags`
    (`FrameBoundPreceding` / `FrameBoundFollowing`; neither set => `CURRENT ROW`
    or bare `UNBOUNDED`), and
  - the full human-readable bound text (e.g. `"UNBOUNDED PRECEDING"`,
    `"5 FOLLOWING"`, `"CURRENT ROW"`) lives in `primary_text`.

## Deferred follow-up: aliases get their own field

Reclaiming `schema_name` fully — i.e. giving table/derived-table **aliases**
their own dedicated field instead of overloading `schema_name` + `HasAlias` — is
a deliberate follow-up. It is **not** done here because it changes an AST
convention that external consumers rely on, and must be coordinated with them
(and fit within the fixed 128-byte layout). Do not attempt it as part of
unrelated hardening work.
