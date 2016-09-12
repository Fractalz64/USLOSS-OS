/* ------------------------------------------------------------------------
   phase1.c

   University of Arizona
   Computer Science 452
   Fall 2015

   ------------------------------------------------------------------------ */

#include "phase1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (char *);
extern int start1 (char *);
void dispatcher(void);
void launch();
static void checkDeadlock();
void enableInterrupts(void);
void requireKernelMode(char *);
void clockHandler();

/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 0;

// the process table
procStruct ProcTable[MAXPROC];

// Process lists
// static procPtr ReadyList;
procQueue ReadyList[SENTINELPRIORITY];

// current process ID
procPtr Current;

// the next pid to be assigned
unsigned int nextPid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
             Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("startup()"); 

    int result; // value returned by call to fork1()

    // initialize the process table
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");
    int i; // can't declare loop variables inside the loop because its not in C99 mode
    // init the fields of each process
    for (i = 0; i < MAXPROC; i++) {
        ProcTable[i].status = UNUSED; // set status to be open
        ProcTable[i].pid = -1; // set pid to -1 to show it hasn't been assigned
        ProcTable[i].nextProcPtr = NULL; // set pointers to null
        ProcTable[i].nextSiblingPtr = NULL;
        ProcTable[i].nextQuitSibling = NULL;
        ProcTable[i].startFunc = NULL;
        ProcTable[i].priority = -1;
        ProcTable[i].stack = NULL;
        ProcTable[i].stackSize = -1;
        ProcTable[i].parentPtr = NULL;
        initProcQueue(&ProcTable[i].childrenQueue, CHILDREN); 
        initProcQueue(&ProcTable[i].quitChildrenQueue, QUITCHILDREN); 
		ProcTable[i].zapStatus = 0;
    }

    // Initialize the ReadyList, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    for (i = 0; i < SENTINELPRIORITY; i++) {
        initProcQueue(&ReadyList[i], READYLIST);
    }

    // initialize current process pointer so that dispatcher doesn't have issues at startup
    Current = &ProcTable[MAXPROC-1]; 

    // Initialize the clock interrupt handler
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;

    // startup a sentinel process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for sentinel\n");
    result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                    SENTINELPRIORITY);
    if (result < 0) {
        if (DEBUG && debugflag) {
            USLOSS_Console("startup(): fork1 of sentinel returned error, ");
            USLOSS_Console("halting...\n");
        }
        USLOSS_Halt(1);
    }
  
    // start the test process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for start1\n");
    result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
    if (result < 0) {
        USLOSS_Console("startup(): fork1 for start1 returned an error, ");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("startup(): Should not see this message! ");
    USLOSS_Console("Returned from fork1 call that created start1\n");

    return;
} /* startup */


/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("finish()"); 

    if (DEBUG && debugflag)
        USLOSS_Console("in finish...\n");
} /* finish */


/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*startFunc)(char *), char *arg,
          int stacksize, int priority)
{
    int procSlot = -1;

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);

    // test if in kernel mode; halt if in user mode
    requireKernelMode("fork1()"); 
    disableInterrupts(); 

    // Return if stack size is too small
    if (stacksize < USLOSS_MIN_STACK) { // found in usloss.h
        USLOSS_Console("fork1(): Stack size too small.\n");
        return -2; // from the phase1 pdf
    }

    // Return if startFunc is null
    if (startFunc == NULL) { 
        USLOSS_Console("fork1(): Start function is null.\n");
        return -1; // from the phase1 pdf
    }

    // Return if name is null
    if (name == NULL) { 
        USLOSS_Console("fork1(): Process name is null.\n");
        return -1; // from the phase1 pdf
    }

    // Return if priority is out of range (except sentinel, which is below the min)
    if ((priority > MINPRIORITY || priority < MAXPRIORITY) && startFunc != sentinel) { 
        USLOSS_Console("fork1(): Priority is out of range.\n");
        return -1; // from the phase1 pdf
    }

    // find an empty slot in the process table
    //procSlot = nextPid%MAXPROC;
     int i; // can't declare loop variables inside the loop because its not in C99 mode
     for (i = 0; i < MAXPROC; i++) {
         if (ProcTable[i].status == UNUSED) { // found an empty spot
             procSlot = i;
             break; 
         }
     }

    // handle case where there is no empty spot
    if (ProcTable[procSlot].status != UNUSED) {
        USLOSS_Console("fork1(): No empty slot on the process table.\n");
        return -1;
    }

    // fill-in entry in process table */
    if ( strlen(name) >= (MAXNAME - 1) ) {
        USLOSS_Console("fork1(): Process name is too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    strcpy(ProcTable[procSlot].name, name);
    ProcTable[procSlot].startFunc = startFunc;
    if ( arg == NULL )
        ProcTable[procSlot].startArg[0] = '\0';
    else if ( strlen(arg) >= (MAXARG - 1) ) {
        USLOSS_Console("fork1(): argument too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    else
        strcpy(ProcTable[procSlot].startArg, arg);

    // allocate the stack
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): allocating the stack, size %d\n", stacksize);
    ProcTable[procSlot].stack = (char *) malloc(stacksize);
    ProcTable[procSlot].stackSize = stacksize;

    // make sure malloc worked, halt otherwise
    if (ProcTable[procSlot].stack == NULL) {
        USLOSS_Console("fork1(): Malloc failed.  Halting...\n");
        USLOSS_Halt(1);
    }

    // set the process id
    ProcTable[procSlot].pid = nextPid++;
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): set process id to %d\n", ProcTable[procSlot].pid);
    
    // set the process priority
    ProcTable[procSlot].priority = priority;
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): set process priority to %d\n", ProcTable[procSlot].priority);

    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC)
    USLOSS_ContextInit(&(ProcTable[procSlot].state), USLOSS_PsrGet(),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    // More stuff to do here...
    // add process to parent's (current's) list of children, iff parent exists 
    if (Current->pid > -1) {
        if (DEBUG && debugflag)
            USLOSS_Console("fork1(): adding child to parent's list of children...\n");
        enq(&Current->childrenQueue, &ProcTable[procSlot]);
        ProcTable[procSlot].parentPtr = Current; // set parent pointer
        if (DEBUG && debugflag)
            USLOSS_Console("fork1(): parent id: %d, child id: %d\n", ProcTable[procSlot].parentPtr->pid, peek(&Current->childrenQueue)->pid);

    }

    // add process to the approriate ready list
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): adding process to ready list %d...\n", priority);
    enq(&ReadyList[priority-1], &ProcTable[procSlot]);
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): ready list %d size = %d\n", priority-1, ReadyList[priority-1].size);
    ProcTable[procSlot].status = READY; // set status to READY

    // let dispatcher decide which process runs next
    if (startFunc != sentinel) { // don't dispatch sentinel!
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): calling dispatcher...\n\n");
        dispatcher(); 
    }

    // enable interrupts for the parent
    enableInterrupts();

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): returning...\n");
    return ProcTable[procSlot].pid;  // return child's pid
} /* fork1 */


/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("launch()"); 
    disableInterrupts(); 

    int result;

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): started\n");

    // Enable interrupts
    enableInterrupts();

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): starting current process: %d\n\n", Current->pid);

    Current->status = RUNNING; // set status to RUNNING

    // Call the function passed to fork1, and capture its return value
    result = Current->startFunc(Current->startArg);

    if (DEBUG && debugflag)
        USLOSS_Console("Process %d returned to launch\n", Current->pid);

    quit(result);

} /* launch */


/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
             -1 if the process was zapped in the join
             -2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *status)
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("join()"); 
    disableInterrupts(); 

    if (DEBUG && debugflag)
        USLOSS_Console("join(): In join, pid = %d\n", Current->pid);

    // check if has children
    if (Current->childrenQueue.size == 0) {
        USLOSS_Console("join(): No children\n");
        return -2;
    }

    // if current has no quit children, block self and wait.
    if (Current->quitChildrenQueue.size == 0) {
        Current->status = BLOCKED;
        if (DEBUG && debugflag)
            USLOSS_Console("pid %d blocked at priority %d \n\n" , Current->pid, Current->priority - 1);
        deq(&ReadyList[(Current->priority - 1)]); // remove from list
        dispatcher();    
    }

    // get the earliest quit child
    procPtr child = deq(&Current->quitChildrenQueue);
    if (DEBUG && debugflag)
        USLOSS_Console("Found quit child pid = %d, status = %d\n\n", child->pid, child->quitStatus);
    *status = child->quitStatus;
	
	if (child->status == QUIT) {
		ProcTable[(child->pid - 1) % MAXPROC].status = UNUSED;
		deq(&Current->childrenQueue);
	}
	
    return child->pid;
} /* join */


/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int status)
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("quit()"); 
    disableInterrupts(); 

    p1_quit(Current->pid);

    // print error message and halt if process with active children calls quit
    // loop though children to find if any are active
    procPtr childPtr = peek(&Current->childrenQueue);
    while (childPtr != NULL) {
        if (childPtr->status != QUIT) {
            USLOSS_Console("quit(): Error: Process %s has active children.  Halting...\n", Current->name);
			USLOSS_Console("quit(): Error: Child Name: %s status: %d\n", childPtr->name, childPtr->status);
            USLOSS_Halt(1);
        }
        childPtr = childPtr->nextSiblingPtr;
    }

    Current->status = QUIT; // change status to QUIT
    Current->quitStatus = status; // store the given status
     if (DEBUG && debugflag)
        USLOSS_Console("quit(): removing process id %d from ready list %d...\n", Current->pid, Current->priority-1);
    deq(&ReadyList[Current->priority-1]); // remove self from ready list

    if (Current->parentPtr != NULL) {
        if (DEBUG && debugflag)
            USLOSS_Console("quit(): adding process to parent's list if quit children...\n\n");
        enq(&Current->parentPtr->quitChildrenQueue, Current); // add self to parent's quit children list

        if (Current->parentPtr->status == BLOCKED) { // unblock parent
            Current->parentPtr->status = READY;
            enq(&ReadyList[Current->parentPtr->priority-1], Current->parentPtr);
        }
    }

    // to do later: unblock processes that zap'd this process

    // remove any quit children current has form the process table

    dispatcher(); // call dispatcher to run next process
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("dispatcher()"); 
    disableInterrupts(); 

    procPtr nextProcess = NULL;

    // Find the highest priority non-empty process queue
    int i;
    for (i = 0; i < SENTINELPRIORITY; i++) {
        if (ReadyList[i].size > 0) {
            nextProcess = peek(&ReadyList[i]);
            break;
        }
    }

    // Print message and return if the ready list is empty
    if (nextProcess == NULL) {
        USLOSS_Console("dispatcher(): ready list is empty!\n");
        return;
    }

    if (DEBUG && debugflag)
        USLOSS_Console("dispatcher(): next process is %s\n\n", nextProcess->name);

    // update current
    procPtr old = Current;
    Current = nextProcess;

    // your dispatcher should call p1_switch(int old, int new) with the 
    // PID’s of the process that was previously running and the next process to run. 
    // You will enable interrupts before you call USLOSS_ContextSwitch. 
    // The call to p1_switch should be called just before you enable interrupts
    p1_switch(old->pid, nextProcess->pid);
    enableInterrupts();
    USLOSS_ContextSwitch(&old->state, &nextProcess->state);
} /* dispatcher */


/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
             processes are blocked.  The other is to detect and report
             simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
                   and halt.
   ----------------------------------------------------------------------- */
int sentinel (char *dummy)
{
    // test if in kernel mode; halt if in user mode
    requireKernelMode("sentinel()"); 

    if (DEBUG && debugflag)
        USLOSS_Console("sentinel(): called\n");
    while (1)
    {
        checkDeadlock();
        USLOSS_WaitInt();
    }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
    if (DEBUG && debugflag)
        USLOSS_Console("checkDeadlock(): called\n");

    // check if all processes have quit
    int i;
    for (i = 0; i < MAXPROC; i++) { 
        if (ProcTable[i].status != QUIT && ProcTable[i].status != UNUSED && ProcTable[i].startFunc != sentinel) {
            USLOSS_Console("checkDeadlock(): Processes remain. Abnormal termination. \n");
            USLOSS_Halt(1);
        }
    }

    USLOSS_Console("All processes completed.\n");
    USLOSS_Halt(0);
} /* checkDeadlock */


/* ------------------------------------------------------------------------
   Name - clockHandler
   Purpose - Checks if the current process has exceeded its time slice. 
            Calls dispatcher() if necessary.
   Parameters - none
   Returns - nothing
   Side Effects - may call dispatcher()
   ----------------------------------------------------------------------- */
void clockHandler()
{
    if (DEBUG && debugflag)
        USLOSS_Console("clockHandler(): called\n");
} /* clockHandler */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        // We ARE in kernel mode
        USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
        if (DEBUG && debugflag)
            USLOSS_Console("Interrupts disabled.\n");
} /* disableInterrupts */


/*
 * Enables the interrupts.
 */
void enableInterrupts()
{
    // turn the interrupts ON iff we are in kernel mode
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("enable interrupts\n");
        USLOSS_Halt(1);
    } else
        // We ARE in kernel mode
        USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
        if (DEBUG && debugflag)
            USLOSS_Console("Interrupts enabled.\n");
} /* enableInterrupts */


/* ------------------------------------------------------------------------
   Name - requireKernelMode
   Purpose - Checks if we are in kernel mode and prints an error messages
              and halts USLOSS if not.
              It should be called by every function in phase 1.
   Parameters - The name of the function calling it, for the error message.
   Side Effects - Prints and halts if we are not in kernel mode
   ------------------------------------------------------------------------ */
void requireKernelMode(char *name)
{
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        USLOSS_Console("%s: Not in kernel mode. Halting...\n", name);
        USLOSS_Halt(1); // from phase1 pdf
    }
} /* requireKernelMode */


/* ------------------------------------------------------------------------
   Name - zap
   Purpose - 
   Parameters - 
   Returns -  
   Side Effects - 
   ----------------------------------------------------------------------- */
int zap(int pid) {
	int i;
	procPtr process; 
	if (Current->pid == pid) {
		USLOSS_Console("Invalid zap pid\n");
		USLOSS_Halt(1);
	}
	
	for (i = 0; i < MAXPROC; i++) {
		if (ProcTable[i].pid == pid) {
			ProcTable[i].zapStatus = 1;
			process = &ProcTable[i];
		}	
	}
	
	while(1) {
		if (Current->zapStatus == 1) 
			return -1;
		if (process->status == QUIT) 
			return 0;
	}
	
	
	
} 

/* ------------------------------------------------------------------------
   Name - isZapped
   Purpose - 
   Parameters - 
   Returns -  
   Side Effects - 
   ----------------------------------------------------------------------- */
int isZapped() {
	return Current->zapStatus;
}


/* ------------------------------------------------------------------------
   Name - getpid
   Purpose - return pid
   Parameters - none
   Returns - current process pid
   Side Effects - ^
   ----------------------------------------------------------------------- */
int getpid() {
	return Current->pid;	
}

/* ------------------------------------------------------------------------
   Name - dumpProcesses
   Purpose - Prints information about each process on the process table,
             for debugging.
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void dumpProcesses()
{
	//PID	Parent	Priority	Status		# Kids	CPUtime	Name
    int i;
	USLOSS_Console("%s%10s%10s%10s%10s%10s%10s\n", "PID", "NAME", "PARENT", 
				   "PRIORITY", "STATUS", "#KIDS", "CPU_TIME");
    for (i = 0; i < MAXPROC; i++) {
		int p;
		if (ProcTable[i].parentPtr != NULL)
			p = ProcTable[i].parentPtr->pid;
		else
			p = -2;
		USLOSS_Console("%3d%10s%10d%10d%10d%10d%10d\n", ProcTable[i].pid, 
					   ProcTable[i].name, p,
					   ProcTable[i].priority, ProcTable[i].status, 
					   ProcTable[i].childrenQueue.size, -1);
    }
}