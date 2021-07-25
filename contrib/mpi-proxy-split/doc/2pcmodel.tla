------------------------------ MODULE 2pcmodel ------------------------------

EXTENDS Naturals, Sequences, TLC, FiniteSets

\* Instantiate the model with Procs <--  {r1, r2}

\* Parameters of the spec
CONSTANTS Procs    \* The set of all processes (MPI ranks)

\* Global definitions
MPI_Comm == [count |-> 0, expected |-> Cardinality(Procs)]
\* Note CHOOSE is deterministic
Pick(S) == CHOOSE s \in S : TRUE

(*
--algorithm 2pcmodel

variables
  sendCkptIntent = FALSE,
  recvdCkptSignal = [p \in Procs |-> FALSE],
  ckptPending = [p \in Procs |-> FALSE],
  ckptingEnabled = [p \in Procs |-> TRUE],
  freePass = [p \in Procs |-> FALSE],
  rankDone = [p \in Procs |-> FALSE],
  PHASE_1 = 1,
  PHASE_2 = 2,
  comms = [c \in 1..2 |-> MPI_Comm],
  ckptCount = 0,
  sendCkptSignal = FALSE;

define
SetOfReadyRanks == {r \in Procs: ckptingEnabled[r]}
SetOfDoneRanks == {r \in Procs: rankDone[r]}
SetOfPendingRanks == {r \in Procs: ckptPending[r]}
end define;

macro Add_To_Barrier(id, pr)
begin
  comms[id].expected := comms[id].expected + 1;
end macro;

procedure MPI_Barrier(id, pr)
begin
  B0: comms[id].count := comms[id].count + 1;
  B1: await comms[id].count = comms[id].expected;
  B2: return; \* We cannot reuse a barrier in this model
end procedure;

procedure checkCkptSignal(pr)
variables j = 1;
begin
CS1: if recvdCkptSignal[pr] then
CS2:   recvdCkptSignal[pr] := TRUE;
     end if;
CS3: return;
end procedure;

procedure stop(pr, phase)
begin
s0: await freePass[pr]; freePass[pr] := FALSE;
s1: if ckptPending[pr] /\ ckptingEnabled[pr] then
      await recvdCkptSignal[pr];
      call enterCkpt(pr);
    end if;
s2: return;
end procedure;

procedure phase1Stop(pr)
begin
ph1s1: if (ckptPending[self]) then
ph1s2:   call stop(self, PHASE_1);
       end if;
ph1s3: return;
end procedure;

procedure phase2Stop(pr)
begin
ph2s1: if (ckptPending[self]) then
ph2s2:   call stop(self, PHASE_2);
       end if;
ph2s3: return;
end procedure;

procedure enterCkpt(pr)
begin
c1: assert recvdCkptSignal[pr] /\ ckptingEnabled[pr];
c2: ckptCount := ckptCount + 1;
c3: await ckptCount = Cardinality(Procs);
c4: ckptPending[pr] := FALSE; if ckptCount = 2 then print "Ckpted!" end if;
c5: return;
end procedure;

procedure atomicBarrierAndDisableCkpting(id, pr)
begin
ab1: call MPI_Barrier(id, self);
ab2: ckptingEnabled[pr] := FALSE;
ab3: return;
end procedure;

procedure sendFreePassToBlockedRanks()
variables ProcList = Procs, i;
begin
sf1: while ProcList # {} /\ Cardinality(SetOfDoneRanks) = 0 do
       i := Pick(ProcList);
       ProcList := ProcList \ {i};
       \* Send a free pass if either ckpting is disabled or ckpt is pending
       if ckptingEnabled[i] = FALSE \/ ckptPending[i] = TRUE then
         freePass[i] := TRUE;
       end if;
     end while;
sf3: return;
end procedure;

procedure sendCkptPendingToAllRanks()
variables ProcList = Procs, i;
begin
sp1:  while ProcList # {} /\ Cardinality(SetOfDoneRanks) = 0 do
        i := Pick(ProcList);
        ProcList := ProcList \ {i};
        ckptPending[i] := TRUE;
      end while;
sp3:  return;
end procedure;

procedure sendCkptMsgToAllRanks()
variables ProcList = Procs, i;
begin
sc1:  while ProcList # {} do
        i := Pick(ProcList);
        ProcList := ProcList \ {i};
        if Cardinality(SetOfDoneRanks) = 0 then recvdCkptSignal[i] := TRUE; end if;
      end while;
sc3:  return;
end procedure;

procedure bcastCkptingMsg()
begin
BC1: if Cardinality(SetOfDoneRanks) = 0 then
       \* First, send the ckpting intent msg.
BC2:   call sendCkptPendingToAllRanks();
BC21:  await Cardinality(SetOfPendingRanks) = Cardinality(Procs) \/ Cardinality(SetOfDoneRanks) > 0;
       \* Then, wait for all ranks to enable ckpting (or get to a consistent state)
BC3:   while Cardinality(SetOfReadyRanks) <= Cardinality(Procs) /\ Cardinality(SetOfDoneRanks) = 0 do
BC31:    call sendFreePassToBlockedRanks();
       end while;
BC32:  assert Cardinality(SetOfDoneRanks) = 0 => Cardinality(SetOfReadyRanks) = Cardinality(Procs);
BC4:   if Cardinality(SetOfDoneRanks) = 0 /\ Cardinality(SetOfReadyRanks) = Cardinality(Procs) then
         call sendCkptMsgToAllRanks();
       end if;
BC5:   await ckptCount = Cardinality(Procs) \/ Cardinality(SetOfDoneRanks) > 0;
       \* INVARIANT: If a rank did get the ckpt signal on time, then no rank can exit without checkpointing
BC6:   assert ckptCount > 0 => ckptCount = Cardinality(Procs);
BC7:   if ckptCount = Cardinality(Procs) then print "All ckpted" end if;
     end if;
endB: return;
end procedure;

process r \in Procs
begin
\* P0: Add_To_Barrier(comm, self);
P1: if recvdCkptSignal[self] = FALSE then
      call phase1Stop(self);
    else
      assert FALSE;
    end if;
P2: if recvdCkptSignal[self] = FALSE then
      call atomicBarrierAndDisableCkpting(1, self);
    else
      assert FALSE;
    end if;
P3: if recvdCkptSignal[self] = FALSE then
      call phase2Stop(self);
    else
      assert FALSE;
    end if;
P4: if recvdCkptSignal[self] = FALSE then
      call MPI_Barrier(2, self);
    else
      assert FALSE;
    end if;
P5: ckptingEnabled[self] := TRUE;
P6: if recvdCkptSignal[self] then call phase1Stop(self); end if; \* Can also be replaced with await recvdCkptSignal[self]; call enterCkpt(self);
doneRank: rankDone[self] := TRUE;
end process;

process coord = "dmtcp_coord"
variable x = 0;
begin
\* TODO: Do this in a loop?
CO1: call bcastCkptingMsg();
end process;

end algorithm *)
\* BEGIN TRANSLATION
\* Procedure variable ProcList of procedure sendFreePassToBlockedRanks at line 101 col 11 changed to ProcList_
\* Procedure variable i of procedure sendFreePassToBlockedRanks at line 101 col 29 changed to i_
\* Procedure variable ProcList of procedure sendCkptPendingToAllRanks at line 115 col 11 changed to ProcList_s
\* Procedure variable i of procedure sendCkptPendingToAllRanks at line 115 col 29 changed to i_s
\* Parameter id of procedure MPI_Barrier at line 42 col 23 changed to id_
\* Parameter pr of procedure MPI_Barrier at line 42 col 27 changed to pr_
\* Parameter pr of procedure checkCkptSignal at line 49 col 27 changed to pr_c
\* Parameter pr of procedure stop at line 58 col 16 changed to pr_s
\* Parameter pr of procedure phase1Stop at line 68 col 22 changed to pr_p
\* Parameter pr of procedure phase2Stop at line 76 col 22 changed to pr_ph
\* Parameter pr of procedure enterCkpt at line 84 col 21 changed to pr_e
CONSTANT defaultInitValue
VARIABLES sendCkptIntent, recvdCkptSignal, ckptPending, ckptingEnabled,
          freePass, rankDone, PHASE_1, PHASE_2, comms, ckptCount,
          sendCkptSignal, pc, stack

(* define statement *)
SetOfReadyRanks == {r \in Procs: ckptingEnabled[r]}
SetOfDoneRanks == {r \in Procs: rankDone[r]}
SetOfPendingRanks == {r \in Procs: ckptPending[r]}

VARIABLES id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
          ProcList_, i_, ProcList_s, i_s, ProcList, i, x

vars == << sendCkptIntent, recvdCkptSignal, ckptPending, ckptingEnabled,
           freePass, rankDone, PHASE_1, PHASE_2, comms, ckptCount,
           sendCkptSignal, pc, stack, id_, pr_, pr_c, j, pr_s, phase, pr_p,
           pr_ph, pr_e, id, pr, ProcList_, i_, ProcList_s, i_s, ProcList, i,
           x >>

ProcSet == (Procs) \cup {"dmtcp_coord"}

Init == (* Global variables *)
        /\ sendCkptIntent = FALSE
        /\ recvdCkptSignal = [p \in Procs |-> FALSE]
        /\ ckptPending = [p \in Procs |-> FALSE]
        /\ ckptingEnabled = [p \in Procs |-> TRUE]
        /\ freePass = [p \in Procs |-> FALSE]
        /\ rankDone = [p \in Procs |-> FALSE]
        /\ PHASE_1 = 1
        /\ PHASE_2 = 2
        /\ comms = [c \in 1..2 |-> MPI_Comm]
        /\ ckptCount = 0
        /\ sendCkptSignal = FALSE
        (* Procedure MPI_Barrier *)
        /\ id_ = [ self \in ProcSet |-> defaultInitValue]
        /\ pr_ = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure checkCkptSignal *)
        /\ pr_c = [ self \in ProcSet |-> defaultInitValue]
        /\ j = [ self \in ProcSet |-> 1]
        (* Procedure stop *)
        /\ pr_s = [ self \in ProcSet |-> defaultInitValue]
        /\ phase = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure phase1Stop *)
        /\ pr_p = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure phase2Stop *)
        /\ pr_ph = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure enterCkpt *)
        /\ pr_e = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure atomicBarrierAndDisableCkpting *)
        /\ id = [ self \in ProcSet |-> defaultInitValue]
        /\ pr = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendFreePassToBlockedRanks *)
        /\ ProcList_ = [ self \in ProcSet |-> Procs]
        /\ i_ = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendCkptPendingToAllRanks *)
        /\ ProcList_s = [ self \in ProcSet |-> Procs]
        /\ i_s = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure sendCkptMsgToAllRanks *)
        /\ ProcList = [ self \in ProcSet |-> Procs]
        /\ i = [ self \in ProcSet |-> defaultInitValue]
        (* Process coord *)
        /\ x = 0
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> CASE self \in Procs -> "P1"
                                        [] self = "dmtcp_coord" -> "CO1"]

B0(self) == /\ pc[self] = "B0"
            /\ comms' = [comms EXCEPT ![id_[self]].count = comms[id_[self]].count + 1]
            /\ pc' = [pc EXCEPT ![self] = "B1"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, ckptCount, sendCkptSignal, stack, id_,
                            pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                            pr, ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

B1(self) == /\ pc[self] = "B1"
            /\ comms[id_[self]].count = comms[id_[self]].expected
            /\ pc' = [pc EXCEPT ![self] = "B2"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                            id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                            id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                            i, x >>

B2(self) == /\ pc[self] = "B2"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ id_' = [id_ EXCEPT ![self] = Head(stack[self]).id_]
            /\ pr_' = [pr_ EXCEPT ![self] = Head(stack[self]).pr_]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, pr_c, j,
                            pr_s, phase, pr_p, pr_ph, pr_e, id, pr, ProcList_,
                            i_, ProcList_s, i_s, ProcList, i, x >>

MPI_Barrier(self) == B0(self) \/ B1(self) \/ B2(self)

CS1(self) == /\ pc[self] = "CS1"
             /\ IF recvdCkptSignal[pr_c[self]]
                   THEN /\ pc' = [pc EXCEPT ![self] = "CS2"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "CS3"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                             id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                             i, x >>

CS2(self) == /\ pc[self] = "CS2"
             /\ recvdCkptSignal' = [recvdCkptSignal EXCEPT ![pr_c[self]] = TRUE]
             /\ pc' = [pc EXCEPT ![self] = "CS3"]
             /\ UNCHANGED << sendCkptIntent, ckptPending, ckptingEnabled,
                             freePass, rankDone, PHASE_1, PHASE_2, comms,
                             ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                             j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

CS3(self) == /\ pc[self] = "CS3"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ j' = [j EXCEPT ![self] = Head(stack[self]).j]
             /\ pr_c' = [pr_c EXCEPT ![self] = Head(stack[self]).pr_c]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

checkCkptSignal(self) == CS1(self) \/ CS2(self) \/ CS3(self)

s0(self) == /\ pc[self] = "s0"
            /\ freePass[pr_s[self]]
            /\ freePass' = [freePass EXCEPT ![pr_s[self]] = FALSE]
            /\ pc' = [pc EXCEPT ![self] = "s1"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, rankDone, PHASE_1, PHASE_2, comms,
                            ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                            j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

s1(self) == /\ pc[self] = "s1"
            /\ IF ckptPending[pr_s[self]] /\ ckptingEnabled[pr_s[self]]
                  THEN /\ recvdCkptSignal[pr_s[self]]
                       /\ /\ pr_e' = [pr_e EXCEPT ![self] = pr_s[self]]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "enterCkpt",
                                                                   pc        |->  "s2",
                                                                   pr_e      |->  pr_e[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "c1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "s2"]
                       /\ UNCHANGED << stack, pr_e >>
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

s2(self) == /\ pc[self] = "s2"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ pr_s' = [pr_s EXCEPT ![self] = Head(stack[self]).pr_s]
            /\ phase' = [phase EXCEPT ![self] = Head(stack[self]).phase]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_p, pr_ph, pr_e, id, pr, ProcList_,
                            i_, ProcList_s, i_s, ProcList, i, x >>

stop(self) == s0(self) \/ s1(self) \/ s2(self)

ph1s1(self) == /\ pc[self] = "ph1s1"
               /\ IF (ckptPending[self])
                     THEN /\ pc' = [pc EXCEPT ![self] = "ph1s2"]
                     ELSE /\ pc' = [pc EXCEPT ![self] = "ph1s3"]
               /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                               ckptingEnabled, freePass, rankDone, PHASE_1,
                               PHASE_2, comms, ckptCount, sendCkptSignal,
                               stack, id_, pr_, pr_c, j, pr_s, phase, pr_p,
                               pr_ph, pr_e, id, pr, ProcList_, i_, ProcList_s,
                               i_s, ProcList, i, x >>

ph1s2(self) == /\ pc[self] = "ph1s2"
               /\ /\ phase' = [phase EXCEPT ![self] = PHASE_1]
                  /\ pr_s' = [pr_s EXCEPT ![self] = self]
                  /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "stop",
                                                           pc        |->  "ph1s3",
                                                           pr_s      |->  pr_s[self],
                                                           phase     |->  phase[self] ] >>
                                                       \o stack[self]]
               /\ pc' = [pc EXCEPT ![self] = "s0"]
               /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                               ckptingEnabled, freePass, rankDone, PHASE_1,
                               PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                               pr_, pr_c, j, pr_p, pr_ph, pr_e, id, pr,
                               ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

ph1s3(self) == /\ pc[self] = "ph1s3"
               /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
               /\ pr_p' = [pr_p EXCEPT ![self] = Head(stack[self]).pr_p]
               /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
               /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                               ckptingEnabled, freePass, rankDone, PHASE_1,
                               PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                               pr_, pr_c, j, pr_s, phase, pr_ph, pr_e, id, pr,
                               ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

phase1Stop(self) == ph1s1(self) \/ ph1s2(self) \/ ph1s3(self)

ph2s1(self) == /\ pc[self] = "ph2s1"
               /\ IF (ckptPending[self])
                     THEN /\ pc' = [pc EXCEPT ![self] = "ph2s2"]
                     ELSE /\ pc' = [pc EXCEPT ![self] = "ph2s3"]
               /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                               ckptingEnabled, freePass, rankDone, PHASE_1,
                               PHASE_2, comms, ckptCount, sendCkptSignal,
                               stack, id_, pr_, pr_c, j, pr_s, phase, pr_p,
                               pr_ph, pr_e, id, pr, ProcList_, i_, ProcList_s,
                               i_s, ProcList, i, x >>

ph2s2(self) == /\ pc[self] = "ph2s2"
               /\ /\ phase' = [phase EXCEPT ![self] = PHASE_2]
                  /\ pr_s' = [pr_s EXCEPT ![self] = self]
                  /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "stop",
                                                           pc        |->  "ph2s3",
                                                           pr_s      |->  pr_s[self],
                                                           phase     |->  phase[self] ] >>
                                                       \o stack[self]]
               /\ pc' = [pc EXCEPT ![self] = "s0"]
               /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                               ckptingEnabled, freePass, rankDone, PHASE_1,
                               PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                               pr_, pr_c, j, pr_p, pr_ph, pr_e, id, pr,
                               ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

ph2s3(self) == /\ pc[self] = "ph2s3"
               /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
               /\ pr_ph' = [pr_ph EXCEPT ![self] = Head(stack[self]).pr_ph]
               /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
               /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                               ckptingEnabled, freePass, rankDone, PHASE_1,
                               PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                               pr_, pr_c, j, pr_s, phase, pr_p, pr_e, id, pr,
                               ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

phase2Stop(self) == ph2s1(self) \/ ph2s2(self) \/ ph2s3(self)

c1(self) == /\ pc[self] = "c1"
            /\ Assert(recvdCkptSignal[pr_e[self]] /\ ckptingEnabled[pr_e[self]],
                      "Failure of assertion at line 86, column 5.")
            /\ pc' = [pc EXCEPT ![self] = "c2"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                            id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                            id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                            i, x >>

c2(self) == /\ pc[self] = "c2"
            /\ ckptCount' = ckptCount + 1
            /\ pc' = [pc EXCEPT ![self] = "c3"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, sendCkptSignal, stack, id_, pr_,
                            pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

c3(self) == /\ pc[self] = "c3"
            /\ ckptCount = Cardinality(Procs)
            /\ pc' = [pc EXCEPT ![self] = "c4"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                            id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                            id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                            i, x >>

c4(self) == /\ pc[self] = "c4"
            /\ ckptPending' = [ckptPending EXCEPT ![pr_e[self]] = FALSE]
            /\ IF ckptCount = 2
                  THEN /\ PrintT("Ckpted!")
                  ELSE /\ TRUE
            /\ pc' = [pc EXCEPT ![self] = "c5"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptingEnabled,
                            freePass, rankDone, PHASE_1, PHASE_2, comms,
                            ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                            j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

c5(self) == /\ pc[self] = "c5"
            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
            /\ pr_e' = [pr_e EXCEPT ![self] = Head(stack[self]).pr_e]
            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

enterCkpt(self) == c1(self) \/ c2(self) \/ c3(self) \/ c4(self) \/ c5(self)

ab1(self) == /\ pc[self] = "ab1"
             /\ /\ id_' = [id_ EXCEPT ![self] = id[self]]
                /\ pr_' = [pr_ EXCEPT ![self] = self]
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "MPI_Barrier",
                                                         pc        |->  "ab2",
                                                         id_       |->  id_[self],
                                                         pr_       |->  pr_[self] ] >>
                                                     \o stack[self]]
             /\ pc' = [pc EXCEPT ![self] = "B0"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, pr_c,
                             j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

ab2(self) == /\ pc[self] = "ab2"
             /\ ckptingEnabled' = [ckptingEnabled EXCEPT ![pr[self]] = FALSE]
             /\ pc' = [pc EXCEPT ![self] = "ab3"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             freePass, rankDone, PHASE_1, PHASE_2, comms,
                             ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                             j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

ab3(self) == /\ pc[self] = "ab3"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ id' = [id EXCEPT ![self] = Head(stack[self]).id]
             /\ pr' = [pr EXCEPT ![self] = Head(stack[self]).pr]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

atomicBarrierAndDisableCkpting(self) == ab1(self) \/ ab2(self) \/ ab3(self)

sf1(self) == /\ pc[self] = "sf1"
             /\ IF ProcList_[self] # {} /\ Cardinality(SetOfDoneRanks) = 0
                   THEN /\ i_' = [i_ EXCEPT ![self] = Pick(ProcList_[self])]
                        /\ ProcList_' = [ProcList_ EXCEPT ![self] = ProcList_[self] \ {i_'[self]}]
                        /\ IF ckptingEnabled[i_'[self]] = FALSE \/ ckptPending[i_'[self]] = TRUE
                              THEN /\ freePass' = [freePass EXCEPT ![i_'[self]] = TRUE]
                              ELSE /\ TRUE
                                   /\ UNCHANGED freePass
                        /\ pc' = [pc EXCEPT ![self] = "sf1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "sf3"]
                        /\ UNCHANGED << freePass, ProcList_, i_ >>
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, rankDone, PHASE_1, PHASE_2, comms,
                             ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                             j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_s, i_s, ProcList, i, x >>

sf3(self) == /\ pc[self] = "sf3"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_' = [ProcList_ EXCEPT ![self] = Head(stack[self]).ProcList_]
             /\ i_' = [i_ EXCEPT ![self] = Head(stack[self]).i_]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                             pr, ProcList_s, i_s, ProcList, i, x >>

sendFreePassToBlockedRanks(self) == sf1(self) \/ sf3(self)

sp1(self) == /\ pc[self] = "sp1"
             /\ IF ProcList_s[self] # {} /\ Cardinality(SetOfDoneRanks) = 0
                   THEN /\ i_s' = [i_s EXCEPT ![self] = Pick(ProcList_s[self])]
                        /\ ProcList_s' = [ProcList_s EXCEPT ![self] = ProcList_s[self] \ {i_s'[self]}]
                        /\ ckptPending' = [ckptPending EXCEPT ![i_s'[self]] = TRUE]
                        /\ pc' = [pc EXCEPT ![self] = "sp1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "sp3"]
                        /\ UNCHANGED << ckptPending, ProcList_s, i_s >>
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptingEnabled,
                             freePass, rankDone, PHASE_1, PHASE_2, comms,
                             ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                             j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_, i_, ProcList, i, x >>

sp3(self) == /\ pc[self] = "sp3"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList_s' = [ProcList_s EXCEPT ![self] = Head(stack[self]).ProcList_s]
             /\ i_s' = [i_s EXCEPT ![self] = Head(stack[self]).i_s]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                             pr, ProcList_, i_, ProcList, i, x >>

sendCkptPendingToAllRanks(self) == sp1(self) \/ sp3(self)

sc1(self) == /\ pc[self] = "sc1"
             /\ IF ProcList[self] # {}
                   THEN /\ i' = [i EXCEPT ![self] = Pick(ProcList[self])]
                        /\ ProcList' = [ProcList EXCEPT ![self] = ProcList[self] \ {i'[self]}]
                        /\ IF Cardinality(SetOfDoneRanks) = 0
                              THEN /\ recvdCkptSignal' = [recvdCkptSignal EXCEPT ![i'[self]] = TRUE]
                              ELSE /\ TRUE
                                   /\ UNCHANGED recvdCkptSignal
                        /\ pc' = [pc EXCEPT ![self] = "sc1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "sc3"]
                        /\ UNCHANGED << recvdCkptSignal, ProcList, i >>
             /\ UNCHANGED << sendCkptIntent, ckptPending, ckptingEnabled,
                             freePass, rankDone, PHASE_1, PHASE_2, comms,
                             ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                             j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                             ProcList_, i_, ProcList_s, i_s, x >>

sc3(self) == /\ pc[self] = "sc3"
             /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
             /\ ProcList' = [ProcList EXCEPT ![self] = Head(stack[self]).ProcList]
             /\ i' = [i EXCEPT ![self] = Head(stack[self]).i]
             /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                             pr, ProcList_, i_, ProcList_s, i_s, x >>

sendCkptMsgToAllRanks(self) == sc1(self) \/ sc3(self)

BC1(self) == /\ pc[self] = "BC1"
             /\ IF Cardinality(SetOfDoneRanks) = 0
                   THEN /\ pc' = [pc EXCEPT ![self] = "BC2"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "endB"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                             id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                             i, x >>

BC2(self) == /\ pc[self] = "BC2"
             /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendCkptPendingToAllRanks",
                                                      pc        |->  "BC21",
                                                      ProcList_s |->  ProcList_s[self],
                                                      i_s       |->  i_s[self] ] >>
                                                  \o stack[self]]
             /\ ProcList_s' = [ProcList_s EXCEPT ![self] = Procs]
             /\ i_s' = [i_s EXCEPT ![self] = defaultInitValue]
             /\ pc' = [pc EXCEPT ![self] = "sp1"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                             pr, ProcList_, i_, ProcList, i, x >>

BC21(self) == /\ pc[self] = "BC21"
              /\ Cardinality(SetOfPendingRanks) = Cardinality(Procs) \/ Cardinality(SetOfDoneRanks) > 0
              /\ pc' = [pc EXCEPT ![self] = "BC3"]
              /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, PHASE_1,
                              PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                              id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph,
                              pr_e, id, pr, ProcList_, i_, ProcList_s, i_s,
                              ProcList, i, x >>

BC3(self) == /\ pc[self] = "BC3"
             /\ IF Cardinality(SetOfReadyRanks) <= Cardinality(Procs) /\ Cardinality(SetOfDoneRanks) = 0
                   THEN /\ pc' = [pc EXCEPT ![self] = "BC31"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC32"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                             id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                             i, x >>

BC31(self) == /\ pc[self] = "BC31"
              /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendFreePassToBlockedRanks",
                                                       pc        |->  "BC3",
                                                       ProcList_ |->  ProcList_[self],
                                                       i_        |->  i_[self] ] >>
                                                   \o stack[self]]
              /\ ProcList_' = [ProcList_ EXCEPT ![self] = Procs]
              /\ i_' = [i_ EXCEPT ![self] = defaultInitValue]
              /\ pc' = [pc EXCEPT ![self] = "sf1"]
              /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, PHASE_1,
                              PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                              pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                              pr, ProcList_s, i_s, ProcList, i, x >>

BC32(self) == /\ pc[self] = "BC32"
              /\ Assert(Cardinality(SetOfDoneRanks) = 0 => Cardinality(SetOfReadyRanks) = Cardinality(Procs),
                        "Failure of assertion at line 146, column 8.")
              /\ pc' = [pc EXCEPT ![self] = "BC4"]
              /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, PHASE_1,
                              PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                              id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph,
                              pr_e, id, pr, ProcList_, i_, ProcList_s, i_s,
                              ProcList, i, x >>

BC4(self) == /\ pc[self] = "BC4"
             /\ IF Cardinality(SetOfDoneRanks) = 0 /\ Cardinality(SetOfReadyRanks) = Cardinality(Procs)
                   THEN /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "sendCkptMsgToAllRanks",
                                                                 pc        |->  "BC5",
                                                                 ProcList  |->  ProcList[self],
                                                                 i         |->  i[self] ] >>
                                                             \o stack[self]]
                        /\ ProcList' = [ProcList EXCEPT ![self] = Procs]
                        /\ i' = [i EXCEPT ![self] = defaultInitValue]
                        /\ pc' = [pc EXCEPT ![self] = "sc1"]
                   ELSE /\ pc' = [pc EXCEPT ![self] = "BC5"]
                        /\ UNCHANGED << stack, ProcList, i >>
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                             pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                             pr, ProcList_, i_, ProcList_s, i_s, x >>

BC5(self) == /\ pc[self] = "BC5"
             /\ ckptCount = Cardinality(Procs) \/ Cardinality(SetOfDoneRanks) > 0
             /\ pc' = [pc EXCEPT ![self] = "BC6"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                             id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                             i, x >>

BC6(self) == /\ pc[self] = "BC6"
             /\ Assert(ckptCount > 0 => ckptCount = Cardinality(Procs),
                       "Failure of assertion at line 152, column 8.")
             /\ pc' = [pc EXCEPT ![self] = "BC7"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                             id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                             i, x >>

BC7(self) == /\ pc[self] = "BC7"
             /\ IF ckptCount = Cardinality(Procs)
                   THEN /\ PrintT("All ckpted")
                   ELSE /\ TRUE
             /\ pc' = [pc EXCEPT ![self] = "endB"]
             /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                             ckptingEnabled, freePass, rankDone, PHASE_1,
                             PHASE_2, comms, ckptCount, sendCkptSignal, stack,
                             id_, pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                             id, pr, ProcList_, i_, ProcList_s, i_s, ProcList,
                             i, x >>

endB(self) == /\ pc[self] = "endB"
              /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
              /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
              /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                              ckptingEnabled, freePass, rankDone, PHASE_1,
                              PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                              pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e, id,
                              pr, ProcList_, i_, ProcList_s, i_s, ProcList, i,
                              x >>

bcastCkptingMsg(self) == BC1(self) \/ BC2(self) \/ BC21(self) \/ BC3(self)
                            \/ BC31(self) \/ BC32(self) \/ BC4(self)
                            \/ BC5(self) \/ BC6(self) \/ BC7(self)
                            \/ endB(self)

P1(self) == /\ pc[self] = "P1"
            /\ IF recvdCkptSignal[self] = FALSE
                  THEN /\ /\ pr_p' = [pr_p EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "phase1Stop",
                                                                   pc        |->  "P2",
                                                                   pr_p      |->  pr_p[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "ph1s1"]
                  ELSE /\ Assert(FALSE,
                                 "Failure of assertion at line 164, column 7.")
                       /\ pc' = [pc EXCEPT ![self] = "P2"]
                       /\ UNCHANGED << stack, pr_p >>
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_s, phase, pr_ph, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

P2(self) == /\ pc[self] = "P2"
            /\ IF recvdCkptSignal[self] = FALSE
                  THEN /\ /\ id' = [id EXCEPT ![self] = 1]
                          /\ pr' = [pr EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "atomicBarrierAndDisableCkpting",
                                                                   pc        |->  "P3",
                                                                   id        |->  id[self],
                                                                   pr        |->  pr[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "ab1"]
                  ELSE /\ Assert(FALSE,
                                 "Failure of assertion at line 169, column 7.")
                       /\ pc' = [pc EXCEPT ![self] = "P3"]
                       /\ UNCHANGED << stack, id, pr >>
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

P3(self) == /\ pc[self] = "P3"
            /\ IF recvdCkptSignal[self] = FALSE
                  THEN /\ /\ pr_ph' = [pr_ph EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "phase2Stop",
                                                                   pc        |->  "P4",
                                                                   pr_ph     |->  pr_ph[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "ph2s1"]
                  ELSE /\ Assert(FALSE,
                                 "Failure of assertion at line 174, column 7.")
                       /\ pc' = [pc EXCEPT ![self] = "P4"]
                       /\ UNCHANGED << stack, pr_ph >>
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_s, phase, pr_p, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

P4(self) == /\ pc[self] = "P4"
            /\ IF recvdCkptSignal[self] = FALSE
                  THEN /\ /\ id_' = [id_ EXCEPT ![self] = 2]
                          /\ pr_' = [pr_ EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "MPI_Barrier",
                                                                   pc        |->  "P5",
                                                                   id_       |->  id_[self],
                                                                   pr_       |->  pr_[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "B0"]
                  ELSE /\ Assert(FALSE,
                                 "Failure of assertion at line 179, column 7.")
                       /\ pc' = [pc EXCEPT ![self] = "P5"]
                       /\ UNCHANGED << stack, id_, pr_ >>
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, pr_c, j,
                            pr_s, phase, pr_p, pr_ph, pr_e, id, pr, ProcList_,
                            i_, ProcList_s, i_s, ProcList, i, x >>

P5(self) == /\ pc[self] = "P5"
            /\ ckptingEnabled' = [ckptingEnabled EXCEPT ![self] = TRUE]
            /\ pc' = [pc EXCEPT ![self] = "P6"]
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            freePass, rankDone, PHASE_1, PHASE_2, comms,
                            ckptCount, sendCkptSignal, stack, id_, pr_, pr_c,
                            j, pr_s, phase, pr_p, pr_ph, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

P6(self) == /\ pc[self] = "P6"
            /\ IF recvdCkptSignal[self]
                  THEN /\ /\ pr_p' = [pr_p EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "phase1Stop",
                                                                   pc        |->  "doneRank",
                                                                   pr_p      |->  pr_p[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "ph1s1"]
                  ELSE /\ pc' = [pc EXCEPT ![self] = "doneRank"]
                       /\ UNCHANGED << stack, pr_p >>
            /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                            ckptingEnabled, freePass, rankDone, PHASE_1,
                            PHASE_2, comms, ckptCount, sendCkptSignal, id_,
                            pr_, pr_c, j, pr_s, phase, pr_ph, pr_e, id, pr,
                            ProcList_, i_, ProcList_s, i_s, ProcList, i, x >>

doneRank(self) == /\ pc[self] = "doneRank"
                  /\ rankDone' = [rankDone EXCEPT ![self] = TRUE]
                  /\ pc' = [pc EXCEPT ![self] = "Done"]
                  /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                                  ckptingEnabled, freePass, PHASE_1, PHASE_2,
                                  comms, ckptCount, sendCkptSignal, stack, id_,
                                  pr_, pr_c, j, pr_s, phase, pr_p, pr_ph, pr_e,
                                  id, pr, ProcList_, i_, ProcList_s, i_s,
                                  ProcList, i, x >>

r(self) == P1(self) \/ P2(self) \/ P3(self) \/ P4(self) \/ P5(self)
              \/ P6(self) \/ doneRank(self)

CO1 == /\ pc["dmtcp_coord"] = "CO1"
       /\ stack' = [stack EXCEPT !["dmtcp_coord"] = << [ procedure |->  "bcastCkptingMsg",
                                                         pc        |->  "Done" ] >>
                                                     \o stack["dmtcp_coord"]]
       /\ pc' = [pc EXCEPT !["dmtcp_coord"] = "BC1"]
       /\ UNCHANGED << sendCkptIntent, recvdCkptSignal, ckptPending,
                       ckptingEnabled, freePass, rankDone, PHASE_1, PHASE_2,
                       comms, ckptCount, sendCkptSignal, id_, pr_, pr_c, j,
                       pr_s, phase, pr_p, pr_ph, pr_e, id, pr, ProcList_, i_,
                       ProcList_s, i_s, ProcList, i, x >>

coord == CO1

Next == coord
           \/ (\E self \in ProcSet:  \/ MPI_Barrier(self) \/ checkCkptSignal(self)
                                     \/ stop(self) \/ phase1Stop(self)
                                     \/ phase2Stop(self) \/ enterCkpt(self)
                                     \/ atomicBarrierAndDisableCkpting(self)
                                     \/ sendFreePassToBlockedRanks(self)
                                     \/ sendCkptPendingToAllRanks(self)
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
\* Last modified Fri Nov 02 14:47:02 EDT 2018 by rohgarg
\* Last modified Fri Nov 02 02:37:55 EDT 2018 by rgarg
\* Created Thu Nov 01 22:23:18 EDT 2018 by rgarg
