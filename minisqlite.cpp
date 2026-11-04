// MiniSQLite - tiny educational SQL-like database (C++17, standard library only).
// Build:  g++ -std=c++17 -O2 -o minisqlite minisqlite.cpp
// Data is stored in database.db (simple text format, see saveDB / loadDB).
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

const string DB_FILE = "database.db";
const string NAME = "[A-Za-z0-9_]+";

// ---------- data model ----------
struct Column { string name, type; };            // type: "INTEGER" or "TEXT"
struct Value  { string type; long long num; string text; };
struct Table  { vector<Column> cols; vector<vector<Value>> rows; };
using DB = map<string, Table>;                   // table name -> table

struct DBError : runtime_error { using runtime_error::runtime_error; };

// ---------- small helpers ----------
static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static string upper(string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static string show(const Value& v) {
    return v.type == "INTEGER" ? to_string(v.num) : v.text;
}

static bool sameValue(const Value& a, const Value& b) {
    if (a.type != b.type) return false;
    return a.type == "INTEGER" ? a.num == b.num : a.text == b.text;
}

// Split "1, 'Ani, X', 20" by commas that are outside single quotes.
static vector<string> splitCommas(const string& s) {
    vector<string> parts;
    string cur;
    bool inQuote = false;
    for (char ch : s) {
        if (ch == '\'') inQuote = !inQuote;
        if (ch == ',' && !inQuote) { parts.push_back(trim(cur)); cur.clear(); }
        else cur += ch;
    }
    if (inQuote) throw DBError("Syntax error: unclosed quote '.");
    parts.push_back(trim(cur));
    return parts;
}

// 'text' -> TEXT value, 42 -> INTEGER value, anything else -> error.
static Value parseLiteral(const string& raw) {
    string t = trim(raw);
    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'')
        return {"TEXT", 0, t.substr(1, t.size() - 2)};
    static const regex num("^-?[0-9]+$");
    if (regex_match(t, num)) {
        try { return {"INTEGER", stoll(t), ""}; }
        catch (...) { throw DBError("Integer out of range: " + t); }
    }
    throw DBError("Invalid value: '" + t + "'. Use an integer or 'text' in single quotes.");
}

static void checkType(const Column& col, const Value& v) {
    if (v.type != col.type)
        throw DBError("Type mismatch: column '" + col.name + "' is " + col.type +
                      ", got " + v.type + " value " + show(v) + ".");
}

static Table& getTable(DB& db, const string& name) {
    auto it = db.find(name);
    if (it == db.end()) throw DBError("Table '" + name + "' does not exist.");
    return it->second;
}

static size_t colIndex(const Table& t, const string& name) {
    for (size_t i = 0; i < t.cols.size(); i++)
        if (t.cols[i].name == name) return i;
    throw DBError("Column '" + name + "' does not exist.");
}

// ---------- WHERE: only "column = value" ----------
struct Cond { bool has = false; size_t idx = 0; Value val; };

static Cond parseWhere(const Table& t, const string& text) {
    Cond c;
    if (text.empty()) return c;
    static const regex re("^(" + NAME + ")\\s*=\\s*(.+)$");
    smatch m;
    string s = trim(text);
    if (!regex_match(s, m, re))
        throw DBError("Syntax error in WHERE. Only 'column = value' is supported.");
    c.has = true;
    c.idx = colIndex(t, m[1]);
    c.val = parseLiteral(m[2]);
    checkType(t.cols[c.idx], c.val);
    return c;
}

static bool matches(const vector<Value>& row, const Cond& c) {
    return !c.has || sameValue(row[c.idx], c.val);
}

// ---------- storage ----------
// File format (one item per line, so text with any characters except newline is safe):
//   T <table> <number of columns>
//   C <column name> <type>        (repeated)
//   N <number of rows>
//   <value>                        (rows * columns lines, row by row)
static void saveDB(const DB& db) {
    string tmp = DB_FILE + ".tmp";
    {
        ofstream f(tmp);
        if (!f) throw DBError("Could not write " + tmp);
        for (const auto& [name, t] : db) {
            f << "T " << name << " " << t.cols.size() << "\n";
            for (const auto& c : t.cols) f << "C " << c.name << " " << c.type << "\n";
            f << "N " << t.rows.size() << "\n";
            for (const auto& row : t.rows)
                for (const auto& v : row) f << show(v) << "\n";
        }
        f.flush();
        if (!f) throw DBError("Could not write " + tmp);
    }
    // Write to a temp file, then replace: a crash can't leave a half-written database.
    filesystem::rename(tmp, DB_FILE);
}

[[noreturn]] static void corrupted() {
    throw DBError("Cannot read '" + DB_FILE + "': file is corrupted. Fix or delete it.");
}

static DB loadDB() {
    DB db;
    ifstream f(DB_FILE);
    if (!f) { saveDB(db); return db; }  // first run: create an empty database
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        istringstream is(line);
        string tag, name;
        size_t ncols = 0;
        is >> tag >> name >> ncols;
        if (tag != "T" || !is) corrupted();
        Table t;
        for (size_t i = 0; i < ncols; i++) {
            if (!getline(f, line)) corrupted();
            istringstream cs(line);
            string tg;
            Column c;
            cs >> tg >> c.name >> c.type;
            if (tg != "C" || !cs) corrupted();
            t.cols.push_back(c);
        }
        if (!getline(f, line)) corrupted();
        istringstream ns(line);
        string tg;
        size_t nrows = 0;
        ns >> tg >> nrows;
        if (tg != "N" || !ns) corrupted();
        for (size_t r = 0; r < nrows; r++) {
            vector<Value> row;
            for (const auto& c : t.cols) {
                if (!getline(f, line)) corrupted();
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (c.type == "INTEGER") {
                    try { row.push_back({"INTEGER", stoll(line), ""}); }
                    catch (...) { corrupted(); }
                } else {
                    row.push_back({"TEXT", 0, line});
                }
            }
            t.rows.push_back(row);
        }
        db[name] = t;
    }
    return db;
}

// ---------- output ----------
static string formatTable(const Table& t, const vector<const vector<Value>*>& rows) {
    size_t n = t.cols.size();
    vector<size_t> w(n);
    for (size_t i = 0; i < n; i++) w[i] = t.cols[i].name.size();
    for (auto* r : rows)
        for (size_t i = 0; i < n; i++) w[i] = max(w[i], show((*r)[i]).size());

    auto line = [&](const vector<string>& cells) {
        string s;
        for (size_t i = 0; i < n; i++) {
            if (i) s += " | ";
            s += cells[i] + string(w[i] - cells[i].size(), ' ');
        }
        return s;
    };
    vector<string> head;
    for (auto& c : t.cols) head.push_back(c.name);
    string out = line(head) + "\n";
    for (size_t i = 0; i < n; i++) out += (i ? "-+-" : "") + string(w[i], '-');
    out += "\n";
    for (auto* r : rows) {
        vector<string> cells;
        for (auto& v : *r) cells.push_back(show(v));
        out += line(cells) + "\n";
    }
    out += "(" + to_string(rows.size()) + (rows.size() == 1 ? " row)" : " rows)");
    return out;
}

// ---------- command execution ----------
// Works on `db` (a COPY made by the caller). Sets `changed` if the data was modified.
static string execute(DB& db, string sql, bool& changed) {
    sql = trim(sql);
    if (sql.empty() || sql.back() != ';')
        throw DBError("Syntax error: command must end with ';'.");
    sql = trim(sql.substr(0, sql.size() - 1));

    const auto I = regex::icase;
    smatch m;

    // CREATE TABLE name (col TYPE, ...)
    static const regex reCreate("^create\\s+table\\s+(" + NAME + ")\\s*\\((.*)\\)$", I);
    if (regex_match(sql, m, reCreate)) {
        string name = m[1];
        if (db.count(name)) throw DBError("Table '" + name + "' already exists.");
        Table t;
        static const regex reCol("^(" + NAME + ")\\s+(" + NAME + ")$");
        for (const string& part : splitCommas(m[2])) {
            smatch cm;
            if (!regex_match(part, cm, reCol))
                throw DBError("Syntax error in column definition: '" + part +
                              "'. Expected 'name TYPE'.");
            string cname = cm[1], ctype = upper(cm[2]);
            if (ctype != "INTEGER" && ctype != "TEXT")
                throw DBError("Unknown type '" + string(cm[2]) + "'. Supported: INTEGER, TEXT.");
            for (auto& c : t.cols)
                if (c.name == cname) throw DBError("Duplicate column name '" + cname + "'.");
            t.cols.push_back({cname, ctype});
        }
        db[name] = t;
        changed = true;
        return "Table '" + name + "' created.";
    }

    // INSERT INTO name VALUES (v1, v2, ...)
    static const regex reInsert("^insert\\s+into\\s+(" + NAME + ")\\s+values\\s*\\((.*)\\)$", I);
    if (regex_match(sql, m, reInsert)) {
        Table& t = getTable(db, m[1]);
        vector<string> tokens = splitCommas(m[2]);
        if (tokens.size() != t.cols.size())
            throw DBError("Expected " + to_string(t.cols.size()) + " values, got " +
                          to_string(tokens.size()) + ".");
        vector<Value> row;
        for (size_t i = 0; i < tokens.size(); i++) {
            Value v = parseLiteral(tokens[i]);
            checkType(t.cols[i], v);
            row.push_back(v);
        }
        t.rows.push_back(row);
        changed = true;
        return "1 row inserted.";
    }

    // SELECT * FROM name [WHERE col = value]
    static const regex reSelect("^select\\s+\\*\\s+from\\s+(" + NAME + ")(?:\\s+where\\s+(.+))?$", I);
    if (regex_match(sql, m, reSelect)) {
        Table& t = getTable(db, m[1]);
        Cond c = parseWhere(t, m[2].matched ? string(m[2]) : "");
        vector<const vector<Value>*> found;
        for (auto& r : t.rows)
            if (matches(r, c)) found.push_back(&r);
        return formatTable(t, found);
    }

    // UPDATE name SET col = value [WHERE col = value]
    static const regex reUpdate(
        "^update\\s+(" + NAME + ")\\s+set\\s+(" + NAME + ")\\s*=\\s*(.+?)(?:\\s+where\\s+(.+))?$", I);
    if (regex_match(sql, m, reUpdate)) {
        Table& t = getTable(db, m[1]);
        size_t idx = colIndex(t, m[2]);
        Value nv = parseLiteral(m[3]);
        checkType(t.cols[idx], nv);
        Cond c = parseWhere(t, m[4].matched ? string(m[4]) : "");
        int count = 0;
        for (auto& r : t.rows)
            if (matches(r, c)) { r[idx] = nv; count++; }
        if (count > 0) changed = true;
        return to_string(count) + " row(s) updated.";
    }

    // DELETE FROM name [WHERE col = value]
    static const regex reDelete("^delete\\s+from\\s+(" + NAME + ")(?:\\s+where\\s+(.+))?$", I);
    if (regex_match(sql, m, reDelete)) {
        Table& t = getTable(db, m[1]);
        Cond c = parseWhere(t, m[2].matched ? string(m[2]) : "");
        vector<vector<Value>> keep;
        for (auto& r : t.rows)
            if (!matches(r, c)) keep.push_back(r);
        size_t removed = t.rows.size() - keep.size();
        t.rows = keep;
        if (removed > 0) changed = true;
        return to_string(removed) + " row(s) deleted.";
    }

    throw DBError("Unknown or malformed command. Type .help for help.");
}

// Run on a copy -> save to disk -> only then replace the live data.
// If anything fails, the real data and the file stay untouched.
static string runCommand(DB& db, const string& line) {
    DB work = db;
    bool changed = false;
    string result = execute(work, line, changed);
    if (changed) {
        saveDB(work);
        db = work;
    }
    return result;
}

static const char* HELP =
    "Supported commands (end every SQL command with ;):\n"
    "  CREATE TABLE students (id INTEGER, name TEXT, age INTEGER);\n"
    "  INSERT INTO students VALUES (1, 'Ani', 20);\n"
    "  SELECT * FROM students;\n"
    "  SELECT * FROM students WHERE id = 1;\n"
    "  UPDATE students SET age = 21 WHERE id = 1;\n"
    "  DELETE FROM students WHERE id = 2;\n"
    "Service commands:\n"
    "  .help    show this help\n"
    "  .tables  list tables\n"
    "  .exit    quit";

int main() {
    DB db;
    try {
        db = loadDB();
    } catch (const exception& e) {
        cout << "Error: " << e.what() << endl;
        return 1;
    }
    cout << "MiniSQLite\nType .help for help." << endl;

    string line;
    while (true) {
        cout << "minidb> " << flush;
        if (!getline(cin, line)) { cout << endl; break; }  // Ctrl+D / end of input
        line = trim(line);
        if (line.empty()) continue;

        string low = line;
        for (auto& c : low) c = (char)tolower((unsigned char)c);

        if (low == ".exit") break;
        if (low == ".help") { cout << HELP << endl; continue; }
        if (low == ".tables") {
            if (db.empty()) cout << "(no tables)" << endl;
            for (const auto& [name, t] : db) cout << name << endl;
            continue;
        }
        if (line[0] == '.') {
            cout << "Error: unknown command '" << line << "'. Type .help for help." << endl;
            continue;
        }
        try {
            cout << runCommand(db, line) << endl;
        } catch (const exception& e) {   // any error: print it and keep running
            cout << "Error: " << e.what() << endl;
        }
    }
    return 0;
}
