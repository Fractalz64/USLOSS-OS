/*
 * skeleton.c
 *
 * This is a skeleton for phase5 of the programming assignment. It
 * doesn't do much -- it is just intended to get you started.
 */


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
static Process processes[MAXPROC];

FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */
VmStats  vmStats;
Frame *frameTable;
int numFrames;
int faultMBox; // faults waiting for pagers
int pagerPids[MAXPAGERS]; // pids of the pagers
void *vmRegion; // address of the beginning of the virtual memory region



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

    vmRegion = NULL;

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
    int status;
    int dummy;

    // check if VM has already been initialized
    if (vmRegion > 0) {
        return (void *) ((long) -2);
    }

    // check for invalid parameters
    if (mappings != pages) {
        return (void *) ((long) -1);
    }

    CheckMode();
    status = USLOSS_MmuInit(mappings, pages, frames);
    if (status != USLOSS_MMU_OK) {
        USLOSS_Console("vmInitReal: couldn't init MMU, status %d\n", status);
        abort();
    }
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;

    /* 
     * Initialize frame table 
     */
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
    for (i = 0; i < MAXPROC; i++) {
        processes[i].pid = -1; 
        processes[i].numPages = pages; 
        // THIS GOES IN p1_fork() !!!!!
        processes[i].pageTable = malloc( pages * sizeof(PTE)); 

        // initialize each page table entry
        int j;
        for (j = 0; j < pages; j++) {
            processes[i].pageTable->state = UNUSED;
            processes[i].pageTable->frame = -1;
            processes[i].pageTable->diskBlock = -1;
        }   

        // initialize the fault structs
        faults[i].pid = -1;
        faults[i].addr = NULL;
        Mbox_Create(0, 0, &faults[i].replyMbox); // assuming this is just used to block/unblock the process.... probably wrong
    }

   /* 
    * Create the fault mailbox.
    */
    Mbox_Create(MAXPROC, sizeof(FaultMsg), &faultMBox);

   /*
    * Fork the pagers.
    */
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
    int diskBlocks;
    diskSizeReal(1, &dummy, &dummy, &diskBlocks);

    vmStats.pages = pages;
    vmStats.frames = frames;
    vmStats.diskBlocks = diskBlocks; 
    vmStats.freeFrames = frames;
    vmStats.freeDiskBlocks = diskBlocks;

    vmRegion = USLOSS_MmuRegion(&dummy);
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

   /*
    * Kill the pagers here.
    */
    int i;
    for (i = 0; i < MAXPAGERS; i++) {
        if (pagerPids[i] == -1)
            break;
        zap(pagerPids[i]);
    }

    // free page tables and release fault mailboxes
    for (i = 0; i < MAXPROC; i++) {
        free(&processes[i].pageTable); 
        Mbox_Release(faults[i].replyMbox);
    }

    Mbox_Release(faultMBox);

   /* 
    * Print vm statistics.
    */
    USLOSS_Console("vmStats:\n");
    USLOSS_Console("pages: %d\n", vmStats.pages);
    USLOSS_Console("frames: %d\n", vmStats.frames);
    USLOSS_Console("blocks: %d\n", vmStats.diskBlocks);
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

   // send to pagers
   Mbox_Send(faultMBox, fault, sizeof(FaultMsg));

   // block
   Mbox_Send(fault->replyMbox, 0, 0);

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
    while(1) {
        /* Wait for fault to occur (receive from mailbox) */
        FaultMsg *fault = NULL;
        Mbox_Receive(faultMBox, fault, sizeof(FaultMsg));

        /* Look for free frame */
        int i;
        for (i = 0; i < numFrames; i++) {
            if (frameTable[i].status == UNUSED)
                break;
        }

        /* If there isn't one then use clock algorithm to
         * replace a page (perhaps write to disk) */
        if (i == numFrames) {
            // DO THAT STUFF ^
        }

        /* Load page into frame from disk, if necessary */


        /* Unblock waiting (faulting) process */
        Mbox_Send(fault->replyMbox, 0, 0);
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
