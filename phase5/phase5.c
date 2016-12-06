#include <assert.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase5.h>
#include <usyscall.h>
#include <libuser.h>
#include <vm.h>
#include <string.h>

extern void mbox_create(systemArgs *args_ptr);
extern void mbox_release(systemArgs *args_ptr);
extern void mbox_send(systemArgs *args_ptr);
extern void mbox_receive(systemArgs *args_ptr);
extern void mbox_condsend(systemArgs *args_ptr);
extern void mbox_condreceive(systemArgs *args_ptr);
extern int diskSizeReal(int, int*, int*, int*);
extern int start5();

static void
FaultHandler(int  type,  // USLOSS_MMU_INT
             void *arg); // Offset within VM region
static void vmInit(systemArgs *systemArgsPtr);
static void vmDestroy(systemArgs *systemArgsPtr);
void *vmInitReal(int, int, int, int);
void vmDestroyReal();
static int Pager(char *);
void setUserMode();

/* Globals */
int debug5 = 0;
Process processes[MAXPROC];
FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */
VmStats  vmStats;
Frame *frameTable;
int numFrames;
int faultMBox; // faults waiting for pagers
int pagerPids[MAXPAGERS]; // pids of the pagers
void *vmRegion = NULL; // address of the beginning of the virtual memory region



/*
 *----------------------------------------------------------------------
 *
 * start4 --
 *
 * Initializes the VM system call handlers. 
 *
 * Results:
 *      MMU return status
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
int
start4(char *arg)
{
    int pid;
    int result;
    int status;

    /* to get user-process access to mailbox functions */
    systemCallVec[SYS_MBOXCREATE]      = mbox_create;
    systemCallVec[SYS_MBOXRELEASE]     = mbox_release;
    systemCallVec[SYS_MBOXSEND]        = mbox_send;
    systemCallVec[SYS_MBOXRECEIVE]     = mbox_receive;
    systemCallVec[SYS_MBOXCONDSEND]    = mbox_condsend;
    systemCallVec[SYS_MBOXCONDRECEIVE] = mbox_condreceive;

    /* user-process access to VM functions */
    systemCallVec[SYS_VMINIT]    = vmInit;
    systemCallVec[SYS_VMDESTROY] = vmDestroy;

    result = Spawn("Start5", start5, NULL, 8*USLOSS_MIN_STACK, 2, &pid);
    if (result != 0) {
        USLOSS_Console("start4(): Error spawning start5\n");
        Terminate(1);
    }
    result = Wait(&pid, &status);
    if (result != 0) {
        USLOSS_Console("start4(): Error waiting for start5\n");
        Terminate(1);
    }
    Terminate(0);
    return 0; // not reached

} /* start4 */

/*
 *----------------------------------------------------------------------
 *
 * VmInit --
 *
 * Stub for the VmInit system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is initialized.
 *
 *----------------------------------------------------------------------
 */
static void
vmInit(systemArgs *args)
{
    CheckMode();

    int mappings = (long) args->arg1;
    int pages = (long) args->arg2;
    int frames = (long) args->arg3;
    int pagers = (long) args->arg4;

    args->arg1 = vmInitReal(mappings, pages, frames, pagers);

    if ((int) (long) args->arg1 < 0) 
        args->arg4 = args->arg1;
    else 
        args->arg4 = (void *) ((long) 0);
    setUserMode();
} /* vmInit */


/*
 *----------------------------------------------------------------------
 *
 * vmDestroy --
 *
 * Stub for the VmDestroy system call.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      VM system is cleaned up.
 *
 *----------------------------------------------------------------------
 */

static void
vmDestroy(systemArgs *args)
{
   CheckMode();
   vmDestroyReal();
   //setUserMode();
} /* vmDestroy */


/*
 *----------------------------------------------------------------------
 *
 * vmInitReal --
 *
 * Called by vmInit.
 * Initializes the VM system by configuring the MMU and setting
 * up the page tables.
 *
 * Results:
 *      Address of the VM region.
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
void *
vmInitReal(int mappings, int pages, int frames, int pagers)
{
    CheckMode();    
    int status;
    int dummy;

    if (debug5) 
        USLOSS_Console("vmInitReal: started \n");
    
    // check if VM has already been initialized
    if (vmRegion > 0) {
        return (void *) ((long) -2);
    }

    // check for invalid parameters
    if (mappings != pages) {
        return (void *) ((long) -1);
    }

    status = USLOSS_MmuInit(mappings, pages, frames);
    if (status != USLOSS_MMU_OK) {
        USLOSS_Console("vmInitReal: couldn't init MMU, status %d\n", status);
        abort();
    }
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    /* 
     * Initialize frame table 
     */
    if (debug5) 
        USLOSS_Console("vmInitReal: initializing frame table... \n");
    frameTable = malloc(frames * sizeof(Frame));
    int i;
    for (i = 0; i < frames; i++) {
        frameTable[i].pid = -1;
        frameTable[i].status = UNUSED;
    }
    numFrames = frames;

   /*
    * Initialize page tables.
    */
    if (debug5) 
        USLOSS_Console("vmInitReal: initializing page table... \n");
    for (i = 0; i < MAXPROC; i++) {
        processes[i].pid = -1; 
        processes[i].numPages = pages; 
        processes[i].pageTable = NULL;

        // initialize the fault structs
        faults[i].pid = -1;
        faults[i].addr = NULL;
        faults[i].replyMbox = MboxCreate(0, 0); // assuming this is just used to block/unblock the process.... probably wrong
    }

   /* 
    * Create the fault mailbox.
    */
    faultMBox = MboxCreate(pagers, sizeof(FaultMsg));

   /*
    * Fork the pagers.
    */
    if (debug5) 
        USLOSS_Console("vmInitReal: forking pagers... \n");
    // fill pagerPids with -1s
    memset(pagerPids, -1, sizeof(pagerPids));

    // fork the pagers
    for (i = 0; i < pagers; i++) {
        pagerPids[i] = fork1("Pager", Pager, NULL, 8*USLOSS_MIN_STACK, PAGER_PRIORITY);
    }

   /*
    * Zero out, then initialize, the vmStats structure
    */
    memset((char *) &vmStats, 0, sizeof(VmStats));

    // get diskBlocks = tracks on disk
    if (debug5) 
        USLOSS_Console("vmInitReal: getting disk size... \n");
    int diskBlocks;
    diskSizeReal(1, &dummy, &dummy, &diskBlocks);

    vmStats.pages = pages;
    vmStats.frames = frames;
    vmStats.diskBlocks = 2*diskBlocks; 
    vmStats.freeFrames = frames;
    vmStats.freeDiskBlocks = 2*diskBlocks;
    vmStats.new = 1;

    vmRegion = USLOSS_MmuRegion(&dummy);
    memset((char *)vmRegion, 0, dummy*USLOSS_MmuPageSize());
    if (debug5) 
        USLOSS_Console("vmInitReal: returning vmRegion = %d \n", vmRegion);
    return vmRegion;
} /* vmInitReal */


/*
 *----------------------------------------------------------------------
 *
 * PrintStats --
 *
 *      Print out VM statistics.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Stuff is printed to the USLOSS_Console.
 *
 *----------------------------------------------------------------------
 */
void
PrintStats(void)
{
     USLOSS_Console("VmStats\n");
     USLOSS_Console("pages:          %d\n", vmStats.pages);
     USLOSS_Console("frames:         %d\n", vmStats.frames);
     USLOSS_Console("diskBlocks:     %d\n", vmStats.diskBlocks);
     USLOSS_Console("freeFrames:     %d\n", vmStats.freeFrames);
     USLOSS_Console("freeDiskBlocks: %d\n", vmStats.freeDiskBlocks);
     USLOSS_Console("switches:       %d\n", vmStats.switches);
     USLOSS_Console("faults:         %d\n", vmStats.faults);
     USLOSS_Console("new:            %d\n", vmStats.new);
     USLOSS_Console("pageIns:        %d\n", vmStats.pageIns);
     USLOSS_Console("pageOuts:       %d\n", vmStats.pageOuts);
     USLOSS_Console("replaced:       %d\n", vmStats.replaced);
} /* PrintStats */


/*
 *----------------------------------------------------------------------
 *
 * vmDestroyReal --
 *
 * Called by vmDestroy.
 * Frees all of the global data structures
 *
 * Results:
 *      None
 *
 * Side effects:
 *      The MMU is turned off.
 *
 *----------------------------------------------------------------------
 */
void
vmDestroyReal(void)
{

    CheckMode();
    USLOSS_MmuDone();

    // do nothing if VM hasn't been initialized yet
    if (!vmRegion)
        return;

    if (debug5) 
        USLOSS_Console("vmDestroyReal: called \n");

   /*
    * Kill the pagers here.
    */
    int i, status;
    FaultMsg dummy;
    for (i = 0; i < MAXPAGERS; i++) {
        if (pagerPids[i] == -1)
            break;
        if (debug5) 
            USLOSS_Console("vmDestroyReal: zapping pager %d, pid %d \n", i, pagerPids[i]);
        MboxSend(faultMBox, &dummy, sizeof(FaultMsg)); // wake up pager
        zap(pagerPids[i]);
        join(&status);
    }

    // release fault mailboxes
    for (i = 0; i < MAXPROC; i++) {
        MboxRelease(faults[i].replyMbox);
    }

    MboxRelease(faultMBox);

    if (debug5) 
        USLOSS_Console("vmDestroyReal: released fault mailboxes \n");

   /* 
    * Print vm statistics.
    */
    PrintStats();
    /* and so on... */

    vmRegion = NULL;
} /* vmDestroyReal */

/*
 *----------------------------------------------------------------------
 *
 * FaultHandler
 *
 * Handles an MMU interrupt. Simply stores information about the
 * fault in a queue, wakes a waiting pager, and blocks until
 * the fault has been handled.
 *
 * Results:
 * None.
 *
 * Side effects:
 * The current process is blocked until the fault is handled.
 *
 *----------------------------------------------------------------------
 */
static void
FaultHandler(int  type /* USLOSS_MMU_INT */,
             void *arg  /* Offset within VM region */)
{
    if (debug5) 
        USLOSS_Console("FaultHandler: called for process %d \n", getpid());

   int cause;

   int offset = (int) (long) arg;

   assert(type == USLOSS_MMU_INT);
   cause = USLOSS_MmuGetCause();
   assert(cause == USLOSS_MMU_FAULT);
   vmStats.faults++;
   /*
    * Fill in faults[pid % MAXPROC], send it to the pagers, and wait for the
    * reply.
    */
   int pid = getpid();
   FaultMsg *fault = &faults[pid % MAXPROC];
   fault->pid = pid;
   fault->addr = processes[pid % MAXPROC].pageTable + offset;
   fault->pageNum = offset/USLOSS_MmuPageSize();

   // send to pagers
    if (debug5) 
        USLOSS_Console("FaultHandler: created fault message for proc %d, address %d, sending to pagers... \n", fault->pid, fault->addr);
   MboxSend(faultMBox, fault, sizeof(FaultMsg));

    if (debug5) 
        USLOSS_Console("FaultHandler: sent fault to pagers, blocking... \n");
   // block
   MboxSend(fault->replyMbox, 0, 0);

} /* FaultHandler */

/*
 *----------------------------------------------------------------------
 *
 * Pager 
 *
 * Kernel process that handles page faults and does page replacement.
 *
 * Results:
 * None.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
static int
Pager(char *buf)
{
    while(!isZapped()) {
        /* Wait for fault to occur (receive from mailbox) */
        FaultMsg fault;
        MboxReceive(faultMBox, &fault, sizeof(FaultMsg));
        if (isZapped())
            break; 
        if (debug5) 
            USLOSS_Console("Pager: got fault from process %d, address %d, page %d\n", fault.pid, fault.addr, fault.pageNum);
        Process *proc = &processes[fault.pid % MAXPROC];

        /* Look for free frame */
        int i;
        for (i = 0; i < numFrames; i++) {
            if (frameTable[i].status == UNUSED) {
                if (debug5) 
                    USLOSS_Console("Pager: found frame %d free \n", i);
                break;
            }
        }

        /* If there isn't one then use clock algorithm to
         * replace a page (perhaps write to disk) */
        if (i == numFrames) {
            // DO THAT STUFF ^
        }

        /* Load page into frame from disk, if necessary */


        // set frame in PTE
        proc->pageTable[fault.pageNum].frame = i;

        if (debug5) 
            USLOSS_Console("Pager: set page %d to frame %d, unblocking process %d \n", fault.pageNum, i, proc->pid);
        /* Unblock waiting (faulting) process */
        MboxReceive(fault.replyMbox, 0, 0);
    }
    return 0;
} /* Pager */


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

