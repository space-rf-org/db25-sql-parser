# DB25 Parser - Test Specification & TDD Strategy

## Test-Driven Development Approach

We follow strict TDD principles:
1. **Write tests first** - Define expected behavior before implementation
2. **Red-Green-Refactor** - Fail, Pass, Optimize
3. **Incremental development** - One test at a time
4. **100% test coverage** - Every parser path must be tested

## Test Categories

### 1. Basic Parsing Tests
- **Empty/Invalid Input** - Error handling for edge cases
- **Token Stream** - Verify tokenizer integration
- **Error Messages** - Meaningful error reporting

### 2. SQL Statement Tests

#### SELECT Statements (Priority 1)
- [x] Simple SELECT * FROM table
- [x] SELECT with column list
- [x] SELECT with aliases (AS)
- [x] SELECT DISTINCT
- [x] WHERE clause with simple conditions
- [x] WHERE with AND/OR/NOT
- [x] ORDER BY (ASC/DESC)
- [x] LIMIT and OFFSET
- [x] GROUP BY
- [x] HAVING clause
- [x] JOIN (INNER, LEFT, RIGHT, FULL)
- [x] Multiple JOINs
- [x] Subqueries (IN, EXISTS, scalar)

#### DML Statements (Priority 2)
- [x] INSERT with values
- [x] INSERT with multiple rows
- [x] INSERT from SELECT
- [x] UPDATE with SET clause
- [x] UPDATE with multiple columns
- [x] DELETE with WHERE
- [x] DELETE all rows

#### DDL Statements (Priority 3)
- [x] CREATE TABLE with columns
- [x] Column types and constraints
- [x] PRIMARY KEY, FOREIGN KEY
- [ ] ALTER TABLE
- [ ] DROP TABLE
- [ ] CREATE INDEX

### 3. Expression Tests (Pratt Parser)

#### Operators by Precedence
1. **Primary** - Literals, Identifiers, Parentheses
2. **Unary** - NOT, -, +
3. **Multiplicative** - *, /, %
4. **Additive** - +, -
5. **Comparison** - <, >, <=, >=, =, <>, !=
6. **Logical** - AND, OR
7. **Special** - IN, BETWEEN, LIKE, IS NULL

#### Expression Types
- [x] Binary expressions with precedence
- [x] Unary expressions
- [x] Function calls
- [x] CASE expressions
- [x] Type casting
- [ ] Window functions

### 4. Literal Tests
- [x] Integer literals
- [x] Float/Decimal literals
- [x] String literals (single/double quotes)
- [x] Boolean literals (TRUE/FALSE)
- [x] NULL literal
- [ ] Date/Time literals
- [ ] Binary literals

### 5. Advanced Features
- [x] Common Table Expressions (CTEs)
- [ ] Window functions
- [ ] Recursive CTEs
- [ ] UNION/INTERSECT/EXCEPT
- [ ] Prepared statements

### 6. Error Recovery Tests
- [x] Missing semicolons
- [x] Extra semicolons
- [x] Invalid syntax recovery
- [ ] Partial statement completion
- [ ] Suggestion generation

### 7. Performance Tests
- [x] Deeply nested expressions (100+ levels)
- [x] Large SELECT lists (100+ columns)
- [x] Complex WHERE conditions
- [ ] Large IN lists (1000+ items)
- [ ] Stress testing with generated SQL

### 8. Memory Tests
- [x] Arena allocator reuse
- [x] Node count validation
- [ ] Memory leak detection
- [ ] Arena reset verification

## Implementation Priority

### Phase 1: Core SELECT (Week 1)
1. Basic SELECT with FROM
2. WHERE clause with simple conditions
3. SELECT list with columns
4. Basic expressions (literals, identifiers)

### Phase 2: Expressions (Week 1-2)
1. Pratt parser implementation
2. Binary operators with precedence
3. Comparison operators
4. Logical operators (AND/OR/NOT)

### Phase 3: Advanced SELECT (Week 2)
1. ORDER BY implementation
2. GROUP BY and aggregates
3. HAVING clause
4. LIMIT/OFFSET

### Phase 4: JOINs (Week 2-3)
1. Inner JOIN
2. Left/Right JOIN
3. JOIN conditions (ON clause)
4. Multiple JOINs

### Phase 5: DML Statements (Week 3)
1. INSERT implementation
2. UPDATE implementation
3. DELETE implementation

### Phase 6: Subqueries (Week 3-4)
1. Subqueries in WHERE
2. Subqueries in FROM
3. Correlated subqueries
4. EXISTS/IN operators

### Phase 7: DDL Statements (Week 4)
1. CREATE TABLE
2. Column definitions
3. Constraints

## Test Metrics

### Coverage Goals
- **Line Coverage**: 95%+
- **Branch Coverage**: 90%+
- **Statement Types**: 100%
- **Expression Types**: 100%
- **Error Cases**: 100%

### Performance Targets
- **Parse Time**: < 1ms for typical queries
- **Memory Usage**: < 10KB for typical queries
- **Deep Nesting**: Handle 1000+ levels
- **Large Queries**: Handle 10MB+ SQL

## AST Validation

Each test must verify:
1. **Correct node types** - Proper AST node classification
2. **Proper hierarchy** - Parent-child relationships
3. **Data integrity** - Values stored correctly
4. **Location tracking** - Line/column information
5. **Memory efficiency** - Using arena allocator

## Error Testing Strategy

For each statement type, test:
1. **Missing keywords** - "SELECT FROM users"
2. **Wrong order** - "FROM users SELECT *"
3. **Invalid tokens** - Special characters, malformed strings
4. **Incomplete statements** - "SELECT * FROM"
5. **Type mismatches** - "WHERE 'string' + 1"

## Integration Points

### Tokenizer Integration
- Verify token stream is consumed correctly
- EOF handling
- Comment/whitespace filtering works
- Token position tracking

### Arena Allocator Integration
- All nodes allocated in arena
- No memory leaks
- Proper reset between parses
- Memory reuse verification

### Error Reporting
- Line and column numbers accurate
- Error messages descriptive
- Context provided for errors
- Recovery suggestions when possible

## Test Execution Strategy

```bash
# Run all parser tests
./build/test_parser_comprehensive

# Run specific test suite
./build/test_parser_comprehensive --gtest_filter="ParserTest.Simple*"

# Run with memory checking
valgrind ./build/test_parser_comprehensive

# Run with coverage
./build/test_parser_comprehensive --coverage
```

## Success Criteria

A test is considered passing when:
1. Parser returns success/failure as expected
2. AST structure matches specification
3. All node types are correct
4. Memory is properly managed
5. Performance is within targets

## Next Steps

1. **Implement parse_statement()** - Dispatch to statement parsers
2. **Implement parse_select_stmt()** - Start with simple SELECT
3. **Implement expression parser** - Pratt algorithm
4. **Add one feature at a time** - Follow test order
5. **Refactor after each phase** - Maintain clean code