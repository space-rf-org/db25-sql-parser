-- DB25 Parser - Supported EBNF Test Suite
-- Tests all features that ARE supported by our parser

-- ============================================================================
-- TEST 1: Complex SELECT with CTEs, all operators, and expressions
-- ============================================================================

WITH RECURSIVE dept_hierarchy AS (
    SELECT 
        dept_id,
        dept_name,
        parent_id,
        1 AS level
    FROM departments
    WHERE parent_id IS NULL
    UNION ALL
    SELECT 
        d.dept_id,
        d.dept_name,
        d.parent_id,
        h.level + 1
    FROM departments d
    INNER JOIN dept_hierarchy h ON d.parent_id = h.dept_id
    WHERE h.level < 5
)
SELECT 
    h.dept_id,
    h.dept_name || ' (Level ' || CAST(h.level AS VARCHAR(10)) || ')' AS display_name,
    h.level,
    e.employee_count,
    e.total_salary,
    e.avg_salary,
    CASE 
        WHEN e.employee_count > 50 THEN 'Large'
        WHEN e.employee_count > 20 THEN 'Medium'
        WHEN e.employee_count > 0 THEN 'Small'
        ELSE 'Empty'
    END AS team_size,
    EXTRACT(YEAR FROM CURRENT_DATE) AS current_year,
    CAST(e.total_salary / 12 AS DECIMAL(10,2)) AS monthly_cost,
    h.dept_id & 0xFF AS masked_id,
    h.dept_id | 0x100 AS flagged_id,
    h.dept_id ^ 0x55 AS xor_id,
    h.level << 2 AS shifted_level,
    h.dept_id >> 1 AS half_id,
    e.employee_count % 10 AS mod_count,
    NOT (e.employee_count > 100) AS is_small_team,
    EXISTS (SELECT 1 FROM projects p WHERE p.dept_id = h.dept_id) AS has_projects,
    h.dept_name LIKE 'Sales%' AS is_sales,
    h.level BETWEEN 1 AND 3 AS is_top_level,
    h.dept_id IN (1, 2, 3, 4, 5) AS is_core_dept,
    e.avg_salary IS NOT NULL AS has_employees,
    COALESCE(e.employee_count, 0) AS safe_count,
    CONCAT('DEPT-', CAST(h.dept_id AS VARCHAR)) AS dept_code,
    SUBSTRING(h.dept_name FROM 1 FOR 10) AS short_name,
    TRIM(h.dept_name) AS clean_name,
    LENGTH(h.dept_name) AS name_length
FROM dept_hierarchy h
LEFT JOIN (
    SELECT 
        dept_id,
        COUNT(*) AS employee_count,
        SUM(salary) AS total_salary,
        AVG(salary) AS avg_salary,
        MAX(salary) AS max_salary,
        MIN(salary) AS min_salary
    FROM employees
    WHERE is_active = true
    GROUP BY dept_id
    HAVING COUNT(*) > 0
) e ON h.dept_id = e.dept_id
WHERE h.level <= 5
  AND (e.employee_count > 0 OR h.level = 1)
ORDER BY h.level, e.employee_count DESC NULLS LAST
LIMIT 100 OFFSET 0;

-- ============================================================================
-- TEST 2: INSERT with expressions and subqueries
-- ============================================================================

INSERT INTO employees (
    employee_id,
    name,
    dept_id,
    salary,
    bonus,
    hire_date,
    is_active,
    manager_id
)
VALUES 
    (1001, 'John Smith', 1, 75000, 5000, '2024-01-15', true, NULL),
    (1002, 'Jane Doe', 2, 85000, 7500, '2024-02-01', true, 1001),
    (1003, 'Bob Johnson', 1, 65000, NULL, '2024-03-01', true, 1001);

INSERT INTO audit_log (action, user_id, timestamp, details)
SELECT 
    'EMPLOYEE_ADDED' AS action,
    current_user AS user_id,
    CURRENT_TIMESTAMP AS timestamp,
    'Added ' || COUNT(*) || ' employees' AS details
FROM employees
WHERE hire_date >= CURRENT_DATE - INTERVAL '30' DAY;

-- ============================================================================
-- TEST 3: UPDATE with joins and complex conditions
-- ============================================================================

UPDATE employees e
SET 
    salary = e.salary * 1.05,
    bonus = CASE 
        WHEN e.salary > 80000 THEN e.salary * 0.15
        WHEN e.salary > 60000 THEN e.salary * 0.10
        ELSE e.salary * 0.05
    END,
    updated_at = CURRENT_TIMESTAMP
FROM departments d
WHERE e.dept_id = d.dept_id
  AND d.is_active = true
  AND e.is_active = true
  AND e.hire_date < CURRENT_DATE - INTERVAL '365' DAY
  AND EXISTS (
      SELECT 1 
      FROM performance_reviews pr
      WHERE pr.employee_id = e.employee_id
        AND pr.score >= 3.5
  );

-- ============================================================================
-- TEST 4: DELETE with subqueries
-- ============================================================================

DELETE FROM temp_data
WHERE created_at < CURRENT_TIMESTAMP - INTERVAL '7' DAY
  AND id NOT IN (
      SELECT DISTINCT reference_id 
      FROM active_sessions 
      WHERE last_activity > CURRENT_TIMESTAMP - INTERVAL '1' DAY
  )
  AND category IN ('TEMP', 'CACHE', 'SESSION');

-- ============================================================================
-- TEST 5: CREATE TABLE with all supported constraints
-- ============================================================================

CREATE TABLE IF NOT EXISTS test_table (
    id INTEGER PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    email VARCHAR(255) UNIQUE,
    dept_id INTEGER REFERENCES departments(dept_id),
    salary DECIMAL(10,2) CHECK (salary > 0),
    hire_date DATE DEFAULT CURRENT_DATE,
    is_active BOOLEAN DEFAULT true,
    metadata JSON,
    tags TEXT[],
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(name, email),
    CHECK(salary < 1000000)
);

-- ============================================================================
-- TEST 6: CREATE INDEX variations
-- ============================================================================

CREATE INDEX idx_test_name ON test_table (name);
CREATE UNIQUE INDEX idx_test_email ON test_table (email);
CREATE INDEX idx_test_composite ON test_table (dept_id, is_active);
CREATE INDEX idx_test_expression ON test_table ((LOWER(name)));

-- ============================================================================
-- TEST 7: CREATE VIEW with complex query
-- ============================================================================

CREATE VIEW employee_summary AS
SELECT 
    d.dept_name,
    COUNT(e.employee_id) AS employee_count,
    AVG(e.salary) AS avg_salary,
    MAX(e.salary) AS max_salary,
    MIN(e.salary) AS min_salary
FROM departments d
LEFT JOIN employees e ON d.dept_id = e.dept_id
WHERE d.is_active = true
GROUP BY d.dept_id, d.dept_name
HAVING COUNT(e.employee_id) > 0;

-- ============================================================================
-- TEST 8: ALTER TABLE statements
-- ============================================================================

ALTER TABLE test_table ADD COLUMN middle_name VARCHAR(100);
ALTER TABLE test_table DROP COLUMN tags;
ALTER TABLE test_table RENAME COLUMN email TO email_address;
ALTER TABLE test_table ALTER COLUMN salary SET DEFAULT 50000;

-- ============================================================================
-- TEST 9: DROP statements
-- ============================================================================

DROP TABLE IF EXISTS old_table CASCADE;
DROP INDEX IF EXISTS old_index;
DROP VIEW IF EXISTS old_view CASCADE;

-- ============================================================================
-- TEST 10: TRUNCATE
-- ============================================================================

TRUNCATE TABLE temp_data;
TRUNCATE TABLE audit_log RESTART IDENTITY CASCADE;

-- ============================================================================
-- TEST 11: Complex joins (INNER, LEFT, RIGHT, FULL, CROSS)
-- ============================================================================

SELECT 
    e.name AS employee_name,
    d.dept_name,
    m.name AS manager_name,
    p.project_name,
    c.company_name
FROM employees e
INNER JOIN departments d ON e.dept_id = d.dept_id
LEFT JOIN employees m ON e.manager_id = m.employee_id
RIGHT JOIN projects p ON d.dept_id = p.dept_id
FULL OUTER JOIN companies c ON d.company_id = c.company_id
CROSS JOIN (SELECT 1 AS dummy) x
WHERE e.is_active = true
  AND d.is_active = true;

-- ============================================================================
-- TEST 12: Subqueries in various positions
-- ============================================================================

SELECT 
    e.name,
    e.salary,
    (SELECT AVG(salary) FROM employees WHERE dept_id = e.dept_id) AS dept_avg,
    (SELECT dept_name FROM departments WHERE dept_id = e.dept_id) AS dept_name
FROM employees e
WHERE e.salary > (
    SELECT AVG(salary) * 1.2 
    FROM employees 
    WHERE dept_id = e.dept_id
)
AND e.dept_id IN (
    SELECT dept_id 
    FROM departments 
    WHERE budget > 100000
)
AND EXISTS (
    SELECT 1 
    FROM performance_reviews pr 
    WHERE pr.employee_id = e.employee_id 
      AND pr.score > 4
);

-- ============================================================================
-- TEST 13: UNION, INTERSECT, EXCEPT inside SELECT
-- ============================================================================

SELECT employee_id, name, 'Employee' AS type FROM employees WHERE is_active = true
UNION ALL
SELECT contractor_id, name, 'Contractor' AS type FROM contractors WHERE end_date > CURRENT_DATE
UNION
SELECT temp_id, name, 'Temp' AS type FROM temp_workers WHERE is_active = true;

-- ============================================================================
-- TEST 14: Window functions (if supported)
-- ============================================================================

SELECT 
    employee_id,
    name,
    salary,
    dept_id,
    ROW_NUMBER() OVER (PARTITION BY dept_id ORDER BY salary DESC) AS salary_rank,
    RANK() OVER (ORDER BY salary DESC) AS global_rank,
    DENSE_RANK() OVER (PARTITION BY dept_id ORDER BY salary DESC) AS dense_rank,
    SUM(salary) OVER (PARTITION BY dept_id) AS dept_total,
    AVG(salary) OVER () AS company_avg,
    LAG(salary, 1) OVER (ORDER BY employee_id) AS prev_salary,
    LEAD(salary, 1) OVER (ORDER BY employee_id) AS next_salary
FROM employees
WHERE is_active = true;

-- ============================================================================
-- TEST 15: All supported functions
-- ============================================================================

SELECT 
    -- Aggregate functions
    COUNT(*) AS total_count,
    COUNT(DISTINCT dept_id) AS unique_depts,
    SUM(salary) AS total_salary,
    AVG(salary) AS avg_salary,
    MAX(salary) AS max_salary,
    MIN(salary) AS min_salary,
    
    -- String functions
    CONCAT(first_name, ' ', last_name) AS full_name,
    SUBSTRING(email FROM 1 FOR 10) AS email_prefix,
    TRIM(BOTH ' ' FROM notes) AS clean_notes,
    LENGTH(name) AS name_length,
    LOWER(name) AS lower_name,
    UPPER(name) AS upper_name,
    
    -- Date functions
    EXTRACT(YEAR FROM hire_date) AS hire_year,
    EXTRACT(MONTH FROM hire_date) AS hire_month,
    EXTRACT(DAY FROM hire_date) AS hire_day,
    
    -- Math functions
    ABS(balance) AS abs_balance,
    ROUND(salary, 2) AS rounded_salary,
    FLOOR(salary / 1000) AS salary_thousands,
    CEIL(bonus / 100) AS bonus_hundreds,
    
    -- Type conversion
    CAST(employee_id AS VARCHAR(10)) AS emp_id_str,
    CAST(salary AS INTEGER) AS salary_int,
    CAST('2024-01-01' AS DATE) AS fixed_date,
    
    -- Conditional
    COALESCE(bonus, 0) AS safe_bonus,
    NULLIF(dept_id, 0) AS non_zero_dept,
    GREATEST(salary, bonus, 50000) AS max_value,
    LEAST(salary, 100000) AS capped_salary
    
FROM employees
GROUP BY employee_id;

-- ============================================================================
-- END OF SUPPORTED EBNF TEST SUITE
-- ============================================================================