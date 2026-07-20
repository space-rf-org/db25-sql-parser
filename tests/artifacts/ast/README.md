# AST Visualization Artifacts

Rendered abstract-syntax-tree figures for a set of representative, non-trivial
SQL queries. They serve as a visual regression reference and as documentation
of the tree shapes the parser produces for common analytical and DML patterns.

Each figure is provided in three forms:

- `*.dot` — Graphviz source (the canonical, diff-friendly artifact)
- `*.svg` — vector rendering (crisp at any zoom; preferred for docs)
- `*.png` — raster rendering at 110 dpi (for quick preview)

The style is intentionally monochrome: fill shade and border weight encode a
node's broad category (statement / clause / expression / reference / literal),
so the figures stay readable in print and in grayscale.

## Regenerating

The artifacts are reproducible from source. Build the `ast_to_dot` tool
(`-DBUILD_TOOLS=ON`), ensure Graphviz `dot` is on `PATH`, then run:

```sh
scripts/generate_ast_artifacts.sh            # uses build/ast_to_dot
scripts/generate_ast_artifacts.sh path/to/ast_to_dot   # explicit tool path
```

The five queries below are the single source of truth encoded in that script.

## Figures

| # | Figure | Query | What it exercises |
|---|--------|-------|-------------------|
| 01 | `01_analytical_window` | `SELECT d.name, COUNT(*) AS n, RANK() OVER (PARTITION BY d.id ORDER BY SUM(e.salary) DESC) FROM departments d JOIN employees e ON d.id = e.dept_id WHERE e.active = true GROUP BY d.id, d.name HAVING COUNT(*) > 5 ORDER BY n DESC LIMIT 10` | Window function with `PARTITION BY`/`ORDER BY`, aggregate + `RANK()`, `JOIN … ON`, `GROUP BY`/`HAVING`, `ORDER BY`, `LIMIT` |
| 02 | `02_recursive_cte` | `WITH RECURSIVE ancestors AS (SELECT id, parent_id FROM nodes WHERE id = 42 UNION ALL SELECT n.id, n.parent_id FROM nodes n JOIN ancestors a ON n.id = a.parent_id) SELECT * FROM ancestors` | Recursive CTE: anchor + recursive members joined by `UNION ALL`, self-reference through the CTE name |
| 03 | `03_correlated_subquery` | `SELECT e.name FROM employees e WHERE e.salary > (SELECT AVG(salary) FROM employees x WHERE x.dept_id = e.dept_id) AND e.dept_id IN (SELECT id FROM departments WHERE region = 'EU')` | Scalar correlated subquery in a predicate, plus an `IN (subquery)`; outer-column correlation across the boundary |
| 04 | `04_set_operation` | `SELECT id, name FROM current_users UNION SELECT id, name FROM (SELECT id, name FROM archived_users WHERE active = false) AS a` | `UNION` of two selects; second branch reads from an aliased derived table (subquery in `FROM`) |
| 05 | `05_dml_insert` | `INSERT INTO audit (user_id, action, ts) VALUES (1, 'login', now()), (2, 'logout', now())` | `INSERT` with an explicit column list and multiple `VALUES` rows, including a function call (`now()`) as a value |
