#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>
#include <sems.h>
#include <string.h>

/* ------------------------- Prototypes ----------------------------------- */
void requireKernelMode(char *);
void nullsys3(systemArgs *);
void spawn(systemArgs *);
void wait(systemArgs *);
void terminate(systemArgs *);
void semCreate(systemArgs *);
void semP(systemArgs *);
void semV(systemArgs *);
void semFree(systemArgs *);
void getTimeOfDay(systemArgs *);
void cpuTime(systemArgs *);
void getPID(systemArgs *);
int spawnReal(char *, int(*)(char *), char *, int, int);
int spawnLaunch(char *);
int waitReal(int *);
void terminateReal(int);
void emptyProc(int);
void initProc(int);
void setUserMode();
void initProcQueue(procQueue*, int);
void enq(procQueue*, procPtr3);
procPtr3 deq(procQueue*);
procPtr3 peek(procQueue*);
void removeChild(procQueue*, procPtr3);
extern int start3();

/* -------------------------- Globals ------------------------------------- */
int debug3 = 1;

int sems[MAXSEMS];
procStruct3 ProcTable3[MAXPROC];

int 
start2(char *arg)
{
    int pid;
    int status;
    /*
     * Check kernel mode here.
     */
    requireKernelMode("start2");

    /*
     * Data structure initialization as needed...
     */

    int i;
    // populate proc table
    for (i = 0; i < MAXPROC; i++) {
        emptyProc(i);
    }

    // populate system call vec
    for (i = 0; i < USLOSS_MAX_SYSCALLS; i++) {
        systemCallVec[i] = nullsys3;
    }
    systemCallVec[SYS_SPAWN] = spawn;
    systemCallVec[SYS_WAIT] = wait;
    systemCallVec[SYS_TERMINATE] = terminate;
    systemCallVec[SYS_SEMCREATE] = semCreate;
    systemCallVec[SYS_SEMP] = semP;
    systemCallVec[SYS_SEMV] = semV;
    systemCallVec[SYS_SEMFREE] = semFree;
    systemCallVec[SYS_GETTIMEOFDAY] = getTimeOfDay;
    systemCallVec[SYS_CPUTIME] = cpuTime;
    systemCallVec[SYS_GETPID] = getPID;

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscallHandler; spawnReal is the function that
     * contains the implementation and is called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes USLOSS_Syscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawnReal().
     *
     * Here, we only call spawnReal(), since we are already in kernel mode.
     *
     * spawnReal() will create the process by using a call to fork1 to
     * create a process executing the code in spawnLaunch().  spawnReal()
     * and spawnLaunch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawnReal() will
     * return to the original caller of Spawn, while spawnLaunch() will
     * begin executing the function passed to Spawn. spawnLaunch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawnReal() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and 
     * return to the user code that called Spawn.
     */
    if (debug3)
        USLOSS_Console("Spawning start3...\n");
    pid = spawnReal("start3", start3, NULL, USLOSS_MIN_STACK, 3);

    /* Call the waitReal version of your wait code here.
     * You call waitReal (rather than Wait) because start2 is running
     * in kernel (not user) mode.
     */
    pid = waitReal(&status);

    return pid;
} /* start2 */


/* ------------------------------------------------------------------------
   Name - spawn
   Purpose - Extracts arguments and checks for correctness.
   Parameters - systemArgs containing arguments.
   Returns - nothing, but it changes the systemArgs
   ----------------------------------------------------------------------- */
void spawn(systemArgs *args) 
{
    requireKernelMode("spawn");

    if (debug3)
        USLOSS_Console("in spawn...\n");

    int (*func)(char *) = args->arg1;
    char *arg = args->arg2;
    int stack_size = (int) ((long)args->arg3);
    int priority = (int) ((long)args->arg4);    
    char *name = (char *)(args->arg5);

    if (debug3)
        USLOSS_Console("spawn: args are: name = %s, stack size = %d, priority = %d\n", name, stack_size, priority);

    int pid = spawnReal(name, func, arg, stack_size, priority);
    int status = 0;

    // terminate self if zapped
    if (isZapped())
        terminateReal(1); 

    // switch to user mode
  	setUserMode();

    // swtich back to kernel mode and put values for Spawn
    args->arg1 = &pid;
    args->arg4 = &status;// this should be -1 if args are not correct but fork1 checks those so idk if we are supposed to do it here too, or what
} 


/* ------------------------------------------------------------------------
   Name - spawnLaunch
   Purpose - sets up process table entry for new process and starts
                running it in user mode
   Parameters - 
   Returns - 
   ----------------------------------------------------------------------- */
int spawnLaunch(char *startArg) {
    requireKernelMode("spawnLaunch");

    if (debug3)
        USLOSS_Console("in spawnLaunch\n");


    procPtr3 proc = &ProcTable3[getpid() % MAXPROC]; 

    // if spawnReal hasn't done it yet, set up proc table entry
    if (proc->pid == -1) 
        initProc(proc->pid);

    // block until spawnReal is done
    MboxSend(proc->mboxID, 0, 0);

    // switch to user mode
    setUserMode();

    // call the function to start the process
    proc->startFunc(startArg);

    //terminate(0);
    return 0;
}


/* ------------------------------------------------------------------------
   Name - spawnReal
   Purpose - forks a process running spawnLaunch
   Parameters - same as fork1
   Returns - pid
   ----------------------------------------------------------------------- */
int spawnReal(char *name, int (*func)(char *), char *arg, int stack_size, int priority) 
{
    requireKernelMode("spawnReal");

    if (debug3)
        USLOSS_Console("in spawnReal\n");

    // fork the process and get its pid
    int pid = fork1(name, spawnLaunch, arg, stack_size, priority);

    if (debug3)
        USLOSS_Console("spawnReal(): process name = %s, pid = %d\n", name, pid);

    // return -1 if fork failed
    if (pid < 0)
        return -1;

    // now we have the pid, we can set up the proc table entry
    procPtr3 proc = &ProcTable3[pid % MAXPROC]; 

    // if spawnLaunch hasn't done it yet, set up proc table entry
    if (proc->pid == -1) 
        initProc(proc->pid);
    
    proc->startFunc = func; // give proc its starting function

    // unblock the process so spawnLaunch can start it
    MboxReceive(proc->mboxID, 0, 0);

    return pid;
} 


/* ------------------------------------------------------------------------
   Name - wait
   Purpose - waits for a child process to terminate
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void wait(systemArgs *args)
{
    requireKernelMode("wait");

	int *status = args->arg2;
	args->arg1 = (void *) waitReal(status);
    // switch back to user mode
    setUserMode();
}


/* ------------------------------------------------------------------------
   Name - waitReal
   Purpose - calls join and returns pid of joined child
   Parameters - pointer to child's exit status to give to join
   Returns - pid of joined child
   ------------------------------------------------------------------------ */
int waitReal(int *status) 
{
    requireKernelMode("waitReal");

    if (debug3)
        USLOSS_Console("in waitReal\n");
	int pid = join(status);
	return pid;
}


/* ------------------------------------------------------------------------
   Name - terminate
   Purpose - terminates the invoking process and all of its children
   Parameters - termination code
   Returns - nothing
   ------------------------------------------------------------------------ */
void terminate(systemArgs *args)
{
    requireKernelMode("terminate");

    int status = (int)((long)args->arg1);
	terminateReal(status);
    // switch back to user mode
    setUserMode();
}


/* ------------------------------------------------------------------------
   Name - terminateReal
   Purpose - terminates the invoking process and all of its children
   Parameters - termination code
   Returns - nothing
   ------------------------------------------------------------------------ */
void terminateReal(int status) 
{
    requireKernelMode("terminateReal");

    if (debug3)
        USLOSS_Console("in terminateReal, status = %d\n", status);

    procPtr3 proc = &ProcTable3[getpid() % MAXPROC];
    USLOSS_Console("child qeue size = %d\n", proc->childrenQueue.size);
    while (proc->childrenQueue.size > 0) {
        USLOSS_Console("ndfndjfj\n");
        procPtr3 child = deq(&proc->childrenQueue);
        zap(child->pid);
    }
    quit(status);
}


/* ------------------------------------------------------------------------
   Name - semCreate
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void semCreate(systemArgs *args)
{

}


/* ------------------------------------------------------------------------
   Name - semP
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void semP(systemArgs *args)
{

}


/* ------------------------------------------------------------------------
   Name - semV
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void semV(systemArgs *args)
{

}


/* ------------------------------------------------------------------------
   Name - semFree
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void semFree(systemArgs *args)
{

}


/* ------------------------------------------------------------------------
   Name - getTimeOfDay
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void getTimeOfDay(systemArgs *args)
{

}


/* ------------------------------------------------------------------------
   Name - cpuTime
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void cpuTime(systemArgs *args)
{

}


/* ------------------------------------------------------------------------
   Name - getPID
   Purpose - 
   Parameters - systemArgs containing arguments
   Returns - nothing
   ------------------------------------------------------------------------ */
void getPID(systemArgs *args)
{

}


/* an error method to handle invalid syscalls */
void nullsys3(systemArgs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall %d. Terminating...\n", args->number);
    terminateReal(1);
} /* nullsys */


/* initializes proc struct */
void initProc(int pid) {
    requireKernelMode("initProc()"); 

    int i = pid % MAXPROC;

    ProcTable3[i].pid = pid; 
    ProcTable3[i].mboxID = MboxCreate(0, 0);
    ProcTable3[i].startFunc = NULL;
    ProcTable3[i].nextProcPtr = NULL; 
    initProcQueue(&ProcTable3[i].childrenQueue, CHILDREN);
}


/* empties proc struct */
void emptyProc(int pid) {
    requireKernelMode("emptyProc()"); 

    int i = pid % MAXPROC;

    ProcTable3[i].pid = -1; 
    ProcTable3[i].mboxID = -1;
    ProcTable3[i].startFunc = NULL;
    ProcTable3[i].nextProcPtr = NULL; 
}


/* ------------------------------------------------------------------------
   Name - requireKernelMode
   Purpose - Checks if we are in kernel mode and prints an error messages
              and halts USLOSS if not.
   Parameters - The name of the function calling it, for the error message.
   Side Effects - Prints and halts if we are not in kernel mode
   ------------------------------------------------------------------------ */
void requireKernelMode(char *name)
{
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        USLOSS_Console("%s: called while in user mode, by process %d. Halting...\n", 
             name, getpid());
        USLOSS_Halt(1); 
    }
} 


/* ------------------------------------------------------------------------
   Name - setUserMode
   Purpose - switches to user mode
   Parameters - none
   Side Effects - none
   ------------------------------------------------------------------------ */
void setUserMode()
{
    USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
}


/* ------------------------------------------------------------------------
  Below are functions that manipulate ProcQueue:
    initProcQueue, enq, deq, removeChild and peek.
   ----------------------------------------------------------------------- */

/* Initialize the given procQueue */
void initProcQueue(procQueue* q, int type) {
  q->head = NULL;
  q->tail = NULL;
  q->size = 0;
  q->type = type;
}

/* Add the given procPtr3 to the back of the given queue. */
void enq(procQueue* q, procPtr3 p) {
  if (q->head == NULL && q->tail == NULL) {
    q->head = q->tail = p;
  } else {
    if (q->type == READYLIST)
      q->tail->nextProcPtr = p;
    else if (q->type == CHILDREN)
      q->tail->nextSiblingPtr = p;
    q->tail = p;
  }
  q->size++;
}

/* Remove and return the head of the given queue. */
procPtr3 deq(procQueue* q) {
  procPtr3 temp = q->head;
  if (q->head == NULL) {
    return NULL;
  }
  if (q->head == q->tail) {
    q->head = q->tail = NULL; 
  }
  else {
    if (q->type == READYLIST)
      q->head = q->head->nextProcPtr;  
    else if (q->type == CHILDREN)
      q->head = q->head->nextSiblingPtr;  
  }
  q->size--;
  return temp;
}

/* Remove the child process from the queue */
void removeChild(procQueue* q, procPtr3 child) {
  if (q->head == NULL || q->type != CHILDREN)
    return;

  if (q->head == child) {
    deq(q);
    return;
  }

  procPtr3 prev = q->head;
  procPtr3 p = q->head->nextSiblingPtr;

  while (p != NULL) {
    if (p == child) {
      if (p == q->tail)
        q->tail = prev;
      else
        prev->nextSiblingPtr = p->nextSiblingPtr->nextSiblingPtr;
      q->size--;
    }
    prev = p;
    p = p->nextSiblingPtr;
  }
}

/* Return the head of the given queue. */
procPtr3 peek(procQueue* q) {
  if (q->head == NULL) {
    return NULL;
  }
  return q->head;   
}