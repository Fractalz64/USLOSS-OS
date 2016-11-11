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

int debug4 = 1;
int running;

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
int diskSizeReal(int, int*, int*, int*);
int diskWriteReal(int, int, int, int, void *);
int diskReadReal(int, int, int, int, void *);
void requireKernelMode(char *);
void emptyProc(int);
void initProc(int);
void setUserMode();
void initDiskQueue(diskQueue*);
void addDiskQ(diskQueue*, procPtr);
procPtr removeDiskQ(diskQueue*);
// void enq(procQueue*, procPtr);
// procPtr deq(procQueue*);
// procPtr peek(procQueue*);
// void removeChild3(procQueue*, procPtr);
void initHeap(heap *);
void heapAdd(heap *, procPtr);
procPtr heapPeek(heap *);
procPtr heapRemove(heap *);

/* Globals */
procStruct ProcTable[MAXPROC];
heap sleepHeap;
int diskZapped; // indicates if the disk drivers are 'zapped' or not
diskQueue diskQs[USLOSS_DISK_UNITS];
int diskPids[2];

void
start3(void)
{
    // char	name[128];
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
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
        sprintf(diskbuf, "%d", i);
        pid = fork1("Disk driver", DiskDriver, diskbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i);
            USLOSS_Halt(1);
        }

        diskPids[i] = pid;
        sempReal(running); // wait for driver to start running
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

    // disk drivers
    diskZapped = 1;
    sempReal(running); // wait for disk drivers to quit

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
    int result;
    int status;
    int unit = atoi( (char *) arg);     // Unit is passed as arg.

    // get set up in proc table
    initProc(getpid());
    procPtr me = &ProcTable[getpid() % MAXPROC];
    initDiskQueue(&diskQs[unit]);

    // init number of tracks
    int temp;
    diskSizeReal(unit, &temp, &temp, &me->diskTrack);

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(! diskZapped) {
        // block on sem until we get request
        sempReal(me->blockSem);
        if (diskZapped) // check  if we were zapped
            break;

        // get request off queue
        if (diskQs[unit].size > 0) {
            procPtr proc = removeDiskQ(&diskQs[unit]);
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &proc->diskRequest);

            // wait for result
            result = waitDevice(USLOSS_DISK_DEV, unit, &status);
            if (result != 0) {
                return 0;
            }

            semvReal(proc->blockSem); // unblock caller
        }

    }

    semvReal(running); // unblock parent
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

/* sleep function value extraction */
void sleep(systemArgs * args) {
    requireKernelMode("sleep");
    int seconds = (long) args->arg1;
    int retval = sleepReal(seconds);
    args->arg4 = (void *) ((long) retval);
    setUserMode();
}

/* real sleep function */
int sleepReal(int seconds) {
    requireKernelMode("sleepReal");

    if (debug4) 
        USLOSS_Console("sleepReal: called for process %d with %d seconds\n", getpid(), seconds);

    if (seconds < 0) {
        return -1;
    }

    // init/get the process
    if (ProcTable[getpid() % MAXPROC].pid == -1) {
        initProc(getpid());
    }
    procPtr proc = &ProcTable[getpid() % MAXPROC];
    
    // set wake time
    proc->wakeTime = USLOSS_Clock() + seconds*1000000;
    if (debug4) 
        USLOSS_Console("sleepReal: set wake time for process %d to %d, adding to heap...\n", proc->pid, proc->wakeTime);

    heapAdd(&sleepHeap, proc); // add to sleep heap
    if (debug4) 
        USLOSS_Console("sleepReal: Process %d going to sleep until %d\n", proc->pid, proc->wakeTime);
    sempReal(proc->blockSem); // block the process
    if (debug4) 
        USLOSS_Console("sleepReal: Process %d woke up, time is %d\n", proc->pid, USLOSS_Clock());
    return 0;
}

/*
    sysArg.arg1 = diskBuffer;
    sysArg.arg2 = (void *) ((long) sectors);
    sysArg.arg3 = (void *) ((long) track);
    sysArg.arg4 = (void *) ((long) first);
    sysArg.arg5 = (void *) ((long) unit);
*/
void diskRead(systemArgs * args) {
    requireKernelMode("diskRead");
}

void diskWrite(systemArgs * args) {
    requireKernelMode("diskWrite");

    int sectors = (long) args->arg2;
    int track = (long) args->arg3;
    int first = (long) args->arg4;
    int unit = (long) args->arg5;

    int retval = diskWriteReal(unit, first, track, sectors, args->arg1);
    setUserMode();
}

int diskWriteReal(int unit, int track, int first, int sectors, void *buffer) {
    requireKernelMode("diskWriteReal");

    // check args!!!

    // init/get the process
    if (ProcTable[getpid() % MAXPROC].pid == -1) {
        initProc(getpid());
    }
    procPtr proc = &ProcTable[getpid() % MAXPROC];

    proc->diskRequest.opr = USLOSS_DISK_WRITE;
    proc->diskRequest.reg1 = (void *) ((long) first);
    proc->diskRequest.reg1 = buffer;

    return 0;
}

/* extract values from sysargs and call diskSizeReal */
void diskSize(systemArgs * args) {
    requireKernelMode("diskSize");
    int unit = (long) args->arg1;
    int sector, track, disk;
    int retval = diskSizeReal(unit, &sector, &track, &disk);
    args->arg1 = (void *) ((long) sector);
    args->arg2 = (void *) ((long) track);
    args->arg3 = (void *) ((long) disk);
    args->arg4 = (void *) ((long) retval);
    setUserMode();
}

/*------------------------------------------------------------------------
    diskSizeReal: Puts values into pointers for the size of a sector, 
    number of sectors per track, and number of tracks on the disk for the 
    given unit. 
    Returns: -1 if given illegal input, 0 otherwise
 ------------------------------------------------------------------------*/
int diskSizeReal(int unit, int *sector, int *track, int *disk) {
    requireKernelMode("diskSizeReal");

    // check for illegal args
    if (unit < 0 || unit > 1 || sector == NULL || track == NULL || disk == NULL) {
        if (debug4)
            USLOSS_Console("diskSizeReal: given illegal argument(s), returning -1\n");
        return -1;
    }

    procPtr driver = &ProcTable[diskPids[unit]];

    // get the number of tracks for the first time
    if (driver->diskTrack == -1) {
        // init/get the process
        if (ProcTable[getpid() % MAXPROC].pid == -1) {
            initProc(getpid());
        }
        procPtr proc = &ProcTable[getpid() % MAXPROC];

        // set variables
        proc->diskTrack = 0;
        USLOSS_DeviceRequest request;
        request.opr = USLOSS_DISK_TRACKS;
        request.reg1 = &driver->diskTrack;
        proc->diskRequest = request;

        addDiskQ(&diskQs[unit], proc); // add to disk queue 
        semvReal(driver->blockSem);  // wake up disk driver

        if (debug4)
            USLOSS_Console("diskSizeReal: added pid %d to disk queue for unit %d, size = %d, blocking...\n", proc->pid, unit, diskQs[unit].size);

        sempReal(proc->blockSem); // block

        int status;
        USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status);
        if (debug4)
            USLOSS_Console("diskSizeReal: after reading track size for unit %d, status = %d\n", unit, status);
    }

    *sector = USLOSS_DISK_SECTOR_SIZE;
    *track = USLOSS_DISK_TRACK_SIZE;
    *disk = driver->diskTrack;
    return 0;
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
    ProcTable[i].diskTrack = -1;
    ProcTable[i].nextDiskPtr = NULL;
    ProcTable[i].prevDiskPtr = NULL;
}

/* empties proc struct */
void emptyProc(int pid) {
    requireKernelMode("emptyProc()"); 

    int i = pid % MAXPROC;

    ProcTable[i].pid = -1; 
    ProcTable[i].mboxID = -1;
    ProcTable[i].blockSem = -1;
    ProcTable[i].wakeTime = -1;
    ProcTable[i].nextDiskPtr = NULL;
    ProcTable[i].prevDiskPtr = NULL;
}

/* ------------------------------------------------------------------------
  Functions for the dskQueue and heap.
   ----------------------------------------------------------------------- */

/* Initialize the given diskQueue */
void initDiskQueue(diskQueue* q) {
    q->head = NULL;
    q->tail = NULL;
    q->curr = NULL;
    q->size = 0;
}

/* Adds the proc pointer to the disk queue in sorted order */
void addDiskQ(diskQueue* q, procPtr p) {
    if (debug4)
        USLOSS_Console("addDiskQ: adding pid %d, track %d to queue\n", p->pid, p->diskTrack);
    // first add
    if (q->head == NULL) { 
        q->head = q->tail = p;
        q->head->nextDiskPtr = q->tail->nextDiskPtr = NULL;
        q->head->prevDiskPtr = q->tail->prevDiskPtr = NULL;
    }
    else {
        // find the right location to add
        procPtr prev = q->tail;
        procPtr next = q->head;
        while (next->diskTrack < p->diskTrack) {
            prev = next;
            next = next->nextDiskPtr;
            if (next == q->head)
                break;
        }
        prev->nextDiskPtr = p;
        p->prevDiskPtr = prev;
        p->nextDiskPtr = next;
        next->prevDiskPtr = p;
        if (p->diskTrack < q->head->diskTrack)
            q->head = p; // update head
        if (p->diskTrack > q->tail->diskTrack)
            q->tail = p; // update tail
    }
    q->size++;
    if (debug4)
        USLOSS_Console("addDiskQ: add complete, size = %d\n", q->size);
} 

/* Returns and removes the next proc on the disk queue */
procPtr removeDiskQ(diskQueue* q) {
    if (q->curr == NULL) {
        q->curr = q->head;
    }

    procPtr temp = q->curr;

    if (q->curr == q->head) { // remove head
        q->head = q->head->nextDiskPtr;
        q->head->prevDiskPtr = q->tail;
        q->tail->nextDiskPtr = q->head;
        q->curr = q->head;
    }

    else if (q->curr == q->tail) {
        q->tail = q->tail->prevDiskPtr;
        q->tail->nextDiskPtr = q->head;
        q->head->prevDiskPtr = q->tail;
        q->curr = q->tail;
    }

    else {
        q->curr->prevDiskPtr->nextDiskPtr = q->curr->nextDiskPtr;
        q->curr->nextDiskPtr->prevDiskPtr = q->curr->prevDiskPtr;
        q->curr = q->curr->nextDiskPtr;
    }


    q->size--;
    return temp;
} 

// /* Add the given procPtr3 to the back of the given queue. */
// void enq3(procQueue* q, procPtr3 p) {
//   if (q->head == NULL && q->tail == NULL) {
//     q->head = q->tail = p;
//   } else {
//     q->tail->nextDiskPtr = p;
//     q->tail = p;
//   }
//   q->size++;
// }

// /* Remove and return the head of the given queue. */
// procPtr3 deq3(procQueue* q) {
//   procPtr3 temp = q->head;
//   if (q->head == NULL) {
//     return NULL;
//   }
//   if (q->head == q->tail) {
//     q->head = q->tail = NULL; 
//   }
//   else {
//     q->head = q->head->nextDiskPtr;  
//   }
//   q->size--;
//   return temp;
// }

// /* Return the head of the given queue. */
// procPtr peek(procQueue* q) {
//   if (q->head == NULL) {
//     return NULL;
//   }
//   return q->head;   
// }

/* Setup heap, implementation based on https://gist.github.com/aatishnn/8265656 */
void initHeap(heap* h) {
    h->size = 0;
}

/* Add to heap */
void heapAdd(heap * h, procPtr p) {
    // start from bottom and find correct place
    int i, parent;
    for (i = h->size; i > 0; i = parent) {
        parent = (i-1)/2;
        if (h->procs[parent]->wakeTime <= p->wakeTime)
            break;
        // move parent down
        h->procs[i] = h->procs[parent];
    }
    h->procs[i] = p; // put at final location
    h->size++;
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