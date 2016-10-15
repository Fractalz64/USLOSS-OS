#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>

/* ------------------------- Prototypes ----------------------------------- */
void requireKernelMode(char *);

/* -------------------------- Globals ------------------------------------- */
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
    systemCallVec[SYS_SEMV] = sempV;
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
    pid = spawnReal("start3", start3, NULL, USLOSS_MIN_STACK, 3);

    /* Call the waitReal version of your wait code here.
     * You call waitReal (rather than Wait) because start2 is running
     * in kernel (not user) mode.
     */
    pid = waitReal(&status);

} /* start2 */


/* ------------------------------------------------------------------------
   Name - spawn
   Purpose - Extracts arguments and checks for correctness.
   Parameters - systemArgs containing arguments.
   Returns - 
   ----------------------------------------------------------------------- */
void spawn(systemArgs *args) 
{
    requireKernelMode("spawn");

    int (*func)(char *) = args->arg1;
    char *arg = args->arg2;
    int stack_size = (int) args->arg3;
    int priority = (int) args->arg4;    
    char *name = (char *)(args->arg5);

    int pid = spawnReal(name, func, arg, stack_size, priority);

    if (stack_size < USLOSS_MIN_STACK) { 
    	args->arg4 = -1;
    }

    // terminate self if zapped
    if (isZapped())
        terminateReal(); // need to write this

    // switch to user mode
  	setUserMode();

    // swtich back to kernel mode and put values for Spawn
    args->arg1 = pid;
    args->arg4 = 0;// this should be -1 if args are not correct but fork1 checks those so idk if we are supposed to do it here too, or what
    return;
} /* spawn */


/* ------------------------------------------------------------------------
   Name - spawnLaunch
   Purpose - sets up process table entry for new process and starts
                running it in user mode
   Parameters - 
   Returns - 
   ----------------------------------------------------------------------- */
void spawnLaunch(char startArg) {
    requireKernelMode("spawnLaunch");

    // now the process is running so we can get its pid and set up its stuff
    procPtr3 proc = &ProcTable3[getpid() % MAXPROC]; 
    proc->pid = getpid();
    proc->mbox = &MboxCreate(0, 0); // create proc's 0 slot mailbox

    // now block so we can get info from spawnReal
    Send(proc->mbox->mboxID, 0, 0);

    // switch to user mode

    // call the function to start the process
    // this should return the result to launch in phase1
    return proc->startFunc(startArg);
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
    
    // fork the process and get its pid
    int pid = fork1(name, spawnLaunch, arg, stack_size, priority);

    // return -1 if fork failed
    if (pid < 0)
        return -1;

    // now we have the pid, we can access the proc table entry created by spawnLaunch
    procPtr3 proc = ProcTable3[pid % MAXPROC]; 

    // give proc its start function
    proc->startFunc = func;

    // unblock the process so spawnLaunch can start it
    Receive(proc->mbox->mboxID, 0, 0);

    return pid;
} /* spawnReal */


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
} /* requireKernelMode */

void emptyProc(int pid) {
    // test if in kernel mode; halt if in user mode
    requireKernelMode("emptyProc()"); 

    int i = pid % MAXPROC;

    ProcTable3[i].status = EMPTY; // set status to be open
    ProcTable3[i].pid = -1; // set pid to -1 to show it hasn't been assigned
    ProcTable3[i].nextProcPtr = NULL; // set pointers to null
    ProcTable3[i].nextSiblingPtr = NULL;
    ProcTable3[i].nextDeadSibling = NULL;
    ProcTable3[i].startFunc = NULL;
    ProcTable3[i].priority = -1;
    ProcTable3[i].stack = NULL;
    ProcTable3[i].stackSize = -1;
    ProcTable3[i].parentPtr = NULL;
    initProcQueue(&ProcTable3[i].childrenQueue, CHILDREN); 
    initProcQueue(&ProcTable3[i].deadChildrenQueue, DEADCHILDREN); 
    initProcQueue(&ProcTable3[i].zapQueue, ZAP); 
    ProcTable3[i].zapStatus = 0;
    ProcTable3[i].timeStarted = -1;
    ProcTable3[i].cpuTime = -1;
    ProcTable3[i].sliceTime = 0;
    ProcTable3[i].name[0] = 0;

    ProcTable3[i].mutex = MboxCreate(0,0);
}


/* ------------------------------------------------------------------------
   Name - setUserMode
   Purpose - 
   Parameters -
   Side Effects - 
   ------------------------------------------------------------------------ */
void setUserMode()
{
    USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
}

/* ------------------------------------------------------------------------
   Name - wait
   Purpose - 
   Parameters -
   Side Effects - 
   ------------------------------------------------------------------------ */
void wait(systemArgs *args)
{
	int pid;
	int status;

	pid = waitReal(&status);

	args->arg1 = pid;
	args->arg2 = status;

}

int waitReal(int *arg) 
{
	int pid = join(arg);

	return pid;
}

void terminate(systemArgs *args)
{
	terminateReal(args->arg1);
}

void terminateReal(int status) 
{

    while (ProcTable3[pid%MAXPROC].childrenQueue.size > 0) {
        procPtr3 child = deq(ProcTable3[pid%MAXPROC].childrenQueue);
        emptyProc(child->pid);
    }
	emptyProc(getpid());
	quit(status);
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