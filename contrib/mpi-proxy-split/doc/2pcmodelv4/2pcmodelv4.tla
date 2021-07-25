----------------------------- MODULE 2pcmodelv4 -----------------------------

EXTENDS Naturals, Sequences, TLC, FiniteSets

\* The model params are initialized in the cfg files.

\* Parameters of the spec
CONSTANTS Procs, CkptThreads, COORD_PROC,   \* The set of all processes (MPI ranks)
          IS_READY, PHASE_1, IN_CS, PHASE_2, CKPTED, IS_DONE, READY_FOR_CKPT,
          NONE, GET_STATUS, FREE_PASS, INTENT, CKPT,
          NO_RESPONSE,
          UT_OFFSET,
          BarrierList

\* Global definitions

\* An MPI_Comm object is a 2-tuple: <count, expected>
\*   "count" indicates the no. of entered ranks
\*   "expected" indicates the no. of ranks that are part of the communicator
MPI_Comm == [count |-> 0, expected |-> Cardinality(Procs)]

\* Note CHOOSE is deterministic
Pick(S) == CHOOSE s \in S : TRUE

\* Just an enum to indicate the phase
PHASE == {IS_READY, PHASE_1, IN_CS, PHASE_2, IS_DONE}

\* Client response
COORD_MSG_RESPONSE == PHASE \cup {NO_RESPONSE}

COORD_MSG == {NONE, GET_STATUS, FREE_PASS, INTENT, CKPT}

Mutex == [lockCount |-> 0, owner |-> 0]

UT(tid) == tid - UT_OFFSET

(*
--algorithm 2pcmodelv4

variables
  recvdCkptMsg = [p \in Procs |-> FALSE],    \* Indicates whether a ckpt msg (from the coord) was received
  recvdCkptSignal = [p \in Procs |-> FALSE], \* Indicates whether a ckpt signal was raised
  ckptPending = [p \in Procs |-> FALSE],     \* Used by the coord. to indicate intent for ckpting
  ckptingEnabled = [p \in Procs |-> TRUE],   \* Have we entered the barrier?
  freePass = [p \in Procs |-> FALSE],        \* Used by the coord. to allow a blocked rank to continue (until the next safe point)
  rankDone = [p \in Procs |-> FALSE],        \* Indicates if a rank has exited
  comms = [c \in BarrierList |-> MPI_Comm],  \* |BarrierList| communicator objects of type MPI_Comm
  ckptCount = 0,                             \* Indicates the number of ranks that have ckpted successfully
  ckptPendingMutex = Mutex,                  \* Lock around ckptPending
  inWrapper = [p \in Procs |-> FALSE],       \* Has user thread entered the wrapper?
  procState = [p \in Procs |-> IS_READY],    \* Process state; start with IS_READY
  coordMsg = [p \in Procs |-> NONE],         \* Request from coordinator to ckpt thread
  clientResponse = [p \in Procs |-> NO_RESPONSE], \* Response ckpt thread to coordinator
  ckptingEnabledMutex = Mutex,               \* Lock around ckptingEnabled
  coordDone = FALSE;

\* Some useful definitions.
define
SetOfReadyRanks(responses) == {r \in Procs: responses[r] \in {IS_READY}}
SetOfPendingRanks == {r \in Procs: ckptPending[r]}

ProcCount == Cardinality(Procs)
FinishedProcCount == Cardinality({r \in Procs: rankDone[r]})
AckCount == Cardinality({r \in Procs: ~(clientResponse[r] = NO_RESPONSE)})
Phase1Ranks(responses) == Cardinality({r \in Procs: responses[r] = PHASE_1})
Phase2Ranks(responses) == Cardinality({r \in Procs: responses[r] = PHASE_2})
CSRanksCount(responses) == Cardinality({r \in Procs: responses[r] = IN_CS})
DoneRanksCount(responses) == Cardinality({r \in Procs: responses[r] = IS_DONE})

ReadyRanksCount(responses) == Cardinality(SetOfReadyRanks(responses))
FinishedRanks(responses) == Cardinality({r \in responses: responses[r] = IS_DONE})

AllRanksDone == FinishedProcCount = ProcCount
AnyRankDone == FinishedProcCount > 0
AllRanksAlive == FinishedProcCount = 0
AllRanksReady(responses) == (ReadyRanksCount(responses) + Phase1Ranks(responses)) = ProcCount
                            \/ Phase2Ranks(responses) = ProcCount
AllRanksCkpted == ckptCount = ProcCount
AllRanksAckd(aliveRankCount) == AckCount = aliveRankCount

ProcInSaneState(pr) == procState[pr] \in {PHASE_1, IN_CS, PHASE_2, IS_DONE}
ProcInSafeState(pr) == procState[pr] \in {READY_FOR_CKPT, IN_CS, IS_DONE}
end define;

macro CAS(result, addr1, old1, new1) \* Compare-And-Swap instruction
begin
  if ( (addr1 = old1) ) then
    addr1 := new1;
    result := TRUE;
  else
    result := FALSE;
  end if;
end macro;

procedure Lock(pr, mutex)           \* Mutex lock; uses the CAS CPU instruction
variables tempL1, casResultLock;
begin
Lo0: while TRUE do
Lo1:  tempL1 := mutex.lockCount;
Lo2:  CAS(casResultLock, mutex.lockCount, 0, 1);
Lo3:  if (casResultLock /\ tempL1 = 0) then
        mutex.owner := pr;
Lo4:    goto Lo5;
      end if;
     end while;
Lo5: return;
end procedure;

procedure Unlock(pr, mutex)       \* Mutex unlock
begin
Un0: mutex.lockCount := 0;
Un1: mutex.owner := 0;
Un2: return;
end procedure;

procedure setCkptPending(pr)      \* Used by the ckpt thread to set ckptPending
begin
SCP1: call Lock(pr, ckptPendingMutex);
SCP2: ckptPending[pr] := TRUE;
SCP3: call Unlock(pr, ckptPendingMutex);
SCP4: return;
end procedure;

procedure enableDisableCkpting(pr, state) \* Used by the user thread to set or clear ckptingEnabled
begin
EC1: call Lock(pr, ckptingEnabledMutex);
EC2: ckptingEnabled[pr] := state;
EC3: call Unlock(pr, ckptingEnabledMutex);
EC4: return;
end procedure;

\* Represents any blocking MPI collective call
procedure MPI_Barrier(id, pr)
begin
B1: comms[id].count := comms[id].count + 1;
B2: await comms[id].count = comms[id].expected; \* We cannot reuse a barrier in this model
B3: return;
end procedure;

\* Stop and ack the ckpt-intent msg, and wait for a free-pass msg.
procedure stop(pr, phase)
begin
S1: assert ckptPending[pr];
S2: await freePass[pr]; freePass[pr] := FALSE;
\* If a ckpt intent was expressed and if ckpting is enabled, we wait
\* for an imminent ckpt msg from the coordinator
S3: if ckptPending[pr] /\ ckptingEnabled[pr] /\ recvdCkptMsg[pr] then
S4:   procState[pr] := READY_FOR_CKPT;
      await recvdCkptSignal[pr]; recvdCkptMsg[pr] := FALSE;
      call enterCkpt(pr);
    end if;
SN: return;
end procedure;

\* Ckpt handler for each rank
procedure enterCkpt(pr)
begin
C1: assert recvdCkptSignal[pr] /\ ckptingEnabled[pr];
C2: ckptCount := ckptCount + 1;
C3: await AllRanksCkpted; ckptPending[pr] := FALSE; recvdCkptSignal[pr] := FALSE; procState[pr] := CKPTED;
CN: return;
end procedure;

\* Execute MPI collective call
procedure doMpiCollective(comm, pr)
begin
CC1: call MPI_Barrier(comm, pr); \* We get out of barrier only if/when all peers enter
CC2: return;
end procedure

\* Send ckpt-intent msg to all ranks
procedure sendCkptIntentToAllRanks()
variables ProcList = Procs, i;
begin
CI1:  while ProcList # {} /\ FinishedProcCount = 0 do
        i := Pick(ProcList);
        ProcList := ProcList \ {i};
        coordMsg[i] := INTENT;
      end while;
CI2:  return;
end procedure;

\* Send ckpt msg to all ranks
procedure sendCkptMsgToAllRanks()
variables ProcList = Procs, i;
begin
SC1:  while ProcList # {} do
        i := Pick(ProcList);
        ProcList := ProcList \ {i};
        if FinishedProcCount = 0 then coordMsg[i] := CKPT; end if;
      end while;
SC2:  return;
end procedure;

\* Clear client responses
procedure clearResponses()
variables ProcList = Procs, i;
begin
CR1:  while ProcList # {} do
        i := Pick(ProcList);
        ProcList := ProcList \ {i};
        clientResponse[i] := NO_RESPONSE;
      end while;
CR2:  return;
end procedure;

\* Process client responses
procedure processResponses(resps)
variables ProcList = Procs, i;
begin
\* No rank in critical section
PR1: if CSRanksCount(resps) = 0 /\ AllRanksAlive then
       goto PRN;
     end if;
PR2: while ProcList # {} do
       i := Pick(ProcList);
       ProcList := ProcList \ {i};
PR3:   if resps[i] \in {PHASE_1, PHASE_2} then
         coordMsg[i] := FREE_PASS; \* Send free pass if rank is stuck in stop()
       end if;
PR4:   if resps[i] \in {IS_READY, IN_CS} then
         coordMsg[i] := GET_STATUS; \* Nothing to do; just send a regular get-status query
       end if;
     end while;
PRN: return;
end procedure;

\* Broadcast ckpt request to all clients
procedure bcastCkptingMsg()
variable CkptPending = TRUE, responses, aliveRankCount = ProcCount;
begin
BC1: call sendCkptIntentToAllRanks();
BC2: while ~AllRanksDone /\ CkptPending do
\* TODO: If any rank gets done, we should process that msg without waiting on other msgs from other ranks
BC3:   await AllRanksAckd(aliveRankCount) \/ AllRanksDone; \* Wait for responses from all clients
       responses := clientResponse;                        \* Save client responses to a local variable
       aliveRankCount := aliveRankCount - DoneRanksCount(responses); \* Update count of still alive ranks
BC4:   call clearResponses(); \* Clear msg queue so that the clients can move ahead
BC5:   if ~AllRanksDone /\ AllRanksReady(responses) then
BC6:     call sendCkptMsgToAllRanks();
BC7:     await AllRanksCkpted; CkptPending := FALSE;
BC8:     assert ckptCount > 0 => AllRanksCkpted
       else
BC9:     if ~AllRanksDone then
           call processResponses(responses); \* Process client responses
         end if;
       end if;
     end while;
BCN: return;
end procedure;

\* Inform coordinator of the given process state
procedure informCoord(pr, st)
begin
IC1: clientResponse[pr] := st;
IC2: await clientResponse[pr] = NO_RESPONSE \/ coordDone;
     return;
end procedure;

\* An MPI rank
process r \in Procs
variables comm = 1, isPending;
begin
P0: inWrapper[self] := TRUE;
P1: call Lock(self, ckptPendingMutex);
P2: isPending := ckptPending[self]; call Unlock(self, ckptPendingMutex);
P3: if isPending then
      procState[self] := PHASE_1; call stop(self, PHASE_1); \* We stop and acknowledge the ckpt intent msg
    end if;
P4: procState[self] := IN_CS; call enableDisableCkpting(self, FALSE);
P5: call doMpiCollective(comm, self);
P6: call enableDisableCkpting(self, TRUE);
P7: call Lock(self, ckptPendingMutex);
P8: isPending := ckptPending[self]; call Unlock(self, ckptPendingMutex);
P9: if isPending then
      procState[self] := PHASE_2;
      call stop(self, PHASE_2); \* We stop and acknowledge the ckpt intent msg, assuming we had missed it earlier
    end if;
PN: rankDone[self] := TRUE;
    inWrapper[self] := FALSE;
    procState[self] := IS_DONE;
end process;

\* Ckpt thread (1 per MPI rank)
process t \in CkptThreads
variable query, currState;
begin
T1: while ~rankDone[UT(self)] do
T2:   await ~(coordMsg[UT(self)] = NONE) \/ rankDone[UT(self)];
      query := coordMsg[UT(self)]; \* Save coordinator request to a local variable
      coordMsg[UT(self)] := NONE;  \* Clear shared variable
      if rankDone[UT(self)] then   \* Exit if our parent user thread ended
        goto T14;
      end if;
T3:   if query = INTENT then       \* Set ckpt pending, if the coord asked for it
        call setCkptPending(UT(self));
      end if;
\* We wait for the user thread to reach a "sane" state, before reporting back to
\* the coord
T4:   if query \in {INTENT, GET_STATUS}
         /\ inWrapper[UT(self)] then
T5:     await ProcInSaneState(UT(self));
      end if;
T6:   if query = CKPT then         \* Raise the ckpt signal, if we recvd a ckpt msg
T7:     assert inWrapper[UT(self)] => procState[UT(self)] \in {PHASE_1, PHASE_2, IS_READY};
        recvdCkptSignal[UT(self)] := TRUE;
        recvdCkptMsg[UT(self)] := TRUE;
        freePass[UT(self)] := TRUE; \* Set free pass to allow a thread stuck in stop() to move
T8:     await ProcInSafeState(UT(self)); \* Wait for the free pass to take effect
T9:     await procState[UT(self)] = CKPTED \/ rankDone[UT(self)]; \* Wait for user thread to finish ckpting
      end if;
T10:  if query = FREE_PASS then    \* Unblock user thread stuck in stop()
T11:    assert procState[UT(self)] \in {PHASE_1, PHASE_2};
        freePass[UT(self)] := TRUE;
        currState := procState[UT(self)];
\* NOTE: We cannot use ProcInSafeState here, since a user thread may transition
\* from PHASE_1 to PHASE_2 without us ever noticing. This check would then block
\* forever.
T12:    await ~(currState = procState[UT(self)]); \* Wait for a state transition.
      end if;
T13:  if ~rankDone[UT(self)] then  \* If the user thread is still alive, inform the coord of our state
        call informCoord(UT(self), procState[UT(self)]);
      end if;
     end while;
T14: call informCoord(UT(self), IS_DONE);
end process;

\* The DMTCP coordinator process
process coord = COORD_PROC
begin
\* TODO: Do this in a loop?
CO1: call bcastCkptingMsg();
CO2: coordDone := TRUE;
end process;

end algorithm *)
\* BEGIN TRANSLATION
\* Process variable comm of process r at line 262 col 11 changed to comm_
\* Procedure variable ProcList of procedure sendCkptIntentToAllRanks at line 173 col 11 changed to ProcList_
\* Procedure variable i of procedure sendCkptIntentToAllRanks at line 173 col 29 changed to i_
\* Procedure variable ProcList of procedure sendCkptMsgToAllRanks at line 185 col 11 changed to ProcList_s
\* Procedure variable i of procedure sendCkptMsgToAllRanks at line 185 col 29 changed to i_s
\* Procedure variable ProcList of procedure clearResponses at line 197 col 11 changed to ProcList_c
\* Procedure variable i of procedure clearResponses at line 197 col 29 changed to i_c
\* Parameter pr of procedure Lock at line 95 col 16 changed to pr_
\* Parameter mutex of procedure Lock at line 95 col 20 changed to mutex_
\* Parameter pr of procedure Unlock at line 109 col 18 changed to pr_U
\* Parameter pr of procedure setCkptPending at line 116 col 26 changed to pr_s
\* Parameter pr of procedure enableDisableCkpting at line 124 col 32 changed to pr_e
\* Parameter pr of procedure MPI_Barrier at line 133 col 27 changed to pr_M
\* Parameter pr of procedure stop at line 141 col 16 changed to pr_st
\* Parameter pr of procedure enterCkpt at line 156 col 21 changed to pr_en
\* Parameter pr of procedure doMpiCollective at line 165 col 33 changed to pr_d
CONSTANT defaultInitValue
VARIABLES recvdCkptMsg, recvdCkptSignal, ckptPending, ckptingEnabled,
          freePass, rankDone, comms, ckptCount, ckptPendingMutex, inWrapper,
          procState, coordMsg, clientResponse, ckptingEnabledMutex, coordDone,
          pc, stack

(* define statement *)
SetOfReadyRanks(responses) == {r \in Procs: responses[r] \in {IS_READY}}
SetOfPendingRanks == {r \in Procs: ckptPending[r]}

ProcCount == Cardinality(Procs)
FinishedProcCount == Cardinality({r \in Procs: rankDone[r]})
AckCount == Cardinality({r \in Procs: ~(clientResponse[r] = NO_RESPONSE)})
Phase1Ranks(responses) == Cardinality({r \in Procs: responses[r] = PHASE_1})
Phase2Ranks(responses) == Cardinality({r \in Procs: responses[r] = PHASE_2})
CSRanksCount(responses) == Cardinality({r \in Procs: responses[r] = IN_CS})
DoneRanksCount(responses) == Cardinality({r \in Procs: responses[r] = IS_DONE})

ReadyRanksCount(responses) == Cardinality(SetOfReadyRanks(responses))
FinishedRanks(responses) == Cardinality({r \in responses: responses[r] = IS_DONE})

AllRanksDone == FinishedProcCount = ProcCount
AnyRankDone == FinishedProcCount > 0
AllRanksAlive == FinishedProcCount = 0
AllRanksReady(responses) == (ReadyRanksCount(responses) + Phase1Ranks(responses)) = ProcCount
                            \/ Phase2Ranks(responses) = ProcCount
AllRanksCkpted == ckptCount = ProcCount
AllRanksAckd(aliveRankCount) == AckCount = aliveRankCount

ProcInSaneState(pr) == procState[pr] \in {PHASE_1, IN_CS, PHASE_2, IS_DONE}
ProcInSafeState(pr) == procState[pr] \in {READY_FOR_CKPT, IN_CS, IS_DONE}

VARIABLES pr_, mutex_, tempL1, casResultLock, pr_U, mutex, pr_s, pr_e, state,
          id, pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_, i_,
          ProcList_s, i_s, ProcList_c, i_c, resps, ProcList, i, CkptPending,
          responses, aliveRankCount, pr, st, comm_, isPending, query,
          currState

vars == << recvdCkptMsg, recvdCkptSignal, ckptPending, ckptingEnabled,
           freePass, rankDone, comms, ckptCount, ckptPendingMutex, inWrapper,
           procState, coordMsg, clientResponse, ckptingEnabledMutex,
           coordDone, pc, stack, pr_, mutex_, tempL1, casResultLock, pr_U,
           mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en, comm,
           pr_d, ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c, resps,
           ProcList, i, CkptPending, responses, aliveRankCount, pr, st, comm_,
           isPending, query, currState >>

ProcSet == (Procs) \cup (CkptThreads) \cup {COORD_PROC}

Init == (* Global variables *)
        /\ recvdCkptMsg = [p \in Procs |-> FALSE]
        /\ recvdCkptSignal = [p \in Procs |-> FALSE]
        /\ ckptPending = [p \in Procs |-> FALSE]
        /\ ckptingEnabled = [p \in Procs |-> TRUE]
        /\ freePass = [p \in Procs |-> FALSE]
        /\ rankDone = [p \in Procs |-> FALSE]
        /\ comms = [c \in BarrierList |-> MPI_Comm]
        /\ ckptCount = 0
        /\ ckptPendingMutex = Mutex
        /\ inWrapper = [p \in Procs |-> FALSE]
        /\ procState = [p \in Procs |-> IS_READY]
        /\ coordMsg = [p \in Procs |-> NONE]
        /\ clientResponse = [p \in Procs |-> NO_RESPONSE]
        /\ ckptingEnabledMutex = Mutex
        /\ coordDone = FALSE
        (* Procedure Lock *)
        /\ pr_ = [ self \in ProcSet |-> defaultInitValue]
        /\ mutex_ = [ self \in ProcSet |-> defaultInitValue]
        /\ tempL1 = [ self \in ProcSet |-> defaultInitValue]
        /\ casResultLock = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure Unlock *)
        /\ pr_U = [ self \in ProcSet |-> defaultInitValue]
        /\ mutex = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure setCkptPending *)
        /\ pr_s = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure enableDisableCkpting *)
        /\ pr_e = [ self \in ProcSet |-> defaultInitValue]
        /\ state = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure MPI_Barrier *)
        /\ id = [ self \in ProcSet |-> defaultInitValue]
        /\ pr_M = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure stop *)
        /\ pr_st = [ self \in ProcSet |-> defaultInitValue]
        /\ phase = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure enterCkpt *)
        /\ pr_en = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure doMpiCollective *)
        /\ comm = [ self \in ProcSet |-> defaultInitValue]
        /\ pr_d = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendCkptIntentToAllRanks *)
        /\ ProcList_ = [ self \in ProcSet |-> Procs]
        /\ i_ = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendCkptMsgToAllRanks *)
        /\ ProcList_s = [ self \in ProcSet |-> Procs]
        /\ i_s = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure clearResponses *)
        /\ ProcList_c = [ self \in ProcSet |-> Procs]
        /\ i_c = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure processResponses *)
        /\ resps = [ self \in ProcSet |-> defaultInitValue]
        /\ ProcList = [ self \in ProcSet |-> Procs]
        /\ i = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure bcastCkptingMsg *)
        /\ CkptPending = [ self \in ProcSet |-> TRUE]
        /\ responses = [ self \in ProcSet |-> defaultInitValue]
        /\ aliveRankCount = [ self \in ProcSet |-> ProcCount]
        (* Procedure informCoord *)
        /\ pr = [ self \in ProcSet |-> defaultInitValue]
        /\ st = [ self \in ProcSet |-> defaultInitValue]
        (* Process r *)
        /\ comm_ = [self \in Procs |-> 1]
        /\ isPending = [self \in Procs |-> defaultInitValue]
        (* Process t *)
        /\ query = [self \in CkptThreads |-> defaultInitValue]
        /\ currState = [self \in CkptThreads |-> defaultInitValue]
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> CASE self \in Procs -> "P0"
                                        [] self \in CkptThreads -> "T1"
                                        [] self = COORD_PROC -> "CO1"]

Lo0(self) == /\ pc[self] = "Lo0"
             /\ pc' = [pc EXCEPT ![self] = "Lo1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

Lo1(self) == /\ pc[self] = "Lo1"
             /\ tempL1' = [tempL1 EXCEPT ![self] = mutex_[self].lockCount]
             /\ pc' = [pc EXCEPT ![self] = "Lo2"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, pr, st,
                             comm_, isPending, query, currState >>

Lo2(self) == /\ pc[self] = "Lo2"
             /\ IF ( ((mutex_[self].lockCount) = 0) )
                   THEN /\ mutex_' = [mutex_ EXCEPT ![self].lockCount = 1]
                        /\ casResultLock' = [casResultLock EXCEPT ![self] = TRUE]
                   ELSE /\ casResultLock' = [casResultLock EXCEPT ![self] = FALSE]
                        /\ UNCHANGED mutex_
             /\ pc' = [pc EXCEPT ![self] = "Lo3"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, tempL1, pr_U, mutex, pr_s,
                             pr_e, state, id, pr_M, pr_st, phase, pr_en, comm,
                             pr_d, ProcList_, i_, ProcList_s, i_s, ProcList_c,
                             i_c, resps, ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

Lo3(self) == /\ pc[self] = "Lo3"
             /\ IF (casResultLock[self] /\ tempL1[self] = 0)
                   THEN /\ mutex_' = [mutex_ EXCEPT ![self].owner = pr_[self]]
                        /\ pc' = [pc EXCEPT ![self] = "Lo4"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "Lo0"]
                        /\ UNCHANGED mutex_
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, pr, st,
                             comm_, isPending, query, currState >>

Lo4(self) == /\ pc[self] = "Lo4"
             /\ pc' = [pc EXCEPT ![self] = "Lo5"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

Lo5(self) == /\ pc[self] = "Lo5"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ tempL1' = [tempL1 EXCEPT ![self] = Head(stack[self]).tempL1]
             /\ casResultLock' = [casResultLock EXCEPT ![self] = Head(stack[self]).casResultLock]
             /\ pr_' = [pr_ EXCEPT ![self] = Head(stack[self]).pr_]
             /\ mutex_' = [mutex_ EXCEPT ![self] = Head(stack[self]).mutex_]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

Lock(self) == Lo0(self) \/ Lo1(self) \/ Lo2(self) \/ Lo3(self) \/ Lo4(self)
                 \/ Lo5(self)

Un0(self) == /\ pc[self] = "Un0"
             /\ mutex' = [mutex EXCEPT ![self].lockCount = 0]
             /\ pc' = [pc EXCEPT ![self] = "Un1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, pr_s, pr_e, state, id, pr_M,
                             pr_st, phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, pr, st,
                             comm_, isPending, query, currState >>

Un1(self) == /\ pc[self] = "Un1"
             /\ mutex' = [mutex EXCEPT ![self].owner = 0]
             /\ pc' = [pc EXCEPT ![self] = "Un2"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, pr_s, pr_e, state, id, pr_M,
                             pr_st, phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, pr, st,
                             comm_, isPending, query, currState >>

Un2(self) == /\ pc[self] = "Un2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ pr_U' = [pr_U EXCEPT ![self] = Head(stack[self]).pr_U]
             /\ mutex' = [mutex EXCEPT ![self] = Head(stack[self]).mutex]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                             comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

Unlock(self) == Un0(self) \/ Un1(self) \/ Un2(self)

SCP1(self) == /\ pc[self] = "SCP1"
              /\ /\ mutex_' = [mutex_ EXCEPT ![self] = ckptPendingMutex]
                 /\ pr_' = [pr_ EXCEPT ![self] = pr_s[self]]
                 /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Lock",
                                                          pc        |->  "SCP2",
                                                          tempL1    |->  tempL1[self],
                                                          casResultLock |->  casResultLock[self],
                                                          pr_       |->  pr_[self],
                                                          mutex_    |->  mutex_[self] ] >>
                                                      \o stack[self]]
              /\ tempL1' = [tempL1 EXCEPT ![self] = defaultInitValue]
              /\ casResultLock' = [casResultLock EXCEPT ![self] = defaultInitValue]
              /\ pc' = [pc EXCEPT ![self] = "Lo0"]
              /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, comms,
                              ckptCount, ckptPendingMutex, inWrapper,
                              procState, coordMsg, clientResponse,
                              ckptingEnabledMutex, coordDone, pr_U, mutex,
                              pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                              comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                              ProcList_c, i_c, resps, ProcList, i, CkptPending,
                              responses, aliveRankCount, pr, st, comm_,
                              isPending, query, currState >>

SCP2(self) == /\ pc[self] = "SCP2"
              /\ ckptPending' = [ckptPending EXCEPT ![pr_s[self]] = TRUE]
              /\ pc' = [pc EXCEPT ![self] = "SCP3"]
              /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptingEnabled,
                              freePass, rankDone, comms, ckptCount,
                              ckptPendingMutex, inWrapper, procState, coordMsg,
                              clientResponse, ckptingEnabledMutex, coordDone,
                              stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                              mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                              pr_en, comm, pr_d, ProcList_, i_, ProcList_s,
                              i_s, ProcList_c, i_c, resps, ProcList, i,
                              CkptPending, responses, aliveRankCount, pr, st,
                              comm_, isPending, query, currState >>

SCP3(self) == /\ pc[self] = "SCP3"
              /\ /\ mutex' = [mutex EXCEPT ![self] = ckptPendingMutex]
                 /\ pr_U' = [pr_U EXCEPT ![self] = pr_s[self]]
                 /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Unlock",
                                                          pc        |->  "SCP4",
                                                          pr_U      |->  pr_U[self],
                                                          mutex     |->  mutex[self] ] >>
                                                      \o stack[self]]
              /\ pc' = [pc EXCEPT ![self] = "Un0"]
              /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, comms,
                              ckptCount, ckptPendingMutex, inWrapper,
                              procState, coordMsg, clientResponse,
                              ckptingEnabledMutex, coordDone, pr_, mutex_,
                              tempL1, casResultLock, pr_s, pr_e, state, id,
                              pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                              i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                              ProcList, i, CkptPending, responses,
                              aliveRankCount, pr, st, comm_, isPending, query,
                              currState >>

SCP4(self) == /\ pc[self] = "SCP4"
              /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
              /\ pr_s' = [pr_s EXCEPT ![self] = Head(stack[self]).pr_s]
              /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
              /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, comms,
                              ckptCount, ckptPendingMutex, inWrapper,
                              procState, coordMsg, clientResponse,
                              ckptingEnabledMutex, coordDone, pr_, mutex_,
                              tempL1, casResultLock, pr_U, mutex, pr_e, state,
                              id, pr_M, pr_st, phase, pr_en, comm, pr_d,
                              ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c,
                              resps, ProcList, i, CkptPending, responses,
                              aliveRankCount, pr, st, comm_, isPending, query,
                              currState >>

setCkptPending(self) == SCP1(self) \/ SCP2(self) \/ SCP3(self)
                           \/ SCP4(self)

EC1(self) == /\ pc[self] = "EC1"
             /\ /\ mutex_' = [mutex_ EXCEPT ![self] = ckptingEnabledMutex]
                /\ pr_' = [pr_ EXCEPT ![self] = pr_e[self]]
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Lock",
                                                         pc        |->  "EC2",
                                                         tempL1    |->  tempL1[self],
                                                         casResultLock |->  casResultLock[self],
                                                         pr_       |->  pr_[self],
                                                         mutex_    |->  mutex_[self] ] >>
                                                     \o stack[self]]
             /\ tempL1' = [tempL1 EXCEPT ![self] = defaultInitValue]
             /\ casResultLock' = [casResultLock EXCEPT ![self] = defaultInitValue]
             /\ pc' = [pc EXCEPT ![self] = "Lo0"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

EC2(self) == /\ pc[self] = "EC2"
             /\ ckptingEnabled' = [ckptingEnabled EXCEPT ![pr_e[self]] = state[self]]
             /\ pc' = [pc EXCEPT ![self] = "EC3"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             freePass, rankDone, comms, ckptCount,
                             ckptPendingMutex, inWrapper, procState, coordMsg,
                             clientResponse, ckptingEnabledMutex, coordDone,
                             stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                             mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

EC3(self) == /\ pc[self] = "EC3"
             /\ /\ mutex' = [mutex EXCEPT ![self] = ckptingEnabledMutex]
                /\ pr_U' = [pr_U EXCEPT ![self] = pr_e[self]]
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Unlock",
                                                         pc        |->  "EC4",
                                                         pr_U      |->  pr_U[self],
                                                         mutex     |->  mutex[self] ] >>
                                                     \o stack[self]]
             /\ pc' = [pc EXCEPT ![self] = "Un0"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                             comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

EC4(self) == /\ pc[self] = "EC4"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ pr_e' = [pr_e EXCEPT ![self] = Head(stack[self]).pr_e]
             /\ state' = [state EXCEPT ![self] = Head(stack[self]).state]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, id, pr_M, pr_st, phase, pr_en,
                             comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

enableDisableCkpting(self) == EC1(self) \/ EC2(self) \/ EC3(self)
                                 \/ EC4(self)

B1(self) == /\ pc[self] = "B1"
            /\ comms' = [comms EXCEPT ![id[self]].count = comms[id[self]].count + 1]
            /\ pc' = [pc EXCEPT ![self] = "B2"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, ckptCount,
                            ckptPendingMutex, inWrapper, procState, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                            mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

B2(self) == /\ pc[self] = "B2"
            /\ comms[id[self]].count = comms[id[self]].expected
            /\ pc' = [pc EXCEPT ![self] = "B3"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

B3(self) == /\ pc[self] = "B3"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ id' = [id EXCEPT ![self] = Head(stack[self]).id]
            /\ pr_M' = [pr_M EXCEPT ![self] = Head(stack[self]).pr_M]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_U, mutex, pr_s, pr_e, state, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

MPI_Barrier(self) == B1(self) \/ B2(self) \/ B3(self)

S1(self) == /\ pc[self] = "S1"
            /\ Assert(ckptPending[pr_st[self]],
                      "Failure of assertion at line 143, column 5.")
            /\ pc' = [pc EXCEPT ![self] = "S2"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

S2(self) == /\ pc[self] = "S2"
            /\ freePass[pr_st[self]]
            /\ freePass' = [freePass EXCEPT ![pr_st[self]] = FALSE]
            /\ pc' = [pc EXCEPT ![self] = "S3"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, rankDone, comms, ckptCount,
                            ckptPendingMutex, inWrapper, procState, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                            mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

S3(self) == /\ pc[self] = "S3"
            /\ IF ckptPending[pr_st[self]] /\ ckptingEnabled[pr_st[self]] /\ recvdCkptMsg[pr_st[self]]
                  THEN /\ pc' = [pc EXCEPT ![self] = "S4"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "SN"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

S4(self) == /\ pc[self] = "S4"
            /\ procState' = [procState EXCEPT ![pr_st[self]] = READY_FOR_CKPT]
            /\ recvdCkptSignal[pr_st[self]]
            /\ recvdCkptMsg' = [recvdCkptMsg EXCEPT ![pr_st[self]] = FALSE]
            /\ /\ pr_en' = [pr_en EXCEPT ![self] = pr_st[self]]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "enterCkpt",
                                                        pc        |->  "SN",
                                                        pr_en     |->  pr_en[self] ] >>
                                                    \o stack[self]]
            /\ pc' = [pc EXCEPT ![self] = "C1"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            freePass, rankDone, comms, ckptCount,
                            ckptPendingMutex, inWrapper, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            pr_, mutex_, tempL1, casResultLock, pr_U, mutex,
                            pr_s, pr_e, state, id, pr_M, pr_st, phase, comm,
                            pr_d, ProcList_, i_, ProcList_s, i_s, ProcList_c,
                            i_c, resps, ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

SN(self) == /\ pc[self] = "SN"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ pr_st' = [pr_st EXCEPT ![self] = Head(stack[self]).pr_st]
            /\ phase' = [phase EXCEPT ![self] = Head(stack[self]).phase]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_en,
                            comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

stop(self) == S1(self) \/ S2(self) \/ S3(self) \/ S4(self) \/ SN(self)

C1(self) == /\ pc[self] = "C1"
            /\ Assert(recvdCkptSignal[pr_en[self]] /\ ckptingEnabled[pr_en[self]],
                      "Failure of assertion at line 158, column 5.")
            /\ pc' = [pc EXCEPT ![self] = "C2"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

C2(self) == /\ pc[self] = "C2"
            /\ ckptCount' = ckptCount + 1
            /\ pc' = [pc EXCEPT ![self] = "C3"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptPendingMutex, inWrapper, procState, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                            mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

C3(self) == /\ pc[self] = "C3"
            /\ AllRanksCkpted
            /\ ckptPending' = [ckptPending EXCEPT ![pr_en[self]] = FALSE]
            /\ recvdCkptSignal' = [recvdCkptSignal EXCEPT ![pr_en[self]] = FALSE]
            /\ procState' = [procState EXCEPT ![pr_en[self]] = CKPTED]
            /\ pc' = [pc EXCEPT ![self] = "CN"]
            /\ UNCHANGED << recvdCkptMsg, ckptingEnabled, freePass, rankDone,
                            comms, ckptCount, ckptPendingMutex, inWrapper,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

CN(self) == /\ pc[self] = "CN"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ pr_en' = [pr_en EXCEPT ![self] = Head(stack[self]).pr_en]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                            phase, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

enterCkpt(self) == C1(self) \/ C2(self) \/ C3(self) \/ CN(self)

CC1(self) == /\ pc[self] = "CC1"
             /\ /\ id' = [id EXCEPT ![self] = comm[self]]
                /\ pr_M' = [pr_M EXCEPT ![self] = pr_d[self]]
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "MPI_Barrier",
                                                         pc        |->  "CC2",
                                                         id        |->  id[self],
                                                         pr_M      |->  pr_M[self] ] >>
                                                     \o stack[self]]
             /\ pc' = [pc EXCEPT ![self] = "B1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

CC2(self) == /\ pc[self] = "CC2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ comm' = [comm EXCEPT ![self] = Head(stack[self]).comm]
             /\ pr_d' = [pr_d EXCEPT ![self] = Head(stack[self]).pr_d]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

doMpiCollective(self) == CC1(self) \/ CC2(self)

CI1(self) == /\ pc[self] = "CI1"
             /\ IF ProcList_[self] # {} /\ FinishedProcCount = 0
                   THEN /\ i_' = [i_ EXCEPT ![self] = Pick(ProcList_[self])]
                        /\ ProcList_' = [ProcList_ EXCEPT ![self] = ProcList_[self] \ {i_'[self]}]
                        /\ coordMsg' = [coordMsg EXCEPT ![i_'[self]] = INTENT]
                        /\ pc' = [pc EXCEPT ![self] = "CI1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "CI2"]
                        /\ UNCHANGED << coordMsg, ProcList_, i_ >>
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             clientResponse, ckptingEnabledMutex, coordDone,
                             stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                             mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_s, i_s, ProcList_c,
                             i_c, resps, ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

CI2(self) == /\ pc[self] = "CI2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_' = [ProcList_ EXCEPT ![self] = Head(stack[self]).ProcList_]
             /\ i_' = [i_ EXCEPT ![self] = Head(stack[self]).i_]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

sendCkptIntentToAllRanks(self) == CI1(self) \/ CI2(self)

SC1(self) == /\ pc[self] = "SC1"
             /\ IF ProcList_s[self] # {}
                   THEN /\ i_s' = [i_s EXCEPT ![self] = Pick(ProcList_s[self])]
                        /\ ProcList_s' = [ProcList_s EXCEPT ![self] = ProcList_s[self] \ {i_s'[self]}]
                        /\ IF FinishedProcCount = 0
                              THEN /\ coordMsg' = [coordMsg EXCEPT ![i_s'[self]] = CKPT]
                              ELSE /\ TRUE
                                   /\ UNCHANGED coordMsg
                        /\ pc' = [pc EXCEPT ![self] = "SC1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "SC2"]
                        /\ UNCHANGED << coordMsg, ProcList_s, i_s >>
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             clientResponse, ckptingEnabledMutex, coordDone,
                             stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                             mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_, i_, ProcList_c, i_c,
                             resps, ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

SC2(self) == /\ pc[self] = "SC2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_s' = [ProcList_s EXCEPT ![self] = Head(stack[self]).ProcList_s]
             /\ i_s' = [i_s EXCEPT ![self] = Head(stack[self]).i_s]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

sendCkptMsgToAllRanks(self) == SC1(self) \/ SC2(self)

CR1(self) == /\ pc[self] = "CR1"
             /\ IF ProcList_c[self] # {}
                   THEN /\ i_c' = [i_c EXCEPT ![self] = Pick(ProcList_c[self])]
                        /\ ProcList_c' = [ProcList_c EXCEPT ![self] = ProcList_c[self] \ {i_c'[self]}]
                        /\ clientResponse' = [clientResponse EXCEPT ![i_c'[self]] = NO_RESPONSE]
                        /\ pc' = [pc EXCEPT ![self] = "CR1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "CR2"]
                        /\ UNCHANGED << clientResponse, ProcList_c, i_c >>
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, ckptingEnabledMutex, coordDone, stack,
                             pr_, mutex_, tempL1, casResultLock, pr_U, mutex,
                             pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                             comm, pr_d, ProcList_, i_, ProcList_s, i_s, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

CR2(self) == /\ pc[self] = "CR2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_c' = [ProcList_c EXCEPT ![self] = Head(stack[self]).ProcList_c]
             /\ i_c' = [i_c EXCEPT ![self] = Head(stack[self]).i_c]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

clearResponses(self) == CR1(self) \/ CR2(self)

PR1(self) == /\ pc[self] = "PR1"
             /\ IF CSRanksCount(resps[self]) = 0 /\ AllRanksAlive
                   THEN /\ pc' = [pc EXCEPT ![self] = "PRN"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "PR2"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

PR2(self) == /\ pc[self] = "PR2"
             /\ IF ProcList[self] # {}
                   THEN /\ i' = [i EXCEPT ![self] = Pick(ProcList[self])]
                        /\ ProcList' = [ProcList EXCEPT ![self] = ProcList[self] \ {i'[self]}]
                        /\ pc' = [pc EXCEPT ![self] = "PR3"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "PRN"]
                        /\ UNCHANGED << ProcList, i >>
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             CkptPending, responses, aliveRankCount, pr, st,
                             comm_, isPending, query, currState >>

PR3(self) == /\ pc[self] = "PR3"
             /\ IF resps[self][i[self]] \in {PHASE_1, PHASE_2}
                   THEN /\ coordMsg' = [coordMsg EXCEPT ![i[self]] = FREE_PASS]
                   ELSE /\ TRUE
                        /\ UNCHANGED coordMsg
             /\ pc' = [pc EXCEPT ![self] = "PR4"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             clientResponse, ckptingEnabledMutex, coordDone,
                             stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                             mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

PR4(self) == /\ pc[self] = "PR4"
             /\ IF resps[self][i[self]] \in {IS_READY, IN_CS}
                   THEN /\ coordMsg' = [coordMsg EXCEPT ![i[self]] = GET_STATUS]
                   ELSE /\ TRUE
                        /\ UNCHANGED coordMsg
             /\ pc' = [pc EXCEPT ![self] = "PR2"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             clientResponse, ckptingEnabledMutex, coordDone,
                             stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                             mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

PRN(self) == /\ pc[self] = "PRN"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList' = [ProcList EXCEPT ![self] = Head(stack[self]).ProcList]
             /\ i' = [i EXCEPT ![self] = Head(stack[self]).i]
             /\ resps' = [resps EXCEPT ![self] = Head(stack[self]).resps]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

processResponses(self) == PR1(self) \/ PR2(self) \/ PR3(self) \/ PR4(self)
                             \/ PRN(self)

BC1(self) == /\ pc[self] = "BC1"
             /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendCkptIntentToAllRanks",
                                                      pc        |->  "BC2",
                                                      ProcList_ |->  ProcList_[self],
                                                      i_        |->  i_[self] ] >>
                                                  \o stack[self]]
             /\ ProcList_' = [ProcList_ EXCEPT ![self] = Procs]
             /\ i_' = [i_ EXCEPT ![self] = defaultInitValue]
             /\ pc' = [pc EXCEPT ![self] = "CI1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

BC2(self) == /\ pc[self] = "BC2"
             /\ IF ~AllRanksDone /\ CkptPending[self]
                   THEN /\ pc' = [pc EXCEPT ![self] = "BC3"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BCN"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

BC3(self) == /\ pc[self] = "BC3"
             /\ AllRanksAckd(aliveRankCount[self]) \/ AllRanksDone
             /\ responses' = [responses EXCEPT ![self] = clientResponse]
             /\ aliveRankCount' = [aliveRankCount EXCEPT ![self] = aliveRankCount[self] - DoneRanksCount(responses'[self])]
             /\ pc' = [pc EXCEPT ![self] = "BC4"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, pr, st, comm_,
                             isPending, query, currState >>

BC4(self) == /\ pc[self] = "BC4"
             /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "clearResponses",
                                                      pc        |->  "BC5",
                                                      ProcList_c |->  ProcList_c[self],
                                                      i_c       |->  i_c[self] ] >>
                                                  \o stack[self]]
             /\ ProcList_c' = [ProcList_c EXCEPT ![self] = Procs]
             /\ i_c' = [i_c EXCEPT ![self] = defaultInitValue]
             /\ pc' = [pc EXCEPT ![self] = "CR1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

BC5(self) == /\ pc[self] = "BC5"
             /\ IF ~AllRanksDone /\ AllRanksReady(responses[self])
                   THEN /\ pc' = [pc EXCEPT ![self] = "BC6"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC9"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

BC6(self) == /\ pc[self] = "BC6"
             /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendCkptMsgToAllRanks",
                                                      pc        |->  "BC7",
                                                      ProcList_s |->  ProcList_s[self],
                                                      i_s       |->  i_s[self] ] >>
                                                  \o stack[self]]
             /\ ProcList_s' = [ProcList_s EXCEPT ![self] = Procs]
             /\ i_s' = [i_s EXCEPT ![self] = defaultInitValue]
             /\ pc' = [pc EXCEPT ![self] = "SC1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

BC7(self) == /\ pc[self] = "BC7"
             /\ AllRanksCkpted
             /\ CkptPending' = [CkptPending EXCEPT ![self] = FALSE]
             /\ pc' = [pc EXCEPT ![self] = "BC8"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, responses, aliveRankCount, pr, st,
                             comm_, isPending, query, currState >>

BC8(self) == /\ pc[self] = "BC8"
             /\ Assert(ckptCount > 0 => AllRanksCkpted,
                       "Failure of assertion at line 242, column 10.")
             /\ pc' = [pc EXCEPT ![self] = "BC2"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

BC9(self) == /\ pc[self] = "BC9"
             /\ IF ~AllRanksDone
                   THEN /\ /\ resps' = [resps EXCEPT ![self] = responses[self]]
                           /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "processResponses",
                                                                    pc        |->  "BC2",
                                                                    ProcList  |->  ProcList[self],
                                                                    i         |->  i[self],
                                                                    resps     |->  resps[self] ] >>
                                                                \o stack[self]]
                        /\ ProcList' = [ProcList EXCEPT ![self] = Procs]
                        /\ i' = [i EXCEPT ![self] = defaultInitValue]
                        /\ pc' = [pc EXCEPT ![self] = "PR1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC2"]
                        /\ UNCHANGED << stack, resps, ProcList, i >>
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

BCN(self) == /\ pc[self] = "BCN"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ CkptPending' = [CkptPending EXCEPT ![self] = Head(stack[self]).CkptPending]
             /\ responses' = [responses EXCEPT ![self] = Head(stack[self]).responses]
             /\ aliveRankCount' = [aliveRankCount EXCEPT ![self] = Head(stack[self]).aliveRankCount]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, pr, st, comm_, isPending, query, currState >>

bcastCkptingMsg(self) == BC1(self) \/ BC2(self) \/ BC3(self) \/ BC4(self)
                            \/ BC5(self) \/ BC6(self) \/ BC7(self)
                            \/ BC8(self) \/ BC9(self) \/ BCN(self)

IC1(self) == /\ pc[self] = "IC1"
             /\ clientResponse' = [clientResponse EXCEPT ![pr[self]] = st[self]]
             /\ pc' = [pc EXCEPT ![self] = "IC2"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, ckptingEnabledMutex, coordDone, stack,
                             pr_, mutex_, tempL1, casResultLock, pr_U, mutex,
                             pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                             comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query, currState >>

IC2(self) == /\ pc[self] = "IC2"
             /\ clientResponse[pr[self]] = NO_RESPONSE \/ coordDone
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ pr' = [pr EXCEPT ![self] = Head(stack[self]).pr]
             /\ st' = [st EXCEPT ![self] = Head(stack[self]).st]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, comm_,
                             isPending, query, currState >>

informCoord(self) == IC1(self) \/ IC2(self)

P0(self) == /\ pc[self] = "P0"
            /\ inWrapper' = [inWrapper EXCEPT ![self] = TRUE]
            /\ pc' = [pc EXCEPT ![self] = "P1"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, procState, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                            mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

P1(self) == /\ pc[self] = "P1"
            /\ /\ mutex_' = [mutex_ EXCEPT ![self] = ckptPendingMutex]
               /\ pr_' = [pr_ EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Lock",
                                                        pc        |->  "P2",
                                                        tempL1    |->  tempL1[self],
                                                        casResultLock |->  casResultLock[self],
                                                        pr_       |->  pr_[self],
                                                        mutex_    |->  mutex_[self] ] >>
                                                    \o stack[self]]
            /\ tempL1' = [tempL1 EXCEPT ![self] = defaultInitValue]
            /\ casResultLock' = [casResultLock EXCEPT ![self] = defaultInitValue]
            /\ pc' = [pc EXCEPT ![self] = "Lo0"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

P2(self) == /\ pc[self] = "P2"
            /\ isPending' = [isPending EXCEPT ![self] = ckptPending[self]]
            /\ /\ mutex' = [mutex EXCEPT ![self] = ckptPendingMutex]
               /\ pr_U' = [pr_U EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Unlock",
                                                        pc        |->  "P3",
                                                        pr_U      |->  pr_U[self],
                                                        mutex     |->  mutex[self] ] >>
                                                    \o stack[self]]
            /\ pc' = [pc EXCEPT ![self] = "Un0"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                            comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_, query,
                            currState >>

P3(self) == /\ pc[self] = "P3"
            /\ IF isPending[self]
                  THEN /\ procState' = [procState EXCEPT ![self] = PHASE_1]
                       /\ /\ phase' = [phase EXCEPT ![self] = PHASE_1]
                          /\ pr_st' = [pr_st EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "stop",
                                                                   pc        |->  "P4",
                                                                   pr_st     |->  pr_st[self],
                                                                   phase     |->  phase[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "S1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "P4"]
                       /\ UNCHANGED << procState, stack, pr_st, phase >>
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            pr_, mutex_, tempL1, casResultLock, pr_U, mutex,
                            pr_s, pr_e, state, id, pr_M, pr_en, comm, pr_d,
                            ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c,
                            resps, ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

P4(self) == /\ pc[self] = "P4"
            /\ procState' = [procState EXCEPT ![self] = IN_CS]
            /\ /\ pr_e' = [pr_e EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "enableDisableCkpting",
                                                        pc        |->  "P5",
                                                        pr_e      |->  pr_e[self],
                                                        state     |->  state[self] ] >>
                                                    \o stack[self]]
               /\ state' = [state EXCEPT ![self] = FALSE]
            /\ pc' = [pc EXCEPT ![self] = "EC1"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            pr_, mutex_, tempL1, casResultLock, pr_U, mutex,
                            pr_s, id, pr_M, pr_st, phase, pr_en, comm, pr_d,
                            ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c,
                            resps, ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

P5(self) == /\ pc[self] = "P5"
            /\ /\ comm' = [comm EXCEPT ![self] = comm_[self]]
               /\ pr_d' = [pr_d EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "doMpiCollective",
                                                        pc        |->  "P6",
                                                        comm      |->  comm[self],
                                                        pr_d      |->  pr_d[self] ] >>
                                                    \o stack[self]]
            /\ pc' = [pc EXCEPT ![self] = "CC1"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                            phase, pr_en, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

P6(self) == /\ pc[self] = "P6"
            /\ /\ pr_e' = [pr_e EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "enableDisableCkpting",
                                                        pc        |->  "P7",
                                                        pr_e      |->  pr_e[self],
                                                        state     |->  state[self] ] >>
                                                    \o stack[self]]
               /\ state' = [state EXCEPT ![self] = TRUE]
            /\ pc' = [pc EXCEPT ![self] = "EC1"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_U, mutex, pr_s, id, pr_M, pr_st, phase, pr_en,
                            comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

P7(self) == /\ pc[self] = "P7"
            /\ /\ mutex_' = [mutex_ EXCEPT ![self] = ckptPendingMutex]
               /\ pr_' = [pr_ EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Lock",
                                                        pc        |->  "P8",
                                                        tempL1    |->  tempL1[self],
                                                        casResultLock |->  casResultLock[self],
                                                        pr_       |->  pr_[self],
                                                        mutex_    |->  mutex_[self] ] >>
                                                    \o stack[self]]
            /\ tempL1' = [tempL1 EXCEPT ![self] = defaultInitValue]
            /\ casResultLock' = [casResultLock EXCEPT ![self] = defaultInitValue]
            /\ pc' = [pc EXCEPT ![self] = "Lo0"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

P8(self) == /\ pc[self] = "P8"
            /\ isPending' = [isPending EXCEPT ![self] = ckptPending[self]]
            /\ /\ mutex' = [mutex EXCEPT ![self] = ckptPendingMutex]
               /\ pr_U' = [pr_U EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "Unlock",
                                                        pc        |->  "P9",
                                                        pr_U      |->  pr_U[self],
                                                        mutex     |->  mutex[self] ] >>
                                                    \o stack[self]]
            /\ pc' = [pc EXCEPT ![self] = "Un0"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_s, pr_e, state, id, pr_M, pr_st, phase, pr_en,
                            comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_, query,
                            currState >>

P9(self) == /\ pc[self] = "P9"
            /\ IF isPending[self]
                  THEN /\ procState' = [procState EXCEPT ![self] = PHASE_2]
                       /\ /\ phase' = [phase EXCEPT ![self] = PHASE_2]
                          /\ pr_st' = [pr_st EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "stop",
                                                                   pc        |->  "PN",
                                                                   pr_st     |->  pr_st[self],
                                                                   phase     |->  phase[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "S1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "PN"]
                       /\ UNCHANGED << procState, stack, pr_st, phase >>
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, coordMsg,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            pr_, mutex_, tempL1, casResultLock, pr_U, mutex,
                            pr_s, pr_e, state, id, pr_M, pr_en, comm, pr_d,
                            ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c,
                            resps, ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

PN(self) == /\ pc[self] = "PN"
            /\ rankDone' = [rankDone EXCEPT ![self] = TRUE]
            /\ inWrapper' = [inWrapper EXCEPT ![self] = FALSE]
            /\ procState' = [procState EXCEPT ![self] = IS_DONE]
            /\ pc' = [pc EXCEPT ![self] = "Done"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, comms, ckptCount,
                            ckptPendingMutex, coordMsg, clientResponse,
                            ckptingEnabledMutex, coordDone, stack, pr_, mutex_,
                            tempL1, casResultLock, pr_U, mutex, pr_s, pr_e,
                            state, id, pr_M, pr_st, phase, pr_en, comm, pr_d,
                            ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c,
                            resps, ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

r(self) == P0(self) \/ P1(self) \/ P2(self) \/ P3(self) \/ P4(self)
              \/ P5(self) \/ P6(self) \/ P7(self) \/ P8(self) \/ P9(self)
              \/ PN(self)

T1(self) == /\ pc[self] = "T1"
            /\ IF ~rankDone[UT(self)]
                  THEN /\ pc' = [pc EXCEPT ![self] = "T2"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "T14"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T2(self) == /\ pc[self] = "T2"
            /\ ~(coordMsg[UT(self)] = NONE) \/ rankDone[UT(self)]
            /\ query' = [query EXCEPT ![self] = coordMsg[UT(self)]]
            /\ coordMsg' = [coordMsg EXCEPT ![UT(self)] = NONE]
            /\ IF rankDone[UT(self)]
                  THEN /\ pc' = [pc EXCEPT ![self] = "T14"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "T3"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            clientResponse, ckptingEnabledMutex, coordDone,
                            stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                            mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, currState >>

T3(self) == /\ pc[self] = "T3"
            /\ IF query[self] = INTENT
                  THEN /\ /\ pr_s' = [pr_s EXCEPT ![self] = UT(self)]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "setCkptPending",
                                                                   pc        |->  "T4",
                                                                   pr_s      |->  pr_s[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "SCP1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "T4"]
                       /\ UNCHANGED << stack, pr_s >>
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, pr_, mutex_, tempL1, casResultLock,
                            pr_U, mutex, pr_e, state, id, pr_M, pr_st, phase,
                            pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                            ProcList_c, i_c, resps, ProcList, i, CkptPending,
                            responses, aliveRankCount, pr, st, comm_,
                            isPending, query, currState >>

T4(self) == /\ pc[self] = "T4"
            /\ IF query[self] \in {INTENT, GET_STATUS}
                  /\ inWrapper[UT(self)]
                  THEN /\ pc' = [pc EXCEPT ![self] = "T5"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "T6"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T5(self) == /\ pc[self] = "T5"
            /\ ProcInSaneState(UT(self))
            /\ pc' = [pc EXCEPT ![self] = "T6"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T6(self) == /\ pc[self] = "T6"
            /\ IF query[self] = CKPT
                  THEN /\ pc' = [pc EXCEPT ![self] = "T7"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "T10"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T7(self) == /\ pc[self] = "T7"
            /\ Assert(inWrapper[UT(self)] => procState[UT(self)] \in {PHASE_1, PHASE_2, IS_READY},
                      "Failure of assertion at line 305, column 9.")
            /\ recvdCkptSignal' = [recvdCkptSignal EXCEPT ![UT(self)] = TRUE]
            /\ recvdCkptMsg' = [recvdCkptMsg EXCEPT ![UT(self)] = TRUE]
            /\ freePass' = [freePass EXCEPT ![UT(self)] = TRUE]
            /\ pc' = [pc EXCEPT ![self] = "T8"]
            /\ UNCHANGED << ckptPending, ckptingEnabled, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T8(self) == /\ pc[self] = "T8"
            /\ ProcInSafeState(UT(self))
            /\ pc' = [pc EXCEPT ![self] = "T9"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T9(self) == /\ pc[self] = "T9"
            /\ procState[UT(self)] = CKPTED \/ rankDone[UT(self)]
            /\ pc' = [pc EXCEPT ![self] = "T10"]
            /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, comms,
                            ckptCount, ckptPendingMutex, inWrapper, procState,
                            coordMsg, clientResponse, ckptingEnabledMutex,
                            coordDone, stack, pr_, mutex_, tempL1,
                            casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                            pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                            i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                            ProcList, i, CkptPending, responses,
                            aliveRankCount, pr, st, comm_, isPending, query,
                            currState >>

T10(self) == /\ pc[self] = "T10"
             /\ IF query[self] = FREE_PASS
                   THEN /\ pc' = [pc EXCEPT ![self] = "T11"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "T13"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

T11(self) == /\ pc[self] = "T11"
             /\ Assert(procState[UT(self)] \in {PHASE_1, PHASE_2},
                       "Failure of assertion at line 313, column 9.")
             /\ freePass' = [freePass EXCEPT ![UT(self)] = TRUE]
             /\ currState' = [currState EXCEPT ![self] = procState[UT(self)]]
             /\ pc' = [pc EXCEPT ![self] = "T12"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, rankDone, comms, ckptCount,
                             ckptPendingMutex, inWrapper, procState, coordMsg,
                             clientResponse, ckptingEnabledMutex, coordDone,
                             stack, pr_, mutex_, tempL1, casResultLock, pr_U,
                             mutex, pr_s, pr_e, state, id, pr_M, pr_st, phase,
                             pr_en, comm, pr_d, ProcList_, i_, ProcList_s, i_s,
                             ProcList_c, i_c, resps, ProcList, i, CkptPending,
                             responses, aliveRankCount, pr, st, comm_,
                             isPending, query >>

T12(self) == /\ pc[self] = "T12"
             /\ ~(currState[self] = procState[UT(self)])
             /\ pc' = [pc EXCEPT ![self] = "T13"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, stack, pr_, mutex_, tempL1,
                             casResultLock, pr_U, mutex, pr_s, pr_e, state, id,
                             pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                             i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                             ProcList, i, CkptPending, responses,
                             aliveRankCount, pr, st, comm_, isPending, query,
                             currState >>

T13(self) == /\ pc[self] = "T13"
             /\ IF ~rankDone[UT(self)]
                   THEN /\ /\ pr' = [pr EXCEPT ![self] = UT(self)]
                           /\ st' = [st EXCEPT ![self] = procState[UT(self)]]
                           /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "informCoord",
                                                                    pc        |->  "T1",
                                                                    pr        |->  pr[self],
                                                                    st        |->  st[self] ] >>
                                                                \o stack[self]]
                        /\ pc' = [pc EXCEPT ![self] = "IC1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "T1"]
                        /\ UNCHANGED << stack, pr, st >>
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, comm_,
                             isPending, query, currState >>

T14(self) == /\ pc[self] = "T14"
             /\ /\ pr' = [pr EXCEPT ![self] = UT(self)]
                /\ st' = [st EXCEPT ![self] = IS_DONE]
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "informCoord",
                                                         pc        |->  "Done",
                                                         pr        |->  pr[self],
                                                         st        |->  st[self] ] >>
                                                     \o stack[self]]
             /\ pc' = [pc EXCEPT ![self] = "IC1"]
             /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, comms,
                             ckptCount, ckptPendingMutex, inWrapper, procState,
                             coordMsg, clientResponse, ckptingEnabledMutex,
                             coordDone, pr_, mutex_, tempL1, casResultLock,
                             pr_U, mutex, pr_s, pr_e, state, id, pr_M, pr_st,
                             phase, pr_en, comm, pr_d, ProcList_, i_,
                             ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                             i, CkptPending, responses, aliveRankCount, comm_,
                             isPending, query, currState >>

t(self) == T1(self) \/ T2(self) \/ T3(self) \/ T4(self) \/ T5(self)
              \/ T6(self) \/ T7(self) \/ T8(self) \/ T9(self) \/ T10(self)
              \/ T11(self) \/ T12(self) \/ T13(self) \/ T14(self)

CO1 == /\ pc[COORD_PROC] = "CO1"
       /\ stack' = [stack EXCEPT ![COORD_PROC] = << [ procedure |->  "bcastCkptingMsg",
                                                      pc        |->  "CO2",
                                                      CkptPending |->  CkptPending[COORD_PROC],
                                                      responses |->  responses[COORD_PROC],
                                                      aliveRankCount |->  aliveRankCount[COORD_PROC] ] >>
                                                  \o stack[COORD_PROC]]
       /\ CkptPending' = [CkptPending EXCEPT ![COORD_PROC] = TRUE]
       /\ responses' = [responses EXCEPT ![COORD_PROC] = defaultInitValue]
       /\ aliveRankCount' = [aliveRankCount EXCEPT ![COORD_PROC] = ProcCount]
       /\ pc' = [pc EXCEPT ![COORD_PROC] = "BC1"]
       /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                       ckptingEnabled, freePass, rankDone, comms, ckptCount,
                       ckptPendingMutex, inWrapper, procState, coordMsg,
                       clientResponse, ckptingEnabledMutex, coordDone, pr_,
                       mutex_, tempL1, casResultLock, pr_U, mutex, pr_s, pr_e,
                       state, id, pr_M, pr_st, phase, pr_en, comm, pr_d,
                       ProcList_, i_, ProcList_s, i_s, ProcList_c, i_c, resps,
                       ProcList, i, pr, st, comm_, isPending, query, currState >>

CO2 == /\ pc[COORD_PROC] = "CO2"
       /\ coordDone' = TRUE
       /\ pc' = [pc EXCEPT ![COORD_PROC] = "Done"]
       /\ UNCHANGED << recvdCkptMsg, recvdCkptSignal, ckptPending,
                       ckptingEnabled, freePass, rankDone, comms, ckptCount,
                       ckptPendingMutex, inWrapper, procState, coordMsg,
                       clientResponse, ckptingEnabledMutex, stack, pr_, mutex_,
                       tempL1, casResultLock, pr_U, mutex, pr_s, pr_e, state,
                       id, pr_M, pr_st, phase, pr_en, comm, pr_d, ProcList_,
                       i_, ProcList_s, i_s, ProcList_c, i_c, resps, ProcList,
                       i, CkptPending, responses, aliveRankCount, pr, st,
                       comm_, isPending, query, currState >>

coord == CO1 \/ CO2

Next == coord
           \/ (\E self \in ProcSet:  \/ Lock(self) \/ Unlock(self)
                                     \/ setCkptPending(self)
                                     \/ enableDisableCkpting(self)
                                     \/ MPI_Barrier(self) \/ stop(self)
                                     \/ enterCkpt(self) \/ doMpiCollective(self)
                                     \/ sendCkptIntentToAllRanks(self)
                                     \/ sendCkptMsgToAllRanks(self)
                                     \/ clearResponses(self)
                                     \/ processResponses(self)
                                     \/ bcastCkptingMsg(self) \/ informCoord(self))
           \/ (\E self \in Procs: r(self))
           \/ (\E self \in CkptThreads: t(self))
           \/ (* Disjunct to prevent deadlock on termination *)
              ((\A self \in ProcSet: pc[self] = "Done") /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION


=============================================================================
\* Modification History
\* Last modified Thu Nov 08 08:09:43 PST 2018 by rgarg
\* Last modified Mon Nov 05 21:02:52 EST 2018 by rohgarg
\* Created Mon Nov 05 10:57:58 EST 2018 by rohgarg
