/*
    Copyright (C) 2015 Matthias P. Braendli (http://opendigitalradio.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    charset.cpp
         This is test code for the character set conversion. It does not
         get compiled by default.
         Please use
         g++ -Wall -Wextra -std=c++11 -o charset-test charset-test.cpp
         to compile this, and create a input.txt file with test data.

    Authors:
         Matthias P. Braendli <matthias@mpb.li>
*/

#include "charset.h"
#include <iostream>
#include <fstream>

#include <cstdio>

using namespace std;

int main(int argc, char** argv)
{
    string test_file_path("input.txt");

    ifstream fs8(test_file_path);
    if (!fs8.is_open()) {
        cerr << "Could not open " << test_file_path << endl;
        return 1;
    }

    CharsetConverter conv;

    string line;
    // Play with all the lines in the file
    while (getline(fs8, line)) {
        cout << conv.convert(line) << endl;
    }

    return 0;
}


