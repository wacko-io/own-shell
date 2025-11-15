#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cctype>

using namespace std;

static string history_path() {
    const char* home = getenv("HOME");
    string base = home ? string(home) : "";
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/.kubsh_history";
}

static void append_history(const string& line) {
    ofstream out(history_path(), ios::app);
    if (out) {
        out << line << '\n';
    }
}

void print_history() {
    ifstream in(history_path());
    if (!in) {
        cout << "История пуста" << endl;
        return;
    }

    string line;
    int num = 1;
    while (getline(in, line)) {
        cout << num++ << "  " << line << endl;
    }
}

string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

vector<string> tokenize(const string& line) {
    vector<string> out;
    string cur;
    bool in_s = false, in_d = false;

    for (char c : line) {
        if (c == '\'' && !in_d) {
            in_s = !in_s;
            cur.push_back(c);
            continue;
        }
        if (c == '"' && !in_s) {
            in_d = !in_d;
            cur.push_back(c);
            continue;
        }
        if (!in_s && !in_d && isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

string unquote(const string& s) {
    if (s.size() >= 2) {
        char q = s.front();
        if ((q == '\'' || q == '"') && s.back() == q) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

bool is_builtin(const string& cmd) {
    return (
        cmd == "echo"   ||
        cmd == "history"||
        cmd == "\\q"    ||
        cmd == "\\e"    ||
        cmd == "debug"
    );
}

int run_builtin(const vector<string>& tokens) {
    const string& cmd = tokens[0];

    if (cmd == "\\q") {
        return 255;
    }

    if (cmd == "history") {
        print_history();
        return 0;
    }

    if (cmd == "echo") {
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (i > 1) cout << ' ';
            cout << unquote(tokens[i]);
        }
        cout << '\n';
        return 0;
    }

    if (cmd == "debug") {
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (i > 1) cout << ' ';
            cout << unquote(tokens[i]);
        }
        cout << '\n';
        return 0;
    }

    if (cmd == "\\e") {
        if (tokens.size() < 2) {
            cerr << "Usage: \\e $VARNAME" << endl;
            return 1;
        }

        string var = tokens[1];
        if (!var.empty() && var[0] == '$') var.erase(0, 1);

        const char* val = getenv(var.c_str());
        string s = val ? string(val) : "";

        if (var == "PATH") {
            size_t start = 0;
            while (true) {
                size_t pos = s.find(':', start);
                if (pos == string::npos) {
                    cout << s.substr(start) << endl;
                    break;
                }
                cout << s.substr(start, pos - start) << endl;
                start = pos + 1;
            }
        } else {
            cout << s << endl;
        }
        return 0;
    }

    return 0;
}

int main() {
    string line;

    while (true) {
        cerr << "$ " << flush;

        if (!getline(cin, line)) {
            cout << endl;
            break;
        }

        line = trim(line);
        if (line.empty()) continue;

        if (line == "\\q") {
            break;
        }

        vector<string> tokens = tokenize(line);
        if (tokens.empty()) continue;

        append_history(line);

        if (is_builtin(tokens[0])) {
            int rc = run_builtin(tokens);
            if (rc == 255) {
                break;
            }
            continue;
        }

        cout << tokens[0] << ": command not found" << endl;
    }

    return 0;
}