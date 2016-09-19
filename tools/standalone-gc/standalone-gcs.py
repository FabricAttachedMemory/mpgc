#!/usr/bin/env python3
##
#
#  Multi Process Garbage Collector
#  Copyright © 2016 Hewlett Packard Enterprise Development Company LP.
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#  As an exception, the copyright holders of this Library grant you permission
#  to (i) compile an Application with the Library, and (ii) distribute the 
#  Application containing code generated by the Library and added to the 
#  Application during this compilation process under terms of your choice, 
#  provided you also meet the terms and conditions of the Application license.
#



import subprocess
import sys

def main():
	procStr = "./standalone-gc"
	workers = []
	for i in range(int(sys.argv[1])):
		workers.append(subprocess.Popen(procStr.split()))
	for w in workers:
		w.wait()
	sys.exit(0)

if __name__ == '__main__':
	main()