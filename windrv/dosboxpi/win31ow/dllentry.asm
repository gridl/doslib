
section text class=CODE

extern MiniLibMain_

; NTS we don't give a crap about the module handle, etc. we care about passing control to C and returning the result
global __DLLstart_
..start:
__DLLstart_:
    push        cs
    call        MiniLibMain_
    retf

