/*
 * kernel/power/suspend.c - Suspend to RAM and standby functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#define pr_fmt(fmt) "PM: " fmt

#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/syscalls.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/ftrace.h>
#include <trace/events/power.h>
#include <linux/compiler.h>
#include <linux/moduleparam.h>
#include <linux/wakeup_reason.h>
#include "power.h"
#include <soc/qcom/boot_stats.h>
#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Cong.Dai@psw.bsp.tp 2018/08/30 modified for stop system enter sleep before low irq handled
__attribute__((weak)) int check_touchirq_triggered(void) {return 0;}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Bo.Xiang@BSP.Sensor, 2019-08-21, add for ignore non_wakeup sensor notify event wihle ap is going to sleep
#include <linux/soc/qcom/smem_state.h>
#include <linux/gpio.h>
extern struct qcom_smem_state *qstate;
#define PROC_AWAKE_ID 12 /* 12th bit */ 
#define AWAKE_BIT BIT(PROC_AWAKE_ID)
#endif //CONFIG_PRODUCT_REALME_TRINKET

const char * const pm_labels[] = {
	[PM_SUSPEND_TO_IDLE] = "freeze",
	[PM_SUSPEND_STANDBY] = "standby",
	[PM_SUSPEND_MEM] = "mem",
};
const char *pm_states[PM_SUSPEND_MAX];
static const char * const mem_sleep_labels[] = {
	[PM_SUSPEND_TO_IDLE] = "s2idle",
	[PM_SUSPEND_STANDBY] = "shallow",
	[PM_SUSPEND_MEM] = "deep",
};
const char *mem_sleep_states[PM_SUSPEND_MAX];

suspend_state_t mem_sleep_current = PM_SUSPEND_TO_IDLE;
suspend_state_t mem_sleep_default = PM_SUSPEND_MAX;
suspend_state_t pm_suspend_target_state;
EXPORT_SYMBOL_GPL(pm_suspend_target_state);

unsigned int pm_suspend_global_flags;
EXPORT_SYMBOL_GPL(pm_suspend_global_flags);

static const struct platform_suspend_ops *suspend_ops;
static const struct platform_s2idle_ops *s2idle_ops;
static DECLARE_WAIT_QUEUE_HEAD(s2idle_wait_head);

enum s2idle_states __read_mostly s2idle_state;
static DEFINE_RAW_SPINLOCK(s2idle_lock);

void s2idle_set_ops(const struct platform_s2idle_ops *ops)
{
	lock_system_sleep();
	s2idle_ops = ops;
	unlock_system_sleep();
}

extern void thaw_fingerprintd(void);

static void s2idle_begin(void)
{
	s2idle_state = S2IDLE_STATE_NONE;
}

static void s2idle_enter(void)
{
	trace_suspend_resume(TPS("machine_suspend"), PM_SUSPEND_TO_IDLE, true);

	raw_spin_lock_irq(&s2idle_lock);
	if (pm_wakeup_pending())
		goto out;

	s2idle_state = S2IDLE_STATE_ENTER;
	raw_spin_unlock_irq(&s2idle_lock);

	get_online_cpus();
	cpuidle_resume();

	/* Push all the CPUs into the idle loop. */
	wake_up_all_idle_cpus();
	/* Make the current CPU wait so it can enter the idle loop too. */
	wait_event(s2idle_wait_head,
		   s2idle_state == S2IDLE_STATE_WAKE);

	cpuidle_pause();
	put_online_cpus();

	raw_spin_lock_irq(&s2idle_lock);

 out:
	s2idle_state = S2IDLE_STATE_NONE;
	raw_spin_unlock_irq(&s2idle_lock);

	trace_suspend_resume(TPS("machine_suspend"), PM_SUSPEND_TO_IDLE, false);
}

static void s2idle_loop(void)
{
	pm_pr_dbg("suspend-to-idle\n");

	for (;;) {
		int error;
		bool leave_s2idle = false;

		dpm_noirq_begin();

		/*
		 * Suspend-to-idle equals
		 * frozen processes + suspended devices + idle processors.
		 * Thus s2idle_enter() should be called right after
		 * all devices have been suspended.
		 *
		 * Wakeups during the noirq suspend of devices may be spurious,
		 * so prevent them from terminating the loop right away.
		 */
		error = dpm_noirq_suspend_devices(PMSG_SUSPEND);
		if (!error) {
			s2idle_enter();
			/*
			 * Once we enter s2idle_enter(), returning means that
			 * either:
			 * 1) an abort was detected prior to suspending, or
			 * 2) something caused us to wake from suspended
			 * If we got an abort or a wakeup interrupt, we need
			 * to break out of this loop.  If we were woken by
			 * an interrupt that technically doesn't require a
			 * full wakeup (only a few corner cases), we're going
			 * to wake up anyway, because the way this new
			 * s2idle_loop() flow works, the resume of devices
			 * below will cause an abort even if we could
			 * otherwise have looped back into suspend.
			 */
			leave_s2idle = true;
		} else if (error == -EBUSY && pm_wakeup_pending()) {
			leave_s2idle = true;
			error = 0;
		}

		if (!error && s2idle_ops && s2idle_ops->wake)
			s2idle_ops->wake();

		dpm_noirq_resume_devices(PMSG_RESUME);

		dpm_noirq_end();

		if (error)
			break;

		if (s2idle_ops && s2idle_ops->sync)
			s2idle_ops->sync();

		if (leave_s2idle || pm_wakeup_pending())
			break;

		/*
		 * Since we are going to loop around and attempt to go back
		 * into suspend, ensure that all wakeup reason logging from
		 * this partial resume gets cleared first (which will also
		 * reenable wakeup reason logging).
		 */
		pm_wakeup_clear(false);
	}

	pm_pr_dbg("resume from suspend-to-idle\n");
}

void s2idle_wake(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&s2idle_lock, flags);
	if (s2idle_state > S2IDLE_STATE_NONE) {
		s2idle_state = S2IDLE_STATE_WAKE;
		wake_up(&s2idle_wait_head);
	}
	raw_spin_unlock_irqrestore(&s2idle_lock, flags);
}
EXPORT_SYMBOL_GPL(s2idle_wake);

static bool valid_state(suspend_state_t state)
{
	/*
	 * PM_SUSPEND_STANDBY and PM_SUSPEND_MEM states need low level
	 * support and need to be valid to the low level
	 * implementation, no valid callback implies that none are valid.
	 */
	return suspend_ops && suspend_ops->valid && suspend_ops->valid(state);
}

void __init pm_states_init(void)
{
	/* "mem" and "freeze" are always present in /sys/power/state. */
	pm_states[PM_SUSPEND_MEM] = pm_labels[PM_SUSPEND_MEM];
	pm_states[PM_SUSPEND_TO_IDLE] = pm_labels[PM_SUSPEND_TO_IDLE];
	/*
	 * Suspend-to-idle should be supported even without any suspend_ops,
	 * initialize mem_sleep_states[] accordingly here.
	 */
	mem_sleep_states[PM_SUSPEND_TO_IDLE] = mem_sleep_labels[PM_SUSPEND_TO_IDLE];
}

static int __init mem_sleep_default_setup(char *str)
{
	suspend_state_t state;

	for (state = PM_SUSPEND_TO_IDLE; state <= PM_SUSPEND_MEM; state++)
		if (mem_sleep_labels[state] &&
		    !strcmp(str, mem_sleep_labels[state])) {
			mem_sleep_default = state;
			break;
		}

	return 1;
}
__setup("mem_sleep_default=", mem_sleep_default_setup);

/**
 * suspend_set_ops - Set the global suspend method table.
 * @ops: Suspend operations to use.
 */
void suspend_set_ops(const struct platform_suspend_ops *ops)
{
	lock_system_sleep();

	suspend_ops = ops;

	if (valid_state(PM_SUSPEND_STANDBY)) {
		mem_sleep_states[PM_SUSPEND_STANDBY] = mem_sleep_labels[PM_SUSPEND_STANDBY];
		pm_states[PM_SUSPEND_STANDBY] = pm_labels[PM_SUSPEND_STANDBY];
		if (mem_sleep_default == PM_SUSPEND_STANDBY)
			mem_sleep_current = PM_SUSPEND_STANDBY;
	}
	if (valid_state(PM_SUSPEND_MEM)) {
		mem_sleep_states[PM_SUSPEND_MEM] = mem_sleep_labels[PM_SUSPEND_MEM];
		if (mem_sleep_default >= PM_SUSPEND_MEM)
			mem_sleep_current = PM_SUSPEND_MEM;
	}

	unlock_system_sleep();
}
EXPORT_SYMBOL_GPL(suspend_set_ops);

/**
 * suspend_valid_only_mem - Generic memory-only valid callback.
 *
 * Platform drivers that implement mem suspend only and only need to check for
 * that in their .valid() callback can use this instead of rolling their own
 * .valid() callback.
 */
int suspend_valid_only_mem(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}
EXPORT_SYMBOL_GPL(suspend_valid_only_mem);

static bool sleep_state_supported(suspend_state_t state)
{
	return state == PM_SUSPEND_TO_IDLE || (suspend_ops && suspend_ops->enter);
}

static int platform_suspend_prepare(suspend_state_t state)
{
	return state != PM_SUSPEND_TO_IDLE && suspend_ops->prepare ?
		suspend_ops->prepare() : 0;
}

static int platform_suspend_prepare_late(suspend_state_t state)
{
	return state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->prepare ?
		s2idle_ops->prepare() : 0;
}

static int platform_suspend_prepare_noirq(suspend_state_t state)
{
	return state != PM_SUSPEND_TO_IDLE && suspend_ops->prepare_late ?
		suspend_ops->prepare_late() : 0;
}

static void platform_resume_noirq(suspend_state_t state)
{
	if (state != PM_SUSPEND_TO_IDLE && suspend_ops->wake)
		suspend_ops->wake();
}

static void platform_resume_early(suspend_state_t state)
{
	if (state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->restore)
		s2idle_ops->restore();
}

static void platform_resume_finish(suspend_state_t state)
{
	if (state != PM_SUSPEND_TO_IDLE && suspend_ops->finish)
		suspend_ops->finish();
}

static int platform_suspend_begin(suspend_state_t state)
{
	if (state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->begin)
		return s2idle_ops->begin();
	else if (suspend_ops && suspend_ops->begin)
		return suspend_ops->begin(state);
	else
		return 0;
}

static void platform_resume_end(suspend_state_t state)
{
	if (state == PM_SUSPEND_TO_IDLE && s2idle_ops && s2idle_ops->end)
		s2idle_ops->end();
	else if (suspend_ops && suspend_ops->end)
		suspend_ops->end();
}

static void platform_recover(suspend_state_t state)
{
	if (state != PM_SUSPEND_TO_IDLE && suspend_ops->recover)
		suspend_ops->recover();
}

static bool platform_suspend_again(suspend_state_t state)
{
	return state != PM_SUSPEND_TO_IDLE && suspend_ops->suspend_again ?
		suspend_ops->suspend_again() : false;
}

#ifdef CONFIG_PM_DEBUG
static unsigned int pm_test_delay = 5;
module_param(pm_test_delay, uint, 0644);
MODULE_PARM_DESC(pm_test_delay,
		 "Number of seconds to wait before resuming from suspend test");
#endif

static int suspend_test(int level)
{
#ifdef CONFIG_PM_DEBUG
#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	pr_info("%s pm_test_level:%d, level:%d\n", __func__,
		pm_test_level, level);
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
	if (pm_test_level == level) {
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
		pr_info("suspend debug: Waiting for %d second(s).\n",
				pm_test_delay);
#else
		pr_err("suspend debug: Waiting for %d second(s).\n",
				pm_test_delay);
#endif
		mdelay(pm_test_delay * 1000);
		return 1;
	}
#endif /* !CONFIG_PM_DEBUG */
	return 0;
}

/**
 * suspend_prepare - Prepare for entering system sleep state.
 *
 * Common code run for every system sleep state that can be entered (except for
 * hibernation).  Run suspend notifiers, allocate the "suspend" console and
 * freeze processes.
 */
static int suspend_prepare(suspend_state_t state)
{
	int error, nr_calls = 0;

	if (!sleep_state_supported(state))
		return -EPERM;

	pm_prepare_console();

	error = __pm_notifier_call_chain(PM_SUSPEND_PREPARE, -1, &nr_calls);
	if (error) {
		nr_calls--;
		goto Finish;
	}

	trace_suspend_resume(TPS("freeze_processes"), 0, true);
	error = suspend_freeze_processes();
	trace_suspend_resume(TPS("freeze_processes"), 0, false);
	if (!error)
		return 0;

	suspend_stats.failed_freeze++;
	dpm_save_failed_step(SUSPEND_FREEZE);
 Finish:
	__pm_notifier_call_chain(PM_POST_SUSPEND, nr_calls, NULL);
	pm_restore_console();
	return error;
}

/* default implementation */
void __weak arch_suspend_disable_irqs(void)
{
	local_irq_disable();
}

/* default implementation */
void __weak arch_suspend_enable_irqs(void)
{
	local_irq_enable();
}

/**
 * suspend_enter - Make the system enter the given sleep state.
 * @state: System sleep state to enter.
 * @wakeup: Returns information that the sleep state should not be re-entered.
 *
 * This function should be called after devices have been suspended.
 */
static int suspend_enter(suspend_state_t state, bool *wakeup)
{
	char suspend_abort[MAX_SUSPEND_ABORT_LEN];
	int error, last_dev;

	error = platform_suspend_prepare(state);
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (error)
		goto Platform_finish;
#else
	if (error) {
		pr_info("%s platform_suspend_prepare fail\n", __func__);
		goto Platform_finish;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

	error = dpm_suspend_late(PMSG_SUSPEND);
	if (error) {
		last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
		last_dev %= REC_FAILED_NUM;
		pr_err("late suspend of devices failed\n");
		log_suspend_abort_reason("%s device failed to power down",
			suspend_stats.failed_devs[last_dev]);
		goto Platform_finish;
	}
	error = platform_suspend_prepare_late(state);
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (error)
		goto Devices_early_resume;
#else
	if (error) {
		pr_info("%s prepare late fail\n", __func__);
		goto Devices_early_resume;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

	if (state == PM_SUSPEND_TO_IDLE && pm_test_level != TEST_PLATFORM) {
		s2idle_loop();
		goto Platform_early_resume;
	}

	error = dpm_suspend_noirq(PMSG_SUSPEND);
	if (error) {
		last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
		last_dev %= REC_FAILED_NUM;
		pr_err("noirq suspend of devices failed\n");
		log_suspend_abort_reason("noirq suspend of %s device failed",
			suspend_stats.failed_devs[last_dev]);
		goto Platform_early_resume;
	}
	error = platform_suspend_prepare_noirq(state);
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (error)
		goto Platform_wake;
#else
	if (error) {
		pr_info("%s prepare_noirq fail\n", __func__);
		goto Platform_wake;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;
#else
	if (suspend_test(TEST_PLATFORM)) {
		pr_info("%s test_platform fail\n", __func__);
		goto Platform_wake;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

	error = disable_nonboot_cpus();
	if (error || suspend_test(TEST_CPUS)) {
		goto Enable_cpus;
	}

	arch_suspend_disable_irqs();
	BUG_ON(!irqs_disabled());

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Cong.Dai@psw.bsp.tp 2018/08/30 modified for stop system enter sleep before low irq handled
	if (check_touchirq_triggered()) {
		error = -EBUSY;
		goto Enable_irqs;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	pr_info("%s syscore_suspend\n", __func__);
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

	error = syscore_suspend();
	if (!error) {
		*wakeup = pm_wakeup_pending();
		if (!(suspend_test(TEST_CORE) || *wakeup)) {
			trace_suspend_resume(TPS("machine_suspend"),
				state, true);
			error = suspend_ops->enter(state);
			trace_suspend_resume(TPS("machine_suspend"),
				state, false);
		} else if (*wakeup) {
			pm_get_active_wakeup_sources(suspend_abort,
				MAX_SUSPEND_ABORT_LEN);
			log_suspend_abort_reason(suspend_abort);
			error = -EBUSY;
		}
		syscore_resume();
	}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Cong.Dai@psw.bsp.tp 2018/08/30 modified for stop system enter sleep before low irq handled
 Enable_irqs:
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
	arch_suspend_enable_irqs();
	BUG_ON(irqs_disabled());

 Enable_cpus:
	enable_nonboot_cpus();

 Platform_wake:
	thaw_fingerprintd();

	platform_resume_noirq(state);
	dpm_resume_noirq(PMSG_RESUME);

 Platform_early_resume:
	platform_resume_early(state);

 Devices_early_resume:
	dpm_resume_early(PMSG_RESUME);

 Platform_finish:
	platform_resume_finish(state);
	return error;
}

/**
 * suspend_devices_and_enter - Suspend devices and enter system sleep state.
 * @state: System sleep state to enter.
 */
int suspend_devices_and_enter(suspend_state_t state)
{
	int error;
	bool wakeup = false;

#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (!sleep_state_supported(state))
		return -ENOSYS;
#else
	if (!sleep_state_supported(state)) {
		pr_info("sleep_state_supported false\n");
		return -ENOSYS;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

	pm_suspend_target_state = state;

	error = platform_suspend_begin(state);
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (error)
		goto Close;
#else
	if (error) {
		pr_info("%s platform_suspend_begin fail\n", __func__);
		goto Close;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
	suspend_console();
	suspend_test_start();
	error = dpm_suspend_start(PMSG_SUSPEND);
	if (error) {
		pr_err("Some devices failed to suspend, or early wake event detected\n");
		log_suspend_abort_reason("Some devices failed to suspend, or early wake event detected");
		goto Recover_platform;
	}
	suspend_test_finish("suspend devices");
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (suspend_test(TEST_DEVICES))
		goto Recover_platform;
#else
	if (suspend_test(TEST_DEVICES)) {
		pr_info("%s TEST_DEVICES fail\n", __func__);
		goto Recover_platform;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
	do {
		error = suspend_enter(state, &wakeup);
	} while (!error && !wakeup && platform_suspend_again(state));

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	pr_info("suspend_enter end, error:%d, wakeup:%d\n", error, wakeup);
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
 Resume_devices:
	suspend_test_start();
	dpm_resume_end(PMSG_RESUME);
	suspend_test_finish("resume devices");
	trace_suspend_resume(TPS("resume_console"), state, true);
	resume_console();
	trace_suspend_resume(TPS("resume_console"), state, false);

 Close:
	platform_resume_end(state);
	pm_suspend_target_state = PM_SUSPEND_ON;
	return error;

 Recover_platform:
	platform_recover(state);
	goto Resume_devices;
}

/**
 * suspend_finish - Clean up before finishing the suspend sequence.
 *
 * Call platform code to clean up, restart processes, and free the console that
 * we've allocated. This routine is not called for hibernation.
 */
static void suspend_finish(void)
{
	suspend_thaw_processes();
	pm_notifier_call_chain(PM_POST_SUSPEND);
	pm_restore_console();
}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/*xing.xiong@BSP.Kernel.Statbily, 2018/03/24, Add for speed up suspend workaround*/
/**
* Sync the filesystem in seperate workqueue.
* Then check it finishing or not periodically and
* abort if any wakeup source comes in. That can reduce
* the wakeup latency
*
*/
static bool sys_sync_completed = false;
static void sys_sync_work_func(struct work_struct *work);
static DECLARE_WORK(sys_sync_work, sys_sync_work_func);
static DECLARE_WAIT_QUEUE_HEAD(sys_sync_wait);
static void sys_sync_work_func(struct work_struct *work)
{
    trace_suspend_resume(TPS("sync_filesystems"), 0, true);
    pr_info(KERN_INFO "PM: Syncing filesystems ... ");
    sys_sync();
    pr_cont("done.\n");
    trace_suspend_resume(TPS("sync_filesystems"), 0, false);
    sys_sync_completed = true;
    wake_up(&sys_sync_wait);
}

static int sys_sync_queue(void)
{
    int work_status = work_busy(&sys_sync_work);

    /*maybe some irq coming here before pending check*/
    pm_wakeup_clear(true);

    /*Check if the previous work still running.*/
    if (!(work_status & WORK_BUSY_PENDING)) {
        if (work_status & WORK_BUSY_RUNNING) {
            while (wait_event_timeout(sys_sync_wait, sys_sync_completed,
                        msecs_to_jiffies(100)) == 0) {
                if (pm_wakeup_pending()) {
                    pr_info("PM: Pre-Syncing abort\n");
                    goto abort;
                }
            }
            pr_info("PM: Pre-Syncing done\n");
        }
        sys_sync_completed = false;
        schedule_work(&sys_sync_work);
    }

    while (wait_event_timeout(sys_sync_wait, sys_sync_completed,
                    msecs_to_jiffies(100)) == 0) {
        if (pm_wakeup_pending()) {
            pr_info("PM: Syncing abort\n");
            goto abort;
        }
    }

    pr_info("PM: Syncing done\n");
    return 0;
abort:
    return -EAGAIN;
}
#endif /*CONFIG_PRODUCT_REALME_TRINKET*/

/**
 * enter_state - Do common work needed to enter system sleep state.
 * @state: System sleep state to enter.
 *
 * Make sure that no one else is trying to put the system into a sleep state.
 * Fail if that's not the case.  Otherwise, prepare for system suspend, make the
 * system enter the given sleep state and clean up after wakeup.
 */
static int enter_state(suspend_state_t state)
{
	int error;

	trace_suspend_resume(TPS("suspend_enter"), state, true);
	if (state == PM_SUSPEND_TO_IDLE) {
#ifdef CONFIG_PM_DEBUG
		if (pm_test_level != TEST_NONE && pm_test_level <= TEST_CPUS) {
			pr_warn("Unsupported test mode for suspend to idle, please choose none/freezer/devices/platform.\n");
			return -EAGAIN;
		}
#endif
	} else if (!valid_state(state)) {
#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
		pr_info("%s invalid_state\n", __func__);
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
		return -EINVAL;
	}
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (!mutex_trylock(&pm_mutex))
		return -EBUSY;
#else
	if (!mutex_trylock(&pm_mutex)) {
		pr_info("%s mutex_trylock fail\n", __func__);
		return -EBUSY;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

	pm_wakeup_clear(true);
	if (state == PM_SUSPEND_TO_IDLE)
		s2idle_begin();

#ifndef CONFIG_SUSPEND_SKIP_SYNC
#ifndef CONFIG_PRODUCT_REALME_TRINKET
/*xing.xiong@BSP.Kernel.Statbily, 2018/03/24, Add for speed up suspend workaround*/
	trace_suspend_resume(TPS("sync_filesystems"), 0, true);
	pr_info("Syncing filesystems ... ");
	sys_sync();
	pr_cont("done.\n");
	trace_suspend_resume(TPS("sync_filesystems"), 0, false);
#else
    error = sys_sync_queue();
    if (error) {
        pr_err("%s sys_sync_queue fail\n", __func__);
        goto Unlock;
    }
#endif
#endif

	pm_pr_dbg("Preparing system for sleep (%s)\n", mem_sleep_labels[state]);
	pm_suspend_clear_flags();
	error = suspend_prepare(state);
#ifndef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	if (error)
		goto Unlock;
#else
	if (error) {
		pr_info("%s suspend_prepare error:%d\n", __func__, error);
		goto Unlock;
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	pr_info("%s suspend_prepare success\n", __func__);
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
	if (suspend_test(TEST_FREEZER))
		goto Finish;

	trace_suspend_resume(TPS("suspend_enter"), state, false);
	pm_pr_dbg("Suspending system (%s)\n", mem_sleep_labels[state]);
	pm_restrict_gfp_mask();
	error = suspend_devices_and_enter(state);
	pm_restore_gfp_mask();
#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Nanwei.Deng@BSP.CHG.Basic 2018/05/03 modify for power debug
	pr_info("%s suspend_devices_and_enter end\n", __func__);
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

 Finish:
	events_check_enabled = false;
	pm_pr_dbg("Finishing wakeup.\n");
	suspend_finish();
 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}

/**
 * pm_suspend - Externally visible function for suspending the system.
 * @state: System sleep state to enter.
 *
 * Check if the value of @state represents one of the supported states,
 * execute enter_state() and update system suspend statistics.
 */
int pm_suspend(suspend_state_t state)
{
	int error;

	if (state <= PM_SUSPEND_ON || state >= PM_SUSPEND_MAX)
		return -EINVAL;

#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Bo.Xiang@BSP.Sensor, 2019-08-21, add for ignore non_wakeup sensor notify event wihle ap is going to sleep
    qcom_smem_state_update_bits(qstate, AWAKE_BIT, 0);
    pr_err("%s: PM_SUSPEND_PREPARE smp2p_change_state", __func__);
#endif //CONFIG_PRODUCT_REALME_TRINKET
	pr_info("suspend entry (%s)\n", mem_sleep_labels[state]);
	error = enter_state(state);
#ifdef CONFIG_PRODUCT_REALME_TRINKET
//Bo.Xiang@BSP.Sensor, 2019-08-21, add for ignore non_wakeup sensor notify event wihle ap is going to sleep
    qcom_smem_state_update_bits(qstate, AWAKE_BIT, AWAKE_BIT);
    pr_err("%s: PM_POST_SUSPEND smp2p_change_state", __func__);
#endif //CONFIG_PRODUCT_REALME_TRINKET
	if (error) {
		suspend_stats.fail++;
		dpm_save_failed_errno(error);
	} else {
		suspend_stats.success++;
	}
	pr_info("suspend exit\n");
	measure_wake_up_time();
	return error;
}
EXPORT_SYMBOL(pm_suspend);
