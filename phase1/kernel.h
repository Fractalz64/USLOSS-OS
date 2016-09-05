/* Patrick's DEBUG printing constant... */
#define DEBUG 1

typedef struct procStruct procStruct;

typedef struct procStruct * procPtr;

struct procStruct {
	procPtr         nextProcPtr;
	procPtr         childProcPtr;
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
	int             open;          /* 1 if this slot in the process table is empty, 0 if it is taken */
	procPtr         parentPtr;
};

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

/* Queue struct for the Ready Lists */
typedef struct procQueue procQueue;

struct procQueue {
	procPtr head;
	procPtr tail;
	int size;
};

void enq(procQueue* q, procPtr p) {
	USLOSS_Console("enquing process id %d\n", p->pid);
	if (q->head == NULL && q->tail == NULL) {
		q->head = q->tail = p;
	} else {
		q->tail->nextProcPtr = p;
		q->tail = p;
	}
	q->size++;
	USLOSS_Console("head = %s\n", q->head->name);
	USLOSS_Console("tail = %s\n", q->tail->name);
	USLOSS_Console("size = %d\n", q->size);
}

procPtr deq(procQueue* q) {
	procPtr temp = q->head;
	if (q->head == NULL) {
		printf("Empty Queue\n");
		return NULL;
	}
	USLOSS_Console("dequing process id %d\n", q->head->pid);
	if (q->head == q->tail) {
		q->head = q->tail = NULL; 
	}
	else {
		q->head = q->head->nextProcPtr;  
		USLOSS_Console("head = %s\n", q->head->name);
		USLOSS_Console("tail = %s\n", q->tail->name);
	}
	q->size--;
	USLOSS_Console("size = %d\n", q->size);
	return temp;
}

procPtr peek(procQueue* q) {
	if (q->head == NULL) {
		printf("Empty Queue\n");
		return NULL;
	}
	return q->head;   
}
