/*
 *
 *  Multi Process Garbage Collector
 *  Copyright © 2016 Hewlett Packard Enterprise Development Company LP.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  As an exception, the copyright holders of this Library grant you permission
 *  to (i) compile an Application with the Library, and (ii) distribute the 
 *  Application containing code generated by the Library and added to the 
 *  Application during this compilation process under terms of your choice, 
 *  provided you also meet the terms and conditions of the Application license.
 *
 */

#include "mpgc/gc_string.h"


using namespace mpgc;
using namespace std;

int main() {
  gc_string s1 = "Hi";
  cout << s1 << endl;
  s1 += " there";
  cout << s1 << endl;
  gc_string s2{s1 + ", mom"};
  cout << s2 << endl;
  s2.erase(2, 6);
  cout << s2 << endl;
  auto p = s2.find("mom");
  cout << "'mom' at " << p << endl;
  p = s2.find_first_of("aeiou");
  cout << "first vowel at " << p << endl;
  p = s2.find_last_of("aeiou");
  cout << "last vowel at " << p << endl;
  gc_string s3 = "42";
  int n = std::stoi(s3);
  cout << "'" << s3 << "' parses as " << n << endl;

  external_gc_string e1 = s2;
  cout << "'" << e1 << "'" << endl;
  external_gc_string e2 = std::move(s2);
  cout << "'" << e2 << "'" << endl;
  cout << "'" << s2 << "'" << endl;
  s2 = std::move(e2);
  cout << "'" << e2 << "'" << endl;
  cout << "'" << s2 << "'" << endl;
  
}

namespace test_gc {

  void foo() {
    gc_string s1;
    gc_string s2 = "Hi there";
    s1 = s2;
    gc_string s3{s2,4};

    cout << s3 << endl;
    s3.insert(2, 1, 'a');
    s3.insert(5, "Testing");

    string ss = "Standard";
    string s4{ss};
    s4.insert(2, ss);
    s4.erase(6);
    s4.erase(s4.begin()+2, s4.begin()+5);
    
    cin >> s3;

    std::string x = "Hi";

    bool test = s1 == s2;
    test = s1 == nullptr;
    test = s1 == "Hi";
    test = s1 == x;

    gc_string s7 = x + s1 + "Foo";

    cout << test;
    
  }
  
}
  
