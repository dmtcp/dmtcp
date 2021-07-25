----------------------------- MODULE 2pcmodelv2 -----------------------------

EXTENDS Naturals, Sequences, TLC, FiniteSets

\* Instantiate the model with Procs <--  {r1, r2}; NumBarriers <- Some natural number

\* Parameters of the spec
CONSTANTS Procs,    \* The set of all processes (MPI ranks)
          PHASE_1,
          PHASE_2,
          NumBarriers

\* Global definitions

\* An MPI_Comm object is a 2-tuple: <count, expected>
\*   "count" indicates the no. of entered ranks
\*   "expected" indicates the no. of ranks that are part of the communicator
MPI_Comm == [count |-> 0, expected |-> Cardinality(Procs)]

\* Note CHOOSE is deterministic
Pick(S) == CHOOSE s \in S : TRUE

\* Just an enum to indicate the phase
PHASE == {PHASE_1, PHASE_2}

(*
--algorithm 2pcmodelv2

variables
  recvdCkptSignal = [p \in Procs |-> FALSE], \* Indicates whether a ckpt msg was received
  ckptPending = [p \in Procs |-> FALSE],     \* Used by the coord. to indicate intent for ckpting
  ckptingEnabled = [p \in Procs |-> TRUE],   \* Have we entered the barrier?
  ckptIntentAck = [p \in Procs |-> FALSE],   \* Used by the ranks to indicate whether the ckpt-intent msg has been seen
  freePass = [p \in Procs |-> FALSE],        \* Used by the coord. to allow a blocked rank to continue (until the next safe point)
  rankDone = [p \in Procs |-> FALSE],        \* Indicates if a rank has exited
  rankCkptPhase = [p \in Procs |-> PHASE_1], \* Indicates the current phase of a rank
  comms = [c \in NumBarriers |-> MPI_Comm],  \* "NumBarrier" communicator objects of type MPI_Comm
  ckptCount = 0;                             \* Indicates the number of ranks that have ckpted successfully

\* Some useful definitions.
define
SetOfReadyRanks == {r \in Procs: ckptingEnabled[r]}
SetOfPendingRanks == {r \in Procs: ckptPending[r]}

FinishedProcCount == Cardinality({r \in Procs: rankDone[r]})
AckCount == Cardinality({r \in Procs: ckptIntentAck[r]})
Phase1Ranks == Cardinality({r \in Procs: rankCkptPhase[r] = PHASE_1})
Phase2Ranks == Cardinality({r \in Procs: rankCkptPhase[r] = PHASE_2})
ProcCount == Cardinality(Procs)

AllRanksDone == FinishedProcCount = ProcCount
AllRanksCkpted == ckptCount = ProcCount
AllRanksInSamePhase == (Phase1Ranks = ProcCount \/ Phase2Ranks = ProcCount)
AllRanksAckd == AckCount = ProcCount
end define;

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
S2: ckptIntentAck[pr] := TRUE; rankCkptPhase[pr] := phase;
S3: await freePass[pr]; freePass[pr] := FALSE;
\* If a ckpt intent was expressed and if ckpting is enabled, we wait
\* for an imminent ckpt msg from the coordinator
S4: if ckptPending[pr] /\ ckptingEnabled[pr] then
      await recvdCkptSignal[pr];
      call enterCkpt(pr);
    end if;
S5: return;
end procedure;

\* Ckpt handler for each rank
procedure enterCkpt(pr)
begin
C1: assert recvdCkptSignal[pr] /\ ckptingEnabled[pr];
C2: ckptCount := ckptCount + 1;
C3: await AllRanksCkpted; ckptPending[pr] := FALSE; recvdCkptSignal[pr] := FALSE;
C5: return;
end procedure;

\* Execute MPI collective call
procedure doMpiCollective(comm, pr)
begin
CC1: ckptingEnabled[self] := FALSE;
CC2: call MPI_Barrier(comm, pr); \* We get out of barrier only if/when all peers enter
CC3: ckptingEnabled[self] := TRUE;
CC4: return;
end procedure

\* Send free-pass msg to all the blocked ranks
procedure sendFreePassToBlockedRanks()
variables ProcList = Procs, i;
begin
SF1: while ProcList # {} /\ FinishedProcCount = 0 do
       i := Pick(ProcList);
       ProcList := ProcList \ {i};
       \* Send a free pass if either ckpting is disabled or ckpt is pending
       if ckptingEnabled[i] = FALSE \/ ckptPending[i] = TRUE then
         freePass[i] := TRUE;
       end if;
     end while;
SF2: return;
end procedure;

\* Send ckpt-intent msg to all ranks
procedure sendCkptIntentToAllRanks()
variables ProcList = Procs, i;
begin
CI1:  while ProcList # {} /\ FinishedProcCount = 0 do
        i := Pick(ProcList);
        ProcList := ProcList \ {i};
        ckptPending[i] := TRUE;
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
        if FinishedProcCount = 0 then recvdCkptSignal[i] := TRUE; end if;
      end while;
SC2:  return;
end procedure;

\* The idea here is to do rounds: wait for ranks to reach a safe point.
\* All ranks must acknowledge our ckpt intent, before we can send them a ckpt msg.
\* If we haven't received intent-ack msg from some rank, it means that they either
\* didn't receive our ckpt intent msg, or they haven't yet processed it.
procedure bcastCkptingMsg()
variable CkptPending = TRUE;
begin
\* First, express intent for checkpointing
BC1: call sendCkptIntentToAllRanks();
\* While all processes are still alive, do
BC2: while ~AllRanksDone /\ CkptPending do
\* We need to allow ranks to get to the next safe point. We need to do this
\* until all the ranks have seen the ckpt-intent msg.
\* INVARIANT: at the end of this, all ranks must be stopped at the same phase
\*            point: either before a barrier; or, after the barrier. Note that
\*            this barrier must be the same for all the ranks.
BC3:   while ~AllRanksAckd /\ ~AllRanksDone do
         call sendFreePassToBlockedRanks();
       end while;
\* If we have received acks from all ranks, it implies that the ranks have
\* reached a safe stopping point. So, we can unblock them and ask them to
\* take a checkpoint.
BC4:   if AllRanksAckd /\ ~AllRanksDone then
BC5:     call sendFreePassToBlockedRanks();
BC6:     call sendCkptMsgToAllRanks();
\* Wait for all ranks to complete their checkpointing.
BC7:     await AllRanksCkpted; CkptPending := FALSE;
BC8:     assert ckptCount > 0 => AllRanksCkpted /\ AllRanksInSamePhase;
       end if;
     end while;
BC9: return;
end procedure;

\* An MPI rank
process r \in Procs
variables comm = 1;
begin
P1: if ckptPending[self] then
      call stop(self, PHASE_1); \* We stop and acknowledge the ckpt intent msg
    end if;
P2: call doMpiCollective(comm, self);
P3: if ckptPending[self] then
      call stop(self, PHASE_1); \* We stop and acknowledge the ckpt intent msg, assuming we had missed it earlier
    end if;
P4: rankDone[self] := TRUE;
end process;

\* The DMTCP coordinator process
process coord = "dmtcp_coord"
begin
\* TODO: Do this in a loop?
CO1: call bcastCkptingMsg();
end process;

end algorithm *)
\* BEGIN TRANSLATION
\* Process variable comm of process r at line 172 col 11 changed to comm_
\* Procedure variable ProcList of procedure sendFreePassToBlockedRanks at line 100 col 11 changed to ProcList_
\* Procedure variable i of procedure sendFreePassToBlockedRanks at line 100 col 29 changed to i_
\* Procedure variable ProcList of procedure sendCkptIntentToAllRanks at line 115 col 11 changed to ProcList_s
\* Procedure variable i of procedure sendCkptIntentToAllRanks at line 115 col 29 changed to i_s
\* Parameter pr of procedure MPI_Barrier at line 58 col 27 changed to pr_
\* Parameter pr of procedure stop at line 66 col 16 changed to pr_s
\* Parameter pr of procedure enterCkpt at line 81 col 21 changed to pr_e
CONSTANT defaultInitValue
VARIABLES recvdCkptSignal, ckptPending, ckptingEnabled, ckptIntentAck,
          freePass, rankDone, rankCkptPhase, comms, ckptCount, pc, stack

(* define statement *)
SetOfReadyRanks == {r \in Procs: ckptingEnabled[r]}
SetOfPendingRanks == {r \in Procs: ckptPending[r]}

FinishedProcCount == Cardinality({r \in Procs: rankDone[r]})
AckCount == Cardinality({r \in Procs: ckptIntentAck[r]})
Phase1Ranks == Cardinality({r \in Procs: rankCkptPhase[r] = PHASE_1})
Phase2Ranks == Cardinality({r \in Procs: rankCkptPhase[r] = PHASE_2})
ProcCount == Cardinality(Procs)

AllRanksDone == FinishedProcCount = ProcCount
AllRanksCkpted == ckptCount = ProcCount
AllRanksInSamePhase == (Phase1Ranks = ProcCount \/ Phase2Ranks = ProcCount)
AllRanksAckd == AckCount = ProcCount

VARIABLES id, pr_, pr_s, phase, pr_e, comm, pr, ProcList_, i_, ProcList_s,
          i_s, ProcList, i, CkptPending, comm_

vars == << recvdCkptSignal, ckptPending, ckptingEnabled, ckptIntentAck,
           freePass, rankDone, rankCkptPhase, comms, ckptCount, pc, stack, id,
           pr_, pr_s, phase, pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
           ProcList, i, CkptPending, comm_ >>

ProcSet == (Procs) \cup {"dmtcp_coord"}

Init == (* Global variables *)
        /\ recvdCkptSignal = [p \in Procs |-> FALSE]
        /\ ckptPending = [p \in Procs |-> FALSE]
        /\ ckptingEnabled = [p \in Procs |-> TRUE]
        /\ ckptIntentAck = [p \in Procs |-> FALSE]
        /\ freePass = [p \in Procs |-> FALSE]
        /\ rankDone = [p \in Procs |-> FALSE]
        /\ rankCkptPhase = [p \in Procs |-> PHASE_1]
        /\ comms = [c \in 1..2 |-> MPI_Comm]
        /\ ckptCount = 0
        (* Procedure MPI_Barrier *)
        /\ id = [ self \in ProcSet |-> defaultInitValue]
        /\ pr_ = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure stop *)
        /\ pr_s = [ self \in ProcSet |-> defaultInitValue]
        /\ phase = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure enterCkpt *)
        /\ pr_e = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure doMpiCollective *)
        /\ comm = [ self \in ProcSet |-> defaultInitValue]
        /\ pr = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendFreePassToBlockedRanks *)
        /\ ProcList_ = [ self \in ProcSet |-> Procs]
        /\ i_ = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendCkptIntentToAllRanks *)
        /\ ProcList_s = [ self \in ProcSet |-> Procs]
        /\ i_s = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendCkptMsgToAllRanks *)
        /\ ProcList = [ self \in ProcSet |-> Procs]
        /\ i = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure bcastCkptingMsg *)
        /\ CkptPending = [ self \in ProcSet |-> TRUE]
        (* Process r *)
        /\ comm_ = [self \in Procs |-> 1]
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> CASE self \in Procs -> "P1"
                                        [] self = "dmtcp_coord" -> "CO1"]

B1(self) == /\ pc[self] = "B1"
            /\ comms' = [comms EXCEPT ![id[self]].count = comms[id[self]].count + 1]
            /\ pc' = [pc EXCEPT ![self] = "B2"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            ckptCount, stack, id, pr_, pr_s, phase, pr_e, comm,
                            pr, ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

B2(self) == /\ pc[self] = "B2"
            /\ comms[id[self]].count = comms[id[self]].expected
            /\ pc' = [pc EXCEPT ![self] = "B3"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, stack, id, pr_, pr_s, phase,
                            pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                            ProcList, i, CkptPending, comm_ >>

B3(self) == /\ pc[self] = "B3"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ id' = [id EXCEPT ![self] = Head(stack[self]).id]
            /\ pr_' = [pr_ EXCEPT ![self] = Head(stack[self]).pr_]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, pr_s, phase, pr_e, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

MPI_Barrier(self) == B1(self) \/ B2(self) \/ B3(self)

S1(self) == /\ pc[self] = "S1"
            /\ Assert(ckptPending[pr_s[self]],
                      "Failure of assertion at line 68, column 5.")
            /\ pc' = [pc EXCEPT ![self] = "S2"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, stack, id, pr_, pr_s, phase,
                            pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                            ProcList, i, CkptPending, comm_ >>

S2(self) == /\ pc[self] = "S2"
            /\ ckptIntentAck' = [ckptIntentAck EXCEPT ![pr_s[self]] = TRUE]
            /\ rankCkptPhase' = [rankCkptPhase EXCEPT ![pr_s[self]] = phase[self]]
            /\ pc' = [pc EXCEPT ![self] = "S3"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            freePass, rankDone, comms, ckptCount, stack, id,
                            pr_, pr_s, phase, pr_e, comm, pr, ProcList_, i_,
                            ProcList_s, i_s, ProcList, i, CkptPending, comm_ >>

S3(self) == /\ pc[self] = "S3"
            /\ freePass[pr_s[self]]
            /\ freePass' = [freePass EXCEPT ![pr_s[self]] = FALSE]
            /\ pc' = [pc EXCEPT ![self] = "S4"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, rankDone, rankCkptPhase, comms,
                            ckptCount, stack, id, pr_, pr_s, phase, pr_e, comm,
                            pr, ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

S4(self) == /\ pc[self] = "S4"
            /\ IF ckptPending[pr_s[self]] /\ ckptingEnabled[pr_s[self]]
                  THEN /\ recvdCkptSignal[pr_s[self]]
                       /\ /\ pr_e' = [pr_e EXCEPT ![self] = pr_s[self]]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "enterCkpt",
                                                                   pc        |->  "S5",
                                                                   pr_e      |->  pr_e[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "C1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "S5"]
                       /\ UNCHANGED << stack, pr_e >>
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, id, pr_, pr_s, phase, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

S5(self) == /\ pc[self] = "S5"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ pr_s' = [pr_s EXCEPT ![self] = Head(stack[self]).pr_s]
            /\ phase' = [phase EXCEPT ![self] = Head(stack[self]).phase]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, id, pr_, pr_e, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

stop(self) == S1(self) \/ S2(self) \/ S3(self) \/ S4(self) \/ S5(self)

C1(self) == /\ pc[self] = "C1"
            /\ Assert(recvdCkptSignal[pr_e[self]] /\ ckptingEnabled[pr_e[self]],
                      "Failure of assertion at line 83, column 5.")
            /\ pc' = [pc EXCEPT ![self] = "C2"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, stack, id, pr_, pr_s, phase,
                            pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                            ProcList, i, CkptPending, comm_ >>

C2(self) == /\ pc[self] = "C2"
            /\ ckptCount' = ckptCount + 1
            /\ pc' = [pc EXCEPT ![self] = "C3"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, stack, id, pr_, pr_s, phase, pr_e, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

C3(self) == /\ pc[self] = "C3"
            /\ AllRanksCkpted
            /\ ckptPending' = [ckptPending EXCEPT ![pr_e[self]] = FALSE]
            /\ recvdCkptSignal' = [recvdCkptSignal EXCEPT ![pr_e[self]] = FALSE]
            /\ pc' = [pc EXCEPT ![self] = "C5"]
            /\ UNCHANGED << ckptingEnabled, ckptIntentAck, freePass, rankDone,
                            rankCkptPhase, comms, ckptCount, stack, id, pr_,
                            pr_s, phase, pr_e, comm, pr, ProcList_, i_,
                            ProcList_s, i_s, ProcList, i, CkptPending, comm_ >>

C5(self) == /\ pc[self] = "C5"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ pr_e' = [pr_e EXCEPT ![self] = Head(stack[self]).pr_e]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, id, pr_, pr_s, phase, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

enterCkpt(self) == C1(self) \/ C2(self) \/ C3(self) \/ C5(self)

CC1(self) == /\ pc[self] = "CC1"
             /\ ckptingEnabled' = [ckptingEnabled EXCEPT ![self] = FALSE]
             /\ pc' = [pc EXCEPT ![self] = "CC2"]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptIntentAck,
                             freePass, rankDone, rankCkptPhase, comms,
                             ckptCount, stack, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList_s, i_s,
                             ProcList, i, CkptPending, comm_ >>

CC2(self) == /\ pc[self] = "CC2"
             /\ /\ id' = [id EXCEPT ![self] = comm[self]]
                /\ pr_' = [pr_ EXCEPT ![self] = pr[self]]
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "MPI_Barrier",
                                                         pc        |->  "CC3",
                                                         id        |->  id[self],
                                                         pr_       |->  pr_[self] ] >>
                                                     \o stack[self]]
             /\ pc' = [pc EXCEPT ![self] = "B1"]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, pr_s, phase, pr_e, comm, pr,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i,
                             CkptPending, comm_ >>

CC3(self) == /\ pc[self] = "CC3"
             /\ ckptingEnabled' = [ckptingEnabled EXCEPT ![self] = TRUE]
             /\ pc' = [pc EXCEPT ![self] = "CC4"]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptIntentAck,
                             freePass, rankDone, rankCkptPhase, comms,
                             ckptCount, stack, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList_s, i_s,
                             ProcList, i, CkptPending, comm_ >>

CC4(self) == /\ pc[self] = "CC4"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ comm' = [comm EXCEPT ![self] = Head(stack[self]).comm]
             /\ pr' = [pr EXCEPT ![self] = Head(stack[self]).pr]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i,
                             CkptPending, comm_ >>

doMpiCollective(self) == CC1(self) \/ CC2(self) \/ CC3(self) \/ CC4(self)

SF1(self) == /\ pc[self] = "SF1"
             /\ IF ProcList_[self] # {} /\ FinishedProcCount = 0
                   THEN /\ i_' = [i_ EXCEPT ![self] = Pick(ProcList_[self])]
                        /\ ProcList_' = [ProcList_ EXCEPT ![self] = ProcList_[self] \ {i_'[self]}]
                        /\ IF ckptingEnabled[i_'[self]] = FALSE \/ ckptPending[i_'[self]] = TRUE
                              THEN /\ freePass' = [freePass EXCEPT ![i_'[self]] = TRUE]
                              ELSE /\ TRUE
                                   /\ UNCHANGED freePass
                        /\ pc' = [pc EXCEPT ![self] = "SF1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "SF2"]
                        /\ UNCHANGED << freePass, ProcList_, i_ >>
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, rankDone, rankCkptPhase, comms,
                             ckptCount, stack, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_s, i_s, ProcList, i,
                             CkptPending, comm_ >>

SF2(self) == /\ pc[self] = "SF2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_' = [ProcList_ EXCEPT ![self] = Head(stack[self]).ProcList_]
             /\ i_' = [i_ EXCEPT ![self] = Head(stack[self]).i_]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_s, i_s, ProcList, i,
                             CkptPending, comm_ >>

sendFreePassToBlockedRanks(self) == SF1(self) \/ SF2(self)

CI1(self) == /\ pc[self] = "CI1"
             /\ IF ProcList_s[self] # {} /\ FinishedProcCount = 0
                   THEN /\ i_s' = [i_s EXCEPT ![self] = Pick(ProcList_s[self])]
                        /\ ProcList_s' = [ProcList_s EXCEPT ![self] = ProcList_s[self] \ {i_s'[self]}]
                        /\ ckptPending' = [ckptPending EXCEPT ![i_s'[self]] = TRUE]
                        /\ pc' = [pc EXCEPT ![self] = "CI1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "CI2"]
                        /\ UNCHANGED << ckptPending, ProcList_s, i_s >>
             /\ UNCHANGED << recvdCkptSignal, ckptingEnabled, ckptIntentAck,
                             freePass, rankDone, rankCkptPhase, comms,
                             ckptCount, stack, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList, i, CkptPending,
                             comm_ >>

CI2(self) == /\ pc[self] = "CI2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_s' = [ProcList_s EXCEPT ![self] = Head(stack[self]).ProcList_s]
             /\ i_s' = [i_s EXCEPT ![self] = Head(stack[self]).i_s]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList, i, CkptPending,
                             comm_ >>

sendCkptIntentToAllRanks(self) == CI1(self) \/ CI2(self)

SC1(self) == /\ pc[self] = "SC1"
             /\ IF ProcList[self] # {}
                   THEN /\ i' = [i EXCEPT ![self] = Pick(ProcList[self])]
                        /\ ProcList' = [ProcList EXCEPT ![self] = ProcList[self] \ {i'[self]}]
                        /\ IF FinishedProcCount = 0
                              THEN /\ recvdCkptSignal' = [recvdCkptSignal EXCEPT ![i'[self]] = TRUE]
                              ELSE /\ TRUE
                                   /\ UNCHANGED recvdCkptSignal
                        /\ pc' = [pc EXCEPT ![self] = "SC1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "SC2"]
                        /\ UNCHANGED << recvdCkptSignal, ProcList, i >>
             /\ UNCHANGED << ckptPending, ckptingEnabled, ckptIntentAck,
                             freePass, rankDone, rankCkptPhase, comms,
                             ckptCount, stack, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList_s, i_s,
                             CkptPending, comm_ >>

SC2(self) == /\ pc[self] = "SC2"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList' = [ProcList EXCEPT ![self] = Head(stack[self]).ProcList]
             /\ i' = [i EXCEPT ![self] = Head(stack[self]).i]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList_s, i_s,
                             CkptPending, comm_ >>

sendCkptMsgToAllRanks(self) == SC1(self) \/ SC2(self)

BC1(self) == /\ pc[self] = "BC1"
             /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendCkptIntentToAllRanks",
                                                      pc        |->  "BC2",
                                                      ProcList_s |->  ProcList_s[self],
                                                      i_s       |->  i_s[self] ] >>
                                                  \o stack[self]]
             /\ ProcList_s' = [ProcList_s EXCEPT ![self] = Procs]
             /\ i_s' = [i_s EXCEPT ![self] = defaultInitValue]
             /\ pc' = [pc EXCEPT ![self] = "CI1"]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList, i, CkptPending,
                             comm_ >>

BC2(self) == /\ pc[self] = "BC2"
             /\ IF ~AllRanksDone /\ CkptPending[self]
                   THEN /\ pc' = [pc EXCEPT ![self] = "BC3"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC5"]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, stack, id, pr_, pr_s, phase,
                             pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                             ProcList, i, CkptPending, comm_ >>

BC3(self) == /\ pc[self] = "BC3"
             /\ IF ~AllRanksAckd /\ ~AllRanksDone
                   THEN /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendFreePassToBlockedRanks",
                                                                 pc        |->  "BC3",
                                                                 ProcList_ |->  ProcList_[self],
                                                                 i_        |->  i_[self] ] >>
                                                             \o stack[self]]
                        /\ ProcList_' = [ProcList_ EXCEPT ![self] = Procs]
                        /\ i_' = [i_ EXCEPT ![self] = defaultInitValue]
                        /\ pc' = [pc EXCEPT ![self] = "SF1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC4"]
                        /\ UNCHANGED << stack, ProcList_, i_ >>
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_s, i_s, ProcList, i,
                             CkptPending, comm_ >>

BC4(self) == /\ pc[self] = "BC4"
             /\ IF AllRanksAckd /\ ~AllRanksDone
                   THEN /\ pc' = [pc EXCEPT ![self] = "BC41"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC2"]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, stack, id, pr_, pr_s, phase,
                             pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                             ProcList, i, CkptPending, comm_ >>

BC41(self) == /\ pc[self] = "BC41"
              /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendFreePassToBlockedRanks",
                                                       pc        |->  "BC42",
                                                       ProcList_ |->  ProcList_[self],
                                                       i_        |->  i_[self] ] >>
                                                   \o stack[self]]
              /\ ProcList_' = [ProcList_ EXCEPT ![self] = Procs]
              /\ i_' = [i_ EXCEPT ![self] = defaultInitValue]
              /\ pc' = [pc EXCEPT ![self] = "SF1"]
              /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                              ckptIntentAck, freePass, rankDone, rankCkptPhase,
                              comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                              comm, pr, ProcList_s, i_s, ProcList, i,
                              CkptPending, comm_ >>

BC42(self) == /\ pc[self] = "BC42"
              /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendCkptMsgToAllRanks",
                                                       pc        |->  "BC43",
                                                       ProcList  |->  ProcList[self],
                                                       i         |->  i[self] ] >>
                                                   \o stack[self]]
              /\ ProcList' = [ProcList EXCEPT ![self] = Procs]
              /\ i' = [i EXCEPT ![self] = defaultInitValue]
              /\ pc' = [pc EXCEPT ![self] = "SC1"]
              /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                              ckptIntentAck, freePass, rankDone, rankCkptPhase,
                              comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                              comm, pr, ProcList_, i_, ProcList_s, i_s,
                              CkptPending, comm_ >>

BC43(self) == /\ pc[self] = "BC43"
              /\ AllRanksCkpted
              /\ CkptPending' = [CkptPending EXCEPT ![self] = FALSE]
              /\ pc' = [pc EXCEPT ![self] = "BC44"]
              /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                              ckptIntentAck, freePass, rankDone, rankCkptPhase,
                              comms, ckptCount, stack, id, pr_, pr_s, phase,
                              pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                              ProcList, i, comm_ >>

BC44(self) == /\ pc[self] = "BC44"
              /\ Assert(ckptCount > 0 => AllRanksCkpted /\ AllRanksInSamePhase,
                        "Failure of assertion at line 164, column 10.")
              /\ pc' = [pc EXCEPT ![self] = "BC2"]
              /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                              ckptIntentAck, freePass, rankDone, rankCkptPhase,
                              comms, ckptCount, stack, id, pr_, pr_s, phase,
                              pr_e, comm, pr, ProcList_, i_, ProcList_s, i_s,
                              ProcList, i, CkptPending, comm_ >>

BC5(self) == /\ pc[self] = "BC5"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ CkptPending' = [CkptPending EXCEPT ![self] = Head(stack[self]).CkptPending]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                             ckptIntentAck, freePass, rankDone, rankCkptPhase,
                             comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                             comm, pr, ProcList_, i_, ProcList_s, i_s,
                             ProcList, i, comm_ >>

bcastCkptingMsg(self) == BC1(self) \/ BC2(self) \/ BC3(self) \/ BC4(self)
                            \/ BC41(self) \/ BC42(self) \/ BC43(self)
                            \/ BC44(self) \/ BC5(self)

P1(self) == /\ pc[self] = "P1"
            /\ IF ckptPending[self]
                  THEN /\ /\ phase' = [phase EXCEPT ![self] = PHASE_1]
                          /\ pr_s' = [pr_s EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "stop",
                                                                   pc        |->  "P2",
                                                                   pr_s      |->  pr_s[self],
                                                                   phase     |->  phase[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "S1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "P2"]
                       /\ UNCHANGED << stack, pr_s, phase >>
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, id, pr_, pr_e, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

P2(self) == /\ pc[self] = "P2"
            /\ /\ comm' = [comm EXCEPT ![self] = comm_[self]]
               /\ pr' = [pr EXCEPT ![self] = self]
               /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "doMpiCollective",
                                                        pc        |->  "P3",
                                                        comm      |->  comm[self],
                                                        pr        |->  pr[self] ] >>
                                                    \o stack[self]]
            /\ pc' = [pc EXCEPT ![self] = "CC1"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, id, pr_, pr_s, phase, pr_e,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

P3(self) == /\ pc[self] = "P3"
            /\ IF ckptPending[self]
                  THEN /\ /\ phase' = [phase EXCEPT ![self] = PHASE_1]
                          /\ pr_s' = [pr_s EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "stop",
                                                                   pc        |->  "P4",
                                                                   pr_s      |->  pr_s[self],
                                                                   phase     |->  phase[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "S1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "P4"]
                       /\ UNCHANGED << stack, pr_s, phase >>
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankDone, rankCkptPhase,
                            comms, ckptCount, id, pr_, pr_e, comm, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

P4(self) == /\ pc[self] = "P4"
            /\ rankDone' = [rankDone EXCEPT ![self] = TRUE]
            /\ pc' = [pc EXCEPT ![self] = "Done"]
            /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                            ckptIntentAck, freePass, rankCkptPhase, comms,
                            ckptCount, stack, id, pr_, pr_s, phase, pr_e, comm,
                            pr, ProcList_, i_, ProcList_s, i_s, ProcList, i,
                            CkptPending, comm_ >>

r(self) == P1(self) \/ P2(self) \/ P3(self) \/ P4(self)

CO1 == /\ pc["dmtcp_coord"] = "CO1"
       /\ stack' = [stack EXCEPT !["dmtcp_coord"] = << [ procedure |->  "bcastCkptingMsg",
                                                         pc        |->  "Done",
                                                         CkptPending |->  CkptPending["dmtcp_coord"] ] >>
                                                     \o stack["dmtcp_coord"]]
       /\ CkptPending' = [CkptPending EXCEPT !["dmtcp_coord"] = TRUE]
       /\ pc' = [pc EXCEPT !["dmtcp_coord"] = "BC1"]
       /\ UNCHANGED << recvdCkptSignal, ckptPending, ckptingEnabled,
                       ckptIntentAck, freePass, rankDone, rankCkptPhase, comms,
                       ckptCount, id, pr_, pr_s, phase, pr_e, comm, pr,
                       ProcList_, i_, ProcList_s, i_s, ProcList, i, comm_ >>

coord == CO1

Next == coord
           \/ (\E self \in ProcSet:  \/ MPI_Barrier(self) \/ stop(self)
                                     \/ enterCkpt(self) \/ doMpiCollective(self)
                                     \/ sendFreePassToBlockedRanks(self)
                                     \/ sendCkptIntentToAllRanks(self)
                                     \/ sendCkptMsgToAllRanks(self)
                                     \/ bcastCkptingMsg(self))
           \/ (\E self \in Procs: r(self))
           \/ (* Disjunct to prevent deadlock on termination *)
              ((\A self \in ProcSet: pc[self] = "Done") /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION


=============================================================================
\* Modification History
\* Last modified Sat Nov 03 11:47:49 EDT 2018 by rgarg
\* Created Sat Nov 03 00:07:53 EDT 2018 by rgarg
