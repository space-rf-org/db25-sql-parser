# SQL Support & Scope

This document records what the DB25 SQL parser currently supports, what is
partial, and what is deliberately **out of scope**. It reflects the parser's
actual, test-verified behavior — not aspirational grammar.

## Supported

### Queries (SELECT)
- All standard clauses: `SELECT [DISTINCT]`, `FROM`, `WHERE`, `GROUP BY`,
  `HAVING`, `ORDER BY` (with `ASC`/`DESC`, `NULLS FIRST/LAST`), `LIMIT`/`OFFSET`.
- Joins: `INNER`, `LEFT`, `RIGHT`, `FULL`, `CROSS` (with optional `OUTER`),
  both `ON <condition>` and `USING (col, ...)`.
- Derived tables / subqueries in `FROM` and `JOIN`, e.g. `FROM (SELECT ...) AS t`.
- Scalar subqueries, `IN (subquery)`, `EXISTS`/`NOT EXISTS`, correlated subqueries.
- CTEs, including `WITH RECURSIVE`.
- Window functions with `OVER (PARTITION BY ... ORDER BY ... ROWS|RANGE ...)`.
- `GROUP BY` with `GROUPING SETS` / `CUBE` / `ROLLUP`.
- Set operations: `UNION [ALL]`, `INTERSECT [ALL]`, `EXCEPT [ALL]`.

### Expressions
- Full operator-precedence (Pratt) parsing.
- `CASE`, `CAST(... AS ...)`, `BETWEEN`, `IN (list)`, `LIKE`, `IS [NOT] NULL`.
- Function calls (including `DISTINCT` args), `EXTRACT`, comparison with
  `ANY`/`ALL`/`SOME`.
- `ARRAY[...]` constructors, `ROW(...)` and bare `(a, b, c)` tuple expressions,
  `INTERVAL '<literal>'`.

### DML
- `INSERT` (`VALUES`, `INSERT ... SELECT`, `DEFAULT VALUES`).
- `UPDATE` (with `FROM` and `RETURNING`).
- `DELETE` (with `USING` and `RETURNING`).

### DDL & statements
- `CREATE TABLE` with column definitions, constraints, and array types (`TEXT[]`).
- `CREATE INDEX` / `CREATE VIEW` / `ALTER TABLE` / `DROP` / `TRUNCATE` (core forms).
- `CREATE TRIGGER` / `CREATE SCHEMA` (basic).
- Transaction control: `BEGIN`/`COMMIT`/`ROLLBACK`/`SAVEPOINT`/`RELEASE`.
- `EXPLAIN`, `VALUES`, `SET`, and utility statements (`VACUUM`, `ANALYZE`, ...).
- Multiple `;`-separated statements via `parse_script()`.

## Partial / known limitations
- `NATURAL JOIN` and explicit `LATERAL` joins are not parsed.
- Schema-qualified table names in `FROM` (`schema.table`) are limited.
- `INSERT ... ON CONFLICT`: the clause is recognized but conflict targets and
  `DO UPDATE SET` are not fully parsed.
- The parser is lenient: trailing tokens after an otherwise-complete statement
  are not rejected. Strict trailing-garbage detection needs the recursive-descent
  parsers to reliably consume all tokens first.
- Error reporting returns a `ParseError` (the parser never aborts the process),
  but error *recovery* is minimal — parsing stops at the first error.

## Out of scope (intentionally)

These SQL features are **not** planned. They add large grammar and
semantic-analysis surface for little practical benefit here, and are better
treated as liabilities than as gaps:

- Row pattern recognition (`MATCH_RECOGNIZE`).
- `MULTISET` operations; ordered-set and statistical aggregates.
- `PIVOT` / `UNPIVOT`.
- XML type and XPath/XQuery expressions.
- Temporal / system-versioned tables (SQL:2011/2016 period predicates).
- Geometric, network (`INET`/`CIDR`), and range data types.
- `GROUPS` window-frame mode and frame `EXCLUDE`.
- Dialect-specific identifier quoting (MySQL backticks, SQL Server brackets) —
  a tokenizer-level concern.

If any of these becomes a real requirement, it should be scoped as its own
deliberate project with dedicated grammar, AST, and validation work.
