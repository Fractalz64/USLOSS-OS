#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <providedPrototypes.h>
#include <usyscall.h>
#include <stdlib.h> /* needed for atoi() */

#define ABS(a,b) (a-b > 0 ? a-b : -(a-b))

int 	running;

static int ClockDriver(char *);
static int DiskDriver(char *);
static int TermDriver(char *);
extern int start4();

void sleep(systemArgs *);
void diskRead(systemArgs *);
void diskWrite(systemArgs *);
void diskSize(systemArgs *);
void termRead(systemArgs *);
void termWrite(systemArgs *);

procStruct ProcTable[MAXPROC];
procQueue sleepQ;

void
start3(void)
{
    char	name[128];
    char    termbuf[10];
    char    diskbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;

    /*
     * Check kernel mode here.
     */
    requireKernelMode("start3");

    // initialize proc table
    for (i = 0; i < MAXPROC; i++) {
        emptyProc(i);
    }

    // sleep queue
    initProcQueue(&sleepQ, SLEEP);

    // initialize systemCallVec
    systemCallVec[SYS_SLEEP] = sleep;
    systemCallVec[SYS_DISKREAD] = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE] = diskSize;
    systemCallVec[SYS_TERMREAD] = termRead;
    systemCallVec[SYS_TERMWRITE] = termWrite;

    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    running = semcreateReal(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
    	USLOSS_Console("start3(): Can't create clock driver\n");
    	USLOSS_Halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running.
     */

    sempReal(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
        sprintf(diskbuf, "%d", i);
        pid = fork1(name, DiskDriver, diskbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }
    }

    // May be other stuff to do here before going on to terminal drivers

    /*
     * Create terminal device drivers.
     */


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters, as shown in provided_prototypes.h
     */
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);

    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver

    // eventually, at the end:
    quit(0);
    
}

static int
ClockDriver(char *arg)
{
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(! isZapped()) {
	result = waitdevice(USLOSS_CLOCK_DEV, 0, &status);
	if (result != 0) {
	    return 0;
	}
	/*
	 * Compute the current time and wake up any processes
	 * whose time has come.
	 */
    }
}

static int
DiskDriver(char *arg)
{
    return 0;
}

static int
TermDriver(char *arg)
{
    return 0;
}

static int 
TermReader(char * arg) 
{
    return 0;
}

static int 
TermWriter(char * arg) 
{
    return 0;
}

// sleep function value extraction
void sleep(systemArgs * args) {
    int seconds = (long) args->arg1;
    int retval = sleepReal(seconds);
    args->arg4 = (void *) ((long) retval);
    setUserMode();
}

// real sleep function
int sleepReal(int seconds) {
    if (seconds < 0) {
        return -1;
    }

    ProcTable[getpid() % MAXPROC].wakeTime = USLOSS_CLOCK() + seconds;

}


void diskRead(systemArgs * args) {
    
}

void diskWrite(systemArgs * args) {
    
}

void diskSize(systemArgs * args) {
    
}

void termRead(systemArgs * args) {
    
}

void termWrite(systemArgs * args) {
    
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
   Side Effects - switches to user mode
   ------------------------------------------------------------------------ */
void setUserMode()
{
    USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
}

/* initializes proc struct */
void initProc(int pid) {
    requireKernelMode("initProc()"); 

    int i = pid % MAXPROC;

    ProcTable[i].pid = pid; 
    ProcTable[i].mboxID = MboxCreate(0, 0);
    ProcTable[i].startFunc = NULL;
    ProcTable[i].nextProcPtr = NULL; 
    initProcQueue(&ProcTable[i].childrenQueue, CHILDREN);
}

/* empties proc struct */
void emptyProc(int pid) {
    requireKernelMode("emptyProc()"); 

    int i = pid % MAXPROC;

    ProcTable[i].pid = -1; 
    ProcTable[i].mboxID = -1;
    ProcTable[i].startFunc = NULL;
    ProcTable[i].nextProcPtr = NULL; 

    ProcTable[i].nextSleepPtr = NULL;
    ProcTable[i].wakeTime = -1;
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

/* Add the given procPtr to the back of the given queue. */
// q for sleeping is really annoying, i'll f inish it later
void enq(procQueue* q, procPtr p) {
  if (q->head == NULL && q->tail == NULL) {
    q->head = q->tail = p;
  } else {
    if (q->type == BLOCKED)
      q->tail->nextProcPtr = p;
    else if (q->type == CHILDREN)
      q->tail->nextSiblingPtr = p;
    else if (q->type == SLEEP) {
        procPtr curr = q->head;
        procPtr prev = curr;
        while (curr->wakeTime < p->wakeTime) {
            prev = curr;
            curr = curr->nextSleepPtr;
        }
        p->nextSleepPtr = curr;
        prev->nextSleepPtr = p;
        q->size++;
        return;
    }
    q->tail = p;
  }
  q->size++;
}

/* Remove and return the head of the given queue. */
procPtr deq(procQueue* q) {
  procPtr temp = q->head;
  if (q->head == NULL) {
    return NULL;
  }
  if (q->head == q->tail) {
    q->head = q->tail = NULL; 
  }
  else {
    if (q->type == BLOCKED)
      q->head = q->head->nextProcPtr;  
    else if (q->type == CHILDREN)
      q->head = q->head->nextSiblingPtr; 
    else if (q->type == SLEEP) 
      q->head = q->head->nextSleepPtr; 
  }
  q->size--;
  return temp;
}

/* Remove the child process from the queue */
void removeChild3(procQueue* q, procPtr child) {
  if (q->head == NULL || q->type != CHILDREN)
    return;

  if (q->head == child) {
    deq3(q);
    return;
  }

  procPtr prev = q->head;
  procPtr p = q->head->nextSiblingPtr;

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
procPtr peek(procQueue* q) {
  if (q->head == NULL) {
    return NULL;
  }
  return q->head;   
}