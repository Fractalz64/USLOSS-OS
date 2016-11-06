#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <providedPrototypes.h>
#include <p4structs.h>
#include <usyscall.h>
#include <stdlib.h> /* needed for atoi() */
#include <stdio.h>

#define ABS(a,b) (a-b > 0 ? a-b : -(a-b))

int debug4 = 0;
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
int sleepReal(int);
void requireKernelMode(char *);
void emptyProc(int);
void initProc(int);
void setUserMode();
// void initProcQueue(procQueue*, int);
// void enq(procQueue*, procPtr);
// procPtr deq(procQueue*);
// procPtr peek(procQueue*);
// void removeChild3(procQueue*, procPtr);
void initHeap(heap *);
void heapAdd(heap *, procPtr);
procPtr heapPeek(heap *);
procPtr heapRemove(heap *);

procStruct ProcTable[MAXPROC];
heap sleepHeap;

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
    initHeap(&sleepHeap);

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
    // for (i = 0; i < USLOSS_DISK_UNITS; i++) {
    //     sprintf(diskbuf, "%d", i);
    //     pid = fork1("Disk driver", DiskDriver, diskbuf, USLOSS_MIN_STACK, 2);
    //     if (pid < 0) {
    //         USLOSS_Console("start3(): Can't create disk driver %d\n", i);
    //         USLOSS_Halt(1);
    //     }
    // }

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
    if (debug4) 
        USLOSS_Console("ClockDriver: running\n");

    // Infinite loop until we are zap'd
    while(! isZapped()) {
	    result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
	    if (result != 0) {
	        return 0;
	    }

	    // Compute the current time and wake up any processes whose time has come.
        procPtr proc;
        while (sleepHeap.size > 0 && USLOSS_Clock() >= heapPeek(&sleepHeap)->wakeTime) {
            proc = heapRemove(&sleepHeap);
            if (debug4) 
                USLOSS_Console("ClockDriver: Waking up process %d\n", proc->pid);
            semvReal(proc->blockSem); 
        }
    }
    return 0;
}

static int
DiskDriver(char *arg)
{
    int unit = atoi( (char *) arg);     // Unit is passed as arg.
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
    requireKernelMode("sleep");
    int seconds = (long) args->arg1;
    int retval = sleepReal(seconds);
    args->arg4 = (void *) ((long) retval);
    setUserMode();
}

// real sleep function
int sleepReal(int seconds) {
    requireKernelMode("sleepReal");
    if (seconds < 0) {
        return -1;
    }

    // init/get the process
    if (ProcTable[getpid() % MAXPROC].pid == -1) 
        initProc(getpid());
    procPtr proc = &ProcTable[getpid() % MAXPROC];

    // set wake time
    proc->wakeTime = USLOSS_Clock() + seconds*1000000;
    heapAdd(&sleepHeap, proc); // add to sleep heap
    if (debug4) 
        USLOSS_Console("sleepReal: Process %d going to sleep until %d\n", proc->pid, proc->wakeTime);
    sempReal(proc->blockSem); // block the process
    if (debug4) 
        USLOSS_Console("sleepReal: Process %d woke up, time is %d\n", proc->pid, USLOSS_Clock());
    return 0;
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
    ProcTable[i].blockSem = semcreateReal(0);
    ProcTable[i].wakeTime = -1;
}

/* empties proc struct */
void emptyProc(int pid) {
    requireKernelMode("emptyProc()"); 

    int i = pid % MAXPROC;

    ProcTable[i].pid = -1; 
    ProcTable[i].mboxID = -1;
    ProcTable[i].mboxID = -1;
    ProcTable[i].blockSem = -1;
    ProcTable[i].wakeTime = -1;
}

// /* ------------------------------------------------------------------------
//   Below are functions that manipulate ProcQueue:
//     initProcQueue, enq, deq, removeChild and peek.
//    ----------------------------------------------------------------------- */

// /* Initialize the given procQueue */
// void initProcQueue(procQueue* q, int type) {
//   q->head = NULL;
//   q->tail = NULL;
//   q->size = 0;
//   q->type = type;
// }

// /* Add the given procPtr to the back of the given queue. */
// // q for sleeping is really annoying, i'll f inish it later
// void enq(procQueue* q, procPtr p) {
//   if (q->head == NULL && q->tail == NULL) {
//     q->head = q->tail = p;
//   } else {
//     if (q->type == BLOCKED)
//       q->tail->nextProcPtr = p;
//     else if (q->type == CHILDREN)
//       q->tail->nextSiblingPtr = p;
//     else if (q->type == SLEEP) {
//         procPtr curr = q->head;
//         procPtr prev = curr;
//         while (curr->wakeTime < p->wakeTime) {
//             prev = curr;
//             curr = curr->nextSleepPtr;
//         }
//         p->nextSleepPtr = curr;
//         prev->nextSleepPtr = p;
//         q->size++;
//         return;
//     }
//     q->tail = p;
//   }
//   q->size++;
// }

// /* Remove and return the head of the given queue. */
// procPtr deq(procQueue* q) {
//   procPtr temp = q->head;
//   if (q->head == NULL) {
//     return NULL;
//   }
//   if (q->head == q->tail) {
//     q->head = q->tail = NULL; 
//   }
//   else {
//     if (q->type == BLOCKED)
//       q->head = q->head->nextProcPtr;  
//     else if (q->type == CHILDREN)
//       q->head = q->head->nextSiblingPtr; 
//     else if (q->type == SLEEP) 
//       q->head = q->head->nextSleepPtr; 
//   }
//   q->size--;
//   return temp;
// }

// /* Remove the child process from the queue */
// void removeChild3(procQueue* q, procPtr child) {
//   if (q->head == NULL || q->type != CHILDREN)
//     return;

//   if (q->head == child) {
//     deq(q);
//     return;
//   }

//   procPtr prev = q->head;
//   procPtr p = q->head->nextSiblingPtr;

//   while (p != NULL) {
//     if (p == child) {
//       if (p == q->tail)
//         q->tail = prev;
//       else
//         prev->nextSiblingPtr = p->nextSiblingPtr->nextSiblingPtr;
//       q->size--;
//     }
//     prev = p;
//     p = p->nextSiblingPtr;
//   }
// }

// /* Return the head of the given queue. */
// procPtr peek(procQueue* q) {
//   if (q->head == NULL) {
//     return NULL;
//   }
//   return q->head;   
// }

/* Setup heap */
void initHeap(heap* h) {
    h->size = 0;
}

/* Add to heap */
void heapAdd(heap * h, procPtr p) {
    // add to bottom, then move up until it is in place
    int i = h->size;
    h->procs[h->size++] = p;
    while (h->procs[i/2]->wakeTime > p->wakeTime) {
        // move parent down
        h->procs[i] = h->procs[i/2];
        i /= 2;
    }
    h->procs[i] = p; // put at final location
    if (debug4) 
        USLOSS_Console("heapAdd: Added proc %d to heap at index %d, size = %d\n", p->pid, i, h->size);
}

/* Return min process on heap */
procPtr heapPeek(heap * h) {
    return h->procs[0];
}

/* Remove earlist waking process form the heap */
procPtr heapRemove(heap * h) {
  if (h->size == 0)
    return NULL;

    procPtr removed = h->procs[0]; // remove min
    h->size--;
    h->procs[0] = h->procs[h->size]; // put last in first spot

    // re-heapify
    int i = 0, left, right, min = 0;
    while (i*2 <= h->size) {
        // get locations of children
        left = i*2 + 1;
        right = i*2 + 2;

        // get min child
        if (left <= h->size && h->procs[left]->wakeTime < h->procs[min]->wakeTime) 
            min = left;
        if (right <= h->size && h->procs[right]->wakeTime < h->procs[min]->wakeTime) 
            min = right;

        // swap current with min child if needed
        if (min != i) {
            procPtr temp = h->procs[i];
            h->procs[i] = h->procs[min];
            h->procs[min] = temp;
            i = min;
        }
        else
            break; // otherwise we're done
    }
    if (debug4) 
        USLOSS_Console("heapRemove: Called, returning pid %d, size = %d\n", removed->pid, h->size);
    return removed;
}