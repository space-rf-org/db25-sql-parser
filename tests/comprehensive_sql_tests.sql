-- ============================================================================
-- DB25 SQL Parser - Comprehensive Test Suite
-- ============================================================================
-- This file contains exhaustive SQL test cases organized by category and complexity.
-- Each section tests specific parser capabilities.
-- Format: Each SQL statement should be on its own line (no semicolons needed)

-- ============================================================================
-- SELECT FUNDAMENTALS (Basic SELECT features)
-- ============================================================================

-- Basic SELECT
SELECT * FROM users
SELECT id FROM users
SELECT id, name FROM users
SELECT id, name, email FROM users
SELECT users.id FROM users
SELECT u.id FROM users u
SELECT users.* FROM users

-- SELECT with DISTINCT
SELECT DISTINCT name FROM users
SELECT DISTINCT id, name FROM users
SELECT DISTINCT users.name FROM users
SELECT DISTINCT u.name FROM users u

-- SELECT with ALL
SELECT ALL name FROM users
SELECT ALL * FROM users

-- Column aliases
SELECT id AS user_id FROM users
SELECT id user_id FROM users
SELECT name AS "User Name" FROM users
SELECT id AS user_id, name AS user_name FROM users
SELECT u.id AS user_id FROM users u

-- Keywords as column names
SELECT date FROM events
SELECT time FROM logs
SELECT timestamp FROM records
SELECT level FROM categories
SELECT min FROM stats
SELECT max FROM stats
SELECT count FROM metrics
SELECT sum FROM totals
SELECT avg FROM averages
SELECT table.date FROM table
SELECT t.time FROM logs t

-- Literals
SELECT 1
SELECT 123
SELECT -456
SELECT 3.14
SELECT -2.718
SELECT 'string'
SELECT "double quoted"
SELECT TRUE
SELECT FALSE
SELECT NULL
SELECT 1, 2, 3
SELECT 'a', 'b', 'c'

-- Basic expressions
SELECT 1 + 2
SELECT 10 - 5
SELECT 3 * 4
SELECT 10 / 2
SELECT 10 % 3
SELECT -id FROM users
SELECT +id FROM users
SELECT (1 + 2) * 3
SELECT ((1 + 2) * (3 + 4))

-- ============================================================================
-- SELECT WITH CLAUSES (FROM, WHERE, GROUP BY, etc.)
-- ============================================================================

-- FROM clause
SELECT * FROM users
SELECT * FROM schema.users
SELECT * FROM database.schema.users
SELECT * FROM users, products
SELECT * FROM users u
SELECT * FROM users AS u
SELECT * FROM users u, products p
SELECT * FROM (SELECT * FROM users) AS subquery

-- WHERE clause - Basic conditions
SELECT * FROM users WHERE id = 1
SELECT * FROM users WHERE id > 10
SELECT * FROM users WHERE id < 100
SELECT * FROM users WHERE id >= 10
SELECT * FROM users WHERE id <= 100
SELECT * FROM users WHERE id != 5
SELECT * FROM users WHERE id <> 5
SELECT * FROM users WHERE name = 'John'
SELECT * FROM users WHERE active = TRUE
SELECT * FROM users WHERE deleted = FALSE
SELECT * FROM users WHERE email IS NULL
SELECT * FROM users WHERE email IS NOT NULL

-- WHERE clause - Logical operators
SELECT * FROM users WHERE id = 1 AND active = TRUE
SELECT * FROM users WHERE id = 1 OR id = 2
SELECT * FROM users WHERE NOT active
SELECT * FROM users WHERE NOT (id = 1)
SELECT * FROM users WHERE id = 1 AND name = 'John' AND active = TRUE
SELECT * FROM users WHERE id = 1 OR name = 'John' OR email = 'test@test.com'
SELECT * FROM users WHERE (id = 1 OR id = 2) AND active = TRUE
SELECT * FROM users WHERE id = 1 AND (name = 'John' OR name = 'Jane')

-- WHERE clause - BETWEEN
SELECT * FROM users WHERE id BETWEEN 1 AND 10
SELECT * FROM users WHERE id NOT BETWEEN 1 AND 10
SELECT * FROM users WHERE created_at BETWEEN '2024-01-01' AND '2024-12-31'

-- WHERE clause - IN
SELECT * FROM users WHERE id IN (1, 2, 3)
SELECT * FROM users WHERE id NOT IN (1, 2, 3)
SELECT * FROM users WHERE name IN ('John', 'Jane', 'Bob')
SELECT * FROM users WHERE id IN (SELECT user_id FROM orders)

-- WHERE clause - LIKE
SELECT * FROM users WHERE name LIKE 'John%'
SELECT * FROM users WHERE name LIKE '%son'
SELECT * FROM users WHERE name LIKE '%oh%'
SELECT * FROM users WHERE name LIKE 'J_hn'
SELECT * FROM users WHERE name NOT LIKE 'John%'

-- WHERE clause - EXISTS
SELECT * FROM users WHERE EXISTS (SELECT 1 FROM orders WHERE orders.user_id = users.id)
SELECT * FROM users WHERE NOT EXISTS (SELECT 1 FROM orders WHERE orders.user_id = users.id)

-- GROUP BY clause
SELECT dept FROM employees GROUP BY dept
SELECT dept, COUNT(*) FROM employees GROUP BY dept
SELECT dept, team FROM employees GROUP BY dept, team
SELECT dept, team, COUNT(*) FROM employees GROUP BY dept, team
SELECT dept, SUM(salary) FROM employees GROUP BY dept
SELECT dept, AVG(salary) FROM employees GROUP BY dept
SELECT dept, MIN(salary), MAX(salary) FROM employees GROUP BY dept
SELECT YEAR(created_at), COUNT(*) FROM orders GROUP BY YEAR(created_at)
SELECT dept, COUNT(*) AS emp_count FROM employees GROUP BY dept

-- HAVING clause
SELECT dept, COUNT(*) FROM employees GROUP BY dept HAVING COUNT(*) > 10
SELECT dept, AVG(salary) FROM employees GROUP BY dept HAVING AVG(salary) > 50000
SELECT dept, SUM(salary) FROM employees GROUP BY dept HAVING SUM(salary) > 1000000
SELECT dept, COUNT(*) FROM employees GROUP BY dept HAVING COUNT(*) > 10 AND COUNT(*) < 100
SELECT dept FROM employees GROUP BY dept HAVING MAX(salary) - MIN(salary) > 10000
SELECT * FROM employees HAVING COUNT(*) > 10

-- ORDER BY clause
SELECT * FROM users ORDER BY id
SELECT * FROM users ORDER BY id ASC
SELECT * FROM users ORDER BY id DESC
SELECT * FROM users ORDER BY name, id
SELECT * FROM users ORDER BY name ASC, id DESC
SELECT * FROM users ORDER BY 1
SELECT * FROM users ORDER BY 1, 2
SELECT id, name FROM users ORDER BY 2
SELECT * FROM users ORDER BY created_at DESC, id ASC

-- LIMIT clause
SELECT * FROM users LIMIT 10
SELECT * FROM users LIMIT 100
SELECT * FROM users ORDER BY id LIMIT 10
SELECT * FROM users WHERE active = TRUE LIMIT 5
SELECT * FROM users ORDER BY created_at DESC LIMIT 1

-- OFFSET clause (if supported)
SELECT * FROM users LIMIT 10 OFFSET 20
SELECT * FROM users ORDER BY id LIMIT 10 OFFSET 100

-- ============================================================================
-- JOINS (All types of JOIN operations)
-- ============================================================================

-- INNER JOIN
SELECT * FROM users JOIN orders ON users.id = orders.user_id
SELECT * FROM users INNER JOIN orders ON users.id = orders.user_id
SELECT u.*, o.* FROM users u JOIN orders o ON u.id = o.user_id
SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.user_id
SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id

-- LEFT JOIN
SELECT * FROM users LEFT JOIN orders ON users.id = orders.user_id
SELECT * FROM users LEFT OUTER JOIN orders ON users.id = orders.user_id
SELECT u.*, o.* FROM users u LEFT JOIN orders o ON u.id = o.user_id
SELECT u.name, COUNT(o.id) FROM users u LEFT JOIN orders o ON u.id = o.user_id GROUP BY u.name

-- RIGHT JOIN
SELECT * FROM orders RIGHT JOIN users ON orders.user_id = users.id
SELECT * FROM orders RIGHT OUTER JOIN users ON orders.user_id = users.id

-- FULL JOIN
SELECT * FROM users FULL JOIN orders ON users.id = orders.user_id
SELECT * FROM users FULL OUTER JOIN orders ON users.id = orders.user_id

-- CROSS JOIN
SELECT * FROM users CROSS JOIN products
SELECT * FROM users, products

-- Multiple JOINs
SELECT * FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id
SELECT * FROM users u LEFT JOIN orders o ON u.id = o.user_id LEFT JOIN products p ON o.product_id = p.id
SELECT * FROM t1 JOIN t2 ON t1.a = t2.a JOIN t3 ON t2.b = t3.b JOIN t4 ON t3.c = t4.c

-- Complex JOIN conditions
SELECT * FROM users u JOIN orders o ON u.id = o.user_id AND o.status = 'completed'
SELECT * FROM users u JOIN orders o ON u.id = o.user_id AND u.active = TRUE AND o.total > 100
SELECT * FROM users u JOIN orders o ON u.id = o.user_id OR u.temp_id = o.temp_user_id

-- Self JOIN
SELECT e1.name, e2.name AS manager FROM employees e1 JOIN employees e2 ON e1.manager_id = e2.id
SELECT e1.*, e2.* FROM employees e1 LEFT JOIN employees e2 ON e1.manager_id = e2.id

-- ============================================================================
-- AGGREGATE FUNCTIONS
-- ============================================================================

-- COUNT
SELECT COUNT(*) FROM users
SELECT COUNT(id) FROM users
SELECT COUNT(DISTINCT name) FROM users
SELECT COUNT(1) FROM users
SELECT dept, COUNT(*) FROM employees GROUP BY dept

-- SUM
SELECT SUM(amount) FROM orders
SELECT SUM(quantity * price) FROM order_items
SELECT user_id, SUM(amount) FROM orders GROUP BY user_id
SELECT SUM(DISTINCT amount) FROM orders

-- AVG
SELECT AVG(salary) FROM employees
SELECT AVG(age) FROM users
SELECT dept, AVG(salary) FROM employees GROUP BY dept
SELECT AVG(DISTINCT salary) FROM employees

-- MIN/MAX
SELECT MIN(salary) FROM employees
SELECT MAX(salary) FROM employees
SELECT MIN(created_at) FROM users
SELECT MAX(created_at) FROM users
SELECT dept, MIN(salary), MAX(salary) FROM employees GROUP BY dept
SELECT MIN(date) FROM events
SELECT MAX(time) FROM logs

-- Multiple aggregates
SELECT COUNT(*), SUM(amount), AVG(amount), MIN(amount), MAX(amount) FROM orders
SELECT dept, COUNT(*) AS count, AVG(salary) AS avg_sal FROM employees GROUP BY dept

-- ============================================================================
-- SUBQUERIES
-- ============================================================================

-- Scalar subqueries
SELECT * FROM users WHERE id = (SELECT MAX(id) FROM users)
SELECT * FROM users WHERE salary > (SELECT AVG(salary) FROM users)
SELECT name, (SELECT COUNT(*) FROM orders WHERE orders.user_id = users.id) AS order_count FROM users

-- IN subqueries
SELECT * FROM users WHERE id IN (SELECT user_id FROM orders)
SELECT * FROM users WHERE id NOT IN (SELECT user_id FROM orders WHERE status = 'cancelled')
SELECT * FROM products WHERE category_id IN (SELECT id FROM categories WHERE active = TRUE)

-- EXISTS subqueries
SELECT * FROM users WHERE EXISTS (SELECT 1 FROM orders WHERE orders.user_id = users.id)
SELECT * FROM users WHERE NOT EXISTS (SELECT 1 FROM orders WHERE orders.user_id = users.id)
SELECT * FROM users u WHERE EXISTS (SELECT 1 FROM orders o WHERE o.user_id = u.id AND o.total > 1000)

-- Correlated subqueries
SELECT * FROM users u WHERE salary > (SELECT AVG(salary) FROM users u2 WHERE u2.dept = u.dept)
SELECT name, (SELECT MAX(order_date) FROM orders o WHERE o.user_id = u.id) AS last_order FROM users u

-- FROM subqueries
SELECT * FROM (SELECT * FROM users WHERE active = TRUE) AS active_users
SELECT * FROM (SELECT id, name FROM users) u JOIN (SELECT user_id, COUNT(*) FROM orders GROUP BY user_id) o ON u.id = o.user_id
SELECT AVG(order_count) FROM (SELECT user_id, COUNT(*) AS order_count FROM orders GROUP BY user_id) AS user_orders

-- ============================================================================
-- SET OPERATIONS
-- ============================================================================

-- UNION
SELECT id FROM users UNION SELECT id FROM customers
SELECT id, name FROM users UNION SELECT id, name FROM customers
SELECT * FROM users WHERE active = TRUE UNION SELECT * FROM users WHERE premium = TRUE

-- UNION ALL
SELECT id FROM users UNION ALL SELECT id FROM customers
SELECT name FROM users UNION ALL SELECT name FROM employees

-- INTERSECT
SELECT id FROM users INTERSECT SELECT user_id FROM orders
SELECT name FROM users INTERSECT SELECT name FROM employees

-- EXCEPT/MINUS
SELECT id FROM users EXCEPT SELECT user_id FROM orders
SELECT id FROM users MINUS SELECT user_id FROM orders

-- Chained set operations
SELECT id FROM t1 UNION SELECT id FROM t2 UNION SELECT id FROM t3
SELECT id FROM t1 UNION SELECT id FROM t2 INTERSECT SELECT id FROM t3
SELECT id FROM t1 INTERSECT SELECT id FROM t2 EXCEPT SELECT id FROM t3

-- ============================================================================
-- COMMON TABLE EXPRESSIONS (CTEs)
-- ============================================================================

-- Simple CTE
WITH user_orders AS (SELECT * FROM orders) SELECT * FROM user_orders
WITH active_users AS (SELECT * FROM users WHERE active = TRUE) SELECT COUNT(*) FROM active_users
WITH dept_stats AS (SELECT dept, COUNT(*) AS cnt FROM employees GROUP BY dept) SELECT * FROM dept_stats WHERE cnt > 10

-- CTE with column list
WITH user_stats (user_id, order_count, total_amount) AS (SELECT user_id, COUNT(*), SUM(amount) FROM orders GROUP BY user_id) SELECT * FROM user_stats

-- Multiple CTEs
WITH active_users AS (SELECT * FROM users WHERE active = TRUE), recent_orders AS (SELECT * FROM orders WHERE order_date > '2024-01-01') SELECT * FROM active_users JOIN recent_orders ON active_users.id = recent_orders.user_id

-- Nested CTE reference
WITH base_data AS (SELECT * FROM sales WHERE year = 2024), aggregated AS (SELECT product_id, SUM(amount) AS total FROM base_data GROUP BY product_id) SELECT * FROM aggregated WHERE total > 1000

-- Recursive CTE
WITH RECURSIVE numbers AS (SELECT 1 AS n UNION ALL SELECT n + 1 FROM numbers WHERE n < 10) SELECT * FROM numbers
WITH RECURSIVE employee_hierarchy AS (SELECT id, name, manager_id, 1 AS level FROM employees WHERE manager_id IS NULL UNION ALL SELECT e.id, e.name, e.manager_id, h.level + 1 FROM employees e JOIN employee_hierarchy h ON e.manager_id = h.id) SELECT * FROM employee_hierarchy
WITH RECURSIVE tree AS (SELECT id, parent_id, name, name AS path FROM categories WHERE parent_id IS NULL UNION ALL SELECT c.id, c.parent_id, c.name, tree.path || '/' || c.name FROM categories c JOIN tree ON c.parent_id = tree.id) SELECT * FROM tree

-- ============================================================================
-- CASE EXPRESSIONS
-- ============================================================================

-- Simple CASE
SELECT CASE status WHEN 'active' THEN 'Active' WHEN 'inactive' THEN 'Inactive' ELSE 'Unknown' END FROM users
SELECT CASE dept WHEN 'IT' THEN 'Information Technology' WHEN 'HR' THEN 'Human Resources' ELSE dept END FROM employees

-- Searched CASE
SELECT CASE WHEN age < 18 THEN 'Minor' WHEN age < 65 THEN 'Adult' ELSE 'Senior' END FROM users
SELECT CASE WHEN salary < 30000 THEN 'Low' WHEN salary < 70000 THEN 'Medium' WHEN salary < 100000 THEN 'High' ELSE 'Very High' END FROM employees
SELECT CASE WHEN COUNT(*) = 0 THEN 'No orders' WHEN COUNT(*) = 1 THEN 'One order' ELSE 'Multiple orders' END FROM orders GROUP BY user_id

-- Nested CASE
SELECT CASE WHEN age < 18 THEN 'Minor' ELSE CASE WHEN age < 65 THEN 'Adult' ELSE 'Senior' END END FROM users
SELECT CASE WHEN dept = 'IT' THEN CASE WHEN salary > 100000 THEN 'Senior IT' ELSE 'Junior IT' END ELSE 'Non-IT' END FROM employees

-- CASE in WHERE
SELECT * FROM users WHERE CASE WHEN created_at > '2024-01-01' THEN active = TRUE ELSE TRUE END
SELECT * FROM orders WHERE amount > CASE WHEN priority = 'high' THEN 100 ELSE 50 END

-- CASE in ORDER BY
SELECT * FROM users ORDER BY CASE WHEN premium = TRUE THEN 0 ELSE 1 END, created_at DESC
SELECT * FROM products ORDER BY CASE category WHEN 'featured' THEN 1 WHEN 'popular' THEN 2 ELSE 3 END

-- ============================================================================
-- SELECT ADVANCED (Complex features and combinations)
-- ============================================================================

-- Complex nested subqueries
SELECT * FROM users WHERE id IN (SELECT user_id FROM orders WHERE product_id IN (SELECT id FROM products WHERE category_id IN (SELECT id FROM categories WHERE name = 'Electronics')))
SELECT * FROM (SELECT * FROM (SELECT * FROM users WHERE active = TRUE) WHERE created_at > '2024-01-01') WHERE email IS NOT NULL

-- Correlated subqueries with aggregates
SELECT u.*, (SELECT COUNT(*) FROM orders o WHERE o.user_id = u.id) AS order_count, (SELECT SUM(amount) FROM orders o WHERE o.user_id = u.id) AS total_spent FROM users u
SELECT dept, name, salary FROM employees e WHERE salary = (SELECT MAX(salary) FROM employees e2 WHERE e2.dept = e.dept)

-- HAVING with complex conditions
SELECT dept, COUNT(*) AS cnt, AVG(salary) AS avg_sal FROM employees GROUP BY dept HAVING COUNT(*) > 10 AND AVG(salary) > 50000
SELECT user_id FROM orders GROUP BY user_id HAVING SUM(amount) > 1000 AND COUNT(*) > 5 AND MAX(order_date) > '2024-01-01'

-- Mixed set operations with different complexities
SELECT id FROM users WHERE active = TRUE UNION SELECT user_id FROM orders WHERE status = 'completed' INTERSECT SELECT user_id FROM reviews WHERE rating > 4
(SELECT * FROM users WHERE active = TRUE) UNION ALL (SELECT * FROM users WHERE premium = TRUE) ORDER BY created_at DESC

-- ============================================================================
-- SELECT ULTRA COMPLEX (Stress testing parser capabilities)
-- ============================================================================

-- Deeply nested queries
SELECT * FROM (SELECT * FROM (SELECT * FROM (SELECT * FROM (SELECT * FROM (SELECT * FROM users)))))
SELECT * FROM users WHERE id IN (SELECT user_id FROM orders WHERE product_id IN (SELECT id FROM products WHERE category_id IN (SELECT id FROM categories WHERE parent_id IN (SELECT id FROM categories WHERE name LIKE '%Tech%'))))

-- Multiple CTEs with complex references
WITH RECURSIVE cats AS (SELECT * FROM categories WHERE parent_id IS NULL UNION ALL SELECT c.* FROM categories c JOIN cats ON c.parent_id = cats.id), prods AS (SELECT * FROM products WHERE category_id IN (SELECT id FROM cats)), orders_filtered AS (SELECT * FROM orders WHERE product_id IN (SELECT id FROM prods)) SELECT u.name, COUNT(o.id) FROM users u LEFT JOIN orders_filtered o ON u.id = o.user_id GROUP BY u.name HAVING COUNT(o.id) > 0

-- Complex JOIN with subqueries and aggregates
SELECT u.name, o.order_count, p.product_count, r.avg_rating FROM users u LEFT JOIN (SELECT user_id, COUNT(*) AS order_count FROM orders GROUP BY user_id) o ON u.id = o.user_id LEFT JOIN (SELECT seller_id, COUNT(*) AS product_count FROM products GROUP BY seller_id) p ON u.id = p.seller_id LEFT JOIN (SELECT user_id, AVG(rating) AS avg_rating FROM reviews GROUP BY user_id) r ON u.id = r.user_id WHERE u.active = TRUE AND (o.order_count > 10 OR p.product_count > 5 OR r.avg_rating > 4.5)

-- Massive expression
SELECT CASE WHEN (a + b) * (c - d) / NULLIF(e, 0) > 100 AND (f BETWEEN 1 AND 10 OR g IN (1,2,3,4,5)) AND h LIKE '%test%' AND EXISTS (SELECT 1 FROM t2 WHERE t2.x = t1.id) THEN 'Complex' ELSE 'Simple' END FROM t1

-- All clauses combined
WITH cte AS (SELECT * FROM base_table) SELECT DISTINCT u.id, u.name, COUNT(o.id) AS order_count, SUM(o.amount) AS total_amount, AVG(o.amount) AS avg_amount, CASE WHEN COUNT(o.id) > 10 THEN 'Frequent' WHEN COUNT(o.id) > 5 THEN 'Regular' ELSE 'Occasional' END AS customer_type FROM users u LEFT JOIN orders o ON u.id = o.user_id WHERE u.active = TRUE AND u.created_at > '2020-01-01' AND EXISTS (SELECT 1 FROM products p WHERE p.seller_id = u.id) GROUP BY u.id, u.name HAVING COUNT(o.id) > 0 AND SUM(o.amount) > 100 ORDER BY total_amount DESC, u.name ASC LIMIT 100

-- ============================================================================
-- SELECT EDGE CASES (Parser boundary testing)
-- ============================================================================

-- Empty/minimal queries
SELECT 1
SELECT *
SELECT NULL

-- Maximum identifier length (testing parser limits)
SELECT very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_long_column_name FROM very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_long_table_name

-- Special characters in identifiers (quoted)
SELECT "column-with-dashes" FROM "table-name"
SELECT "column.with.dots" FROM "schema.table"
SELECT "column with spaces" FROM "table name"

-- Mixed case handling
SeLeCt * FrOm UsErS
SELECT * FROM users WHERE ID = 1
select * from USERS where id = 1

-- Operator precedence testing
SELECT 1 + 2 * 3
SELECT (1 + 2) * 3
SELECT 1 + 2 * 3 - 4 / 2
SELECT NOT a OR b AND c
SELECT a = b AND c = d OR e = f
SELECT a AND b OR c AND d

-- Parenthesis balancing
SELECT ((((1))))
SELECT (((1 + 2) * 3) - 4)
SELECT * FROM ((((users))))

-- Keywords in different contexts
SELECT select FROM from WHERE where = select
SELECT "SELECT" FROM "FROM" WHERE "WHERE" = "SELECT"
SELECT MIN(min) FROM MAX WHERE max > min

-- NULL handling
SELECT NULL IS NULL
SELECT NULL IS NOT NULL
SELECT 1 = NULL
SELECT NULL = NULL
SELECT COALESCE(NULL, NULL, 1)

-- Boolean expressions
SELECT TRUE AND TRUE
SELECT FALSE OR TRUE
SELECT NOT FALSE
SELECT TRUE = TRUE
SELECT TRUE IS TRUE

-- ============================================================================
-- DDL - CREATE INDEX/VIEW
-- ============================================================================

CREATE INDEX idx_users_email ON users (email)
CREATE UNIQUE INDEX idx_users_id ON users (id)
CREATE INDEX IF NOT EXISTS idx_users_name ON users (name)
CREATE INDEX idx_active ON users (email) WHERE active = true

CREATE VIEW active_users AS SELECT * FROM users WHERE active = true
CREATE OR REPLACE VIEW user_summary AS SELECT id, name FROM users
CREATE VIEW orders_view (order_id, customer, total) AS SELECT id, customer_id, amount FROM orders

-- ============================================================================
-- DDL - DROP STATEMENTS
-- ============================================================================

DROP TABLE users
DROP TABLE IF EXISTS users
DROP TABLE users CASCADE
DROP TABLE users RESTRICT

DROP INDEX idx_users_email
DROP INDEX IF EXISTS idx_users_email

DROP VIEW active_users
DROP VIEW IF EXISTS user_summary

-- ============================================================================
-- DDL - ALTER TABLE
-- ============================================================================

ALTER TABLE users ADD COLUMN age INT
ALTER TABLE users ADD COLUMN email VARCHAR(255) NOT NULL
ALTER TABLE users DROP COLUMN age
ALTER TABLE users DROP COLUMN IF EXISTS email
ALTER TABLE users RENAME TO customers
ALTER TABLE users RENAME COLUMN email TO email_address
ALTER TABLE users ALTER COLUMN age SET DEFAULT 0
ALTER TABLE users ALTER COLUMN age DROP DEFAULT

-- ============================================================================
-- DDL - TRUNCATE
-- ============================================================================

TRUNCATE TABLE users
TRUNCATE users
TRUNCATE TABLE users CASCADE
TRUNCATE TABLE users RESTRICT

-- ============================================================================
-- DDL - CREATE TABLE (Data Definition Language)
-- ============================================================================

-- Basic CREATE TABLE
CREATE TABLE users (id INT)
CREATE TABLE users (id INT, name VARCHAR(255))
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL)

-- With multiple columns and constraints
CREATE TABLE employees (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, email VARCHAR(255) UNIQUE, salary DECIMAL(10,2), dept_id INT, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)

-- With foreign keys
CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, product_id INT, FOREIGN KEY (user_id) REFERENCES users(id), FOREIGN KEY (product_id) REFERENCES products(id))

-- With composite primary key
CREATE TABLE order_items (order_id INT, product_id INT, quantity INT, PRIMARY KEY (order_id, product_id))

-- With check constraints
CREATE TABLE products (id INT PRIMARY KEY, name VARCHAR(255), price DECIMAL(10,2) CHECK (price > 0), stock INT CHECK (stock >= 0))

-- IF NOT EXISTS
CREATE TABLE IF NOT EXISTS users (id INT PRIMARY KEY, name VARCHAR(255))

-- ============================================================================
-- DML - INSERT (Data Manipulation Language)
-- ============================================================================

-- Basic INSERT
INSERT INTO users VALUES (1, 'John')
INSERT INTO users (id, name) VALUES (1, 'John')
INSERT INTO users (name, id) VALUES ('John', 1)

-- Multiple rows
INSERT INTO users VALUES (1, 'John'), (2, 'Jane'), (3, 'Bob')
INSERT INTO users (id, name) VALUES (1, 'John'), (2, 'Jane'), (3, 'Bob')

-- INSERT with SELECT
INSERT INTO users SELECT * FROM temp_users
INSERT INTO users (id, name) SELECT id, name FROM temp_users
INSERT INTO users (id, name) SELECT id, name FROM temp_users WHERE active = TRUE

-- INSERT with DEFAULT values
INSERT INTO users (id, name, created_at) VALUES (1, 'John', DEFAULT)
INSERT INTO users DEFAULT VALUES

-- ============================================================================
-- DML - UPDATE
-- ============================================================================

-- Basic UPDATE
UPDATE users SET name = 'John'
UPDATE users SET name = 'John' WHERE id = 1
UPDATE users SET name = 'John', email = 'john@example.com' WHERE id = 1

-- UPDATE with expressions
UPDATE users SET age = age + 1
UPDATE users SET salary = salary * 1.1 WHERE dept = 'IT'
UPDATE products SET price = price * 0.9 WHERE category = 'sale'

-- UPDATE with subquery
UPDATE users SET status = 'premium' WHERE id IN (SELECT user_id FROM orders GROUP BY user_id HAVING COUNT(*) > 10)
UPDATE employees SET salary = (SELECT AVG(salary) FROM employees e2 WHERE e2.dept = employees.dept)

-- UPDATE with CASE
UPDATE users SET status = CASE WHEN age < 18 THEN 'minor' WHEN age < 65 THEN 'adult' ELSE 'senior' END

-- ============================================================================
-- DML - DELETE
-- ============================================================================

-- Basic DELETE
DELETE FROM users
DELETE FROM users WHERE id = 1
DELETE FROM users WHERE active = FALSE
DELETE FROM users WHERE created_at < '2020-01-01'

-- DELETE with subquery
DELETE FROM users WHERE id IN (SELECT user_id FROM blacklist)
DELETE FROM users WHERE NOT EXISTS (SELECT 1 FROM orders WHERE orders.user_id = users.id)

-- ============================================================================
-- End of SQL Test Suite
-- ============================================================================