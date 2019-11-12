/**
 * @file kernel/kthread.c
 */


#include <kernel/kthread.h>
#include <kernel/export.h>
#include <kernel/smp.h>
#include <kernel/kmem.h>
#include <kernel/err.h>
#include <kernel/printk.h>

#include <asm-generic/irqflags.h>

#include <asm/io.h>
#include <asm/switch_to.h>
#include <asm/spinlock.h>

#include <kernel/string.h>

#include <kernel/tick.h>

#include <generated/autoconf.h> /* XXX need common CPU include */



#define MSG "KTHREAD: "


struct remove_this_declaration /*{
	struct list_head new;
	struct list_head run;
	struct list_head wake;
	struct list_head dead;
}*/ _kthreads = {
	.new  = LIST_HEAD_INIT(_kthreads.new),
	.run  = LIST_HEAD_INIT(_kthreads.run),
	.wake = LIST_HEAD_INIT(_kthreads.wake),
	.dead = LIST_HEAD_INIT(_kthreads.dead)
};

static struct spinlock kthread_spinlock;



struct thread_info *current_set[CONFIG_SMP_CPUS_MAX]; /* XXX */


/**
 * @brief lock critical kthread section
 */

 void kthread_lock(void)
{
	spin_lock_raw(&kthread_spinlock);
}


/**
 * @brief unlock critical kthread section
 */

void kthread_unlock(void)
{
	spin_unlock(&kthread_spinlock);
}


/* this should be a thread with a semaphore
 * that is unlocked by schedule() if dead tasks
 * were added
 * (need to irq_disable/kthread_lock)
 */

void kthread_cleanup_dead(void)
{
	struct task_struct *p_elem;
	struct task_struct *p_tmp;

	list_for_each_entry_safe(p_elem, p_tmp, &_kthreads.dead, node) {
		list_del(&p_elem->node);
		kfree(p_elem->stack);
		kfree(p_elem->name);
		kfree(p_elem);
	}
}



void sched_yield(void)
{
	struct task_struct *tsk;

	tsk = current_set[smp_cpu_id()]->task;
//	if (tsk->attr.policy == SCHED_EDF)
	tsk->runtime = 0;

	schedule();
}


__attribute__((unused))
/* static */ void kthread_set_sched_policy(struct task_struct *task,
				     enum sched_policy policy)
{
	arch_local_irq_disable();
	kthread_lock();
	task->attr.policy = policy;
	kthread_unlock();
	arch_local_irq_enable();
}



int threadcnt;
int kthread_wake_up(struct task_struct *task)
{
	int ret = 0;

	ktime now;


	threadcnt++;

	if (task->state != TASK_NEW)
		return -EINVAL;

	ret = sched_enqueue(task);
	if(ret)
		return ret;
#if 1
	kthread_lock();
	arch_local_irq_disable();
	now = ktime_get();
#if 1
	/* XXX need function in sched.c to do that */
	task->sched->wake_next_task(task->sched->tq, task->on_cpu, now);

	if (task->on_cpu != KTHREAD_CPU_AFFINITY_NONE)
		smp_send_reschedule(task->on_cpu);
#endif

	arch_local_irq_enable();
	kthread_unlock();
#endif

	return 0;
}


struct task_struct *kthread_init_main(void)
{
	int cpu;

	struct task_struct *task;


	cpu = smp_cpu_id();

	task = kmalloc(sizeof(*task));


	if (!task)
		return ERR_PTR(-ENOMEM);

	/* XXX accessors */
	task->attr.policy = SCHED_RR; /* default */
	task->attr.priority = 1;
	task->on_cpu = cpu;

	arch_promote_to_task(task);

	task->name = "KERNEL";
	BUG_ON(sched_set_policy_default(task));

	arch_local_irq_disable();
	kthread_lock();

	current_set[cpu] = &task->thread_info;


	task->state = TASK_RUN;

	sched_enqueue(task);
	/*list_add_tail(&task->node, &_kthreads.run);*/

	smp_send_reschedule(cpu);


	kthread_unlock();
	arch_local_irq_enable();

	return task;
}




static struct task_struct *kthread_create_internal(int (*thread_fn)(void *data),
						   void *data, int cpu,
						   const char *namefmt,
						   va_list args)
{
	struct task_struct *task;

	task = kzalloc(sizeof(*task));


	if (!task)
		return ERR_PTR(-ENOMEM);


	/* XXX: need stack size detection and realloc/migration code */

	task->stack = kzalloc(8192 + STACK_ALIGN); /* XXX */

	BUG_ON((int) task->stack > (0x40800000 - 4096 + 1));

	if (!task->stack) {
		kfree(task);
		return ERR_PTR(-ENOMEM);
	}


	task->stack_bottom = task->stack; /* XXX */
	task->stack_top = ALIGN_PTR(task->stack, STACK_ALIGN) +8192/4; /* XXX */
	BUG_ON(task->stack_top > (task->stack + (8192/4 + STACK_ALIGN/4)));

#if 0
	/* XXX: need wmemset() */
	memset(task->stack, 0xab, 8192 + STACK_ALIGN);
#else
#if 0
	{
		int i;
		for (i = 0; i < (8192 + STACK_ALIGN) / 4; i++)
			((int *) task->stack)[i] = 0xdeadbeef;

	}
#endif
#endif

	/* dummy */
	task->name = kmalloc(32);
	BUG_ON(!task->name);
	vsnprintf(task->name, 32, namefmt, args);

	if (sched_set_policy_default(task)) {
		pr_crit("KTHREAD: must destroy task at this point\n");
		BUG();
	}

	task->total = 0;
	task->slices = 0;
	task->on_cpu = cpu;
	arch_init_task(task, thread_fn, data);

	task->state = TASK_NEW;

	arch_local_irq_disable();
	kthread_lock();

	/** XXX **/ /*sched_enqueue(task); */
	//list_add_tail(&task->node, &_kthreads.new);

	kthread_unlock();
	arch_local_irq_enable();

	return task;
}




/**
 * @brief create a new kernel thread
 *
 * @param thread_fn the function to run in the thread
 * @param data a user data pointer for thread_fn, may be NULL
 *
 * @param cpu set the cpu affinity
 *
 * @param name_fmt a printf format string name for the thread
 *
 * @param ... parameters to the format string
 */

struct task_struct *kthread_create(int (*thread_fn)(void *data),
				   void *data, int cpu,
				   const char *namefmt,
				   ...)
{
	struct task_struct *task;
	va_list args;

	va_start(args, namefmt);
	task = kthread_create_internal(thread_fn, data, cpu, namefmt, args);
	va_end(args);

	return task;
}
EXPORT_SYMBOL(kthread_create);



