/*
 * These are the definitions for phase 3 of the project
 */

#ifndef _PHASE3_H
#define _PHASE3_H

#define MAXSEMS         200

#endif /* _PHASE3_H */

/* 
* Process struct for phase 3
*/
typedef struct procStruct3 procStruct3;
typedef struct procStruct3 * procPtr3;

struct procStruct3 {
    int             pid;
    mailbox 		*mbox; /* 0 slot mailbox belonging to this process */
	int (* startFunc) (char *);   /* function where process begins */
    procPtr3     	nextProc;
};
