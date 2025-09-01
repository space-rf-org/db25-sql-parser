# DB25 SQL Parser - Grammar Gap Analysis Report

## Executive Summary

Analysis of modern SQL:2011/2016 features against our DB25_SQL_GRAMMAR.ebnf reveals significant gaps in supporting contemporary analytical SQL. While our parser achieves 98.3% coverage of traditional SQL, it lacks most modern OLAP and temporal features.

## Missing Grammar Features by Category

### 1. **TEMPORAL QUERIES (SQL:2011)** ❌ Not in Grammar

**Missing Clauses:**
- `FOR SYSTEM_TIME AS OF <timestamp>`
- `FOR SYSTEM_TIME FROM <start> TO <end>`
- `FOR SYSTEM_TIME BETWEEN <start> AND <end>`
- `FOR PORTION OF <period> FROM <start> TO <end>`
- `FOR BUSINESS_TIME`

**Required Grammar Extension:**
```ebnf
temporal_clause = for_system_time | for_application_time ;
for_system_time = "FOR" "SYSTEM_TIME" 
                  ( "AS" "OF" expression
                  | "FROM" expression "TO" expression
                  | "BETWEEN" expression "AND" expression ) ;
```

### 2. **JSON OPERATIONS (SQL:2016)** ❌ Not in Grammar

**Missing Features:**
- `JSON_TABLE` function with LATERAL
- `JSON_OBJECT`, `JSON_ARRAY`, `JSON_ARRAYAGG`
- JSON path operators: `->`, `->>`
- JSON predicates: `@>`, `@?`, `@@`
- `COLUMNS` clause with `PATH`, `FOR ORDINALITY`
- `NESTED PATH` for hierarchical JSON

**Required Grammar Extension:**
```ebnf
json_table = "JSON_TABLE" "(" expression "," json_path 
             "COLUMNS" "(" json_column_def { "," json_column_def } ")" ")" ;
json_column_def = column_name data_type "PATH" json_path
                | column_name "FOR" "ORDINALITY" 
                | "NESTED" "PATH" json_path "COLUMNS" "(" json_column_def { "," json_column_def } ")" ;
```

### 3. **MATCH_RECOGNIZE (SQL:2016)** ❌ Not in Grammar

**Pattern Recognition Clause - Completely Missing:**
- `MATCH_RECOGNIZE` block
- `PARTITION BY`, `ORDER BY` within match
- `MEASURES` clause
- `PATTERN` with regex-like syntax
- `DEFINE` for pattern variables
- `ONE ROW PER MATCH` / `ALL ROWS PER MATCH`
- `AFTER MATCH SKIP` strategies

**Required Grammar:**
```ebnf
match_recognize = "MATCH_RECOGNIZE" "("
                  [ partition_clause ]
                  [ order_by_clause ]
                  [ measures_clause ]
                  rows_per_match
                  [ after_match ]
                  "PATTERN" "(" pattern_expression ")"
                  "DEFINE" pattern_definitions
                  ")" ;
```

### 4. **ADVANCED WINDOW FEATURES** ⚠️ Partial

**Missing:**
- `IGNORE NULLS` / `RESPECT NULLS` in window functions
- `FROM FIRST` / `FROM LAST` in NTH_VALUE
- `GROUPS` frame unit (only have ROWS and RANGE)
- Named window reuse in expressions
- `EXCLUDE` clause in frames

### 5. **INTERVAL ARITHMETIC** ❌ Not in Grammar

**Missing:**
- `INTERVAL 'n' unit` literals
- Date/time arithmetic with intervals
- `DATE '2025-01-01'` literals
- `TIMESTAMP '2025-01-01 10:00:00'` literals

### 6. **LATERAL JOINS** ⚠️ Partial

**Missing:**
- `CROSS JOIN LATERAL`
- `LEFT JOIN LATERAL ... ON TRUE`
- Correlation with outer query in LATERAL

### 7. **MERGE STATEMENT (SQL:2003)** ❌ Not in Grammar

```ebnf
merge_statement = "MERGE" "INTO" table_name
                  "USING" table_source "ON" condition
                  when_matched_clause
                  when_not_matched_clause
                  [ when_not_matched_by_source ] ;
```

### 8. **GROUPING SETS Extensions** ⚠️ Partial

**Missing:**
- `GROUPING()` function
- `GROUPING_ID()` function
- `CUBE()` operation
- `ROLLUP()` operation
- Complex grouping set combinations

### 9. **FILTER Clause for Aggregates** ❌ Not in Grammar

```ebnf
aggregate_function = function_name "(" [ expression ] ")"
                    [ "FILTER" "(" "WHERE" condition ")" ] ;
```

### 10. **Advanced Aggregates** ❌ Not in Grammar

**Missing:**
- `LISTAGG ... ON OVERFLOW TRUNCATE`
- `PERCENTILE_CONT() WITHIN GROUP`
- `PERCENTILE_DISC() WITHIN GROUP`
- `MODE() WITHIN GROUP`
- `STRING_AGG()` with ORDER BY
- `ARRAY_AGG()` with ORDER BY and FILTER

### 11. **CONNECT BY (Hierarchical)** ❌ Not in Grammar

Oracle-style hierarchical queries:
- `START WITH`
- `CONNECT BY PRIOR`
- `LEVEL` pseudocolumn
- `SYS_CONNECT_BY_PATH()`
- `CONNECT_BY_ISLEAF`, `CONNECT_BY_ISCYCLE`

### 12. **PIVOT/UNPIVOT** ❌ Not in Grammar

```ebnf
pivot_clause = "PIVOT" "(" aggregate_function 
               "FOR" column "IN" "(" pivot_values ")" ")" ;
unpivot_clause = "UNPIVOT" "(" column "FOR" column "IN" "(" unpivot_columns ")" ")" ;
```

### 13. **TABLESAMPLE** ❌ Not in Grammar

```ebnf
tablesample_clause = "TABLESAMPLE" sampling_method "(" percentage ")"
                     [ "REPEATABLE" "(" seed ")" ] ;
sampling_method = "BERNOULLI" | "SYSTEM" ;
```

### 14. **VALUES as Table Constructor** ⚠️ Limited

Current grammar has basic VALUES for INSERT, missing:
- Standalone VALUES statement with CTEs
- VALUES with column aliases
- VALUES in FROM clause

### 15. **GENERATED Columns** ❌ Not in Grammar

```ebnf
column_definition = column_name data_type
                   [ "GENERATED" "ALWAYS" "AS" "(" expression ")" 
                     [ "STORED" | "VIRTUAL" ] ] ;
```

### 16. **Advanced Data Types** ❌ Not in Grammar

**Missing:**
- `JSON` / `JSONB`
- `ARRAY` types: `INT[]`, `TEXT[]`
- `ENUM` types
- `tsvector` / `tsquery` (full-text search)
- `FLOAT[n]` for vectors/embeddings
- `daterange`, `tsrange`, etc.

### 17. **EXCLUDE Constraints** ❌ Not in Grammar

```ebnf
exclude_constraint = "EXCLUDE" "USING" index_method 
                     "(" exclude_element { "," exclude_element } ")" ;
```

### 18. **FOR UPDATE Extensions** ⚠️ Limited

**Missing:**
- `FOR UPDATE OF table_name`
- `FOR UPDATE NOWAIT`
- `FOR UPDATE SKIP LOCKED`
- `FOR SHARE`
- `FOR KEY SHARE`

### 19. **ON CONFLICT (UPSERT)** ❌ Not in Grammar

```ebnf
on_conflict_clause = "ON" "CONFLICT" [ conflict_target ] 
                     "DO" ( "NOTHING" | "UPDATE" "SET" update_list ) ;
```

### 20. **RETURNING Clause** ❌ Not in Grammar

```ebnf
returning_clause = "RETURNING" ( "*" | expression_list ) ;
```

## Impact Assessment

### Critical for Modern Analytics (Must Have)
1. JSON operations - Essential for NoSQL/document workloads
2. LATERAL joins - Required for complex correlated queries
3. Advanced window functions - Core OLAP functionality
4. FILTER clause - Standard SQL:2003 feature
5. MERGE statement - ETL operations

### Important for Completeness (Should Have)
1. Temporal queries - Growing adoption
2. GROUPING SETS/CUBE/ROLLUP - OLAP standard
3. Advanced aggregates (WITHIN GROUP) - Analytics
4. ON CONFLICT - Common in applications
5. RETURNING clause - Reduces round trips

### Nice to Have (Could Have)
1. MATCH_RECOGNIZE - Advanced pattern matching
2. PIVOT/UNPIVOT - Convenience features
3. CONNECT BY - Oracle compatibility
4. TABLESAMPLE - Sampling large datasets
5. GENERATED columns - Computed columns

## Recommendations

### Phase 1: Core Modern SQL (2-3 weeks)
- Add JSON operations (JSON_TABLE, operators)
- Implement LATERAL joins properly
- Add FILTER clause for aggregates
- Support INTERVAL arithmetic
- Implement MERGE statement

### Phase 2: Analytics Extensions (2-3 weeks)
- Complete GROUPING SETS/CUBE/ROLLUP
- Add WITHIN GROUP aggregates
- Implement advanced window options
- Add ON CONFLICT clause
- Support RETURNING clause

### Phase 3: Advanced Features (3-4 weeks)
- Temporal queries (FOR SYSTEM_TIME)
- MATCH_RECOGNIZE pattern matching
- PIVOT/UNPIVOT operations
- TABLESAMPLE clause
- GENERATED columns

### Phase 4: Vendor Extensions (Optional)
- CONNECT BY (Oracle)
- Array operations (PostgreSQL)
- Full-text search (PostgreSQL)
- Vector types for ML

## Grammar Extension Size Estimate

Current EBNF: ~500 rules
Required additions: ~150-200 new rules
Complexity increase: ~30-40%

## Parser Implementation Impact

- **Tokenizer**: Need new operators (->>, @>, ::, [])
- **Parser**: ~50 new parse functions
- **AST**: ~30 new node types
- **Memory**: Minimal impact with arena allocator
- **Performance**: MATCH_RECOGNIZE may need specialized handling

## Conclusion

While the DB25 parser excels at traditional SQL (98.3% coverage), it lacks most SQL:2011/2016 features required for modern analytical workloads. The grammar needs substantial extension (~30-40% growth) to support contemporary SQL.

Priority should be given to JSON operations, LATERAL joins, and advanced aggregates as these are commonly used in production systems. MATCH_RECOGNIZE and temporal queries, while powerful, can be deferred as they have limited adoption.

The good news: Our architecture (SIMD tokenizer, arena allocator, grammar-driven parser) can handle these extensions without major refactoring.