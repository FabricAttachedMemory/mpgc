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

#include "mpgc/gc_desc.h"

#include <iostream>
#include <algorithm>

using namespace mpgc;
using namespace std;

void process(string s) {
  transform(s.begin(), s.end(), s.begin(), ::toupper);
  if (s.length() >=2 && s[0] == '0' && s[1] == 'X') {
    s = s.substr(2);
  }
  uint64_t n;
  istringstream(s) >> hex >> n;
  s = "0x"+s;
  gc_descriptor::trace_desc(n, s.data());
}

template <typename Iter>
void loop(Iter from, Iter to) {
  for_each(from, to, process);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    loop(argv+1, argv+argc);
  } else {
    loop(istream_iterator<string>(cin),
         istream_iterator<string>());
  }
}
  