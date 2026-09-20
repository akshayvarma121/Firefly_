* BUG COMMENT: Created to catch a bug where the solver would cycle or return invalid results 
* instead of reporting an explicit UNBOUNDED status.
* 
* Maximize x + y (Minimize -x - y)
* Subject to:
*   x - y <= 5
*   x, y >= 0
NAME          UNBND
ROWS
 N  OBJ
 L  R1
COLUMNS
    X         OBJ       -1.0
    X         R1        1.0
    Y         OBJ       -1.0
    Y         R1        -1.0
RHS
    RHS1      R1        5.0
BOUNDS
ENDATA
