#include <iostream>
#include <string>
using namespace std;
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

        cout << line << endl;
    }
    return 0;
}