/**
 * @file kernel/sched/round_robin.c
 *
 * @brief round-robin scheduler
 */


#include <errno.h>
#include <kernel/init.h>
#include <kernel/tick.h>
#include <kernel/kthread.h>
#include <asm/spinlock.h>

#define MSG "SCHED_RR: "

#define MIN_RR_SLICE_NS		1000000


static struct spinlock rr_spinlock;


/**
 * @brief lock critical rr section
 */

static void rr_lock(void)
{
	spin_lock_raw(&rr_spinlock);
}


/**
 * @brief unlock critical rr section
 */

static void rr_unlock(void)
{
	spin_unlock(&rr_spinlock);
}


static struct task_struct *rr_pick_next(struct task_queue tq[], int cpu,
					ktime now)
{
	struct task_struct *next = NULL;
	struct task_struct *tmp;


	if (list_empty(&tq[0].run))
		return NULL;

	rr_lock();
	list_for_each_entry_safe(next, tmp, &tq[0].run, node) {


		if (next->on_cpu == KTHREAD_CPU_AFFINITY_NONE
		    || next->on_cpu == cpu) {

		if (next->state == TASK_RUN) {
			/* XXX: must pick head first, then move tail on put()
			 * following a scheduling event. for now, just force
			 * round robin
			 */
			list_move_tail(&next->node, &tq[0].run);

			/* reset runtime */
			next->runtime = (next->attr.priority * MIN_RR_SLICE_NS);



		}

		if (next->state == TASK_IDLE)
			list_move_tail(&next->node, &tq[0].run);

		if (next->state == TASK_DEAD)
			list_move_tail(&next->node, &tq[0].dead);

		break;


		} else {
			next = NULL;
			continue;
		}


	}

	rr_unlock();

	return next;
}


/* this sucks, wrong place. keep for now */
static void rr_wake_next(struct task_queue tq[], int cpu, ktime now)
{

	struct task_struct *task;

	if (list_empty(&tq[0].wake))
		return;


	task = list_entry(tq[0].wake.next, struct task_struct, node);

	BUG_ON(task->attr.policy != SCHED_RR);

	task->state = TASK_RUN;

	rr_lock();
	list_move(&task->node, &tq[0].run);
	rr_unlock();
}


static int rr_enqueue(struct task_queue tq[], struct task_struct *task)
{

	task->runtime = (task->attr.priority * MIN_RR_SLICE_NS);

	rr_lock();
	if (task->state == TASK_RUN)
		list_add_tail(&task->node, &tq[0].run);
	else
		list_add_tail(&task->node, &tq[0].wake);

	rr_unlock();

	return 0;
}

/**
 * @brief get the requested timeslice of a task
 *
 * @note RR timeslices are determined from their configured priority
 *	 XXX: must not be 0
 *
 * @note for now, just take the minimum tick period to fit as many RR slices
 *	 as possible. This will jack up the IRQ rate, but RR tasks only run when
 *	 the system is not otherwise busy;
 *	 still, a larger (configurable) extra factor may be desirable
 */

static ktime rr_timeslice_ns(struct task_struct *task)
{
	return (ktime) (task->attr.priority * MIN_RR_SLICE_NS);
}



/**
 * @brief sanity-check sched_attr configuration
 *
 * @return 0 on success -EINVAL on error
 */

static int rr_check_sched_attr(struct sched_attr *attr)
{
	if (attr->policy != SCHED_RR) {
		pr_err(MSG "attribute policy is %d, expected SCHED_RR (%d)\n",
			attr->policy, SCHED_RR);
		return -EINVAL;
	}

	if (!attr->priority) {
		pr_warn(MSG "minimum priority is 1, adjusted\n");
	}

	return 0;
}



/**
 * @brief return the time until the the next task is ready
 *
 * @note RR tasks are always "ready" and they do not have deadlines,
 *	 so this function always returns 0
 */

ktime rr_task_ready_ns(struct task_queue tq[], int cpu, ktime now)
{
	return (ktime) 0ULL;
}




static struct scheduler sched_rr = {
	.policy           = SCHED_RR,
	.pick_next_task   = rr_pick_next,
	.wake_next_task   = rr_wake_next,
	.enqueue_task     = rr_enqueue,
	.timeslice_ns     = rr_timeslice_ns,
	.task_ready_ns    = rr_task_ready_ns,
	.check_sched_attr = rr_check_sched_attr,
	.sched_priority   = 0,
};



static int sched_rr_init(void)
{
	int i;

	/* XXX */

	for (i = 0; i < CONFIG_SMP_CPUS_MAX; i++) {
		INIT_LIST_HEAD(&sched_rr.tq[i].new);
		INIT_LIST_HEAD(&sched_rr.tq[i].wake);
		INIT_LIST_HEAD(&sched_rr.tq[i].run);
		INIT_LIST_HEAD(&sched_rr.tq[i].dead);
	}


	sched_register(&sched_rr);

	return 0;
}
postcore_initcall(sched_rr_init);
