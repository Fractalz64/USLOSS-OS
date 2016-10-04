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
#include "handler.c"

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

/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

mailbox MailBoxTable[MAXMBOX]; // the mail boxes 
mailSlot MailSlotTable[MAXSLOTS]; // the mail slots
mboxProc mboxProcTable[MAXPROC];  // the processes

// the total number of mailboxes and mail slots in use
int numBoxes, numSlots;

// next mailbox/slot id to be assigned
int nextMboxID = 0, nextSlotID = 0, nextProc = 0;

// system call vector
void (*syscall_vec[MAXSYSCALLS])(systemArgs *args);

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

    // allocate mailboxes for interrupt handlers
    IOmailboxes[CLOCKBOX] = MboxCreate(0, sizeof(int)); // one clock unit
    IOmailboxes[TERMBOX] = MboxCreate(0, sizeof(int));  // 4 terminal units
    IOmailboxes[TERMBOX+1] = MboxCreate(0, sizeof(int));
    IOmailboxes[TERMBOX+2] = MboxCreate(0, sizeof(int));
    IOmailboxes[TERMBOX+3] = MboxCreate(0, sizeof(int));
    IOmailboxes[DISKBOX] = MboxCreate(0, sizeof(int));  // two disk units
    IOmailboxes[DISKBOX+1] = MboxCreate(0, sizeof(int));

    // init interrupt handlers
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler2;
    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;

    // set all system calls to nullsys, fill next phase
    for (i = 0; i < MAXSYSCALLS; i++) {
        syscall_vec[i] = nullsys;
    }

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
    if (numBoxes == MAXMBOX || slots < 0 || slot_size < 0 || slot_size > MAX_MESSAGE) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxCreate(): illegal args or max boxes reached, returning -1\n");
        return -1;
    }

    // if the index is taken, find the first avaliable index
    if (nextMboxID >= MAXMBOX || MailBoxTable[nextMboxID].status == ACTIVE) {
        for (int i = 0; i < MAXMBOX; i++) {
            if (MailBoxTable[i].status == INACTIVE) {
                nextMboxID = i;
                break;
            }
        }
    }

    // get mailbox
    mailbox *box = &MailBoxTable[nextMboxID];

    // initialize fields
    box->mboxID = nextMboxID++;
    box->totalSlots = slots;
    box->slotSize = slot_size;
    box->status = ACTIVE;
    initQueue(&box->slots, SLOTQUEUE);
    initQueue(&box->blockedProcsSend, PROCQUEUE);
    initQueue(&box->blockedProcsReceive, PROCQUEUE);

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
    if (mailboxID < 0 || mailboxID >= MAXMBOX) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxRelease(): called with invalid mailboxID: %d, returning -1\n", mailboxID);
        return -1;
    }

    // get mailbox
    mailbox *box = &MailBoxTable[mailboxID];

    // check if mailbox is in use
    if (box == NULL || box->status == INACTIVE) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxRelease(): mailbox %d is already released, returning -1\n", mailboxID);
        return -1;
    }

    // empty the slots in the mailbox
    while (box->slots.size > 0) {
        slotPtr slot = (slotPtr)deq(&box->slots);
        emptySlot(slot->slotID);
    }

    // release the mailbox
    emptyBox(mailboxID);

    if (DEBUG2 && debugflag2) 
        USLOSS_Console("MboxRelease(): released mailbox %d\n", mailboxID);

    // unblock any processes blocked on a send 
    while (box->blockedProcsSend.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsSend);
        unblockProc(proc->pid);
        disableInterrupts(); // re-disable interrupts
    }

    // unblock any processes blocked on a receive 
    while (box->blockedProcsReceive.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsReceive);
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

    // if the index is taken, find the first avaliable index
    if (nextSlotID >= MAXSLOTS || MailSlotTable[nextSlotID].status == USED) {
        for (int i = 0; i < MAXSLOTS; i++) {
            if (MailSlotTable[i].status == EMPTY) {
                nextSlotID = i;
                break;
            }
        }
    }

    // assumes parameters were already checked to be valid by caller
    slotPtr slot = &MailSlotTable[nextSlotID];
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
   Name - sendToProc
   Purpose - sends a message directly to a process
   Parameters - pointer to the process, message to put in the slot, 
                and the message size.
   Returns - 0 for success or -1 for failure
   Side Effects - none? 
   ----------------------------------------------------------------------- */
int sendToProc(mboxProcPtr proc, void *msg_ptr, int msg_size)
{
    // check for error cases and return -1
    if (proc == NULL || proc->msg_ptr == NULL || proc->msg_size < msg_size) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("sendToProc(): invalid args, returning -1\n");
        proc->msg_size = -1;
        return -1;
    }

    // copy the message
    memcpy(proc->msg_ptr, msg_ptr, msg_size);
    proc->msg_size = msg_size;

    if (DEBUG2 && debugflag2) 
        USLOSS_Console("sendToProc(): gave message size %d to process %d\n", msg_size, proc->pid);

    return 0;
}


/* ------------------------------------------------------------------------
   Name - send
   Purpose - Put a message into a slot for the indicated mailbox.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg, 
                type of send ( 0 for regular, 1 for conditional)
   Returns - zero if successful, -1 if invalid args, -2 if not sent.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int send(int mbox_id, void *msg_ptr, int msg_size, int conditional)
{
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("MboxSend()");
    if (DEBUG2 && debugflag2) 
        USLOSS_Console("send(): called with mbox_id: %d, msg_ptr: %d, msg_size: %d, conditional: %d\n", mbox_id, msg_ptr, msg_size, conditional);

    // invalid mbox_id
    if (mbox_id < 0 || mbox_id >= MAXMBOX) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): called with invalid mbox_id: %d, returning -1\n", mbox_id);
        enableInterrupts(); // re-enable interrupts
        return -1;
    }

    // get the mailbox
    mailbox *box = &MailBoxTable[mbox_id];

    // check for invalid arguments
    if (box->status == INACTIVE || msg_size < 0 || msg_size > box->slotSize) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): called with and invalid argument, returning -1\n", mbox_id);
        enableInterrupts(); // re-enable interrupts
        return -1;
    }

    // handle blocked receiver
    if (box->blockedProcsReceive.size > 0 && (box->slots.size < box->totalSlots || box->totalSlots == 0)) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsReceive);
        // give the message to the receiver
        int result = sendToProc(proc, msg_ptr, msg_size);
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxSend(): unblocking process %d that was blocked on receive\n", proc->pid);
        unblockProc(proc->pid);
        enableInterrupts(); // re-enable interrupts
        if (result < 0) 
            return -1;
        return 0;
    }

    // if all the slots are taken, block caller until slots are avaliable
    if (box->slots.size == box->totalSlots) {
        // don't block on a conditional send, return -2 instead
        if (conditional) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxSend(): conditional send failed, returning -2\n");
            enableInterrupts(); // re-enable interrupts
            return -2;
        }

        // init proc details
        mboxProc mproc;
        mproc.nextMboxProc = NULL;
        mproc.pid = getpid();
        mproc.msg_ptr = msg_ptr;
        mproc.msg_size = msg_size;

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
        enableInterrupts(); // enable interrupts before return
        return 0;
    }

    // if the mail slot table overflows, that is an error that should halt USLOSS
    if (numSlots == MAXSLOTS) {
        if (conditional) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("No slots avaliable for conditional send to box %d, returning -2\n", mbox_id);
            return -2;
        }
        USLOSS_Console("Mail slot table overflow. Halting...\n");
        USLOSS_Halt(1);
    }

    // create a new slot and add the message to it
    int slotID = createSlot(msg_ptr, msg_size);
    slotPtr slot = &MailSlotTable[slotID];
    enq(&box->slots, slot); // add slot to mailbox

    enableInterrupts(); // enable interrupts before return
    return 0;
} /* send */


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
    return send(mbox_id, msg_ptr, msg_size, 0);
} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxCondSend
   Purpose - Put a message into a slot for the indicated mailbox.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args, -2 if message not sent
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size)
{
    return send(mbox_id, msg_ptr, msg_size, 1);
} /* MboxCondSend */


/* ------------------------------------------------------------------------
   Name - receive
   Purpose - Get a msg from a slot of the indicated mailbox.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received, type of receive (0 for regular, 1 for conditional)
   Returns - actual size of msg if successful, -1 if invalid args, 
                -2 if conditional receive failed.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int receive(int mbox_id, void *msg_ptr, int msg_size, int conditional)
{
    // disable interrupts and require kernel mode
    disableInterrupts();
    requireKernelMode("MboxReceive()");
    slotPtr slot;

    // invalid mbox_id
    if (mbox_id < 0 || mbox_id >= MAXMBOX) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): called with invalid mbox_id: %d, returning -1\n", mbox_id);
        enableInterrupts(); // re-enable interrupts
        return -1;
    }

    mailbox *box = &MailBoxTable[mbox_id];
    int size;

    // make sure box is valid
    if (box->status == INACTIVE) {
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): invalid box id: %d, returning -1\n", mbox_id);
        enableInterrupts(); // re-enable interrupts
        return -1;
    }

    // handle 0 slot mailbox
    if (box->totalSlots == 0) {
        mboxProc mproc;
        mproc.nextMboxProc = NULL;
        mproc.pid = getpid();
        mproc.msg_ptr = msg_ptr;
        mproc.msg_size = msg_size;

        // if a process has sent, unblock it and get the message
        if (box->blockedProcsSend.size > 0) {
            mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsSend);
            sendToProc(&mproc, proc->msg_ptr, proc->msg_size);
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxReceive(): unblocking process %d that was blocked on send to 0 slot mailbox\n", proc->pid);
            unblockProc(proc->pid);
        }
        // otherwise block the receiver (if not conditional)
        else if (!conditional) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxReceive(): blocking process %d on 0 slot mailbox\n", mproc.pid);
            enq(&box->blockedProcsReceive, &mproc);
            blockMe(NO_MESSAGES);

            if (isZapped() || box->status == INACTIVE) {
                if (DEBUG2 && debugflag2) 
                    USLOSS_Console("MboxSend(): process %d was zapped while blocked on a send, returning -3\n", mproc.pid);
                enableInterrupts(); // enable interrupts before return
                return -3;
            }     
        }

        enableInterrupts(); // re-enable interrupts
        return mproc.msg_size;
    }

    // block if there are no messages avaliable 
    if (box->slots.size == 0) {
        // init proc details
        mboxProc mproc;
        mproc.nextMboxProc = NULL;
        mproc.pid = getpid();
        mproc.msg_ptr = msg_ptr;
        mproc.msg_size = msg_size;
        mproc.messageReceived = NULL;

        // handle 0 slot mailbox, if a process has sent, unblock it and get the message
        if (box->totalSlots == 0 && box->blockedProcsSend.size > 0) {
            mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsSend);
            sendToProc(&mproc, proc->msg_ptr, proc->msg_size);
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxReceive(): unblocking process %d that was blocked on send to 0 slot mailbox\n", proc->pid);
            unblockProc(proc->pid);
            enableInterrupts(); // re-enable interrupts
            return mproc.msg_size;
        }

        // don't block on a conditional receive, return -2 instead
        if (conditional) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxReceive(): conditional receive failed, returning -2\n");
            enableInterrupts(); // re-enable interrupts
            return -2;
        }

        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): no messages avaliable, blocking pid %d...\n", mproc.pid);

        // add to queue of blocked procs on a receive
        enq(&box->blockedProcsReceive, &mproc);
        blockMe(NO_MESSAGES); // block
        disableInterrupts(); // disable interrupts again when it gets unblocked

        // return -3 if process zap'd or the mailbox released while blocked on the mailbox
        if (isZapped() || box->status == INACTIVE) {
            if (DEBUG2 && debugflag2) 
                USLOSS_Console("MboxReceive(): either process %d was zapped, mailbox was freed, or we did not get the message, returning -3\n", mproc.pid);
            enableInterrupts(); // enable interrupts before return
            return -3;
        }

        return mproc.msg_size;
    }

    else
        slot = deq(&box->slots); // get the mailSlot

    // check if they don't have enough room for the message
    if (slot == NULL || slot->status == EMPTY || msg_size < slot->messageSize) {
        if (DEBUG2 && debugflag2 && (slot == NULL || slot->status == EMPTY)) 
                USLOSS_Console("MboxReceive(): mail slot null or empty, returning -1\n");
        else if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): no room for message, room provided: %d, message size: %d, returning -1\n", msg_size, slot->messageSize);
        enableInterrupts(); // re-enable interrupts
        return -1;
    }

    // finally, copy the message
    size = slot->messageSize;
    memcpy(msg_ptr, slot->message, size);

    // free the mail slot
    emptySlot(slot->slotID);

    // unblock a proc that is blocked on a send to this mailbox
    if (box->blockedProcsSend.size > 0) {
        mboxProcPtr proc = (mboxProcPtr)deq(&box->blockedProcsSend);
        // create slot for the sender's message
        int slotID = createSlot(proc->msg_ptr, proc->msg_size);
        slotPtr slot = &MailSlotTable[slotID];
        enq(&box->slots, slot); // add the slot
        // unblock the sender
        if (DEBUG2 && debugflag2) 
            USLOSS_Console("MboxReceive(): unblocking process %d that was blocked on send\n", proc->pid);
        unblockProc(proc->pid);
    }

    enableInterrupts(); // enable interrupts before return
    return size;
} /* receive */


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
    return receive(mbox_id, msg_ptr, msg_size, 0);
} /* MboxReceive */


/* ------------------------------------------------------------------------
   Name - MboxCondReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args,
                -2 if receive failed.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxCondReceive(int mbox_id, void *msg_ptr, int msg_size)
{
    return receive(mbox_id, msg_ptr, msg_size, 1);
} /* MboxCondReceive */


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