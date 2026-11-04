# MiniSQLite (C++)

A small educational SQL-like database for the terminal. C++17, standard library only.

## Build and run

```
g++ -std=c++17 -O2 -o minisqlite minisqlite.cpp
./minisqlite
```

On first run an empty `database.db` is created. Data is saved after every successful change and restored on the next start.

## Supported commands

Every SQL command is one line and ends with `;`. Keywords are case-insensitive.

```
CREATE TABLE students (id INTEGER, name TEXT, age INTEGER);
INSERT INTO students VALUES (1, 'Ani', 20);
SELECT * FROM students;
SELECT * FROM students WHERE id = 1;
UPDATE students SET age = 21 WHERE id = 1;
DELETE FROM students WHERE id = 2;
```

Service commands: `.help`, `.tables`, `.exit`

## How it works

- **Parsing:** each command is matched with `std::regex` (case-insensitive). Values are split by commas outside single quotes, and each becomes an INTEGER or a `'TEXT'` literal.
- **Structure:** `map<string, Table>`; a `Table` has `vector<Column>` (name, type) and `vector<vector<Value>>` (rows).
- **Storage:** plain text file `database.db`: `T` table line, `C` column lines, `N` row count, then one value per line.
- **Finding rows:** `WHERE col = value` gives a column index; every row with an equal value at that index is selected, updated or deleted.
- **Safety:** each command runs on a copy; the file is written to a temp file and renamed over the old one. A failed command changes nothing.

## Limitations

- Types: `INTEGER` and `TEXT` only. No NULL, JOIN, ORDER BY, indexes, transactions.
- `WHERE`: a single `column = value`. `SELECT` supports `*` only; `UPDATE` changes one column.
- Names: Latin letters, digits, `_`; case-sensitive. Commands on a single line; no special quote handling inside text.
