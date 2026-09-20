* BUG COMMENT: Created to catch a bug where MARKER lines in MPS files (used for specifying integer variables)
* were parsed as actual variables instead of being ignored or handled as integrality markers.
NAME          TEST2
ROWS
 N  OBJ
 E  R1
 L  R2
COLUMNS
    MARK0000  'MARKER'                 'INTORG'
    X1        OBJ       1.0
    X1        R1        1.0
    X2        R1        2.0
    X2        R2        1.0
    MARK0001  'MARKER'                 'INTEND'
RHS
    RHS1      R1        5.0
    RHS1      R2        20.0
BOUNDS
 FR BND       X1
ENDATA
