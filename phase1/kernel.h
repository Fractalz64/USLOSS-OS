/* Patrick's DEBUG printing constant... */
#define DEBUG 1

typedef struct procStruct procStruct;

typedef struct procStruct * procPtr;

/* Queue struct for the Ready Lists */
typedef struct procQueue procQueue;
#define READYLIST 0
#define CHILDREN 1
#define DEADCHILDREN 2
#define ZAP 3

struct procQueue {
	procPtr head;
	procPtr tail;
	int 	size;
	int 	type; // which procPtr to use for next
};

/* Process struct */
struct procStruct {
	procPtr         nextProcPtr;
	procPtr         nextSiblingPtr;
	char            name[MAXNAME];     /* process's name */
	char            startArg[MAXARG];  /* args passed to process */
	USLOSS_Context  state;             /* current context for process */
	short           pid;               /* process id */
	int             priority;
	int (* startFunc) (char *);   /* function where process begins -- launch */
	char           *stack;
	unsigned int    stackSize;
	int             status;        /* READY, BLOCKED, QUIT, etc. */
	/* other fields as needed... */
	procPtr         parentPtr;
	procQueue 		childrenQueue;  /* queue of the process's children */
	int 			quitStatus;		/* whatever the process returns when it quits */
	procQueue		deadChildrenQueue;	/* list of children who have quit in the order they have quit */
	procPtr 		nextDeadSibling;
	int				zapStatus; // 1 zapped; 0 not zapped
	procQueue		zapQueue;
	procPtr			nextZapPtr;
	int 			timeStarted; // the time the current time slice started
	int 			cpuTime; // the total amount of time the process has been running	
	int 			sliceTime; // how long the process has been running in the current time slice
};

#define TIMESLICE 80000

/* process statuses */
#define EMPTY 0
#define READY 1
#define RUNNING 2
#define JBLOCKED 3
#define QUIT 4
#define ZBLOCKED 5

struct psrBits {
	unsigned int curMode:1;
	unsigned int curIntEnable:1;
	unsigned int prevMode:1;
	unsigned int prevIntEnable:1;
	unsigned int unused:28;
};

union psrValues {
	struct psrBits bits;
	unsigned int integerPart;
};

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY (MINPRIORITY + 1)

/* Initialize the given procQueue */
void initProcQueue(procQueue* q, int type) {
	q->head = NULL;
	q->tail = NULL;
	q->size = 0;
	q->type = type;
}

/* Add the given procPtr to the back of the given queue. */
void enq(procQueue* q, procPtr p) {
	// USLOSS_Console("enquing process id %d\n", p->pid);
	if (q->head == NULL && q->tail == NULL) {
		q->head = q->tail = p;
	} else {
		if (q->type == READYLIST)
			q->tail->nextProcPtr = p;
		else if (q->type == CHILDREN)
			q->tail->nextSiblingPtr = p;
		else if (q->type == ZAP) 
			q->tail->nextZapPtr = p;
		else
			q->tail->nextDeadSibling = p;
		q->tail = p;
	}
	q->size++;
	// USLOSS_Console("head = %s\n", q->head->name);
	// USLOSS_Console("tail = %s\n", q->tail->name);
	// USLOSS_Console("size = %d\n", q->size);
}

/* Remove and return the head of the given queue. */
procPtr deq(procQueue* q) {
	procPtr temp = q->head;
	if (q->head == NULL) {
		// printf("Empty Queue\n");
		return NULL;
	}
	// USLOSS_Console("dequing process id %d\n", q->head->pid);
	if (q->head == q->tail) {
		q->head = q->tail = NULL; 
	}
	else {
		if (q->type == READYLIST)
			q->head = q->head->nextProcPtr;  
		else if (q->type == CHILDREN)
			q->head = q->head->nextSiblingPtr;  
		else if (q->type == ZAP) 
			q->head = q->head->nextZapPtr;
		else 
			q->head = q->head->nextDeadSibling;  
		// USLOSS_Console("head = %s\n", q->head->name);
		// USLOSS_Console("tail = %s\n", q->tail->name);
	}
	q->size--;
	// USLOSS_Console("size = %d\n", q->size);
	return temp;
}

/* Remove the child process with the given pid from the queue */
void qRemoveChild(procQueue* q, int pid) {
	if (q->head == 	NULL || q->type != CHILDREN)
		return;

	if (q->head->pid == pid) {
		deq(q);
		return;
	}

	procPtr prev = q->head;
	procPtr p = q->head->nextSiblingPtr;

	while (p != NULL) {
		if (p->pid == pid) {
			if (p == q->tail)
				q->tail = prev;
			else
				prev->nextSiblingPtr = p->nextSiblingPtr->nextSiblingPtr;
			q->size--;
		}
	}
}

/* Return the head of the given queue. */
procPtr peek(procQueue* q) {
	if (q->head == NULL) {
		// printf("Empty Queue\n");
		return NULL;
	}
	return q->head;   
}
