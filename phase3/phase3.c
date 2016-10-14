#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>

/* ------------------------- Prototypes ----------------------------------- */
void requireKernelMode(char *);

/* -------------------------- Globals ------------------------------------- */
int sems[MAXSEMS];
procStruct3 ProcTable3[MAXPROC];

int 
start2(char *arg)
{
    int pid;
    int status;
    /*
     * Check kernel mode here.
     */
    requireKernelMode("start2");

    /*
     * Data structure initialization as needed...
     */

    // populate system call vec
    int i;
    for (i = 0; i < USLOSS_MAX_SYSCALLS; i++) {
        systemCallVec[i] = nullsys3;
    }
    systemCallVec[SYS_SPAWN] = spawn;
    systemCallVec[SYS_WAIT] = wait;
    systemCallVec[SYS_TERMINATE] = terminate;
    systemCallVec[SYS_SEMCREATE] = semCreate;
    systemCallVec[SYS_SEMP] = 
    systemCallVec[SYS_SEMV] = 
    systemCallVec[SYS_SEMFREE] = 
    systemCallVec[SYS_GETTIMEOFDAY] = 
    systemCallVec[SYS_CPUTIME] = 
    systemCallVec[SYS_GETPID] = 


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscallHandler; spawnReal is the function that
     * contains the implementation and is called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes USLOSS_Syscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawnReal().
     *
     * Here, we only call spawnReal(), since we are already in kernel mode.
     *
     * spawnReal() will create the process by using a call to fork1 to
     * create a process executing the code in spawnLaunch().  spawnReal()
     * and spawnLaunch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawnReal() will
     * return to the original caller of Spawn, while spawnLaunch() will
     * begin executing the function passed to Spawn. spawnLaunch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawnReal() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and 
     * return to the user code that called Spawn.
     */
    pid = spawnReal("start3", start3, NULL, USLOSS_MIN_STACK, 3);

    /* Call the waitReal version of your wait code here.
     * You call waitReal (rather than Wait) because start2 is running
     * in kernel (not user) mode.
     */
    pid = waitReal(&status);

} /* start2 */


/* ------------------------------------------------------------------------
   Name - spawn
   Purpose - Extracts arguments and checks for correctness.
   Parameters - systemArgs containing arguments.
   Returns - 
   ----------------------------------------------------------------------- */
void spawn(systemArgs *args) 
{
    requireKernelMode("spawn");

    int (*func)(char *) = args->arg1;
    char *arg = args->arg2;
    int stack_size = (int) args->arg3;
    int priority = (int) args->arg4;    
    char *name = (char *)(args->arg5);

    int pid = spawnReal(name, func, arg, stack_size, priority);

    // terminate self if zapped
    if (isZapped())
        terminateReal(); // need to write this

    // switch to user mode

    // swtich back to kernel mode and put values for Spawn
    args->arg1 = pid;
    args->arg4 = // this should be -1 if args are not correct but fork1 checks those so idk if we are supposed to do it here too, or what
} /* spawn */


/* ------------------------------------------------------------------------
   Name - spawnLaunch
   Purpose - sets up process table entry for new process and starts
                running it in user mode
   Parameters - 
   Returns - 
   ----------------------------------------------------------------------- */
void spawnLaunch(char startArg) {
    requireKernelMode("spawnLaunch");

    // now the process is running so we can get its pid and set up its stuff
    procPtr3 proc = &ProcTable3[getpid() % MAXPROC]; 
    proc->pid = getpid();
    proc->mbox = &MboxCreate(0, 0); // create proc's 0 slot mailbox

    // now block so we can get info from spawnReal
    Send(proc->mbox->mboxID, 0, 0);

    // switch to user mode

    // call the function to start the process
    // this should return the result to launch in phase1
    return proc->startFunc(startArg);
}


/* ------------------------------------------------------------------------
   Name - spawnReal
   Purpose - forks a process running spawnLaunch
   Parameters - same as fork1
   Returns - pid
   ----------------------------------------------------------------------- */
int spawnReal(char *name, int (*func)(char *), char *arg, int stack_size, int priority) 
{
    requireKernelMode("spawnReal");
    
    // fork the process and get its pid
    int pid = fork1(name, spawnLaunch, arg, stack_size, priority);

    // return -1 if fork failed
    if (pid < 0)
        return -1;

    // now we have the pid, we can access the proc table entry created by spawnLaunch
    procPtr3 proc = ProcTable3[pid % MAXPROC]; 

    // give proc its start function
    proc->startFunc = func;

    // unblock the process so spawnLaunch can start it
    Receive(proc->mbox->mboxID, 0, 0);

    return pid;
} /* spawnReal */


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
        USLOSS_Halt(1); 
    }
} /* requireKernelMode */
