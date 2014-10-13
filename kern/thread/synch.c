/*
 * Synchronization primitives.
 * See synch.h for specifications of the functions.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <curthread.h>
#include <machine/spl.h>
#include <queue.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *namearg, int initial_count)
{
	struct semaphore *sem;

	assert(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}

	sem->name = kstrdup(namearg);
	if (sem->name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	spl = splhigh();
	assert(thread_hassleepers(sem)==0);
	splx(spl);

	/*
	 * Note: while someone could theoretically start sleeping on
	 * the semaphore after the above test but before we free it,
	 * if they're going to do that, they can just as easily wait
	 * a bit and start sleeping on the semaphore after it's been
	 * freed. Consequently, there's not a whole lot of point in 
	 * including the kfrees in the splhigh block, so we don't.
	 */

	kfree(sem->name);
	kfree(sem);
}

void 
P(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	assert(in_interrupt==0);

	spl = splhigh();
	while (sem->count==0) {
		thread_sleep(sem);
	}
	assert(sem->count>0);
	sem->count--;
	splx(spl);
}

void
V(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);
	spl = splhigh();
	sem->count++;
	assert(sem->count>0);
	thread_wakeup(sem);
	splx(spl);
}

////////////////////////////////////////////////////////////
//
// Lock.

/*
 * Allocate memory for one lock structure
 * Straight forward
 */
struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->name = kstrdup(name);
	if (lock->name == NULL) {
		kfree(lock);
		return NULL;
	}
	
	// add stuff here as needed
	lock->status = 0; // initalize to 0, meaning the lock is free
	lock->curthread_with_lock = NULL;
	lock->lock_counter = 0;

	return lock;
}

/*
 * Free lock structure
 * Straight forward
 */
void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	// add stuff here as needed
	
	kfree(lock->name);
	kfree(lock);
}

/*
 * Aquire lock, if the current thread already has the lock, increase the lock counter
 * We will make sure the lock counter is zero before releasing the lock
 */
void
lock_acquire(struct lock *lock)
{
	int spl;
	assert (lock != NULL);

	// disable interrupts
	spl = splhigh();

	// if the same threads attempts to accuire lock twice
	// we increment the lock counter
	if (curthread == lock->curthread_with_lock) {
		lock->lock_counter = lock->lock_counter + 1;
		splx(spl);
		return;
	}

	while (lock->status == 1) {
		thread_sleep(lock);
	}

	
	lock->curthread_with_lock = curthread;
	lock->lock_counter = lock->lock_counter + 1;
	lock->status = 1;

	splx(spl);
}

/*
 * Release lock
 * Check if the current thread holds the lock, can't let different thread releasing eachother's locks
 * We will make sure the lock counter is zero before releasing the lock
 */
void
lock_release(struct lock *lock)
{
	int spl;
	assert (lock != NULL);

	// disable interrupts
	spl = splhigh();
	if (curthread != lock->curthread_with_lock) {
		//can not unlock as this thread does not hold the lock
		//should panic
		splx(spl);
		return;
	}

	lock->lock_counter = lock->lock_counter - 1;

	if (lock->lock_counter == 0) {
		lock->curthread_with_lock = NULL;
		lock->status = 0;
		thread_wakeup (lock);
	}

	splx(spl);
}

int
lock_do_i_hold(struct lock *lock)
{
	// I don't think disabling interrupts is nessesary here,
	// But just in case
	int spl = splhigh();
	if (lock->curthread_with_lock == curthread) {
		splx(spl);
		return 1;
	} else {
		splx(spl);
		return 0;
	}
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->name = kstrdup(name);
	if (cv->name==NULL) {
		kfree(cv);
		return NULL;
	}
	
	// add stuff here as needed
	cv->q = q_create(2);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	// add stuff here as needed
	q_destroy(cv->q);
	kfree(cv->name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int result;
	assert (lock_do_i_hold(lock));
	int spl = splhigh();
	result = q_addtail(cv->q, curthread);
	assert (result == 0);
	lock_release(lock);
	thread_sleep(curthread);
	lock_acquire(lock);
	splx(spl);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int check_empty_q;
	assert (lock_do_i_hold(lock));
	int spl = splhigh();
	check_empty_q = q_empty (cv->q);
	if (check_empty_q == 0)
		thread_wakeup (q_remhead(cv->q));
	splx(spl);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int check_empty_q; 
	assert (cv != NULL);
	assert (lock_do_i_hold(lock));
	int spl = splhigh();
	check_empty_q = q_empty(cv->q);
	while (check_empty_q == 0) {
		thread_wakeup(q_remhead(cv->q));
		check_empty_q = q_empty(cv->q);
	}
	splx(spl);
}
