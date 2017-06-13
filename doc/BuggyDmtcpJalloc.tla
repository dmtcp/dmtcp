--------------------------- MODULE BuggyDmtcpJalloc ----------------------------

(******************************************************************************)
(* This model shows a subtle bug in the current JAlloc memory allocator.      *)
(*                                                                            *)
(* Manifestation of the bug:                                                  *)
(*  THREAD-1                 THREAD-2                                         *)
(* item = _root                                                               *)
(*                      allocate(item)                                        *)
(*                      // grabs _root == item, and trashes next ptr)         *)
(* tmp = item.next                                                            *)
(* // tmp is now garbage, due to THREAD-2                                     *)
(*                      deallocate(item)                                      *)
(*  compare_and_swap(_root, item, tmp)                                        *)
(* // _root becomes garbage                                                   *)
(*                                                                            *)
(* If the bug is not immediately detected, then later some thread can do:     *)
(*   deallocate(item2)                                                        *)
(* Now, _root will be valid, but somewhere in the linked list                 *)
(* an item.next will have a garbage value.                                    *)
(*                                                                            *)
(* If, instead of deallocate(), we did another allocate(), then we will see:  *)
(*   _root was garbage and "item = _root" creates a garbage value for item.   *)
(*   After this, "item.next" will segfault.                                   *)
(*                                                                            *)
(* The bug can be observed by running with the following model parameters and *)
(* and checking for the temporal invariant: [](Root \in HeapAddresses)        *)
(*                                                                            *)
(*   - NAllocs    = 1                                                         *)
(*   - Values     = {v_1, v_2, v_3, v_4, v_5}                                 *)
(*   - Addresses  = {a_1, a_2, a_3, a_4, a_5}                                 *)
(*   - Procs      = {p_1, p_2}                                                *)
(*                                                                            *)
(******************************************************************************)

EXTENDS Naturals, Sequences, TLC, FiniteSets

\* Parameters of the spec
CONSTANTS Values,   \* The set of all values that can be enqueued
          Address,  \* The set of heap addresses
          Procs,    \* The set of all processes (threads)
          NAllocs   \* Num of allocs/deallocs per process (threads)

(****************************** Allocator Model *******************************)

\* NOTE: CHOOSE is deterministic; it will pick the same value every time.
Pick(S) == CHOOSE s \in S : TRUE

\* null is an address not in the Address set
null == CHOOSE n: n \notin Address

\* A heap address is either a valid address or null
HeapAddresses == Address \cup {null}

\* A node is a two-tuple: <address, value>
Node == [next : HeapAddresses, data : Values]

\* Some dummy init node
InitNode ==  [next |-> null,  data |-> null]

(*
--algorithm LockFreeStack
(* rVal: P x I -> Address,
     where P is a process, I is an Integer (representing the I'th allocation),
     and Address is an address from the Heap                                  *)
(* allocIdx: P -> I,
     where P is a processes, I is an Integer (representing the number of
     allocations)                                                             *)
variables
  \* Global barrier to ensure no process can proceed before initialization.
  initialized = FALSE,
  \* Global lock to ensure only one process gets to do the initialization.
  initLock = FALSE,
  \* Mapping of addresses to nodes
  ListOfNodes = [a \in HeapAddresses |-> IF a = null THEN null ELSE InitNode],
  FreeList = Address,
  Root = null,
  \* TLA+ does not allow return values from functions. The following
  \* process-specific data structures are used to keep track of returned values.
  rVal = [p \in Procs |-> [x \in 1..NAllocs |-> HeapAddresses]],
  allocIdx = [p \in Procs |-> 0];

\* This models the atomic compare-and-swap instruction
macro CAS(result, addr1, old1, new1)
begin
  if ( (addr1 = old1) ) then
    addr1 := new1;
    result := TRUE;
  else
    result := FALSE;
  end if;
end macro;

macro New(result)
begin
  if (FreeList # {}) then
    result := Pick(FreeList);
    FreeList := FreeList \ {result};
  else  result := null
  end if;
end macro;

\* While this is invoked by all processes to ensure the linked list-based stack
\* is implemented correctly, only one process is allowed to do the
\* initialization.
procedure CommonInit()
variables idxInit, casResultInit;
begin
I1:   CAS(casResultInit, initLock, FALSE, TRUE);
I2:   if (casResultInit) then
L3:      New(idxInit);
L4:      while (idxInit # null) do
L5:        ListOfNodes[idxInit].next := Root;
L6:        ListOfNodes[idxInit].data := Pick(Values);
L7:        Root := idxInit;
L8:        New(idxInit);
         end while;
L9:      initialized := TRUE;
L10:     return;
       end if;
L11:  return;
end procedure;

(* For the given process, returns a pointer to a freeblock in the heap. This
   is equivalent of a POP operation on a stack and models the following code
   from JAlloc.
    void *allocate()
    {
      FreeItem *item = NULL;
      do {
        // if (_root == NULL) {
        //   expand();
        // }

        item = _root;
      } while (// !item ||
               !__sync_bool_compare_and_swap(&_root, item, item->next));

      item->next = NULL;
      return item;
    }
*)
procedure Allocate(pr, idx)
variables itemAlloc, temp, casResultAlloc;
begin
A0: while (TRUE) do
A1:   itemAlloc := Root;
A2:   temp := ListOfNodes[itemAlloc].next;
A3:   CAS(casResultAlloc, Root, itemAlloc, temp);
A4:   if (casResultAlloc) then
A5:     rVal[pr][idx] := itemAlloc;
A6:     ListOfNodes[itemAlloc].next := null;
A7:     return;
      end if;
    end while;
A8: return;
end procedure;

(* For the given process and address, adds the pointer back to the list of free
   nodes. This is equivalent of a PUSH operation on a stack and models the
   following code from JAlloc.
    void deallocate(void *ptr)
    {
      if (ptr == NULL) { return; }
      FreeItem *item = static_cast<FreeItem *>(ptr);
      do {
        item->next = _root;
      } while (!__sync_bool_compare_and_swap(&_root, item->next, item));
    }
*)
procedure Deallocate(pr, ptr, idx)
variables casResultDealloc, item;
begin
D1: if ptr = null then return; end if;
D2: item := ptr;
D3: while (TRUE) do
D4:   ListOfNodes[item].next := Root;
D5:   CAS(casResultDealloc, Root, ListOfNodes[item].next, item);
D6:   if (casResultDealloc) then
D7:     rVal[pr][idx] :=  null;
D8:     return;
      end if;
    end while;
D9: return;
end procedure;

\* Initialize process-specific data
procedure ProcInit(pr)
variables i = 1;
begin
PI1: allocIdx[pr] := 0;
PI2: while i <= NAllocs do
PI3:   rVal[pr][i] := null;
PI4:   i := i + 1;
    end while;
PI5: return;
end procedure;

\* Simulate a process writing to its allocated buffers
procedure ProcWrite(pr)
variables j = 1;
begin
PW1: while j <= NAllocs do
PW2:   if rVal[pr][j] # null then
         \* Trash the next pointer
PW3:     ListOfNodes[rVal[pr][j]].next := 17;
      end if;
PW4:   j := j + 1;
    end while;
PW5: return;
end procedure;

\* Each process executes deterministically:
\*   it first allocates (pops) NAlloc times,
\*   then writes to each of the NAlloc buffers, and
\*   finally deallocates (pushes) the NAlloc buffers.
process p \in Procs
begin
  P0: call CommonInit();
  P1: await initialized;
  P2: assert Root # null;
  \* Init
  P3: call ProcInit(self);
  \* Pop from the stack
  P4: while allocIdx[self] < NAllocs do
  P5:   allocIdx[self] := allocIdx[self] + 1;
  P6:   call Allocate(self, allocIdx[self]);
      end while;
  \* Write to the allocated buffers
  P7: call ProcWrite(self);
  \* Push back to the stack
  P8: while allocIdx[self] > 0 do
  P9:   call Deallocate(self, rVal[self][NAllocs - allocIdx[self] + 1],
                        NAllocs - allocIdx[self] + 1);
  P10:  allocIdx[self] := allocIdx[self] - 1;
       end while;
end process;

end algorithm *)
\* BEGIN TRANSLATION
\* Parameter pr of procedure Allocate at line 143 col 20 changed to pr_
\* Parameter idx of procedure Allocate at line 143 col 24 changed to idx_
\* Parameter pr of procedure Deallocate at line 171 col 22 changed to pr_D
\* Parameter pr of procedure ProcInit at line 188 col 20 changed to pr_P
CONSTANT defaultInitValue
VARIABLES initialized, initLock, ListOfNodes, FreeList, Root, rVal, allocIdx,
          pc, stack, idxInit, casResultInit, pr_, idx_, itemAlloc, temp,
          casResultAlloc, pr_D, ptr, idx, casResultDealloc, item, pr_P, i, pr,
          j

vars == << initialized, initLock, ListOfNodes, FreeList, Root, rVal, allocIdx,
           pc, stack, idxInit, casResultInit, pr_, idx_, itemAlloc, temp,
           casResultAlloc, pr_D, ptr, idx, casResultDealloc, item, pr_P, i,
           pr, j >>

ProcSet == (Procs)

Init == (* Global variables *)
        /\ initialized = FALSE
        /\ initLock = FALSE
        /\ ListOfNodes = [a \in HeapAddresses |-> IF a = null THEN null ELSE InitNode]
        /\ FreeList = Address
        /\ Root = null
        /\ rVal = [p \in Procs |-> [x \in 1..NAllocs |-> HeapAddresses]]
        /\ allocIdx = [p \in Procs |-> 0]
        (* Procedure CommonInit *)
        /\ idxInit = [ self \in ProcSet |-> defaultInitValue]
        /\ casResultInit = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure Allocate *)
        /\ pr_ = [ self \in ProcSet |-> defaultInitValue]
        /\ idx_ = [ self \in ProcSet |-> defaultInitValue]
        /\ itemAlloc = [ self \in ProcSet |-> defaultInitValue]
        /\ temp = [ self \in ProcSet |-> defaultInitValue]
        /\ casResultAlloc = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure Deallocate *)
        /\ pr_D = [ self \in ProcSet |-> defaultInitValue]
        /\ ptr = [ self \in ProcSet |-> defaultInitValue]
        /\ idx = [ self \in ProcSet |-> defaultInitValue]
        /\ casResultDealloc = [ self \in ProcSet |-> defaultInitValue]
        /\ item = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure ProcInit *)
        /\ pr_P = [ self \in ProcSet |-> defaultInitValue]
        /\ i = [ self \in ProcSet |-> 1]
        (* Procedure ProcWrite *)
        /\ pr = [ self \in ProcSet |-> defaultInitValue]
        /\ j = [ self \in ProcSet |-> 1]
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> "P0"]

I1(self) == /\ pc[self] = "I1"
            /\ IF ( (initLock = FALSE) )
                  THEN /\ initLock' = TRUE
                       /\ casResultInit' = [casResultInit EXCEPT ![self] = TRUE]
                  ELSE /\ casResultInit' = [casResultInit EXCEPT ![self] = FALSE]
                       /\ UNCHANGED initLock
            /\ pc' = [pc EXCEPT ![self] = "I2"]
            /\ UNCHANGED << initialized, ListOfNodes, FreeList, Root, rVal,
                            allocIdx, stack, idxInit, pr_, idx_, itemAlloc,
                            temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

I2(self) == /\ pc[self] = "I2"
            /\ IF (casResultInit[self])
                  THEN /\ pc' = [pc EXCEPT ![self] = "L3"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "L11"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

L3(self) == /\ pc[self] = "L3"
            /\ IF (FreeList # {})
                  THEN /\ idxInit' = [idxInit EXCEPT ![self] = Pick(FreeList)]
                       /\ FreeList' = FreeList \ {idxInit'[self]}
                  ELSE /\ idxInit' = [idxInit EXCEPT ![self] = null]
                       /\ UNCHANGED FreeList
            /\ pc' = [pc EXCEPT ![self] = "L4"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, Root, rVal,
                            allocIdx, stack, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

L4(self) == /\ pc[self] = "L4"
            /\ IF (idxInit[self] # null)
                  THEN /\ pc' = [pc EXCEPT ![self] = "L5"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "L9"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

L5(self) == /\ pc[self] = "L5"
            /\ ListOfNodes' = [ListOfNodes EXCEPT ![idxInit[self]].next = Root]
            /\ pc' = [pc EXCEPT ![self] = "L6"]
            /\ UNCHANGED << initialized, initLock, FreeList, Root, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

L6(self) == /\ pc[self] = "L6"
            /\ ListOfNodes' = [ListOfNodes EXCEPT ![idxInit[self]].data = Pick(Values)]
            /\ pc' = [pc EXCEPT ![self] = "L7"]
            /\ UNCHANGED << initialized, initLock, FreeList, Root, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

L7(self) == /\ pc[self] = "L7"
            /\ Root' = idxInit[self]
            /\ pc' = [pc EXCEPT ![self] = "L8"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

L8(self) == /\ pc[self] = "L8"
            /\ IF (FreeList # {})
                  THEN /\ idxInit' = [idxInit EXCEPT ![self] = Pick(FreeList)]
                       /\ FreeList' = FreeList \ {idxInit'[self]}
                  ELSE /\ idxInit' = [idxInit EXCEPT ![self] = null]
                       /\ UNCHANGED FreeList
            /\ pc' = [pc EXCEPT ![self] = "L4"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, Root, rVal,
                            allocIdx, stack, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

L9(self) == /\ pc[self] = "L9"
            /\ initialized' = TRUE
            /\ pc' = [pc EXCEPT ![self] = "L10"]
            /\ UNCHANGED << initLock, ListOfNodes, FreeList, Root, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

L10(self) == /\ pc[self] = "L10"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ idxInit' = [idxInit EXCEPT ![self] = Head(stack[self]).idxInit]
             /\ casResultInit' = [casResultInit EXCEPT ![self] = Head(stack[self]).casResultInit]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, i, pr, j >>

L11(self) == /\ pc[self] = "L11"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ idxInit' = [idxInit EXCEPT ![self] = Head(stack[self]).idxInit]
             /\ casResultInit' = [casResultInit EXCEPT ![self] = Head(stack[self]).casResultInit]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, i, pr, j >>

CommonInit(self) == I1(self) \/ I2(self) \/ L3(self) \/ L4(self)
                       \/ L5(self) \/ L6(self) \/ L7(self) \/ L8(self)
                       \/ L9(self) \/ L10(self) \/ L11(self)

A0(self) == /\ pc[self] = "A0"
            /\ pc' = [pc EXCEPT ![self] = "A1"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

A1(self) == /\ pc[self] = "A1"
            /\ itemAlloc' = [itemAlloc EXCEPT ![self] = Root]
            /\ pc' = [pc EXCEPT ![self] = "A2"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

A2(self) == /\ pc[self] = "A2"
            /\ temp' = [temp EXCEPT ![self] = ListOfNodes[itemAlloc[self]].next]
            /\ pc' = [pc EXCEPT ![self] = "A3"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

A3(self) == /\ pc[self] = "A3"
            /\ IF ( (Root = itemAlloc[self]) )
                  THEN /\ Root' = temp[self]
                       /\ casResultAlloc' = [casResultAlloc EXCEPT ![self] = TRUE]
                  ELSE /\ casResultAlloc' = [casResultAlloc EXCEPT ![self] = FALSE]
                       /\ Root' = Root
            /\ pc' = [pc EXCEPT ![self] = "A4"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, pr_D, ptr, idx, casResultDealloc,
                            item, pr_P, i, pr, j >>

A4(self) == /\ pc[self] = "A4"
            /\ IF (casResultAlloc[self])
                  THEN /\ pc' = [pc EXCEPT ![self] = "A5"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "A0"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

A5(self) == /\ pc[self] = "A5"
            /\ rVal' = [rVal EXCEPT ![pr_[self]][idx_[self]] = itemAlloc[self]]
            /\ pc' = [pc EXCEPT ![self] = "A6"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

A6(self) == /\ pc[self] = "A6"
            /\ ListOfNodes' = [ListOfNodes EXCEPT ![itemAlloc[self]].next = null]
            /\ pc' = [pc EXCEPT ![self] = "A7"]
            /\ UNCHANGED << initialized, initLock, FreeList, Root, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

A7(self) == /\ pc[self] = "A7"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ itemAlloc' = [itemAlloc EXCEPT ![self] = Head(stack[self]).itemAlloc]
            /\ temp' = [temp EXCEPT ![self] = Head(stack[self]).temp]
            /\ casResultAlloc' = [casResultAlloc EXCEPT ![self] = Head(stack[self]).casResultAlloc]
            /\ pr_' = [pr_ EXCEPT ![self] = Head(stack[self]).pr_]
            /\ idx_' = [idx_ EXCEPT ![self] = Head(stack[self]).idx_]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

A8(self) == /\ pc[self] = "A8"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ itemAlloc' = [itemAlloc EXCEPT ![self] = Head(stack[self]).itemAlloc]
            /\ temp' = [temp EXCEPT ![self] = Head(stack[self]).temp]
            /\ casResultAlloc' = [casResultAlloc EXCEPT ![self] = Head(stack[self]).casResultAlloc]
            /\ pr_' = [pr_ EXCEPT ![self] = Head(stack[self]).pr_]
            /\ idx_' = [idx_ EXCEPT ![self] = Head(stack[self]).idx_]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

Allocate(self) == A0(self) \/ A1(self) \/ A2(self) \/ A3(self) \/ A4(self)
                     \/ A5(self) \/ A6(self) \/ A7(self) \/ A8(self)

D1(self) == /\ pc[self] = "D1"
            /\ IF ptr[self] = null
                  THEN /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                       /\ casResultDealloc' = [casResultDealloc EXCEPT ![self] = Head(stack[self]).casResultDealloc]
                       /\ item' = [item EXCEPT ![self] = Head(stack[self]).item]
                       /\ pr_D' = [pr_D EXCEPT ![self] = Head(stack[self]).pr_D]
                       /\ ptr' = [ptr EXCEPT ![self] = Head(stack[self]).ptr]
                       /\ idx' = [idx EXCEPT ![self] = Head(stack[self]).idx]
                       /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "D2"]
                       /\ UNCHANGED << stack, pr_D, ptr, idx, casResultDealloc,
                                       item >>
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_P, i, pr, j >>

D2(self) == /\ pc[self] = "D2"
            /\ item' = [item EXCEPT ![self] = ptr[self]]
            /\ pc' = [pc EXCEPT ![self] = "D3"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, pr_P, i, pr, j >>

D3(self) == /\ pc[self] = "D3"
            /\ pc' = [pc EXCEPT ![self] = "D4"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

D4(self) == /\ pc[self] = "D4"
            /\ ListOfNodes' = [ListOfNodes EXCEPT ![item[self]].next = Root]
            /\ pc' = [pc EXCEPT ![self] = "D5"]
            /\ UNCHANGED << initialized, initLock, FreeList, Root, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

D5(self) == /\ pc[self] = "D5"
            /\ IF ( (Root = (ListOfNodes[item[self]].next)) )
                  THEN /\ Root' = item[self]
                       /\ casResultDealloc' = [casResultDealloc EXCEPT ![self] = TRUE]
                  ELSE /\ casResultDealloc' = [casResultDealloc EXCEPT ![self] = FALSE]
                       /\ Root' = Root
            /\ pc' = [pc EXCEPT ![self] = "D6"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, rVal,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            item, pr_P, i, pr, j >>

D6(self) == /\ pc[self] = "D6"
            /\ IF (casResultDealloc[self])
                  THEN /\ pc' = [pc EXCEPT ![self] = "D7"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "D3"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

D7(self) == /\ pc[self] = "D7"
            /\ rVal' = [rVal EXCEPT ![pr_D[self]][idx[self]] = null]
            /\ pc' = [pc EXCEPT ![self] = "D8"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            allocIdx, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

D8(self) == /\ pc[self] = "D8"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ casResultDealloc' = [casResultDealloc EXCEPT ![self] = Head(stack[self]).casResultDealloc]
            /\ item' = [item EXCEPT ![self] = Head(stack[self]).item]
            /\ pr_D' = [pr_D EXCEPT ![self] = Head(stack[self]).pr_D]
            /\ ptr' = [ptr EXCEPT ![self] = Head(stack[self]).ptr]
            /\ idx' = [idx EXCEPT ![self] = Head(stack[self]).idx]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_P, i, pr, j >>

D9(self) == /\ pc[self] = "D9"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ casResultDealloc' = [casResultDealloc EXCEPT ![self] = Head(stack[self]).casResultDealloc]
            /\ item' = [item EXCEPT ![self] = Head(stack[self]).item]
            /\ pr_D' = [pr_D EXCEPT ![self] = Head(stack[self]).pr_D]
            /\ ptr' = [ptr EXCEPT ![self] = Head(stack[self]).ptr]
            /\ idx' = [idx EXCEPT ![self] = Head(stack[self]).idx]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_P, i, pr, j >>

Deallocate(self) == D1(self) \/ D2(self) \/ D3(self) \/ D4(self)
                       \/ D5(self) \/ D6(self) \/ D7(self) \/ D8(self)
                       \/ D9(self)

PI1(self) == /\ pc[self] = "PI1"
             /\ allocIdx' = [allocIdx EXCEPT ![pr_P[self]] = 0]
             /\ pc' = [pc EXCEPT ![self] = "PI2"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, stack, idxInit, casResultInit, pr_,
                             idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                             idx, casResultDealloc, item, pr_P, i, pr, j >>

PI2(self) == /\ pc[self] = "PI2"
             /\ IF i[self] <= NAllocs
                   THEN /\ pc' = [pc EXCEPT ![self] = "PI3"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "PI5"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, stack, idxInit,
                             casResultInit, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, i, pr, j >>

PI3(self) == /\ pc[self] = "PI3"
             /\ rVal' = [rVal EXCEPT ![pr_P[self]][i[self]] = null]
             /\ pc' = [pc EXCEPT ![self] = "PI4"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, allocIdx, stack, idxInit, casResultInit,
                             pr_, idx_, itemAlloc, temp, casResultAlloc, pr_D,
                             ptr, idx, casResultDealloc, item, pr_P, i, pr, j >>

PI4(self) == /\ pc[self] = "PI4"
             /\ i' = [i EXCEPT ![self] = i[self] + 1]
             /\ pc' = [pc EXCEPT ![self] = "PI2"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, stack, idxInit,
                             casResultInit, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, pr, j >>

PI5(self) == /\ pc[self] = "PI5"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ i' = [i EXCEPT ![self] = Head(stack[self]).i]
             /\ pr_P' = [pr_P EXCEPT ![self] = Head(stack[self]).pr_P]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, idxInit, casResultInit, pr_,
                             idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                             idx, casResultDealloc, item, pr, j >>

ProcInit(self) == PI1(self) \/ PI2(self) \/ PI3(self) \/ PI4(self)
                     \/ PI5(self)

PW1(self) == /\ pc[self] = "PW1"
             /\ IF j[self] <= NAllocs
                   THEN /\ pc' = [pc EXCEPT ![self] = "PW2"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "PW5"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, stack, idxInit,
                             casResultInit, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, i, pr, j >>

PW2(self) == /\ pc[self] = "PW2"
             /\ IF rVal[pr[self]][j[self]] # null
                   THEN /\ pc' = [pc EXCEPT ![self] = "PW3"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "PW4"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, stack, idxInit,
                             casResultInit, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, i, pr, j >>

PW3(self) == /\ pc[self] = "PW3"
             /\ ListOfNodes' = [ListOfNodes EXCEPT ![rVal[pr[self]][j[self]]].next = 17]
             /\ pc' = [pc EXCEPT ![self] = "PW4"]
             /\ UNCHANGED << initialized, initLock, FreeList, Root, rVal,
                             allocIdx, stack, idxInit, casResultInit, pr_,
                             idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                             idx, casResultDealloc, item, pr_P, i, pr, j >>

PW4(self) == /\ pc[self] = "PW4"
             /\ j' = [j EXCEPT ![self] = j[self] + 1]
             /\ pc' = [pc EXCEPT ![self] = "PW1"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, stack, idxInit,
                             casResultInit, pr_, idx_, itemAlloc, temp,
                             casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                             item, pr_P, i, pr >>

PW5(self) == /\ pc[self] = "PW5"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ j' = [j EXCEPT ![self] = Head(stack[self]).j]
             /\ pr' = [pr EXCEPT ![self] = Head(stack[self]).pr]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, allocIdx, idxInit, casResultInit, pr_,
                             idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                             idx, casResultDealloc, item, pr_P, i >>

ProcWrite(self) == PW1(self) \/ PW2(self) \/ PW3(self) \/ PW4(self)
                      \/ PW5(self)

P0(self) == /\ pc[self] = "P0"
            /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "CommonInit",
                                                     pc        |->  "P1",
                                                     idxInit   |->  idxInit[self],
                                                     casResultInit |->  casResultInit[self] ] >>
                                                 \o stack[self]]
            /\ idxInit' = [idxInit EXCEPT ![self] = defaultInitValue]
            /\ casResultInit' = [casResultInit EXCEPT ![self] = defaultInitValue]
            /\ pc' = [pc EXCEPT ![self] = "I1"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, pr_, idx_, itemAlloc, temp,
                            casResultAlloc, pr_D, ptr, idx, casResultDealloc,
                            item, pr_P, i, pr, j >>

P1(self) == /\ pc[self] = "P1"
            /\ initialized
            /\ pc' = [pc EXCEPT ![self] = "P2"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

P2(self) == /\ pc[self] = "P2"
            /\ Assert(Root # null,
                      "Failure of assertion at line 217, column 7.")
            /\ pc' = [pc EXCEPT ![self] = "P3"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

P3(self) == /\ pc[self] = "P3"
            /\ /\ pr_P' = [pr_P EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "ProcInit",
                                                        pc        |->  "P4",
                                                        i         |->  i[self],
                                                        pr_P      |->  pr_P[self] ] >>
                                                    \o stack[self]]
            /\ i' = [i EXCEPT ![self] = 1]
            /\ pc' = [pc EXCEPT ![self] = "PI1"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr, j >>

P4(self) == /\ pc[self] = "P4"
            /\ IF allocIdx[self] < NAllocs
                  THEN /\ pc' = [pc EXCEPT ![self] = "P5"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "P7"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

P5(self) == /\ pc[self] = "P5"
            /\ allocIdx' = [allocIdx EXCEPT ![self] = allocIdx[self] + 1]
            /\ pc' = [pc EXCEPT ![self] = "P6"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, stack, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i, pr, j >>

P6(self) == /\ pc[self] = "P6"
            /\ /\ idx_' = [idx_ EXCEPT ![self] = allocIdx[self]]
               /\ pr_' = [pr_ EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Allocate",
                                                        pc        |->  "P4",
                                                        itemAlloc |->  itemAlloc[self],
                                                        temp      |->  temp[self],
                                                        casResultAlloc |->  casResultAlloc[self],
                                                        pr_       |->  pr_[self],
                                                        idx_      |->  idx_[self] ] >>
                                                    \o stack[self]]
            /\ itemAlloc' = [itemAlloc EXCEPT ![self] = defaultInitValue]
            /\ temp' = [temp EXCEPT ![self] = defaultInitValue]
            /\ casResultAlloc' = [casResultAlloc EXCEPT ![self] = defaultInitValue]
            /\ pc' = [pc EXCEPT ![self] = "A0"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

P7(self) == /\ pc[self] = "P7"
            /\ /\ pr' = [pr EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "ProcWrite",
                                                        pc        |->  "P8",
                                                        j         |->  j[self],
                                                        pr        |->  pr[self] ] >>
                                                    \o stack[self]]
            /\ j' = [j EXCEPT ![self] = 1]
            /\ pc' = [pc EXCEPT ![self] = "PW1"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_D, ptr, idx,
                            casResultDealloc, item, pr_P, i >>

P8(self) == /\ pc[self] = "P8"
            /\ IF allocIdx[self] > 0
                  THEN /\ pc' = [pc EXCEPT ![self] = "P9"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "Done"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, stack, idxInit, casResultInit, pr_,
                            idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                            idx, casResultDealloc, item, pr_P, i, pr, j >>

P9(self) == /\ pc[self] = "P9"
            /\ /\ idx' = [idx EXCEPT ![self] = NAllocs - allocIdx[self] + 1]
               /\ pr_D' = [pr_D EXCEPT ![self] = self]
               /\ ptr' = [ptr EXCEPT ![self] = rVal[self][NAllocs - allocIdx[self] + 1]]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Deallocate",
                                                        pc        |->  "P10",
                                                        casResultDealloc |->  casResultDealloc[self],
                                                        item      |->  item[self],
                                                        pr_D      |->  pr_D[self],
                                                        ptr       |->  ptr[self],
                                                        idx       |->  idx[self] ] >>
                                                    \o stack[self]]
            /\ casResultDealloc' = [casResultDealloc EXCEPT ![self] = defaultInitValue]
            /\ item' = [item EXCEPT ![self] = defaultInitValue]
            /\ pc' = [pc EXCEPT ![self] = "D1"]
            /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList, Root,
                            rVal, allocIdx, idxInit, casResultInit, pr_, idx_,
                            itemAlloc, temp, casResultAlloc, pr_P, i, pr, j >>

P10(self) == /\ pc[self] = "P10"
             /\ allocIdx' = [allocIdx EXCEPT ![self] = allocIdx[self] - 1]
             /\ pc' = [pc EXCEPT ![self] = "P8"]
             /\ UNCHANGED << initialized, initLock, ListOfNodes, FreeList,
                             Root, rVal, stack, idxInit, casResultInit, pr_,
                             idx_, itemAlloc, temp, casResultAlloc, pr_D, ptr,
                             idx, casResultDealloc, item, pr_P, i, pr, j >>

p(self) == P0(self) \/ P1(self) \/ P2(self) \/ P3(self) \/ P4(self)
              \/ P5(self) \/ P6(self) \/ P7(self) \/ P8(self) \/ P9(self)
              \/ P10(self)

Next == (\E self \in ProcSet:  \/ CommonInit(self) \/ Allocate(self)
                               \/ Deallocate(self) \/ ProcInit(self)
                               \/ ProcWrite(self))
           \/ (\E self \in Procs: p(self))
           \/ (* Disjunct to prevent deadlock on termination *)
              ((\A self \in ProcSet: pc[self] = "Done") /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION
=============================================================================
