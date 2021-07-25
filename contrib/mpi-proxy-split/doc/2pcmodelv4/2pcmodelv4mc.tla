---- MODULE 2pcmodelv4mc ----
EXTENDS 2pcmodelv4, TLC

UT_OFFSET_TMP == 1000
BarrierList_TMP == {1, 2}
Procs_TMP == {1, 2}
CkptThreads_TMP == {1 + UT_OFFSET, 2 + UT_OFFSET}
COORD_PROC_TMP == 3

-------------------------------------------------------------------------------
===============================================================================
