#include <iostream>
#include <string>
using namespace std;
int main() {
    string input;
    while (true) {
        cout << "$ ";
        getline(cin, input);
        cout << input << ": command not found" << endl;
    }
    return 0;
}
