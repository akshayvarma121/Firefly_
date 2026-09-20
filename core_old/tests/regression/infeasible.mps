* BUG COMMENT: Created to catch a bug where the solver would silently return an invalid result or fallback 
* instead of reporting an explicit INFEASIBLE status.
* 
* Maximize x + y (Minimize -x - y)
* Subject to:
*   x + y <= 5
*   x + y >= 10
*   x, y >= 0
NAME          INFEAS
ROWS
 N  OBJ
 L  R1
 G  R2
COLUMNS
    X         OBJ       -1.0
    X         R1        1.0
    X         R2        1.0
    Y         OBJ       -1.0
    Y         R1        1.0
    Y         R2        1.0
RHS
    RHS1      R1        5.0
    RHS1      R2        10.0
BOUNDS
ENDATA
