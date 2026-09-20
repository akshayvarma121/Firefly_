* BUG COMMENT: Created to catch a crash when parsing an MPS file containing non-numeric strings 
* in the values column (e.g., 'NOT_A_NUMBER'). The parser should handle this gracefully and error out instead of crashing.
NAME          BROKEN
ROWS
 N  OBJ
 L  CON1
COLUMNS
    X         OBJ       NOT_A_NUMBER
    X         CON1      2.5
RHS
    RHS1      CON1      10.0
BOUNDS
ENDATA
