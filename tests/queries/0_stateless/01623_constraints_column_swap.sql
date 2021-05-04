SET convert_query_to_cnf = 1;
SET optimize_using_constraints = 1;
SET optimize_move_to_prewhere = 1;

DROP DATABASE IF EXISTS column_swap_test;
DROP TABLE IF EXISTS column_swap_test.test;

CREATE DATABASE column_swap_test;

CREATE TABLE column_swap_test.test (i Int64, a String, b UInt64, CONSTRAINT c1 ASSUME b = cityHash64(a)) ENGINE = MergeTree() ORDER BY i;
INSERT INTO column_swap_test.test VALUES (1, 'cat', 1), (2, 'dog', 2);
INSERT INTO column_swap_test.test SELECT number AS i, format('test {} kek {}', toString(number), toString(number + 10))  AS a, 1 AS b FROM system.numbers LIMIT 1000000;

EXPLAIN SYNTAX SELECT cityHash64(a) + 10, b + 3 FROM column_swap_test.test WHERE cityHash64(a) = 1;
EXPLAIN SYNTAX SELECT cityHash64(a) + 10, b + 3 FROM column_swap_test.test WHERE cityHash64(a) = 0;
EXPLAIN SYNTAX SELECT cityHash64(a) + 10, b + 3 FROM column_swap_test.test WHERE b = 0;
EXPLAIN SYNTAX SELECT cityHash64(a) + 10, b + 3 FROM column_swap_test.test WHERE b = 1;

EXPLAIN SYNTAX SELECT cityHash64(a) + 10 FROM column_swap_test.test WHERE cityHash64(a) = 0;
EXPLAIN SYNTAX SELECT cityHash64(a) + 10, a FROM column_swap_test.test WHERE cityHash64(a) = 0;
EXPLAIN SYNTAX SELECT b + 10, a FROM column_swap_test.test WHERE b = 0;

DROP TABLE column_swap_test.test;

CREATE TABLE column_swap_test.test (i Int64, a String, b String, CONSTRAINT c1 ASSUME a = substring(reverse(b), 1, 1)) ENGINE = MergeTree() ORDER BY i;
INSERT INTO column_swap_test.test SELECT number AS i, toString(number) AS a, format('test {} kek {}', toString(number), toString(number + 10)) b FROM system.numbers LIMIT 1000000;

EXPLAIN SYNTAX SELECT substring(reverse(b), 1, 1), a FROM column_swap_test.test WHERE a = 'c';
EXPLAIN SYNTAX SELECT substring(reverse(b), 1, 1), a FROM column_swap_test.test WHERE substring(reverse(b), 1, 1) = 'c';
EXPLAIN SYNTAX SELECT substring(reverse(b), 1, 1) FROM column_swap_test.test WHERE substring(reverse(b), 1, 1) = 'c';

DROP TABLE column_swap_test.test;

DROP DATABASE column_swap_test;
