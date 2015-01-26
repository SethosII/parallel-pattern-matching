parallel-pattern-matching
=========================

General
-------

This project aims to implement a simple pattern matching in parallel.

Purpose
-------

The program should decide whether there is no, one or more then one "black" rectangle in a matrix off "black" and "white" fields. If there is exactly one "black" rectangle it also returns the coordinates of this rectangle (with zero-based indexing).

Usage
-----

The matrix to search in is defined by a configuration file. Such a file is then passed to the program as a parameter.

An example can be seen here:
```
10 20
2
0 0 0 9 19
1 3 5 7 14
```

The first line contains the number of rows and columns of the matrix. Afterwards is the number of rules defined. The rules consist of five values and describe a rectangle within the matrix. The first value denotes the "color": 0 for "white", 1 for "black" and 2 for toggle. Toggle switches a "white" field to a "black" field and vice versa. The other four values describe the rectangle: the first two denote the upper left angle and the last two the lower right angle. Both are inclusive. This file can now be passed to the program as parameter.

Example output (`mpirun -np 1 ./mustererkennung -f config.txt -v`):
```
Starting
Config:
10 20
2
0 0 0 9 19 
1 3 5 7 14 
Rectangle:
--------------------
--------------------
--------------------
-----##########-----
-----##########-----
-----##########-----
-----##########-----
-----##########-----
--------------------
--------------------
Time elapsed: 0.060933 s
Final result:
1 3 5 7 14 
One black rectangle!
Coordinates:
3 5 7 14
Finished
```

At the beginning the program prints the read values. Afterwards is the resulting matrix shown. "White" fields are represented by dashes and "black" fields by hashes. Then it prints the time needed for the computation. Afterwards is the result given as five values. The first value is the "big" result: 0 for no "black" rectangle, 1 for exactly one "black" rectangle and 2 for more then one "black" rectangle. If the result is 1 then the four other values denote the location of the only "black" rectangle in the matrix exactly as it would be described in a configuration file. Finally the result is shown in a more human-readable form.
