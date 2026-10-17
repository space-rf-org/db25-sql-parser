#!/usr/bin/env python3
"""The falsifiability gate for the DB25 SQL parser.

WHY THIS EXISTS. Fourteen of this repo's 246 commits claim in their message that
a mutation was killed, and every one of those claims was a HAND-RUN act: edit the
source, rebuild, watch a test fail, put the source back, write it down. That
makes the claim true on the day and unenforceable thereafter. Nothing stops a
later change from weakening the test that did the killing, and nothing re-runs
the experiment. (The other 232 commits claim nothing of the kind, which is the
same gap seen from the other side: most changes here were never checked this way
at all.) The umbrella repo has an automated gate (harness/harness.cpp, `kMutants[]`), and
the physical plan, logical plan and analyzer have grown theirs; this repo had
none - the last of the five, and the one with the most tests (thirty-nine
targets) and therefore the most room for a test that checks nothing.

So: the catalog below IS the record of those experiments, and running this file
re-performs them. A mutation that stops being caught fails CI.

WHAT IT DOES. For each mutant: apply it to the source, rebuild, run ctest, record
which test binaries failed, restore the source. A mutant caught by nobody is a
GATE FAILURE - the behaviour it breaks is no longer defended by any test.

THE RULE THIS TOOL EXISTS TO ENFORCE ON ITSELF. A mutation that does not APPLY
looks exactly like a mutation that was not caught: both show a green suite. That
happened twice while this catalog was being written by hand, and both times the
false result was believed for several minutes. So every mutation asserts its
find-string occurs EXACTLY ONCE before it is applied, and a catalog entry whose
pattern has drifted out of the source is a hard error, never a skip.

WHAT IT DOES NOT DO. It kills at TEST-BINARY granularity, not per test case -
this repo's suites are separate executables with their own main(), not a named
registry the way the umbrella's are. So it detects "this behaviour is defended by
no binary", not "this behaviour is defended by no individual assertion". The
vacuity half of the umbrella's gate (every test killed by some mutant) is
reported below as coverage rather than enforced, because a cost-model catalog
aimed at precedence and the depth guard cannot kill the DDL suites, and
pretending otherwise would mean
either an allowlist nobody maintains or a gate somebody disables.
"""
import argparse, json, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# THE CATALOG - the authority, and the record of experiments previously run by
# hand. Each entry names the behaviour it removes; `find` must occur exactly once.
# ---------------------------------------------------------------------------
# A parser is quietly wrong in two ways: it builds the wrong TREE for input it
# accepts, and it accepts input it should refuse. Precedence is the first;
# the depth guard is the second. Both are invisible to a test that only asks
# "did it parse".
MUTANTS = [
    dict(id="M1", name="AND and OR have equal precedence (a OR b AND c mis-associates)",
         file="src/parser/parser_expressions.cpp",
         find="        if (current_token_->keyword_id == db25::Keyword::AND) return 2;  // PREC_AND",
         repl="        if (current_token_->keyword_id == db25::Keyword::AND) return 1;  // MUTANT M1"),
    dict(id="M2", name="* and + have equal precedence (a + b * c mis-associates)",
         file="src/parser/parser_expressions.cpp",
         find='        if (op == "*" || op == "/" || op == "%") return 11;  // PREC_FACTOR',
         repl='        if (op == "*" || op == "/" || op == "%") return 10;  // MUTANT M2'),
    dict(id="M3", name="comparison binds tighter than concatenation (a || b = c mis-associates)",
         file="src/parser/parser_expressions.cpp",
         find='        if (op == "||") return 5;  // PREC_CONCAT',
         repl='        if (op == "||") return 3;  // MUTANT M3'),
    dict(id="M4", name="bitwise AND no longer binds tighter than comparison",
         file="src/parser/parser_expressions.cpp",
         find='        if (op == "&") return 8;   // Bitwise AND',
         repl='        if (op == "&") return 3;   // MUTANT M4'),
    dict(id="M5", name="the recursion DEPTH GUARD never trips (deep nesting is accepted)",
         file="include/db25/parser/parser.hpp",
         find="            if (parser_->current_depth_ >= parser_->config_.max_depth) {",
         repl="            if (false) {"),
    dict(id="M6", name="the depth guard trips but does not record that it did",
         file="include/db25/parser/parser.hpp",
         find="                parser_->depth_exceeded_ = true;",
         repl="                /* MUTANT M6 */;"),

    # M7-M10 defend the third way a parser is quietly wrong, and the one that
    # produced the worst failure found in this repo: it DROPS part of the input
    # and still reports success. `SELECT * FROM data` parsed to
    # `(SelectStmt (SelectList (Star)))` - no FROM clause, no error. M7 and M9
    # restore exactly that, so a suite that checked only has_value() cannot
    # kill them. M8 and M10 cover the other two name positions.
    dict(id="M7", name="a keyword table name produces no TableRef (FROM clause silently dropped)",
         file="src/parser/parser_select.cpp",
         find="    else if (token_is_a_name(current_token_)) {\n        auto* table_node = arena_.allocate<ast::ASTNode>();",
         repl="    else if (current_token_->type == tokenizer::TokenType::Identifier) {  // MUTANT M7\n        auto* table_node = arena_.allocate<ast::ASTNode>();"),
    dict(id="M8", name="a keyword alias is silently dropped (FROM t data loses the alias)",
         file="src/parser/parser_select.cpp",
         find="    if (token_is_a_name(current_token_)) {\n        // Store alias in schema_name field (repurposed for alias)",
         repl="    if (current_token_->type == tokenizer::TokenType::Identifier) {  // MUTANT M8\n        // Store alias in schema_name field (repurposed for alias)"),
    dict(id="M9", name="EVERY keyword counts as a name (NATURAL becomes the left table's alias)",
         file="src/parser/parser_select.cpp",
         find="        case db25::Keyword::UNKNOWN:\n            return false;",
         repl="        case db25::Keyword::UNKNOWN:\n            return true;  // MUTANT M9"),
    dict(id="M10", name="a keyword CTE name is rejected (WITH data AS (...) fails to parse)",
         file="src/parser/parser_select.cpp",
         find="        if (!token_is_a_name(current_token_)) {",
         repl="        if (current_token_->type != tokenizer::TokenType::Identifier) {  // MUTANT M10"),
    dict(id="M11", name="a FROM with no table reference is accepted and the clause dropped",
         file="src/parser/parser_select.cpp",
         find='            error("expected a table reference after FROM");\n            return nullptr;',
         repl="            /* MUTANT M11 */;"),
]


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, **kw)


def build(builddir):
    return sh(f"cmake --build {builddir} -j4").returncode == 0


def failing_tests(builddir):
    """Names of the ctest targets that FAILED. Empty set == suite is green."""
    r = sh(f"ctest --test-dir {builddir} --output-on-failure")
    out = set()
    for line in r.stdout.splitlines():
        line = line.strip()
        if " - " in line and "(Failed)" in line:
            out.add(line.split(" - ", 1)[1].replace(" (Failed)", "").strip())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--only", default="", help="run one mutant by id")
    ap.add_argument("--self-test", action="store_true",
                    help="inject a mutation NOTHING can catch and require this gate to fail. "
                         "A gate that has never been seen to fail is not known to be a gate.")
    args = ap.parse_args()
    bd = args.build_dir

    print("=" * 70)
    print(" db25 parser falsifiability gate")
    print("=" * 70)

    # Preflight: verify EVERY catalog pattern resolves before spending a single
    # rebuild. Finding a drifted anchor after fifteen minutes of building teaches
    # the same thing as finding it in two seconds.
    drift = []
    for m in MUTANTS:
        n = (ROOT / m["file"]).read_text().count(m["find"])
        if n != 1:
            drift.append(f"  {m['id']}: pattern occurs {n}x in {m['file']} (expected exactly 1)")
    if drift:
        print("GATE FAIL: the catalog has drifted from the source.")
        print("\n".join(drift))
        print("\nFix the entries. Deleting them silently retires the experiment they record.")
        return 1
    print(f"[preflight] all {len(MUTANTS)} catalog patterns resolve uniquely")

    if not build(bd):
        print("GATE FAIL: the tree does not build clean before any mutation")
        return 1
    clean_failures = failing_tests(bd)
    if clean_failures:
        print(f"GATE FAIL: {len(clean_failures)} test(s) fail CLEAN: {sorted(clean_failures)}")
        return 1
    print(f"[clean] suite is green\n")

    if args.self_test:
        # A change no assertion depends on: a comment. If the gate reports this
        # as CAUGHT, its notion of "caught" is picking up build noise rather than
        # test failure, and every other result it has ever printed is suspect.
        probe = dict(id="PROBE", name="a comment edit, which no test can possibly detect",
                     file="src/parser/parser_expressions.cpp",
                     find="int Parser::get_precedence() const {",
                     repl="int Parser::get_precedence() const {  // self-test probe")
        chosen = [probe]
    else:
        chosen = [m for m in MUTANTS if not args.only or m["id"] == args.only]
    survivors, killers = [], {}

    for m in chosen:
        path = ROOT / m["file"]
        original = path.read_text()
        # A mutation that does not apply is indistinguishable from one that is
        # not caught. Both show green. So this is a hard error, never a skip.
        n = original.count(m["find"])
        if n != 1:
            print(f"GATE FAIL: {m['id']} pattern occurs {n} times in {m['file']} (expected exactly 1).")
            print("           The catalog has drifted from the source. Fix the entry, do not delete it.")
            return 1
        path.write_text(original.replace(m["find"], m["repl"], 1))
        try:
            if not build(bd):
                # A mutation that does not compile has not tested anything.
                print(f"  {m['id']:<4} BUILD FAIL - mutation does not compile; not a result")
                survivors.append(m)
                continue
            failed = failing_tests(bd)
        finally:
            path.write_text(original)
        if failed:
            print(f"  {m['id']:<4} killed by {', '.join(sorted(failed))}")
            for t in failed:
                killers.setdefault(t, []).append(m["id"])
        else:
            print(f"  {m['id']:<4} *** SURVIVED *** {m['name']}")
            survivors.append(m)

    build(bd)  # leave the tree as we found it

    print()
    if args.self_test:
        if survivors:
            print("SELF-TEST PASSED: the probe survived, so this gate can report failure.")
            print("  (the run above shows the GATE FAILED path working)")
            return 0
        print("SELF-TEST FAILED: a comment edit was reported as CAUGHT.")
        print("  This gate cannot distinguish a test failure from noise; do not trust its results.")
        return 1

    if survivors:
        print(f"GATE FAILED: {len(survivors)} mutant(s) survived:")
        for m in survivors:
            print(f"  {m['id']}: {m['name']}")
        return 1

    print(f"GATE PASSED: all {len(chosen)} mutants caught.")
    print("\n[coverage] which binaries did the killing (information, not a gate):")
    for t, ids in sorted(killers.items()):
        print(f"  {t:<22} {len(ids):>2} mutant(s)")
    # Which binaries never did any killing. NOT a gate - see the module docstring -
    # but printed so the hole is visible and shrinking it is a visible improvement
    # rather than an invisible one.
    # WHOSE tests are these. ctest registers the VENDORED submodules' tests
    # alongside this repo's, and a catalog aimed at this repo cannot kill another
    # repo's binaries - nor should it be scored as though it had failed to. Those
    # repos carry their own gates and their own mutants.
    #
    # The declaring CMakeLists.txt is the only non-guessy way to tell them apart,
    # and ctest's JSON listing carries it. It also sidesteps a trap the text
    # listing set: ctest RIGHT-ALIGNS the number ("Test  #1:" has two spaces where
    # "Test #10:" has one), so matching the literal "Test #" silently dropped
    # tests 1-9 and this report understated its own blind spot for a month.
    listing = json.loads(sh(f"ctest --test-dir {bd} --show-only=json-v1").stdout)
    graph = listing.get("backtraceGraph", {})
    gfiles, gnodes = graph.get("files", []), graph.get("nodes", [])

    def declared_by(t):
        """The CMakeLists.txt that called add_test() for this test, or ""."""
        if "backtrace" not in t or not gnodes:
            return ""
        n = gnodes[t["backtrace"]]
        while "file" not in n and "parent" in n:
            n = gnodes[n["parent"]]
        return gfiles[n["file"]] if "file" in n else ""

    own, foreign = set(), set()
    for t in listing.get("tests", []):
        src = declared_by(t)
        rel = os.path.relpath(src, ROOT) if src else ".."
        vendored = rel.startswith("..") or rel.split(os.sep)[0] in ("external", "_deps")
        (foreign if vendored else own).add(t["name"])

    # Two cross-checks, because this attribution is now load-bearing for the one
    # number in this output that is a MEASUREMENT rather than a verdict. Both are
    # hard failures: a scope report that silently shrinks is the failure mode this
    # whole tool exists to refuse.
    if not own:
        print("GATE FAIL: not one test could be attributed to this repository. "
              "The declaring-file attribution is broken, not the suite.")
        return 1
    declared = re.search(r"Total Tests:\s*(\d+)",
                         sh(f"ctest --test-dir {bd} -N").stdout)
    if declared and int(declared.group(1)) != len(own) + len(foreign):
        print(f"GATE FAIL: attributed {len(own)} own + {len(foreign)} vendored tests "
              f"but ctest declares {declared.group(1)}.")
        return 1

    # A mutant killed ONLY by a vendored test is a real signal, not bookkeeping:
    # this repo's behaviour would then be defended solely by another repo's suite.
    only_foreign = sorted({mid for t in foreign for mid in killers.get(t, [])} -
                          {mid for t in own for mid in killers.get(t, [])})
    if only_foreign:
        print(f"\n[warning] {len(only_foreign)} mutant(s) killed ONLY by vendored tests: "
              f"{', '.join(only_foreign)}")
        print("  This repo's behaviour is defended solely by another repo's suite.")

    if foreign:
        print(f"\n[scope] {len(foreign)} vendored test(s) excluded from the count below: "
              f"{', '.join(sorted(foreign))}")
        print("  They belong to a submodule with its own gate; this catalog is not "
              "scored against them.")

    # Which of THIS repo's binaries never did any killing. NOT a gate - see the
    # module docstring - but printed so the hole is visible, and shrinking it is a
    # visible improvement rather than an invisible one.
    never = sorted(own - set(killers))
    if never:
        print(f"\n[uncovered] {len(never)} of this repo's {len(own)} binaries "
              f"this catalog cannot kill:")
        print("  " + ", ".join(never))
        print("  Not a failure: a catalog aimed at precedence and the depth guard does not reach the arena.")
        print("  It is the measure of how much of the suite this gate currently defends.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
