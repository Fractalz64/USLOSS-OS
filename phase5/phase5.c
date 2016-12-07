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
#include <providedPrototypes.h>

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
int debug5 = 1;
Process processes[MAXPROC];
FaultMsg faults[MAXPROC]; /* Note that a process can have only
                           * one fault at a time, so we can
                           * allocate the messages statically
                           * and index them by pid. */
VmStats  vmStats;
FTE *frameTable;
DTE *diskTable; 
int faultMBox; // faults waiting for pagers
int pagerPids[MAXPAGERS]; // pids of the pagers
void *vmRegion = NULL; // address of the beginning of the virtual memory region
int clockHand; // index of frame the clock hand is currently at
int clockSem; // semaphore for moving the clock hand


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
    frameTable = malloc(frames * sizeof(FTE));
    int i;
    for (i = 0; i < frames; i++) {
        frameTable[i].pid = -1;
        frameTable[i].state = UNUSED;
    }

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
        faults[i].replyMbox = MboxCreate(1, 0); // assuming this is just used to block/unblock the process.... probably wrong
    }

   /* 
    * Create the fault mailbox.
    */
    faultMBox = MboxCreate(pagers, sizeof(FaultMsg));

    // set up the clock hand
    clockHand = 0; // start at frame 0
    clockSem = semcreateReal(1); // mutex

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

    // get diskBlocks = tracks on disk
    if (debug5) 
        USLOSS_Console("vmInitReal: getting disk size... \n");
    int diskBlocks;
    diskSizeReal(SWAPDISK, &dummy, &dummy, &diskBlocks);
    diskBlocks *= 2; // two pages per block

    // init disk table
    if (debug5) 
        USLOSS_Console("vmInitReal: initing disk table, diskBlocks = %d... \n", diskBlocks);
    diskTable = malloc(diskBlocks * sizeof(DTE));
    for (i = 0; i < diskBlocks; i++) {
        diskTable[i].pid = -1;
        diskTable[i].page = -1;
        diskTable[i].track = i/2;
        if (i % 2 == 0) // even blocks start at sector 0
            diskTable[i].sector = 0;
        else // odd blocks start at however many sectors a page takes up
            diskTable[i].sector = USLOSS_MmuPageSize()/USLOSS_DISK_SECTOR_SIZE;
    }

   /*
    * Zero out, then initialize, the vmStats structure
    */
    memset((char *) &vmStats, 0, sizeof(VmStats));
    vmStats.pages = pages;
    vmStats.frames = frames;
    vmStats.diskBlocks = diskBlocks; 
    vmStats.freeFrames = frames;
    vmStats.freeDiskBlocks = diskBlocks;
    vmStats.new = 0;

    vmRegion = USLOSS_MmuRegion(&dummy); // set vmRegion
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
   MboxReceive(fault->replyMbox, 0, 0);

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
    int i, frame;
    char buffer[USLOSS_MmuPageSize()]; // buffer for disk
    Process *proc;
    PTE *page, *oldPage;
    DTE *diskBlock;

    while(!isZapped()) {
        /* Wait for fault to occur (receive from mailbox) */
        FaultMsg fault;
        MboxReceive(faultMBox, &fault, sizeof(FaultMsg));
        if (isZapped())
            break; 
        if (debug5) 
            USLOSS_Console("Pager: got fault from process %d, address %d, page %d\n", fault.pid, fault.addr, fault.pageNum);

        // get process and page 
        proc = &processes[fault.pid % MAXPROC];
        page = &proc->pageTable[fault.pageNum];
        frame = -1; // set frame to -1 until assigned

        /* Look for free frame */
        if (vmStats.freeFrames > 0) {
            for (frame = 0; frame < vmStats.frames; frame++) {
                if (frameTable[frame].state == UNUSED) {
                    // map page 0 to frame so we can write to it later
                    USLOSS_MmuMap(TAG, 0, frame, USLOSS_MMU_PROT_RW);
                    vmStats.freeFrames--; // decrement free frames
                    if (debug5) 
                        USLOSS_Console("Pager: found frame %d free; free frames = %d \n", frame, vmStats.freeFrames);
                    break;
                }
            }
        }

        /* If there isn't one then use clock algorithm to
         * replace a page (perhaps write to disk) */
        else {
            int access;
            if (debug5) 
                USLOSS_Console("Pager: no free frame found, doing clock algo... \n");

            while (frame == -1) {
                sempReal(clockSem); // get mutex
                USLOSS_MmuGetAccess(clockHand, &access); // get access bits

                // if frame is unreferenced (and not being used by another process), use it!
                if ((access & 1) == 0) { // and check if it is not chosen by another pager...
                    frame = clockHand;
                    if (debug5)
                        USLOSS_Console("Pager: replacing frame %d, prev page: %d, prev owner: proc %d \n", clockHand, frameTable[frame].page, frameTable[frame].pid);
                    // TODO: mark frame to not be used by other pagers

                    // update old page
                    oldPage = &processes[frameTable[frame].pid].pageTable[frameTable[frame].page];
                    oldPage->frame = -1;
                    oldPage->state = INCORE;
                }

                else { // clear reference bit
                    USLOSS_MmuSetAccess(clockHand, (access & 2));
                    if (debug5) 
                        USLOSS_Console("Pager: cleared reference bit for frame %d, now access is %d \n", clockHand, access & 1);
                }

                clockHand = (clockHand + 1) % vmStats.frames; // increment clock hand
                semvReal(clockSem); // release mutex
            }

            // map page 0 to frame so we can write to it
            USLOSS_MmuMap(TAG, 0, frame, USLOSS_MMU_PROT_RW);

            // save frame to diiisk 
            if (access >= 2) { // if dirty
                // find disk block for it if it doesn't have one
                if (oldPage->diskBlock == -1) {
                    if (debug5)
                        USLOSS_Console("Pager: finding disk block for page %d... \n", frameTable[frame].page);
                    if (vmStats.freeDiskBlocks == 0) {
                        if (debug5)
                            USLOSS_Console("Pager: no free disk blocks, halting... \n");
                        USLOSS_Halt(1);
                    }
                    for (i = 0; i < vmStats.diskBlocks; i++) {
                        if (diskTable[i].pid == -1) {
                            oldPage->diskBlock = i;
                            diskTable[i].pid = frameTable[frame].pid;
                            diskTable[i].page = frameTable[frame].page;
                            vmStats.freeDiskBlocks--; 
                            if (debug5)
                                USLOSS_Console("Pager: found disk block %d for page %d proc %d, free blocks: %d \n", 
                                    i, diskTable[i].page, diskTable[i].pid, vmStats.freeDiskBlocks);
                            break;
                        }
                    }
                }

                diskBlock = &diskTable[oldPage->diskBlock];
                if (debug5)
                    USLOSS_Console("Pager: page %d dirty, writing to disk track %d, sector %d... \n", 
                        frameTable[frame].page, diskBlock->track, diskBlock->sector);
                // copy from memory
                memcpy(&buffer, vmRegion, USLOSS_MmuPageSize());
                if (debug5)
                    USLOSS_Console("Pager: memcopied \n");
                // write to disk
                diskWriteReal (SWAPDISK, diskBlock->track, diskBlock->sector,
                      USLOSS_MmuPageSize()/USLOSS_DISK_SECTOR_SIZE, &buffer);
                vmStats.pageOuts++; // increment pages saved
                if (debug5)
                    USLOSS_Console("Pager: done writing to disk \n");
            }

        }

        // map page 0 to frame so we can write to it later
        USLOSS_MmuMap(TAG, 0, frame, USLOSS_MMU_PROT_RW);

        // zero out if this is the first time it has been used
        if (page->state == UNUSED) {
            // vmRegion + page# * PageSize
            memset(vmRegion, 0, USLOSS_MmuPageSize());
            vmStats.new++; // increment new
            if (debug5) 
                USLOSS_Console("Pager: zeroed frame %d \n", frame);
        }

        // load page from disk
        else if (page->diskBlock > -1) {
            diskBlock = &diskTable[page->diskBlock];
            if (debug5) 
                USLOSS_Console("Pager: reading contents of page %d from disk block %d, track %d, sector %d to frame %d \n", 
                    fault.pageNum, page->diskBlock, diskBlock->track, diskBlock->sector, frame);
            // read from disk
            diskReadReal (SWAPDISK, diskBlock->track, diskBlock->sector,
                          USLOSS_MmuPageSize()/USLOSS_DISK_SECTOR_SIZE, &buffer);
            // copy to frame
            memcpy(vmRegion, &buffer, USLOSS_MmuPageSize());
            vmStats.pageIns++; // increment pages loaded
        }

        // unmap pager mapping
        USLOSS_MmuSetAccess(frame, 0); // set page to be not referenced and clean
        USLOSS_MmuUnmap(TAG, 0); // unmap page

        // update frame table
        frameTable[frame].pid = proc->pid;
        frameTable[frame].page = fault.pageNum;
        frameTable[frame].state = USED;

        // update page table
        proc->pageTable[fault.pageNum].frame = frame;
        proc->pageTable[fault.pageNum].state = INFRAME;

        if (debug5) 
            USLOSS_Console("Pager: set page %d to frame %d, unblocking process %d \n", frameTable[frame].page, frame, frameTable[frame].pid);
        /* Unblock waiting (faulting) process */
        MboxSend(fault.replyMbox, 0, 0);
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

