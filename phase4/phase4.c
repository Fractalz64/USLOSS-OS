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
static int TermReader(char *);
static int TermWriter(char *);
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

// mailboxes for terminal device
int charRecvMbox[USLOSS_TERM_UNITS]; // receive char
int charSendMbox[USLOSS_TERM_UNITS]; // send char
int lineReadMbox[USLOSS_TERM_UNITS]; // read line
int lineWriteMbox[USLOSS_TERM_UNITS]; // write line
int pidMbox[USLOSS_TERM_UNITS]; // pid to block
int termInt[USLOSS_TERM_UNITS]; // interupt fo rterm

int termProcTable[USLOSS_TERM_UNITS][3]; // keep track of term procs


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

     // mboxes for terminal
     for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        charRecvMbox[i] = MboxCreate(1, MAXLINE);
        charSendMbox[i] = MboxCreate(1, MAXLINE);
        lineReadMbox[i] = MboxCreate(10, MAXLINE);
        lineWriteMbox[i] = MboxCreate(10, MAXLINE); 
        pidMbox[i] = MboxCreate(1, sizeof(int));

     }

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

     for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        sprintf(termbuf, "%d", i); 
        termProcTable[i][0] = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
        termProcTable[i][1] = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        termProcTable[i][2] = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
        sempReal(running);
        sempReal(running);
        sempReal(running);
     }


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

    status = 0;
    zap(clockPID);  // clock driver
    join(&status);

    // zap termreader
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(charRecvMbox[i], NULL, 0);
        zap(termProcTable[i][1]);
        join(&status);
    }

    // zap termwriter
    for (i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(lineWriteMbox[i], NULL, 0);
        zap(termProcTable[i][2]);
        join(&status);
    }

    // zap termdriver
    char filename[50];
    for(i = 0; i < USLOSS_TERM_UNITS; i++)
    {
        int ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void *)((long) ctrl));
        sprintf(filename, "term%d.in", i);
        FILE *f = fopen(filename, "a+");
        fprintf(f, "last line\n");
        fflush(f);
        fclose(f);
        zap(termProcTable[i][0]);
        join(&status);
    }

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
    int result;
    int status;
    int unit = atoi( (char *) arg);     // Unit is passed as arg.

    semvReal(running);
    if (debug4) 
        USLOSS_Console("TermDriver (unit %d): running\n", unit);

    while (!isZapped()) {

        result = waitDevice(USLOSS_TERM_INT, unit, &status);
        if (result != 0) {
            return 0;
        }
        // Try to receive character
        int recv = USLOSS_TERM_STAT_RECV(status);
        if (recv == USLOSS_DEV_BUSY) {
            MboxCondSend(charRecvMbox[unit], &status, sizeof(int));
        }
        else if (recv == USLOSS_DEV_ERROR) {
            if (debug4) 
                USLOSS_Console("TermDriver RECV ERROR\n");
        }
        // Try to send character
        int xmit = USLOSS_TERM_STAT_XMIT(status);
        if (xmit == USLOSS_DEV_READY) {
            MboxCondSend(charSendMbox[unit], &status, sizeof(int));
        }
        else if (xmit == USLOSS_DEV_ERROR) {
            if (debug4) 
                USLOSS_Console("TermDriver XMIT ERROR\n");
        }
    }

    return 0;
}

static int 
TermReader(char * arg) 
{
    int unit = atoi( (char *) arg);     // Unit is passed as arg.
    int i;
    int receive; // char to receive
    char line[MAXLINE + 1]; // line being created/read
    int next = 0; // index in line to write char

    for (i = 0; i < MAXLINE + 1; i++) { 
        line[i] = '\0';
    }

    semvReal(running);
    while (!isZapped()) {
        // receieve characters
        MboxReceive(charRecvMbox[unit], &receive, sizeof(int));
        char ch = USLOSS_TERM_STAT_CHAR(receive);
        line[next] = ch;
        next++;

        // receive line
        if (ch == '\n' || next == MAXLINE) {
            if (debug4) 
                USLOSS_Console("TermReader (unit %d): line send\n", unit);

            line[next] = '\0'; // end with null
            MboxSend(lineReadMbox[unit], line, next);

            // reset line
            for (i = 0; i < MAXLINE + 1; i++) {
                line[i] = '\0';
            } 
            next = 0;
        }

    }
    return 0;
}

static int 
TermWriter(char * arg) 
{
    int unit = atoi( (char *) arg);     // Unit is passed as arg.
    int i;
    int size;
    int ctrl = 0;
    int next;
    int status;
    char line[MAXLINE];

    semvReal(running);
    if (debug4) 
        USLOSS_Console("TermWriter (unit %d): running\n", unit);

    while (!isZapped()) {
        size = MboxReceive(lineWriteMbox[unit], line, MAXLINE); // get line and size

        if (isZapped())
            break;

        // enable xmit interrupt and receive interrupt
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);

        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, ctrl);

        // xmit the line
        next = 0;
        while (next < size) {
            MboxReceive(charSendMbox[unit], &status, sizeof(int));

            // xmit the character
            int x = USLOSS_TERM_STAT_XMIT(status);
            if (x == USLOSS_DEV_READY) {
                //USLOSS_Console("%c string %d unit\n", line[next], unit);

                ctrl = 0;
                ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
                ctrl = USLOSS_TERM_CTRL_CHAR(ctrl, line[next]);
                ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
                ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);

                USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, ctrl);
            }

            next++;
        }

        // disable xmit int
        ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, ctrl);
        termInt[unit] = 0;
        int pid; 
        semvReal(ProcTable[pid % MAXPROC].blockSem);
        MboxReceive(pidMbox[unit], &pid, sizeof(int));
        
    }

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


void diskRead(systemArgs * args) {
    
}

void diskWrite(systemArgs * args) {
    
}

void diskSize(systemArgs * args) {
    
}

void termRead(systemArgs * args) {
    if (debug4)
        USLOSS_Console("termRead\n");
    requireKernelMode("termRead");
    
    char *buffer = (char *) args->arg1;
    int size = (long) args->arg2;
    int unit = (long) args->arg3;

    long retval = termReadReal(unit, size, buffer);

    if (retval == -1) {
        args->arg2 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) -1);
        return;
    }
    args->arg2 = (void *) ((long) retval);
    args->arg4 = (void *) ((long) 0);
    setUserMode();
}

int termReadReal(int unit, int size, char *buffer) {
    if (debug4)
        USLOSS_Console("termReadReal\n");
    requireKernelMode("termReadReal");

    if (unit < 0 || unit > USLOSS_TERM_UNITS - 1 || size < 0) {
        return -1;
    }
    char line[MAXLINE + 1];
    int ctrl = 0;

    //interrupts
    if (termInt[unit] == 0) {
        if (debug4)
            USLOSS_Console("termReadReal enable interrupts\n");
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, ctrl);
        termInt[unit] = 1;
    }
    int retval = MboxReceive(lineReadMbox[unit], &line, MAXLINE);

    if (debug4) 
        USLOSS_Console("termReadReal (unit %d): size %d retval %d \n", unit, size, retval);
    memcpy(buffer, line, size);

    if (retval > size) {
        retval = size;
    }

    return retval;
}

void termWrite(systemArgs * args) {
    if (debug4)
        USLOSS_Console("termWrite\n");
    requireKernelMode("termWrite");
    
    char *text = (char *) args->arg1;
    int size = (long) args->arg2;
    int unit = (long) args->arg3;

    long retval = termWriteReal(unit, size, text);

    if (retval == -1) {
        args->arg2 = (void *) ((long) retval);
        args->arg4 = (void *) ((long) -1);
        return;
    }
    args->arg2 = (void *) ((long) retval);
    args->arg4 = (void *) ((long) 0);
    setUserMode(); 
}

int termWriteReal(int unit, int size, char *text) {
    if (debug4)
        USLOSS_Console("termWriteReal\n");
    requireKernelMode("termWriteReal");

    if (unit < 0 || unit > USLOSS_TERM_UNITS - 1 || size < 0) {
        return -1;
    }

    int pid = getpid();
    MboxSend(pidMbox[unit], &pid, sizeof(int));

    int retval = MboxSend(lineWriteMbox[unit], text, size);
    //USLOSS_Console("%s string %d size\n", text, size);

    sempReal(ProcTable[pid % MAXPROC].blockSem);

    return size;
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
    if (debug4) 
        USLOSS_Console("initProc: initialized process %d\n", pid);
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