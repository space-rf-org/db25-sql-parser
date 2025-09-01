-- DB25 Parser EBNF Complete Test Suite
-- This file exercises every production rule in our EBNF grammar
-- Tests complex nested queries mixing all supported SQL features

-- ============================================================================
-- TEST 1: Maximum complexity SELECT with all clauses, operators, and features
-- ============================================================================

WITH RECURSIVE organization_hierarchy AS (
    -- Anchor: Top-level departments with all expression types
    SELECT 
        d.dept_id,
        d.dept_name || ' (' || CAST(d.dept_id AS VARCHAR(10)) || ')' AS display_name,
        d.parent_dept_id,
        CAST(1 AS INTEGER) AS level,
        CAST(d.dept_name AS VARCHAR(1000)) AS path,
        d.budget * 1.1 + 1000 AS adjusted_budget,
        d.headcount & 0xFF AS masked_headcount,
        d.flags | 0x01 AS updated_flags,
        d.priority ^ 0x0F AS xor_priority,
        d.sort_order << 2 AS shifted_left,
        d.display_order >> 1 AS shifted_right,
        CASE 
            WHEN d.budget > 1000000 THEN 'Large'
            WHEN d.budget > 100000 THEN 'Medium'
            WHEN d.budget > 10000 THEN 'Small'
            ELSE 'Tiny'
        END AS size_category,
        EXTRACT(YEAR FROM d.created_date) AS creation_year,
        NOT d.is_active AS is_inactive,
        EXISTS (SELECT 1 FROM employees e WHERE e.dept_id = d.dept_id) AS has_employees,
        d.budget BETWEEN 10000 AND 1000000 AS budget_in_range,
        d.dept_name LIKE 'Sales%' AS is_sales_dept,
        d.status IN ('Active', 'Pending', 'Review') AS valid_status,
        d.manager_id IS NOT NULL AS has_manager,
        d.manager_id IS NULL AS no_manager,
        COALESCE(d.alt_name, d.dept_name, 'Unknown') AS display_dept,
        CONCAT('DEPT-', CAST(d.dept_id AS VARCHAR(10))) AS dept_code,
        SUBSTRING(d.description FROM 1 FOR 100) AS short_desc,
        TRIM(d.notes) AS clean_notes,
        LENGTH(d.dept_name) AS name_length,
        -d.deficit AS positive_deficit
    FROM departments d
    WHERE d.parent_dept_id IS NULL
      AND d.is_active = true
      AND d.budget % 1000 = 0
      
    UNION ALL
    
    -- Recursive: Child departments with complex joins and subqueries
    SELECT 
        d.dept_id,
        REPEAT('  ', h.level) || d.dept_name AS display_name,
        d.parent_dept_id,
        h.level + 1,
        h.path || ' > ' || d.dept_name,
        (d.budget + h.adjusted_budget) / 2,
        d.headcount & h.masked_headcount,
        d.flags | h.updated_flags,
        d.priority ^ h.xor_priority,
        d.sort_order << 1,
        d.display_order >> 1,
        CASE h.size_category
            WHEN 'Large' THEN 
                CASE 
                    WHEN d.budget > 500000 THEN 'Large'
                    ELSE 'Medium'
                END
            ELSE h.size_category
        END,
        EXTRACT(YEAR FROM d.created_date),
        NOT d.is_active,
        EXISTS (
            SELECT 1 
            FROM employees e 
            WHERE e.dept_id = d.dept_id 
              AND e.salary > (SELECT AVG(salary) FROM employees)
        ),
        d.budget BETWEEN h.adjusted_budget * 0.5 AND h.adjusted_budget * 1.5,
        d.dept_name LIKE h.path || '%',
        d.status IN (SELECT DISTINCT status FROM departments WHERE parent_dept_id = h.dept_id),
        d.manager_id IS NOT NULL,
        d.manager_id IS NULL,
        COALESCE(d.alt_name, h.display_dept),
        CONCAT(h.dept_code, '-', CAST(d.dept_id AS VARCHAR(10))),
        SUBSTRING(d.description FROM h.level * 10 FOR 50),
        TRIM(BOTH ' ' FROM d.notes),
        LENGTH(d.dept_name) + LENGTH(h.path),
        -(d.deficit + h.positive_deficit)
    FROM departments d
    INNER JOIN organization_hierarchy h ON d.parent_dept_id = h.dept_id
    WHERE h.level < 10
      AND d.is_active = true OR h.has_employees = true
),
employee_stats AS (
    SELECT 
        e.dept_id,
        COUNT(*) AS employee_count,
        COUNT(DISTINCT e.manager_id) AS manager_count,
        AVG(e.salary) AS avg_salary,
        MAX(e.salary) AS max_salary,
        MIN(e.salary) AS min_salary,
        SUM(e.bonus) AS total_bonus,
        STRING_AGG(e.name, ', ' ORDER BY e.salary DESC) AS employee_list,
        ARRAY_AGG(e.employee_id ORDER BY e.hire_date) AS employee_ids,
        PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY e.salary) AS median_salary,
        STDDEV(e.salary) AS salary_stddev,
        VAR_POP(e.salary) AS salary_variance,
        BOOL_AND(e.is_fulltime) AS all_fulltime,
        BOOL_OR(e.is_remote) AS any_remote,
        BIT_AND(e.permissions) AS common_permissions,
        BIT_OR(e.permissions) AS all_permissions,
        GROUPING(e.dept_id) AS dept_grouping
    FROM employees e
    LEFT JOIN departments d ON e.dept_id = d.dept_id
    WHERE e.hire_date >= CURRENT_DATE - INTERVAL '365' DAY
      AND e.salary > 0
    GROUP BY ROLLUP(e.dept_id)
    HAVING COUNT(*) > 5
       AND AVG(e.salary) > 50000
)
SELECT DISTINCT ON (h.dept_id)
    h.dept_id,
    h.display_name,
    h.level,
    h.path,
    h.size_category,
    h.has_employees,
    COALESCE(s.employee_count, 0) AS employees,
    COALESCE(s.avg_salary, 0) AS avg_salary,
    CASE 
        WHEN s.employee_count > 100 THEN 'Large Team'
        WHEN s.employee_count > 50 THEN 'Medium Team'
        WHEN s.employee_count > 10 THEN 'Small Team'
        WHEN s.employee_count > 0 THEN 'Tiny Team'
        ELSE 'No Team'
    END AS team_size,
    ROW_NUMBER() OVER (PARTITION BY h.level ORDER BY h.adjusted_budget DESC) AS budget_rank,
    RANK() OVER (ORDER BY s.avg_salary DESC NULLS LAST) AS salary_rank,
    DENSE_RANK() OVER (ORDER BY s.employee_count DESC NULLS LAST) AS size_rank,
    PERCENT_RANK() OVER (PARTITION BY h.size_category ORDER BY h.adjusted_budget) AS budget_percentile,
    CUME_DIST() OVER (ORDER BY h.creation_year, h.dept_id) AS cumulative_distribution,
    NTILE(4) OVER (ORDER BY s.total_bonus NULLS FIRST) AS bonus_quartile,
    LAG(h.display_name, 1, 'None') OVER (ORDER BY h.path) AS prev_dept,
    LEAD(h.display_name, 1, 'None') OVER (ORDER BY h.path) AS next_dept,
    FIRST_VALUE(h.display_name) OVER (PARTITION BY h.level ORDER BY h.dept_id) AS first_in_level,
    LAST_VALUE(h.display_name) OVER (
        PARTITION BY h.level 
        ORDER BY h.dept_id 
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
    ) AS last_in_level,
    NTH_VALUE(h.display_name, 2) OVER (
        PARTITION BY h.level 
        ORDER BY h.dept_id
        ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
    ) AS second_in_level
FROM organization_hierarchy h
LEFT JOIN employee_stats s ON h.dept_id = s.dept_id
CROSS JOIN LATERAL (
    SELECT 
        COUNT(*) AS project_count,
        SUM(budget) AS total_project_budget
    FROM projects p
    WHERE p.dept_id = h.dept_id
      AND p.status = 'Active'
) p
WHERE h.level <= 5
  AND (h.has_employees = true OR h.size_category = 'Large')
  AND h.adjusted_budget > ANY (SELECT budget FROM departments WHERE parent_dept_id IS NULL)
  AND h.adjusted_budget < ALL (SELECT budget * 10 FROM departments WHERE dept_id = 1)
  AND h.dept_id NOT IN (
      SELECT dept_id 
      FROM blacklisted_departments 
      WHERE blacklist_date > CURRENT_DATE - INTERVAL '30' DAY
  )
ORDER BY 
    h.level ASC,
    s.employee_count DESC NULLS LAST,
    h.adjusted_budget DESC,
    h.display_name ASC
LIMIT 100 
OFFSET 10
FOR UPDATE OF h SKIP LOCKED;

-- ============================================================================
-- TEST 2: Complex INSERT with all value types and expressions
-- ============================================================================

INSERT INTO employee_records (
    employee_id,
    name,
    dept_id,
    salary,
    bonus,
    hire_date,
    is_active,
    permissions,
    metadata,
    vector_embedding,
    tags
)
SELECT 
    nextval('employee_seq'),
    'John ' || CAST(generate_series AS VARCHAR),
    (SELECT dept_id FROM departments ORDER BY RANDOM() LIMIT 1),
    50000 + (random() * 100000)::INTEGER,
    CASE 
        WHEN random() > 0.5 THEN random() * 10000
        ELSE NULL
    END,
    CURRENT_DATE - (random() * 365)::INTEGER * INTERVAL '1' DAY,
    random() > 0.1,
    (random() * 255)::INTEGER & 0xFF,
    jsonb_build_object(
        'skills', ARRAY['SQL', 'Python', 'Java'],
        'level', CASE 
            WHEN random() > 0.7 THEN 'Senior'
            WHEN random() > 0.3 THEN 'Mid'
            ELSE 'Junior'
        END
    ),
    ARRAY[random(), random(), random(), random()]::FLOAT[],
    string_to_array('tag1,tag2,tag3', ',')
FROM generate_series(1, 100)
ON CONFLICT (employee_id) 
DO UPDATE SET
    salary = EXCLUDED.salary * 1.1,
    bonus = COALESCE(EXCLUDED.bonus, employee_records.bonus),
    updated_at = CURRENT_TIMESTAMP
RETURNING *;

-- ============================================================================
-- TEST 3: Complex UPDATE with joins and subqueries
-- ============================================================================

UPDATE departments d
SET 
    budget = d.budget * (1 + p.adjustment_factor),
    headcount = d.headcount + COALESCE(e.new_hires, 0),
    last_review = CURRENT_DATE,
    status = CASE 
        WHEN p.performance_score >= 90 THEN 'Excellent'
        WHEN p.performance_score >= 70 THEN 'Good'
        WHEN p.performance_score >= 50 THEN 'Average'
        ELSE 'Needs Improvement'
    END,
    notes = d.notes || ' - Reviewed on ' || CAST(CURRENT_DATE AS VARCHAR)
FROM (
    SELECT 
        dept_id,
        AVG(score) AS performance_score,
        CASE 
            WHEN AVG(score) >= 90 THEN 0.15
            WHEN AVG(score) >= 70 THEN 0.10
            WHEN AVG(score) >= 50 THEN 0.05
            ELSE 0
        END AS adjustment_factor
    FROM performance_reviews
    WHERE review_year = EXTRACT(YEAR FROM CURRENT_DATE)
    GROUP BY dept_id
) p
LEFT JOIN (
    SELECT 
        dept_id,
        COUNT(*) AS new_hires
    FROM employees
    WHERE hire_date >= CURRENT_DATE - INTERVAL '30' DAY
    GROUP BY dept_id
) e ON d.dept_id = e.dept_id
WHERE d.dept_id = p.dept_id
  AND d.is_active = true
  AND EXISTS (
      SELECT 1 
      FROM employees emp 
      WHERE emp.dept_id = d.dept_id
        AND emp.is_active = true
  )
  AND d.budget < (
      SELECT AVG(budget) * 1.5 
      FROM departments 
      WHERE parent_dept_id = d.parent_dept_id
  );

-- ============================================================================
-- TEST 4: Complex DELETE with multiple conditions
-- ============================================================================

DELETE FROM audit_logs
WHERE log_id IN (
    SELECT log_id
    FROM (
        SELECT 
            log_id,
            ROW_NUMBER() OVER (
                PARTITION BY user_id, action_type 
                ORDER BY created_at DESC
            ) AS rn
        FROM audit_logs
        WHERE created_at < CURRENT_TIMESTAMP - INTERVAL '90' DAY
          AND action_type NOT IN ('LOGIN', 'LOGOUT', 'CRITICAL_ERROR')
    ) ranked_logs
    WHERE rn > 10
)
AND NOT EXISTS (
    SELECT 1 
    FROM audit_log_archives 
    WHERE audit_log_archives.original_log_id = audit_logs.log_id
)
AND user_id NOT IN (
    SELECT user_id 
    FROM users 
    WHERE role IN ('ADMIN', 'AUDITOR')
)
RETURNING log_id, user_id, action_type;

-- ============================================================================
-- TEST 5: All DDL statements with complex definitions
-- ============================================================================

-- CREATE TABLE with all constraint types and column definitions
CREATE TABLE IF NOT EXISTS comprehensive_table (
    -- Primary key with auto-increment
    id BIGSERIAL PRIMARY KEY,
    
    -- Various data types
    text_col VARCHAR(255) NOT NULL,
    number_col INTEGER DEFAULT 0 CHECK (number_col >= 0),
    decimal_col DECIMAL(10, 2) NOT NULL,
    boolean_col BOOLEAN DEFAULT false,
    date_col DATE,
    timestamp_col TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    json_col JSONB,
    array_col INTEGER[],
    uuid_col UUID DEFAULT gen_random_uuid(),
    
    -- Foreign key reference
    dept_id INTEGER REFERENCES departments(dept_id) ON DELETE CASCADE,
    
    -- Unique constraint
    email VARCHAR(255) UNIQUE NOT NULL,
    
    -- Composite unique constraint
    UNIQUE (text_col, number_col),
    
    -- Check constraint
    CONSTRAINT positive_decimal CHECK (decimal_col > 0),
    
    -- Exclusion constraint
    EXCLUDE USING gist (daterange(date_col, date_col + INTERVAL '1' DAY) WITH &&)
);

-- CREATE INDEX with various types
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_comprehensive_text 
ON comprehensive_table (text_col);

CREATE UNIQUE INDEX idx_comprehensive_unique 
ON comprehensive_table (email, dept_id) 
WHERE boolean_col = true;

CREATE INDEX idx_comprehensive_gin 
ON comprehensive_table USING gin (json_col);

CREATE INDEX idx_comprehensive_expression 
ON comprehensive_table ((lower(text_col)), (number_col * 2));

-- CREATE VIEW with complex query
CREATE OR REPLACE VIEW comprehensive_view AS
WITH view_cte AS (
    SELECT 
        c.*,
        d.dept_name,
        ROW_NUMBER() OVER (PARTITION BY c.dept_id ORDER BY c.id) AS row_num
    FROM comprehensive_table c
    LEFT JOIN departments d ON c.dept_id = d.dept_id
)
SELECT * FROM view_cte WHERE row_num <= 10;

-- ALTER TABLE statements
ALTER TABLE comprehensive_table 
ADD COLUMN IF NOT EXISTS new_col VARCHAR(100),
ALTER COLUMN text_col SET DEFAULT 'default_value',
ALTER COLUMN number_col TYPE BIGINT,
DROP CONSTRAINT IF EXISTS positive_decimal,
ADD CONSTRAINT new_check CHECK (decimal_col < 1000000),
RENAME COLUMN boolean_col TO is_active;

-- DROP statements
DROP TABLE IF EXISTS old_table CASCADE;
DROP INDEX IF EXISTS old_index;
DROP VIEW IF EXISTS old_view;

-- TRUNCATE statement
TRUNCATE TABLE audit_logs RESTART IDENTITY CASCADE;

-- ============================================================================
-- TEST 6: Complex MERGE statement (if supported)
-- ============================================================================

MERGE INTO target_table t
USING (
    SELECT 
        s.id,
        s.value,
        s.category,
        ROW_NUMBER() OVER (PARTITION BY s.category ORDER BY s.value DESC) AS rank
    FROM source_table s
    WHERE s.is_active = true
) s ON t.id = s.id
WHEN MATCHED AND s.rank = 1 THEN
    UPDATE SET 
        value = s.value * 1.1,
        last_updated = CURRENT_TIMESTAMP
WHEN MATCHED AND s.rank > 1 THEN
    DELETE
WHEN NOT MATCHED THEN
    INSERT (id, value, category, created_at)
    VALUES (s.id, s.value, s.category, CURRENT_TIMESTAMP);

-- ============================================================================
-- TEST 7: Set operations with complex queries
-- ============================================================================

(
    SELECT 
        'Employee' AS type,
        e.employee_id AS id,
        e.name,
        e.salary AS amount,
        d.dept_name
    FROM employees e
    INNER JOIN departments d ON e.dept_id = d.dept_id
    WHERE e.is_active = true
      AND e.salary > 50000
)
UNION ALL
(
    SELECT 
        'Contractor' AS type,
        c.contractor_id AS id,
        c.name,
        c.hourly_rate * 2080 AS amount,
        'External' AS dept_name
    FROM contractors c
    WHERE c.end_date > CURRENT_DATE
)
INTERSECT
(
    SELECT 
        'Person' AS type,
        p.person_id AS id,
        p.full_name AS name,
        p.total_compensation AS amount,
        p.organization AS dept_name
    FROM people p
    WHERE p.year = EXTRACT(YEAR FROM CURRENT_DATE)
)
EXCEPT
(
    SELECT 
        'Terminated' AS type,
        t.person_id AS id,
        t.name,
        0 AS amount,
        'N/A' AS dept_name
    FROM terminations t
    WHERE t.termination_date >= CURRENT_DATE - INTERVAL '30' DAY
)
ORDER BY type, amount DESC
LIMIT 50;

-- ============================================================================
-- TEST 8: Transaction control (if supported)
-- ============================================================================

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;

SAVEPOINT before_updates;

-- Complex updates here
UPDATE departments SET budget = budget * 1.05 WHERE is_active = true;

ROLLBACK TO SAVEPOINT before_updates;

COMMIT;

-- ============================================================================
-- TEST 9: Window functions with all frame specifications
-- ============================================================================

SELECT 
    employee_id,
    name,
    dept_id,
    salary,
    
    -- Various window functions
    SUM(salary) OVER w1 AS dept_total,
    AVG(salary) OVER w2 AS company_avg,
    COUNT(*) OVER w3 AS running_count,
    
    -- Frame specifications
    SUM(salary) OVER (
        PARTITION BY dept_id 
        ORDER BY hire_date 
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    ) AS cumulative_dept_salary,
    
    AVG(salary) OVER (
        ORDER BY salary 
        RANGE BETWEEN 1000 PRECEDING AND 1000 FOLLOWING
    ) AS salary_range_avg,
    
    LAST_VALUE(name) OVER (
        PARTITION BY dept_id 
        ORDER BY salary 
        ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING
    ) AS highest_paid_in_dept
    
FROM employees
WINDOW 
    w1 AS (PARTITION BY dept_id),
    w2 AS (ORDER BY hire_date ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING),
    w3 AS (ORDER BY employee_id)
ORDER BY dept_id, salary DESC;

-- ============================================================================
-- TEST 10: Advanced joins and lateral subqueries
-- ============================================================================

SELECT 
    d.dept_name,
    e.emp_name,
    e.salary,
    p.project_count,
    p.total_budget,
    s.subordinate_count
FROM departments d
INNER JOIN LATERAL (
    SELECT 
        employee_id,
        name AS emp_name,
        salary
    FROM employees
    WHERE dept_id = d.dept_id
    ORDER BY salary DESC
    LIMIT 5
) e ON true
LEFT JOIN LATERAL (
    SELECT 
        COUNT(*) AS project_count,
        SUM(budget) AS total_budget
    FROM projects
    WHERE dept_id = d.dept_id
      AND status = 'Active'
) p ON true
CROSS JOIN LATERAL (
    SELECT COUNT(*) AS subordinate_count
    FROM employees
    WHERE manager_id = e.employee_id
) s
WHERE d.is_active = true
ORDER BY d.dept_name, e.salary DESC;

-- ============================================================================
-- END OF EBNF COMPLETE TEST SUITE
-- ============================================================================