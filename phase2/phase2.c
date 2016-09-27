/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void emptyBox(int);
void emptySlot(int);
void disableInterrupts(void);
void enableInterrupts(void);
void requireKernelMode(char *);
void initQueue(queue*, int);
void enq(queue*, void*);
void *deq(queue*);
void *peek(queue*);

// void mboxinitProcQueue(mboxProcQueue*);
// void mboxenq(mboxProcQueue*, mboxProcPtr);
// mboxProcPtr mboxdeq(mboxProcQueue*);
// mboxProcPtr mboxpeek(mboxProcQueue*);

/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

mailbox MailBoxTable[MAXMBOX]; // the mail boxes 
mailSlot MailSlotTable[MAXSLOTS]; // the mail slots
mboxProc mboxProcTable[MAXPROC];  // the processes
mboxProcPtr Current; // running process
// mboxProcPtr blockedProcs;

// the total number of mailboxes and mail slots in use
int numBoxes, numSlots;

// next mailbox/slot id to be assigned
int nextMboxID = 7, nextSlotID = 0, nextProc = 0;

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");
    requireKernelMode("start1");

    // Disable interrupts
    disableInterrupts();

    // Initialize the mail box table, slots, & other data structures.
    // Initialize USLOSS_IntVec and system call handlers,
    // allocate mailboxes for interrupt handlers.  Etc... 

    // initialize mailbox table
    int i;
    for (i = 0; i < MAXMBOX; i++) {
        emptyBox(i);
    }

    // initialize mail slots
    for (i = 0; i < MAXSLOTS; i++) {
        emptySlot(i);
    }

    numBoxes = 0;
    numSlots = 0;

    enableInterrupts();

    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    int kid_pid, status;
    kid_pid = fork1("start2", start2, NULL, 4 * USLOSS_MIN_STACK, 1);
    if ( join(&status) != kid_pid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }

    return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("MboxCreate()");

    // check if all mailboxes are used, and for illegal arguments
    if (numBoxes == MAXMBOX || slots < 0 || slot_size < 0) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxCreate(): illegal args or max boxes reached, returning -1\n");
        return -1;
    }

    // get mailbox
    mailbox *box = &MailBoxTable[nextMboxID % MAXMBOX];

    // initialize fields
    box->mboxID = nextMboxID++;
    box->totalSlots = slots;
    box->slotSize = slot_size;
    box->status = ACTIVE;
    initQueue(&box->slots, SLOTQUEUE);
    initQueue(&box->blockedProcsSend, PROCQUEUE);
    initQueue(&box->blockedProcsRecieve, PROCQUEUE);

    numBoxes++; // increment mailbox count

    if (DEBUG2 && debugflag2) {
        USLOSS_Console("MboxCreate(): created mailbox with id = %d, totalSlots = %d, slot_size = %d, numBoxes = %d\n", box->mboxID, box->totalSlots, box->slotSize, numBoxes);
    }

    enableInterrupts(); // re-enable interrupts
    return box->mboxID;
} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxRelease
   Purpose - releases a mailbox
   Parameters - ID of the mailbox to release
   Returns - -3 if caller was zap'd, -1 if mailboxID is invalid, 0 otherwise.
   Side Effects - zaps any processes blocked on the mailbox
   ----------------------------------------------------------------------- */
int MboxRelease(int mailboxID) {
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("MboxRelease()");

    // check if mailboxID is invalid
    if (mailboxID < 0) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxRelease(): called with invalid mailboxID: %d, returning -1\n", mailboxID);
        return -1;
    }

    // get mailbox
    mailbox *box = &MailBoxTable[mailboxID % MAXMBOX];

    // check if mailbox is in use
    if (box == NULL || box->status == INACTIVE) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxRelease(): mailbox %d is already released, returning -1\n", mailboxID);
        return -1;
    }

    // empty the slots in the mailbox
    while (box->slots.size > 0) {
        slotPtr slot = (slotPtr)deq(&box->slots);
        emptySlot(slot->slotID % MAXSLOTS);
    }

    // release the mailbox
    emptyBox(mailboxID % MAXSLOTS);

    // unblock any processes blocked on a send 
    while (box->blockedProcsSend.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsSend);
        unblockProc(proc->pid);
        disableInterrupts(); // re-disable interrupts
    }

    // unblock any processes blocked on a recieve 
    while (box->blockedProcsRecieve.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsRecieve);
        unblockProc(proc->pid);
        disableInterrupts(); // re-disable interrupts
    }

    enableInterrupts(); // enable interrupts before return
    return 0;
}


/* ------------------------------------------------------------------------
   Name - createSlot
   Purpose - gets a free slot from the table of mail slots and initializes it 
   Parameters - pointer to the message to put in the slot, and the message size.
   Returns - ID of the new slot.
   Side Effects - initializes one element of the mail slot array. 
   ----------------------------------------------------------------------- */
int createSlot(void *msg_ptr, int msg_size)
{
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("createSlot()");

    // assumes parameters were already checked to me valid by caller
    slotPtr slot = &MailSlotTable[nextSlotID % MAXSLOTS];
    slot->slotID = nextSlotID++;
    slot->status = USED;
    slot->messageSize = msg_size;
    numSlots++;

    // copy the message into the slot
    memcpy(slot->message, msg_ptr, msg_size);

    if (DEBUG2 && debugflag2) 
        USLOSS_Console("createSlot(): created new slot for message size %d, slotID: %d, total slots: %d\n", msg_size, slot->slotID, numSlots);

    return slot->slotID;
} /* createSlot */



/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("MboxSend()");

    // if the mail slot table overflows, that is an error that should halt USLOSS
    if (numSlots == MAXSLOTS) {
        USLOSS_Console("Mail slot table overflow. Halting...\n");
        USLOSS_Halt(1);
    }

    // invalid mbox_id
    if (mbox_id < 0) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): called with invalid mbox_id: %d, returning -1\n", mbox_id);
        return -1;
    }

    // get the mailbox
    mailbox *box = &MailBoxTable[mbox_id % MAXMBOX];

    // check for invalid arguments
    if (box->status == INACTIVE || msg_size < 0 || msg_size > box->slotSize) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): called with and invalid argument, returning -1\n", mbox_id);
        return -1;
    }

    // if all the slots are taken, block caller until slots are avaliable
    if (box->slots.size == box->totalSlots) {
        // init proc details
        mboxProc mproc;
        mproc.nextMboxProc = NULL;
        mproc.pid = getpid();
        mproc.msg_ptr = NULL;
        mproc.messageRecieved = NULL;

        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): all slots are full, blocking pid %d...\n", mproc.pid);

        // add to queue of send blocked processes at this mailbox
        enq(&box->blockedProcsSend, &mproc);

        blockMe(FULL_BOX); // block
        disableInterrupts(); // disable interrupts again when it gets unblocked

        // return -3 if process zap'd or the mailbox released while blocked on the mailbox
        if (isZapped() || box->status == INACTIVE) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxSend(): process %d was zapped while blocked on a send, returning -3\n", mproc.pid);
            enableInterrupts(); // enable interrupts before return
            return -3;
        }
    }


    // otherwise, create a new slot and add the message to it
    int slotID = createSlot(msg_ptr, msg_size);
    slotPtr slot = &MailSlotTable[slotID % MAXSLOTS];

    // if there is a blocked process at this mailbox on a recieve, give it the message and unblock it
    if (box->blockedProcsRecieve.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsRecieve);
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): giving message to blocked process %d\n", proc->pid);
        proc->messageRecieved = slot;
        unblockProc(proc->pid);
    }

    // otherwise, add slot to the mailbox
    else
        enq(&box->slots, slot); 

    enableInterrupts(); // enable interrupts before return
    return 0;
} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("MboxReceive()");
    slotPtr slot;

    // invalid mbox_id
    if (mbox_id < 0) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): called with invalid mbox_id: %d, returning -1\n", mbox_id);
        return -1;
    }

    mailbox *box = &MailBoxTable[mbox_id % MAXMBOX];

    // check for invalid arguments
    if (box->status == INACTIVE || msg_ptr == NULL) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): called with and invalid argument, returning -1\n", mbox_id);
        return -1;
    }

    // block if there are no messages avaliable
    if (box->slots.size == 0) {
        // init proc details
        mboxProc mproc;
        mproc.nextMboxProc = NULL;
        mproc.pid = getpid();
        mproc.msg_ptr = msg_ptr;
        mproc.messageRecieved = NULL;

        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): no messages avaliable, blocking pid %d...\n", mproc.pid);

        // add to queue of blocked procs on a recieve
        enq(&box->blockedProcsRecieve, &mproc);

        blockMe(NO_MESSAGES); // block
        disableInterrupts(); // disable interrupts again when it gets unblocked

        // return -3 if process zap'd or the mailbox released while blocked on the mailbox
        if (isZapped() || box->status == INACTIVE || mproc.messageRecieved == NULL) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxReceive(): either process %d was zapped, mailbox was freed, or we did not get the message, returning -3\n", mproc.pid);
            enableInterrupts(); // enable interrupts before return
            return -3;
        }

        slot = mproc.messageRecieved; // get the message
    }

    else
        slot = deq(&box->slots); // get the mailSlot

    // check if they don't have enough room for the message
    if (slot == NULL || slot->status == EMPTY || msg_size < slot->messageSize) {
        if (DEBUG2 && debugflag2 && (slot == NULL || slot->status == EMPTY)) 
                USLOSS_Console("MboxReceive(): mail slot null or empty, returning -1\n");
        else if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): no room for message, room provided: %d, message size: %d, returning -1\n", msg_size, slot->messageSize);
        return -1;
    }

    // finally, copy the message
    int size = slot->messageSize;
    memcpy(msg_ptr, slot->message, size);

    // free the mail slot
    emptySlot(slot->slotID % MAXSLOTS);

    // unblock any proc that is blocked on a send to this mailbox
    if (box->blockedProcsSend.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsSend);
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): unblocking process %d that was blocked on send\n", proc->pid);
        unblockProc(proc->pid);
    }

    enableInterrupts(); // enable interrupts before return
    return size;
} /* MboxReceive */


/* ------------------------------------------------------------------------
   Name - emptyBox
   Purpose - Initializes a mailbox.
   Parameters - index of the mailbox in the mailbox table.
   Returns - nothing.
   Side Effects - none.
   ----------------------------------------------------------------------- */
void emptyBox(int i)
{
    MailBoxTable[i].mboxID = -1;
    MailBoxTable[i].status = INACTIVE;
    MailBoxTable[i].totalSlots = -1;
    MailBoxTable[i].slotSize = -1;
    numBoxes--; 
} /* emptyBox */


/* ------------------------------------------------------------------------
   Name - emptySlot
   Purpose - Initializes a mail slot.
   Parameters - index of the mail slot in the mail slot table.
   Returns - nothing.
   Side Effects - none.
   ----------------------------------------------------------------------- */
void emptySlot(int i)
{
    MailSlotTable[i].mboxID = -1;
    MailSlotTable[i].status = EMPTY;
    MailSlotTable[i].slotID = -1;
    numSlots--;
} /* emptySlot */


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
        USLOSS_Halt(1); // from phase1 pdf
    }
} /* requireKernelMode */


/*
 * Enables the interrupts.
 */
void enableInterrupts()
{
    // turn the interrupts ON iff we are in kernel mode
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("enable interrupts\n");
        USLOSS_Halt(1);
    } else
        // We ARE in kernel mode
        USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
        // if (DEBUG && debugflag)
        //     USLOSS_Console("Interrupts enabled.\n");
} /* enableInterrupts */


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
        // if (DEBUG && debugflag)
        //     USLOSS_Console("Interrupts disabled.\n");
} /* disableInterrupts */


/* ------------------------------------------------------------------------
  Functions for queue:
    initQueue, enq, deq, and peek.
   ----------------------------------------------------------------------- */

/* Initialize the given queue */
void initQueue(queue* q, int type) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    q->type = type;
}

/* Add the given pointer to the back of the given queue. */
void enq(queue* q, void* p) {
    if (q->head == NULL && q->tail == NULL) {
        q->head = q->tail = p;
    } else {
        if (q->type == SLOTQUEUE)
            ((slotPtr)(q->tail))->nextSlotPtr = p;
        else if (q->type == PROCQUEUE)
            ((mboxProcPtr)(q->tail))->nextMboxProc = p;
        q->tail = p;
    }
    q->size++;
}

/* Remove and return the head of the given queue. */
void* deq(queue* q) {
    void* temp = q->head;
    if (q->head == NULL) {
        return NULL;
    }
    if (q->head == q->tail) {
        q->head = q->tail = NULL; 
    }
    else {
        if (q->type == SLOTQUEUE)
            q->head = ((slotPtr)(q->head))->nextSlotPtr;  
        else if (q->type == PROCQUEUE)
            q->head = ((mboxProcPtr)(q->head))->nextMboxProc;  
    }
    q->size--;
    return temp;
}

/* Return the head of the given queue. */
void* peek(queue* q) {
    return q->head;   
}