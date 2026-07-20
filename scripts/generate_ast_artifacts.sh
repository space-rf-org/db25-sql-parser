#!/usr/bin/env bash
#
# Generate AST visualization artifacts (DOT + SVG + PNG) for a set of
# representative complex queries, using the ast_to_dot tool and Graphviz.
#
# Usage:
#   scripts/generate_ast_artifacts.sh [path-to-ast_to_dot] [output-dir]
#
# Defaults: ast_to_dot is looked up in build/ then on PATH; output goes to
# tests/artifacts/ast/. Requires Graphviz `dot` on PATH.
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool="${1:-}"
outdir="${2:-$here/tests/artifacts/ast}"

if [[ -z "$tool" ]]; then
    for c in "$here/build/ast_to_dot" "$here/build-*/ast_to_dot"; do
        [[ -x $c ]] && tool="$c" && break
    done
    [[ -z "$tool" ]] && tool="$(command -v ast_to_dot || true)"
fi
[[ -x "$tool" ]] || { echo "ast_to_dot not found (build BUILD_TOOLS=ON, or pass its path)"; exit 1; }
command -v dot >/dev/null || { echo "Graphviz 'dot' not found on PATH"; exit 1; }

mkdir -p "$outdir"

# name -> SQL. Keep names zero-padded so the manifest orders naturally.
names=(01_analytical_window 02_recursive_cte 03_correlated_subquery 04_set_operation 05_dml_insert)
declare -A sql
sql[01_analytical_window]="SELECT d.name, COUNT(*) AS n, RANK() OVER (PARTITION BY d.id ORDER BY SUM(e.salary) DESC) FROM departments d JOIN employees e ON d.id = e.dept_id WHERE e.active = true GROUP BY d.id, d.name HAVING COUNT(*) > 5 ORDER BY n DESC LIMIT 10"
sql[02_recursive_cte]="WITH RECURSIVE ancestors AS (SELECT id, parent_id FROM nodes WHERE id = 42 UNION ALL SELECT n.id, n.parent_id FROM nodes n JOIN ancestors a ON n.id = a.parent_id) SELECT * FROM ancestors"
sql[03_correlated_subquery]="SELECT e.name FROM employees e WHERE e.salary > (SELECT AVG(salary) FROM employees x WHERE x.dept_id = e.dept_id) AND e.dept_id IN (SELECT id FROM departments WHERE region = 'EU')"
sql[04_set_operation]="SELECT id, name FROM current_users UNION SELECT id, name FROM (SELECT id, name FROM archived_users WHERE active = false) AS a"
sql[05_dml_insert]="INSERT INTO audit (user_id, action, ts) VALUES (1, 'login', now()), (2, 'logout', now())"

for n in "${names[@]}"; do
    printf '  %-24s' "$n"
    "$tool" "${sql[$n]}" > "$outdir/$n.dot"
    dot -Tsvg -o "$outdir/$n.svg" "$outdir/$n.dot"
    dot -Tpng -Gdpi=110 -o "$outdir/$n.png" "$outdir/$n.dot"
    echo "-> $n.{dot,svg,png}"
done

echo "Artifacts written to $outdir"
