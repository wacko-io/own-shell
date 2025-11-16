#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cctype>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <sys/select.h>
#include <fcntl.h>

using namespace std;
namespace fs = std::filesystem;

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
        cout << "История пуста\n";
        return;
    }
    string line;
    int num = 1;
    while (getline(in, line)) cout << num++ << "  " << line << "\n";
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
        if (!in_s && !in_d && isspace((unsigned char)c)) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

string unquote(const string& s) {
    if (s.size() >= 2) {
        char q = s.front();
        if ((q == '\'' || q == '"') && s.back() == q) return s.substr(1, s.size() - 2);
    }
    return s;
}

struct EtcUser {
    string name;
    string uid;
    string gid;
    string home;
    string shell;
};

static bool shell_allows_login(const string& sh) {
    if (sh.size() < 2) return false;
    return sh.substr(sh.size() - 2) == "sh";
}

static vector<EtcUser> read_passwd_all() {
    vector<EtcUser> users;
    ifstream in("/etc/passwd");
    string line;
    while (getline(in, line)) {
        vector<string> f;
        string cur;
        for (char c : line) {
            if (c == ':') {
                f.push_back(cur);
                cur.clear();
            } else cur.push_back(c);
        }
        f.push_back(cur);
        if (f.size() < 7) continue;
        EtcUser u;
        u.name = f[0];
        u.uid = f[2];
        u.gid = f[3];
        u.home = f[5];
        u.shell = f[6];
        users.push_back(u);
    }
    return users;
}

static vector<EtcUser> read_passwd_login() {
    vector<EtcUser> out;
    auto all = read_passwd_all();
    for (auto& u : all)
        if (shell_allows_login(u.shell))
            out.push_back(u);
    return out;
}

static bool find_user(const string& name, EtcUser& u) {
    auto all = read_passwd_all();
    for (auto& x : all) {
        if (x.name == name) {
            u = x;
            return true;
        }
    }
    return false;
}

static void ensure_tree(const EtcUser& u, const fs::path& root) {
    fs::create_directories(root / u.name);
    {
        ofstream f(root / u.name / "id");
        f << u.uid;
    }
    {
        ofstream f(root / u.name / "home");
        f << u.home;
    }
    {
        ofstream f(root / u.name / "shell");
        f << u.shell;
    }
}

static string choose_uid() {
    auto all = read_passwd_all();
    long mx = 2000;
    for (auto& u : all) {
        try {
            long x = stol(u.uid);
            if (x > mx) mx = x;
        } catch (...) {}
    }
    return to_string(mx + 1);
}

static void append_passwd_user(const string& name, const string& uid) {
    ofstream out("/etc/passwd", ios::app);
    if (!out) return;
    out << name << ":x:" << uid << ":" << uid << "::/home/" << name << ":/bin/bash\n";
}

static void vfs_tick(const fs::path& root) {
    try {
        auto logins = read_passwd_login();
        for (auto& u : logins) ensure_tree(u, root);

        for (auto& p : fs::directory_iterator(root)) {
            if (!p.is_directory()) continue;
            string name = p.path().filename().string();

            bool found = false;
            for (auto& u : logins)
                if (u.name == name)
                    found = true;

            if (!found) {
                string cmd = "adduser --disabled-password --gecos \"\" " + name + " >/dev/null 2>&1";
                int rc = system(cmd.c_str());
                (void)rc;
                EtcUser nu;
                if (!find_user(name, nu)) {
                    string uid = choose_uid();
                    append_passwd_user(name, uid);
                    find_user(name, nu);
                }
                if (!nu.name.empty()) ensure_tree(nu, root);
            }
        }
    } catch (...) {}
}

static void initial_sync(const fs::path& root) {
    fs::create_directories(root);
    auto users = read_passwd_login();
    for (auto& u : users) ensure_tree(u, root);
}

static volatile sig_atomic_t hup_flag = 0;
static void on_hup(int) { hup_flag = 1; }

bool is_builtin(const string& c) {
    return c == "echo" || c == "history" || c == "\\q" || c == "\\e" || c == "debug";
}

int run_builtin(const vector<string>& t) {
    const string& c = t[0];

    if (c == "\\q") return 255;

    if (c == "history") {
        print_history();
        return 0;
    }

    if (c == "echo" || c == "debug") {
        for (size_t i = 1; i < t.size(); ++i) {
            if (i > 1) cout << ' ';
            cout << unquote(t[i]);
        }
        cout << "\n";
        return 0;
    }

    if (c == "\\e") {
        if (t.size() < 2) {
            cerr << "Usage: \\e $VAR\n";
            return 1;
        }
        string var = t[1];
        if (!var.empty() && var[0] == '$') var.erase(0, 1);
        const char* val = getenv(var.c_str());
        string s = val ? val : "";
        if (var == "PATH") {
            size_t start = 0;
            while (true) {
                size_t pos = s.find(':', start);
                if (pos == string::npos) {
                    cout << s.substr(start) << "\n";
                    break;
                }
                cout << s.substr(start, pos - start) << "\n";
                start = pos + 1;
            }
        } else cout << s << "\n";
        return 0;
    }

    return 0;
}

int run_external(const vector<string>& t) {
    vector<char*> argv;
    for (auto& s : t) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        cout << t[0] << ": fork failed\n";
        return 127;
    }

    if (pid == 0) {
        execvp(argv[0], argv.data());
        cout << t[0] << ": command not found" << std::endl;
        _exit(127);
    }

    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return 127;

    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return 127;
}

int main() {
    fs::path root = "/opt/users";
    initial_sync(root);

    struct sigaction sa{};
    sa.sa_handler = on_hup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, nullptr);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    bool running = true;

    while (running) {
        vfs_tick(root);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 5000;

        int r = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);

        if (r > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            string line;
            if (!getline(cin, line)) break;

            if (hup_flag) {
                cout << "Configuration reloaded\n";
                hup_flag = 0;
            }

            line = trim(line);
            if (line.empty()) continue;

            if (line == "\\q") break;

            auto tok = tokenize(line);
            if (tok.empty()) continue;

            append_history(line);

            if (is_builtin(tok[0])) {
                int rc = run_builtin(tok);
                if (rc == 255) break;
                continue;
            }

            run_external(tok);
        }
    }

    return 0;
}