/*
 * arch/arm/kernel/autosmp.c
 *
 * automatically hotplug/unplug multiple cpu cores
 * based on cpu load and suspend state
 *
 * based on the msm_mpdecision code by
 * Copyright (c) 2012-2013, Dennis Rassmann <showp1984@gmail.com>
 *
 * Copyright (C) 2013-2014, Rauf Gungor, http://github.com/mrg666
 * rewrite to simplify and optimize, Jul. 2013, http://goo.gl/cdGw6x
 * optimize more, generalize for n cores, Sep. 2013, http://goo.gl/448qBz
 * generalize for all arch, rename as autosmp, Dec. 2013, http://goo.gl/x5oyhy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. For more details, see the GNU
 * General Public License included with the Linux kernel or available
 * at www.gnu.org/licenses
 */

#include <linux/moduleparam.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/hrtimer.h>
#include <linux/lcd_notify.h>

#define ASMP_TAG "AutoSMP: "
#define ASMP_STARTDELAY 20000

static struct delayed_work asmp_work;
static struct workqueue_struct *asmp_workq;
static struct work_struct suspend_work;
static struct work_struct resume_work;
static struct notifier_block notif;

static struct asmp_param_struct {
	unsigned int delay;
	bool scroff_single_core;
	unsigned int max_cpus;
	unsigned int min_cpus;
	unsigned int cpufreq_up;
	unsigned int cpufreq_down;
	unsigned int cycle_up;
	unsigned int cycle_down;
	bool booted;
} asmp_param = {
	.delay = 100,
	.scroff_single_core = true,
	.max_cpus = 2,
	.min_cpus = 1,
	.cpufreq_up = 60,
	.cpufreq_down = 30,
	.cycle_up = 1,
	.cycle_down = 1,
	.booted = false,
};

static unsigned int cycle = 0, delay0 = 0;
static unsigned long delay_jif = 0;
static int enabled __read_mostly = 1;
static bool asmp_suspended = false;

static void __cpuinit asmp_work_fn(struct work_struct *work)
{
	unsigned int cpu = 0, slow_cpu = 0;
	unsigned int rate, cpu0_rate, slow_rate = UINT_MAX, fast_rate;
	unsigned int max_rate, up_rate, down_rate;
	int nr_cpu_online;

	if ((enabled == 0) || asmp_suspended)
		return;

	cycle++;

	if (asmp_param.delay != delay0) {
		delay0 = asmp_param.delay;
		delay_jif = msecs_to_jiffies(delay0);
	}

	/* get maximum possible freq for cpu0 and
	   calculate up/down limits */
	max_rate  = cpufreq_quick_get_max(cpu);
	up_rate   = asmp_param.cpufreq_up*max_rate/100;
	down_rate = asmp_param.cpufreq_down*max_rate/100;

	/* find current max and min cpu freq to estimate load */
	get_online_cpus();
	nr_cpu_online = num_online_cpus();
	cpu0_rate = cpufreq_quick_get(cpu);
	fast_rate = cpu0_rate;
	for_each_online_cpu(cpu)
		if (cpu) {
			rate = cpufreq_quick_get(cpu);
			if (rate <= slow_rate) {
				slow_cpu = cpu;
				slow_rate = rate;
			} else if (rate > fast_rate)
				fast_rate = rate;
		}
	put_online_cpus();
	if (cpu0_rate < slow_rate)
		slow_rate = cpu0_rate;

	/* hotplug one core if all online cores are over up_rate limit */
	if (slow_rate > up_rate) {
		if ((nr_cpu_online < asmp_param.max_cpus) &&
		    (cycle >= asmp_param.cycle_up)) {
			cpu = cpumask_next_zero(0, cpu_online_mask);
			cpu_up(cpu);
			cycle = 0;
		}
	/* unplug slowest core if all online cores are under down_rate limit */
	} else if (slow_cpu && (fast_rate < down_rate)) {
		if ((nr_cpu_online > asmp_param.min_cpus) &&
		    (cycle >= asmp_param.cycle_down)) {
 			cpu_down(slow_cpu);
			cycle = 0;
		}
	} /* else do nothing */

	queue_delayed_work(asmp_workq, &asmp_work, delay_jif);
}

static void asmp_power_suspend(struct work_struct *work)
{
	unsigned int cpu;

	if (enabled == 0)
		return;

	/* suspend main work thread */
	asmp_suspended = true;
	cancel_delayed_work_sync(&asmp_work);

	/* unplug online cpu cores */
	if (asmp_param.scroff_single_core) {
		for_each_online_cpu(cpu) {
			if (cpu != 0)
				cpu_down(cpu);
		}
	}

	pr_info(ASMP_TAG"suspended : total cpu %d\n", num_online_cpus());
}

static void __ref asmp_power_resume(struct work_struct *work)
{
	unsigned int cpu;

	if (enabled == 0)
		return;

	asmp_suspended = false;

	/* hotplug offline cpu cores */
	if (asmp_param.scroff_single_core) {
		for_each_possible_cpu(cpu) {
			if (cpu == 0)
				continue;
			cpu_up(cpu);
		}
	}

	/* resume main work thread */
	INIT_DELAYED_WORK(&asmp_work, asmp_work_fn);
	queue_delayed_work(asmp_workq, &asmp_work,
			msecs_to_jiffies(asmp_param.delay));

	pr_info(ASMP_TAG"resumed : total cpu %d\n", num_online_cpus());
}

static int lcd_notifier_callback(struct notifier_block *this,
					unsigned long event, void *data)
{
	if (event == LCD_EVENT_ON_START)
		if (!asmp_param.booted)
			asmp_param.booted = true;
		else
			schedule_work(&resume_work);
	else if (event == LCD_EVENT_OFF_START)
		schedule_work(&suspend_work);

	return NOTIFY_OK;
}

static int __cpuinit set_enabled(const char *val, const struct kernel_param *kp)
{
	int ret;
	unsigned int cpu;

	ret = param_set_bool(val, kp);
	if (enabled == 1) {
		queue_delayed_work(asmp_workq, &asmp_work,
				msecs_to_jiffies(asmp_param.delay));
		pr_info(ASMP_TAG"enabled\n");
	} else {
		cancel_delayed_work_sync(&asmp_work);
		for_each_present_cpu(cpu) {
			if (num_online_cpus() >= asmp_param.max_cpus)
				break;
			if (!cpu_online(cpu))
				cpu_up(cpu);
		}
		pr_info(ASMP_TAG"disabled\n");
	}
	return ret;
}

static struct kernel_param_ops module_ops = {
	.set = set_enabled,
	.get = param_get_bool,
};

module_param_cb(enabled, &module_ops, &enabled, 0644);
MODULE_PARM_DESC(enabled, "hotplug/unplug cpu cores based on cpu load");

/***************************** SYSFS START *****************************/
#define define_one_global_ro(_name)					\
static struct global_attr _name =					\
__ATTR(_name, 0444, show_##_name, NULL)

#define define_one_global_rw(_name)					\
static struct global_attr _name =					\
__ATTR(_name, 0644, show_##_name, store_##_name)

struct kobject *asmp_kobject;

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", asmp_param.object);			\
}
show_one(delay, delay);
show_one(scroff_single_core, scroff_single_core);
show_one(min_cpus, min_cpus);
show_one(max_cpus, max_cpus);
show_one(cpufreq_up,cpufreq_up);
show_one(cpufreq_down,cpufreq_down);
show_one(cycle_up, cycle_up);
show_one(cycle_down, cycle_down);

#define store_one(file_name, object)					\
static ssize_t store_##file_name					\
(struct kobject *a, struct attribute *b, const char *buf, size_t count)	\
{									\
	unsigned int input;						\
	int ret;							\
	ret = sscanf(buf, "%u", &input);				\
	if (ret != 1)							\
		return -EINVAL;						\
	asmp_param.object = input;					\
	return count;							\
}									\
define_one_global_rw(file_name);
store_one(delay, delay);
store_one(scroff_single_core, scroff_single_core);
store_one(min_cpus, min_cpus);
store_one(max_cpus, max_cpus);
store_one(cpufreq_up, cpufreq_up);
store_one(cpufreq_down, cpufreq_down);
store_one(cycle_up, cycle_up);
store_one(cycle_down, cycle_down);

static struct attribute *asmp_attributes[] = {
	&delay.attr,
	&scroff_single_core.attr,
	&min_cpus.attr,
	&max_cpus.attr,
	&cpufreq_up.attr,
	&cpufreq_down.attr,
	&cycle_up.attr,
	&cycle_down.attr,
	NULL
};

static struct attribute_group asmp_attr_group = {
	.attrs = asmp_attributes,
	.name = "conf",
};
/****************************** SYSFS END ******************************/

static int __init asmp_init(void)
{
	int rc;

	asmp_workq = alloc_workqueue("asmp", WQ_FREEZABLE | WQ_UNBOUND, 1);
	if (!asmp_workq)
		return -ENOMEM;

	notif.notifier_call = lcd_notifier_callback;
	if (lcd_register_client(&notif)) {
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&asmp_work, asmp_work_fn);
	INIT_WORK(&suspend_work, asmp_power_suspend);
	INIT_WORK(&resume_work, asmp_power_resume);
	queue_delayed_work(asmp_workq, &asmp_work,
			   msecs_to_jiffies(ASMP_STARTDELAY));

	asmp_kobject = kobject_create_and_add("autosmp", kernel_kobj);
	if (asmp_kobject) {
		rc = sysfs_create_group(asmp_kobject, &asmp_attr_group);
		if (rc)
			pr_warn(ASMP_TAG"ERROR, create sysfs group");
	} else
		pr_warn(ASMP_TAG"ERROR, create sysfs kobj");

	pr_info(ASMP_TAG"initialized\n");
	return 0;
}
late_initcall(asmp_init);
