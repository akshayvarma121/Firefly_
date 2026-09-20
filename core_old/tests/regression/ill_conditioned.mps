* BUG COMMENT: Created to catch numerical instability bugs where large coefficient ratios 
* caused the solver to fail, stall, or return suboptimal solutions due to precision loss.
*
* Coefficients range from 1e-6 to 1e6.
NAME          ILLCOND
ROWS
 N  OBJ
 E  R1
 L  R2
COLUMNS
    X1        OBJ       1000000.0
    X1        R1        0.000001
    X1        R2        1000000.0
    X2        OBJ       0.000001
    X2        R1        1000000.0
    X2        R2        0.000001
RHS
    RHS1      R1        10.0
    RHS1      R2        10.0
BOUNDS
ENDATA
