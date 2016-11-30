
#include "usloss.h"
#include <vm.h>
#include <stdlib.h>
#include <mmu.h>
#include <phase5.h>

#define DEBUG 0
#define TAG 0
extern int debugflag;

extern Process processes[MAXPROC];
extern void *vmRegion;
extern VmStats vmStats;

void
p1_fork(int pid)
{
    if (DEBUG && debugflag)
        USLOSS_Console("p1_fork() called: pid = %d\n", pid);

    if (vmRegion > 0) {
    	// create the process's page table
    	processes[pid % MAXPROC].pageTable = malloc( processes[pid % MAXPROC].numPages * sizeof(PTE)); 
    }

} /* p1_fork */


/* As a reminder:
 * In phase 1, p1_switch is called by the dispatcher right before the
 * dispatcher does: enableInterrupts() followed by USLOSS_ContestSwitch()
 */
void
p1_switch(int old, int new)
{
    if (DEBUG && debugflag)
        USLOSS_Console("p1_switch() called: old = %d, new = %d\n", old, new);

    if (vmRegion > 0) {
    	vmStats.switches++;

    	Process *oldProc = &processes[old % MAXPROC];
    	Process *newProc = &processes[new % MAXPROC];

    	// unload old process's mappings
    	int i;
    	for (i = 0; i < oldProc->numPages; i++) {
    		if (oldProc->pageTable[i].frame > 0) { // there is a valid mapping
    			USLOSS_MmuUnmap(TAG, i);
    		}
    	}

		for (i = 0; i < newProc->numPages; i++) {
    		if (newProc->pageTable[i].frame > 0) { // there is a valid mapping
    			USLOSS_MmuMap(TAG, i, newProc->pageTable[i].frame, USLOSS_MMU_PROT_RW);
   			}
    	}    	
    }

} /* p1_switch */

void
p1_quit(int pid)
{
    if (DEBUG && debugflag)
        USLOSS_Console("p1_quit() called: pid = %d\n", pid);

    if (vmRegion > 0) {
    	// clear the page table
    	Process *proc = &processes[pid % MAXPROC];
    	int i;
    	for (i = 0; i < proc->numPages; i++) {
    		proc->pageTable[i].state = UNUSED;
    		proc->pageTable[i].frame = -1;
    		proc->pageTable[i].diskBlock = -1;
    	}

    	// destroy the page table
    	free(proc->pageTable); 
    }
} /* p1_quit */
