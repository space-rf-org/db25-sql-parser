-- ============================================================================
-- DB25 SQL Parser - Ultra-Complex SQL:2016 Test Suite
-- ============================================================================
-- Modern SQL features from SQL:2011, SQL:2016, and vendor extensions
-- Testing parser limits with real-world complex analytical queries

-- ============================================================================
-- RECURSIVE CTEs WITH COMPLEX OPERATIONS
-- ============================================================================

-- Calendar generation with INTERVAL arithmetic
WITH RECURSIVE cal(d) AS (
  SELECT DATE '2025-07-01'
  UNION ALL
  SELECT d + INTERVAL '1' DAY
  FROM cal
  WHERE d < DATE '2025-07-31'
)
SELECT * FROM cal

-- Hierarchical data with path materialization
WITH RECURSIVE org_tree AS (
  SELECT employee_id, name, manager_id, 
         CAST(name AS VARCHAR(1000)) AS path,
         1 AS level
  FROM employees 
  WHERE manager_id IS NULL
  UNION ALL
  SELECT e.employee_id, e.name, e.manager_id,
         t.path || ' > ' || e.name AS path,
         t.level + 1
  FROM employees e
  JOIN org_tree t ON e.manager_id = t.employee_id
  WHERE t.level < 10
)
SELECT * FROM org_tree

-- Graph traversal with cycle detection
WITH RECURSIVE graph_walk(node, path, is_cycle) AS (
  SELECT node_id, ARRAY[node_id], FALSE
  FROM nodes WHERE node_id = 1
  UNION ALL
  SELECT e.to_node, 
         w.path || e.to_node,
         e.to_node = ANY(w.path)
  FROM graph_walk w
  JOIN edges e ON e.from_node = w.node
  WHERE NOT w.is_cycle
)
SELECT * FROM graph_walk

-- ============================================================================
-- TEMPORAL QUERIES (SQL:2011)
-- ============================================================================

-- System-versioned temporal query
SELECT c.customer_id, c.segment, c.country
FROM customers FOR SYSTEM_TIME AS OF TIMESTAMP '2025-07-31 23:59:59' AS c

-- Application-time temporal query
SELECT * 
FROM prices FOR PORTION OF BUSINESS_TIME 
  FROM DATE '2025-01-01' TO DATE '2025-12-31'
WHERE product_id = 100

-- Bi-temporal query
SELECT * 
FROM contracts 
FOR SYSTEM_TIME AS OF TIMESTAMP '2025-06-30 23:59:59'
FOR PORTION OF valid_time FROM DATE '2025-01-01' TO DATE '2025-12-31'

-- ============================================================================
-- JSON OPERATIONS (SQL:2016)
-- ============================================================================

-- JSON_TABLE with LATERAL join
SELECT o.order_id, jt.*
FROM orders o
CROSS JOIN LATERAL JSON_TABLE(
  o.line_items,
  '$.items[*]' 
  COLUMNS (
    line_no     FOR ORDINALITY,
    sku         VARCHAR(128)  PATH '$.sku',
    qty         INTEGER       PATH '$.quantity',
    unit_price  DECIMAL(18,4) PATH '$.price',
    discount    DECIMAL(5,2)  PATH '$.discount' DEFAULT 0,
    tags        JSON          PATH '$.tags',
    NESTED PATH '$.attributes[*]' COLUMNS (
      attr_name  VARCHAR(50)  PATH '$.name',
      attr_value VARCHAR(255) PATH '$.value'
    )
  )
) AS jt

-- JSON construction
SELECT JSON_OBJECT(
  'customer_id' VALUE c.id,
  'name' VALUE c.name,
  'orders' VALUE JSON_ARRAYAGG(
    JSON_OBJECT(
      'order_id' VALUE o.id,
      'total' VALUE o.total,
      'items' VALUE o.item_count
    ) ORDER BY o.created_at DESC
  )
) AS customer_json
FROM customers c
LEFT JOIN orders o ON c.id = o.customer_id
GROUP BY c.id, c.name

-- JSON path expressions
SELECT 
  data -> '$.user.profile.name' AS user_name,
  data ->> '$.settings.notifications' AS notifications,
  data @> '{"active": true}' AS is_active,
  data @? '$.tags[*] ? (@ == "premium")' AS has_premium
FROM user_data

-- ============================================================================
-- MATCH_RECOGNIZE (SQL:2016 - Row Pattern Recognition)
-- ============================================================================

-- Detect suspicious payment patterns
SELECT * FROM payments
MATCH_RECOGNIZE (
  PARTITION BY customer_id
  ORDER BY payment_date
  MEASURES
    FIRST(small.amount) AS first_small_amt,
    COUNT(small.*) AS small_count,
    LAST(large.amount) AS final_large_amt,
    MATCH_NUMBER() AS match_num,
    CLASSIFIER() AS var_match
  ALL ROWS PER MATCH
  AFTER MATCH SKIP TO NEXT ROW
  PATTERN (small{3,} large)
  DEFINE
    small AS small.amount < 10,
    large AS large.amount >= 500 AND 
            large.amount > AVG(small.amount) * 50
) AS suspicious

-- Stock price pattern detection
SELECT * FROM stock_prices
MATCH_RECOGNIZE (
  PARTITION BY symbol
  ORDER BY trade_date
  MEASURES
    FIRST(a.price) AS start_price,
    LAST(z.price) AS end_price,
    COUNT(*) AS days_in_pattern
  ONE ROW PER MATCH
  PATTERN (a b+ c+ d? e* z)
  DEFINE
    a AS TRUE,
    b AS b.price > PREV(b.price),
    c AS c.price < PREV(c.price),
    d AS d.price BETWEEN PREV(d.price) * 0.98 AND PREV(d.price) * 1.02,
    e AS e.volume > AVG(e.volume) OVER (ROWS 5 PRECEDING),
    z AS z.price > FIRST(a.price) * 1.1
)

-- ============================================================================
-- ADVANCED WINDOW FUNCTIONS
-- ============================================================================

-- Multiple window definitions with frame clauses
SELECT 
  customer_id,
  order_date,
  amount,
  -- Running total
  SUM(amount) OVER w1 AS running_total,
  -- Moving average
  AVG(amount) OVER w2 AS moving_avg_7d,
  -- Rank within month
  RANK() OVER w3 AS monthly_rank,
  -- Percentile
  PERCENT_RANK() OVER w4 AS percentile,
  -- Lead/lag with defaults
  LAG(amount, 1, 0) OVER w5 AS prev_amount,
  LEAD(amount, 2, amount) OVER w5 AS next_next_amount,
  -- First/last value with ignore nulls
  FIRST_VALUE(amount) IGNORE NULLS OVER w6 AS first_non_null,
  LAST_VALUE(amount) RESPECT NULLS OVER w6 AS last_with_null,
  -- Nth value
  NTH_VALUE(amount, 3) FROM FIRST OVER w7 AS third_amount
FROM orders
WINDOW 
  w1 AS (PARTITION BY customer_id ORDER BY order_date 
         ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW),
  w2 AS (PARTITION BY customer_id ORDER BY order_date 
         ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING),
  w3 AS (PARTITION BY customer_id, DATE_TRUNC('month', order_date) 
         ORDER BY amount DESC),
  w4 AS (ORDER BY amount),
  w5 AS (PARTITION BY customer_id ORDER BY order_date),
  w6 AS (PARTITION BY customer_id ORDER BY order_date 
         RANGE BETWEEN INTERVAL '30' DAY PRECEDING AND CURRENT ROW),
  w7 AS (PARTITION BY customer_id ORDER BY order_date 
         ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)

-- ============================================================================
-- GROUPING SETS, CUBE, ROLLUP with FILTER
-- ============================================================================

SELECT 
  year,
  quarter,
  month,
  product_category,
  region,
  -- Aggregates with FILTER clause
  SUM(revenue) AS total_revenue,
  SUM(revenue) FILTER (WHERE is_online = TRUE) AS online_revenue,
  SUM(revenue) FILTER (WHERE is_return = FALSE) AS net_revenue,
  COUNT(*) FILTER (WHERE customer_type = 'NEW') AS new_customers,
  -- Grouping indicators
  GROUPING(year) AS year_grouping,
  GROUPING(quarter) AS quarter_grouping,
  GROUPING(month) AS month_grouping,
  GROUPING(product_category) AS category_grouping,
  GROUPING(region) AS region_grouping,
  -- Grouping ID
  GROUPING_ID(year, quarter, month, product_category, region) AS grouping_id,
  -- Advanced aggregates
  LISTAGG(DISTINCT store_id, ',') WITHIN GROUP (ORDER BY store_id) 
    ON OVERFLOW TRUNCATE '...' WITH COUNT AS stores,
  PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY revenue) AS median_revenue,
  MODE() WITHIN GROUP (ORDER BY payment_method) AS common_payment
FROM sales
GROUP BY GROUPING SETS (
  (year, quarter, month, product_category, region),  -- Full detail
  (year, quarter, product_category, region),          -- Quarterly summary
  (year, product_category, region),                   -- Yearly by category
  (year, region),                                     -- Yearly by region
  (product_category, region),                         -- Category/region matrix
  (year),                                             -- Yearly total
  ()                                                  -- Grand total
)
HAVING SUM(revenue) > 10000 OR GROUPING_ID(year, quarter, month, product_category, region) = 0

-- CUBE operation
SELECT 
  product_line,
  store_type,
  customer_segment,
  SUM(sales_amount) AS total_sales,
  COUNT(DISTINCT customer_id) AS unique_customers
FROM transactions
GROUP BY CUBE(product_line, store_type, customer_segment)
ORDER BY GROUPING(product_line), GROUPING(store_type), GROUPING(customer_segment)

-- ROLLUP operation
SELECT 
  year,
  month,
  day,
  SUM(amount) AS daily_total,
  GROUPING(year, month, day) AS level
FROM daily_sales
GROUP BY ROLLUP(year, month, day)

-- ============================================================================
-- LATERAL JOINS AND CROSS APPLY
-- ============================================================================

-- LATERAL with correlated subquery
SELECT c.customer_id, c.name, t.recent_orders
FROM customers c
CROSS JOIN LATERAL (
  SELECT JSON_AGG(
    JSON_BUILD_OBJECT(
      'order_id', o.order_id,
      'date', o.order_date,
      'total', o.total
    ) ORDER BY o.order_date DESC
  ) AS recent_orders
  FROM orders o
  WHERE o.customer_id = c.customer_id
    AND o.order_date >= CURRENT_DATE - INTERVAL '30' DAY
) t

-- Multiple LATERAL joins
SELECT p.product_id, p.name, inv.stock, price.current_price, reviews.avg_rating
FROM products p
LEFT JOIN LATERAL (
  SELECT SUM(quantity) AS stock
  FROM inventory i
  WHERE i.product_id = p.product_id
    AND i.warehouse_id IN (SELECT warehouse_id FROM active_warehouses)
) inv ON TRUE
LEFT JOIN LATERAL (
  SELECT price AS current_price
  FROM price_history ph
  WHERE ph.product_id = p.product_id
    AND ph.valid_from <= CURRENT_DATE
    AND (ph.valid_to IS NULL OR ph.valid_to > CURRENT_DATE)
  ORDER BY ph.valid_from DESC
  FETCH FIRST 1 ROW ONLY
) price ON TRUE
LEFT JOIN LATERAL (
  SELECT AVG(rating) AS avg_rating
  FROM reviews r
  WHERE r.product_id = p.product_id
    AND r.verified = TRUE
    AND r.created_at >= CURRENT_DATE - INTERVAL '90' DAY
) reviews ON TRUE

-- ============================================================================
-- MERGE STATEMENT (SQL:2003)
-- ============================================================================

MERGE INTO target_table t
USING (
  WITH source_data AS (
    SELECT s.id, s.value, s.updated_at,
           ROW_NUMBER() OVER (PARTITION BY s.id ORDER BY s.updated_at DESC) AS rn
    FROM staging_table s
    WHERE s.is_valid = TRUE
  )
  SELECT id, value, updated_at
  FROM source_data
  WHERE rn = 1
) s ON t.id = s.id
WHEN MATCHED AND t.updated_at < s.updated_at THEN
  UPDATE SET 
    value = s.value,
    updated_at = s.updated_at,
    modified_by = CURRENT_USER
WHEN NOT MATCHED THEN
  INSERT (id, value, created_at, updated_at, created_by)
  VALUES (s.id, s.value, CURRENT_TIMESTAMP, s.updated_at, CURRENT_USER)
WHEN NOT MATCHED BY SOURCE AND t.is_active = TRUE THEN
  DELETE

-- ============================================================================
-- CONNECT BY (Oracle-style hierarchical query)
-- ============================================================================

SELECT LEVEL, 
       employee_id,
       manager_id,
       name,
       SYS_CONNECT_BY_PATH(name, ' > ') AS path,
       CONNECT_BY_ISLEAF AS is_leaf,
       CONNECT_BY_ISCYCLE AS is_cycle
FROM employees
START WITH manager_id IS NULL
CONNECT BY NOCYCLE PRIOR employee_id = manager_id
ORDER SIBLINGS BY name

-- ============================================================================
-- PIVOT and UNPIVOT
-- ============================================================================

-- PIVOT operation
SELECT * FROM (
  SELECT customer_id, product_category, revenue
  FROM sales
  WHERE year = 2025
)
PIVOT (
  SUM(revenue) AS total,
  COUNT(*) AS orders
  FOR product_category IN (
    'Electronics' AS elec,
    'Clothing' AS cloth,
    'Food' AS food,
    'Books' AS books
  )
)

-- UNPIVOT operation
SELECT customer_id, category, revenue
FROM quarterly_summary
UNPIVOT (
  revenue FOR category IN (
    q1_electronics AS 'Q1_Electronics',
    q1_clothing AS 'Q1_Clothing',
    q2_electronics AS 'Q2_Electronics',
    q2_clothing AS 'Q2_Clothing'
  )
)

-- ============================================================================
-- FULL OUTER JOIN with complex conditions
-- ============================================================================

SELECT 
  COALESCE(o.order_date, r.return_date) AS transaction_date,
  COALESCE(o.customer_id, r.customer_id) AS customer_id,
  o.order_id,
  o.order_amount,
  r.return_id,
  r.return_amount,
  CASE 
    WHEN o.order_id IS NOT NULL AND r.return_id IS NOT NULL THEN 'BOTH'
    WHEN o.order_id IS NOT NULL THEN 'ORDER_ONLY'
    WHEN r.return_id IS NOT NULL THEN 'RETURN_ONLY'
  END AS transaction_type
FROM orders o
FULL OUTER JOIN returns r
  ON o.order_id = r.order_id
  AND o.customer_id = r.customer_id
  AND ABS(EXTRACT(EPOCH FROM (o.order_date - r.return_date))) < 86400 * 30
WHERE (o.order_date BETWEEN '2025-01-01' AND '2025-12-31'
   OR r.return_date BETWEEN '2025-01-01' AND '2025-12-31')

-- ============================================================================
-- VALUES clause as table constructor
-- ============================================================================

WITH constants (name, value) AS (
  VALUES 
    ('pi', 3.14159),
    ('e', 2.71828),
    ('golden_ratio', 1.61803),
    ('sqrt_2', 1.41421)
)
SELECT * FROM constants

-- Complex VALUES with subqueries
INSERT INTO summary_table
SELECT * FROM (
  VALUES 
    (1, (SELECT MAX(id) FROM users), CURRENT_DATE),
    (2, (SELECT COUNT(*) FROM orders), CURRENT_DATE),
    (3, (SELECT SUM(amount) FROM payments), CURRENT_DATE)
) AS v(metric_id, value, date)

-- ============================================================================
-- TABLESAMPLE
-- ============================================================================

-- Bernoulli sampling
SELECT * FROM large_table TABLESAMPLE BERNOULLI(10) REPEATABLE(42)

-- System sampling
SELECT * FROM huge_table TABLESAMPLE SYSTEM(1) WHERE created_at > '2025-01-01'

-- ============================================================================
-- WITHIN GROUP ordered-set aggregates
-- ============================================================================

SELECT 
  department_id,
  PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY salary) AS median_salary,
  PERCENTILE_DISC(0.75) WITHIN GROUP (ORDER BY salary) AS q75_salary,
  MODE() WITHIN GROUP (ORDER BY job_title) AS common_job,
  STRING_AGG(employee_name, ', ' ORDER BY hire_date) AS employees_by_seniority,
  ARRAY_AGG(skill ORDER BY skill_level DESC) FILTER (WHERE skill_level >= 3) AS top_skills
FROM employees
GROUP BY department_id

-- ============================================================================
-- CHECK constraints in CREATE TABLE
-- ============================================================================

CREATE TABLE complex_table (
  id BIGINT GENERATED ALWAYS AS IDENTITY,
  code VARCHAR(20) NOT NULL UNIQUE,
  status ENUM('NEW', 'ACTIVE', 'SUSPENDED', 'CLOSED'),
  amount DECIMAL(19,4) CHECK (amount >= 0),
  percentage DECIMAL(5,2) CHECK (percentage BETWEEN 0 AND 100),
  start_date DATE NOT NULL,
  end_date DATE,
  metadata JSONB,
  tags TEXT[],
  vector FLOAT[768],
  CONSTRAINT valid_dates CHECK (end_date IS NULL OR end_date >= start_date),
  CONSTRAINT valid_status_amount CHECK (
    (status = 'NEW' AND amount = 0) OR
    (status IN ('ACTIVE', 'SUSPENDED') AND amount > 0) OR
    (status = 'CLOSED')
  ),
  PRIMARY KEY (id),
  FOREIGN KEY (parent_id) REFERENCES complex_table(id) ON DELETE CASCADE,
  EXCLUDE USING gist (code WITH =, daterange(start_date, end_date) WITH &&)
)

-- ============================================================================
-- GENERATED columns
-- ============================================================================

CREATE TABLE products_v2 (
  id INT PRIMARY KEY,
  price DECIMAL(10,2),
  tax_rate DECIMAL(4,2),
  price_with_tax DECIMAL(10,2) GENERATED ALWAYS AS (price * (1 + tax_rate)) STORED,
  search_vector tsvector GENERATED ALWAYS AS (
    to_tsvector('english', name || ' ' || COALESCE(description, ''))
  ) STORED
)

-- ============================================================================
-- Ultra-complex combined query (from the example)
-- ============================================================================

WITH RECURSIVE cal(d) AS (
  SELECT DATE '2025-07-01'
  UNION ALL
  SELECT d + INTERVAL '1' DAY
  FROM cal
  WHERE d < DATE '2025-07-31'
),
fx AS (
  SELECT day, currency, usd_rate
  FROM exchange_rates
  WHERE day BETWEEN DATE '2025-07-01' AND DATE '2025-07-31'
),
customers_asof AS (
  SELECT c.customer_id, c.segment, c.country, c.attributes_json
  FROM customers FOR SYSTEM_TIME AS OF TIMESTAMP '2025-07-31 23:59:59' AS c
),
order_lines AS (
  SELECT o.order_id, o.customer_id,
         CAST(o.created_at AS DATE) AS order_date,
         o.currency, jt.line_no, jt.sku, jt.qty, jt.unit_price, jt.tags_json
  FROM orders AS o
  JOIN LATERAL JSON_TABLE(
    o.lines, '$.items[*]'
    COLUMNS (
      line_no     FOR ORDINALITY,
      sku         VARCHAR(128)  PATH '$.sku',
      qty         INTEGER       PATH '$.qty',
      unit_price  DECIMAL(18,4) PATH '$.price',
      tags_json   JSON          PATH '$.tags'
    )
  ) AS jt ON TRUE
  WHERE CAST(o.created_at AS DATE) BETWEEN DATE '2025-07-01' AND DATE '2025-07-31'
),
suspicious_payments AS (
  SELECT *
  FROM payments
  MATCH_RECOGNIZE (
    PARTITION BY order_id
    ORDER BY paid_at
    MEASURES
      FIRST(small.amount) AS first_small_amt,
      COUNT(small.amount) AS small_count,
      LAST(large.amount) AS final_large_amt,
      LAST(large.paid_at) AS large_time
    ONE ROW PER MATCH
    AFTER MATCH SKIP PAST LAST ROW
    PATTERN (small+ large)
    DEFINE
      small AS small.amount < 10,
      large AS large.amount >= 500
  ) AS mr
),
priced_lines AS (
  SELECT ol.*, fx.usd_rate,
         (ol.qty * ol.unit_price) * fx.usd_rate AS line_total_usd
  FROM order_lines AS ol
  LEFT JOIN fx ON fx.day = ol.order_date AND fx.currency = ol.currency
),
enriched AS (
  SELECT pl.*, ca.segment, ca.country,
         CASE WHEN sp.order_id IS NOT NULL THEN 1 ELSE 0 END AS is_suspicious
  FROM priced_lines AS pl
  LEFT JOIN customers_asof AS ca ON ca.customer_id = pl.customer_id
  LEFT JOIN suspicious_payments AS sp ON sp.order_id = pl.order_id
)
SELECT order_date, segment, country,
       SUM(line_total_usd) AS gross_usd,
       SUM(line_total_usd) FILTER (WHERE is_suspicious=1) AS suspicious_usd,
       COUNT(DISTINCT order_id) AS orders,
       COUNT(*) AS line_items,
       LISTAGG(DISTINCT sku, ',') WITHIN GROUP (ORDER BY sku)
         ON OVERFLOW TRUNCATE '…' WITH COUNT AS skus_sampled,
       RANK() OVER day_win AS revenue_rank_in_day
FROM enriched
GROUP BY GROUPING SETS (
  (order_date, segment, country),
  (order_date, segment),
  (order_date),
  ()
)
WINDOW day_win AS (
  PARTITION BY order_date
  ORDER BY SUM(line_total_usd) DESC
  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
)
ORDER BY order_date NULLS LAST, segment NULLS LAST, country NULLS LAST