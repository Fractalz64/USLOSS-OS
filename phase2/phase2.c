/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>

#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void emptyBox(int);
void emptySlot(int);
void disableInterrupts(void);
void enableInterrupts(void);
void requireKernelMode(char *);
void initSlotQueue(slotQueue*, int);
void enq(slotQueue*, slotPtr);
slotPtr deq(slotQueue*);
slotPtr peek(slotQueue*);


/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

// the mail boxes 
mailbox MailBoxTable[MAXMBOX];

// also need array of mail slots, array of function ptrs to system call 
// handlers, ...
mailSlot MailSlotTable[MAXSLOTS];

// the total number of mailboxes and mail slots in use
int numBoxes, numSlots;

// next mailbox/slot id to be assigned
int nextMboxID = 0, nextSlotID = 0;

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
    if (numBoxes == MAXMBOX || slots < 0 || slot_size < 0)
        return -1;

    // get mailbox
    mailbox *box = &MailBoxTable[nextMboxID % MAXMBOX];

    // initialize fields
    box->mboxID = nextMboxID++;
    box->totalSlots = slots;
    box->slotsTaken = 0;
    box->slotSize = slot_size;
    box->status = ACTIVE;

    numBoxes++; // increment mailbox count


    enableInterrupts(); // re-enable interrupts
    return box->mboxID;
} /* MboxCreate */


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
    requireKernelMode("MboxCreate()");

    // if the mail slot table overflows, that is an error that should halt USLOSS
    if (numSlots == MAXSLOTS) {
        USLOSS_Console("Mail slot table overflow. Halting...\n");
        USLOSS_Halt(1);
    }

    // invalid mbox_id
    if (mbox_id < 0)
        return -1;

    mailbox *box = &MailBoxTable[mbox_id % MAXMBOX];

    // check for invalid arguments
    if (box->status == INACTIVE || msg_size < 0 || msg_size > box->slotSize)
        return -1;

    // if all the slots are taken, block caller until slots are avaliable
    if (box->slotsTaken == box->totalSlots) {
        blockProc(FULLBOX);
        disableInterrupts(); // disable interrupts again when it gets unblocked

        // return -3 if process zap'd or the mailbox released while blocked on the mailbox
        if (isZapped() || box->status == INACTIVE) {
            return -3;
        }
    }

    // create slot for message
    slotPtr slot = &MailSlotTable[nextSlotID % MAXSLOTS];
    slot->slotID = nextSlotID++;
    slot->status = FULL;
    numSlots++;

    // copy the message into the slot
    memcpy(slot->message, msg_ptr, msg_size);

    // add slot to the mailbox
    enq(&box->slots, slot);

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
    MailBoxTable[i].slotsTaken = -1;
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



/* ------------------------------------------------------------------------
  Functions for slotQueue:
    initSlotQueue, enq, deq, and peek.
   ----------------------------------------------------------------------- */

/* Initialize the given slotQueue */
void initSlotQueue(slotQueue* q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

/* Add the given slotPtr to the back of the given queue. */
void enq(slotQueue* q, slotPtr p) {
    if (q->head == NULL && q->tail == NULL) {
        q->head = q->tail = p;
    } else {
        q->tail->nextSlotPtr = p;
        q->tail = p;
    }
    q->size++;
}

/* Remove and return the head of the given queue. */
slotPtr deq(slotQueue* q) {
    slotPtr temp = q->head;
    if (q->head == NULL) {
        return NULL;
    }
    if (q->head == q->tail) {
        q->head = q->tail = NULL; 
    }
    else {
        q->head = q->head->nextSlotPtr;  
    }
    q->size--;
    return temp;
}

/* Return the head of the given queue. */
slotPtr peek(slotQueue* q) {
    if (q->head == NULL) {
        return NULL;
    }
    return q->head;   
}
