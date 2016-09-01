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


/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

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
    int result; // value returned by call to fork1()

    // initialize the process table
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");
    int i; // can't declare loop variables inside the loop because its not in C99 mode
    for (i = 0; i < MAXPROC; i++)
        ProcTable[i].open = 1; // set each slot to be open

    // Initialize the ReadyList, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    for (i = 0; i < SENTINELPRIORITY; i++) {
        ReadyList[i].head = NULL;
        ReadyList[i].tail = NULL;
        ReadyList[i].size = 0;
    }

    // Initialize the clock interrupt handler

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
    if (DEBUG && debugflag)
        USLOSS_Console("in finish...\n");
} /* finish */


/* ------------------------------------------------------------------------
   Author: Charlie Fractal
   Name - requireKernalMode
   Purpose - Checks if we are in kernal mode and prints an error messages
              and halts USLOSS if not.
              It should be called by every function in phase 1.
   Parameters - The name of the function calling it, for the error message.
   Side Effects - Prints and halts if we are not in kernal mode
   ------------------------------------------------------------------------ */
void requireKernalMode(char *name)
{
    if (USLOSS_PsrGet() != 1) { // kernal mode is 1
        USLOSS_Console("%s: Not in kernal mode. Halting...\n", name);
        USLOSS_Halt(1); // from phase1 pdf
    }
} /* requireKernalMode */


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
    requireKernalMode("fork1()"); 

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
    int i; // can't declare loop variables inside the loop because its not in C99 mode
    for (i = 0; i < MAXPROC; i++) {
        if (ProcTable[i].open) { // found an empty spot
            procSlot = i;
            ProcTable[i].open = 0; // set slot to be taken
            break; 
        }
    }

    // handle case where there is no empty spot
    if (procSlot < 0) {
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

    // set the process id
    ProcTable[procSlot].pid = nextPid++;
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): set process id to %d\n", ProcTable[procSlot].pid);

    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC)
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): initializing context...\n");

    USLOSS_ContextInit(&(ProcTable[procSlot].state), USLOSS_PsrGet(),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    // More stuff to do here...
    // add process to the approriate ready list
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): adding process to ready list %d...\n", priority-1);
    enq(ReadyList[priority-1], &ProcTable[procSlot]);
    USLOSS_Console("fork1(): ready list %d size = %d\n", priority-1, ReadyList[priority-1].size);

    // let dispatcher decide which process runs next
    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): calling dispatcher...\n");
    dispatcher(); 

    // enable interrupts for the parent


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
    int result;

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): started\n");

    // Enable interrupts


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
    return -1;  // -1 is not correct! Here to prevent warning.
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
    p1_quit(Current->pid);
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
    procPtr nextProcess = NULL;

    // Find the highest priority non-empty process queue
    int i;
    for (i = 0; i < SENTINELPRIORITY; i++) {
        USLOSS_Console("List: %d, size = %d\n", i, ReadyList[i].size);
        if (ReadyList[i].size > 0) {
            nextProcess = deq(ReadyList[i]);
            break;
        }
    }

    // Print message and return if the ready list is empty
    if (nextProcess == NULL) {
        USLOSS_Console("dispatcher(): ready list is empty!\n");
        return;
    }

    if (DEBUG && debugflag)
        USLOSS_Console("dispatcher(): next process is \n", nextProcess->pid);

    p1_switch(Current->pid, nextProcess->pid);
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
} /* checkDeadlock */


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
} /* disableInterrupts */


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
    int i;
    for (i = 0; i < MAXPROC; i++) {
        USLOSS_Console("PID: %d\n", ProcTable[i].pid);
        USLOSS_Console("Name: %s\n", ProcTable[i].name);
        USLOSS_Console("Priority: %d\n", ProcTable[i].priority);
        USLOSS_Console("Status: %d\n", ProcTable[i].status);
        USLOSS_Console("Open: %d\n", ProcTable[i].open);
    }
}