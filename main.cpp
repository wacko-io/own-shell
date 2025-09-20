#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
using namespace std;

static string history_path() {
    const char* home = getenv("HOME");
    string base = home ? string(home) : "";
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/.kubsh_history";
}

static void append_history(const string& line) {
    ofstream out(history_path(), ios::app);
    if (out) out << line << '\n';
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

int main() {
    string line;
    while (true) {
        cout << "$ " << flush;

        if (!getline(cin, line)) {
            cout << endl;
            break;
        }

        if (line == "\\q")
            break;

        if (line.empty()) continue;

        if (line == "history") {
            print_history();
            append_history(line);
            continue;
        }

        cout << line << endl;
        append_history(line);
    }
    return 0;
}