/*
 * Copyright 2010, Intel Corporation
 *
 * This file is part of PowerTOP
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 * or just google for it.
 *
 * Authors:
 *	Arjan van de Ven <arjan@linux.intel.com>
 */
#include "process.h"
#include "interrupt.h"
#include "timer.h"
#include "work.h"
#include "processdevice.h"
#include "../lib.h"
#include "../report/report.h"
#include "../report/report-data-html.h"
#include "../report/report-maker.h"
#include "../devlist.h"

#include <vector>
#include <algorithm>
#include <stack>

#include <stdio.h>
#include <string.h>
#include <ncurses.h>

#include "../perf/perf_bundle.h"
#include "../perf/perf_event.h"
#include "../parameters/parameters.h"
#include "../display.h"
#include "../measurement/measurement.h"

static  class perf_bundle * perf_events;

vector <class power_consumer *> all_power;

vector< vector<class power_consumer *> > cpu_stack;

vector<int> cpu_level;
vector<int> cpu_credit;
vector<class power_consumer *> cpu_blame;

#define LEVEL_HARDIRQ	1
#define LEVEL_SOFTIRQ	2
#define LEVEL_TIMER	3
#define LEVEL_WAKEUP	4
#define LEVEL_PROCESS	5
#define LEVEL_WORK	6

static uint64_t first_stamp, last_stamp;

double measurement_time;

static void push_consumer(unsigned int cpu, class power_consumer *consumer)
{
	if (cpu_stack.size() <= cpu)
		cpu_stack.resize(cpu + 1);
	cpu_stack[cpu].push_back(consumer);
}

static void pop_consumer(unsigned int cpu)
{
	if (cpu_stack.size() <= cpu)
		cpu_stack.resize(cpu + 1);

	if (cpu_stack[cpu].size())
		cpu_stack[cpu].resize(cpu_stack[cpu].size()-1);
}

static int consumer_depth(unsigned int cpu)
{
	if (cpu_stack.size() <= cpu)
		cpu_stack.resize(cpu + 1);
	return cpu_stack[cpu].size();
}

static class power_consumer *current_consumer(unsigned int cpu)
{
	if (cpu_stack.size() <= cpu)
		cpu_stack.resize(cpu + 1);
	if (cpu_stack[cpu].size())

		return cpu_stack[cpu][cpu_stack[cpu].size()-1];

	return NULL;
}

static void clear_consumers(void)
{
	unsigned int i;
	for (i = 0; i < cpu_stack.size(); i++)
		cpu_stack[i].resize(0);
}

static void consumer_child_time(unsigned int cpu, uint64_t time)
{
	unsigned int i;
	if (cpu_stack.size() <= cpu)
		cpu_stack.resize(cpu + 1);
	for (i = 0; i < cpu_stack[cpu].size(); i++)
		cpu_stack[cpu][i]->child_runtime += time;
}

static void set_wakeup_pending(unsigned int cpu)
{
	if (cpu_credit.size() <= cpu)
		cpu_credit.resize(cpu + 1);

	cpu_credit[cpu] = 1;
}

static void clear_wakeup_pending(unsigned int cpu)
{
	if (cpu_credit.size() <= cpu)
		cpu_credit.resize(cpu + 1);

	cpu_credit[cpu] = 0;
}

static int get_wakeup_pending(unsigned int cpu)
{
	if (cpu_credit.size() <= cpu)
		cpu_credit.resize(cpu + 1);
	return cpu_credit[cpu];
}

static void change_blame(unsigned int cpu, class power_consumer *consumer, int level)
{
	if (cpu_level[cpu] >= level)
		return;
	cpu_blame[cpu] = consumer;
	cpu_level[cpu] = level;
}

static void consume_blame(unsigned int cpu)
{
	if (!get_wakeup_pending(cpu))
		return;
	if (cpu_level.size() <= cpu)
		return;
	if (cpu_blame.size() <= cpu)
		return;
	if (!cpu_blame[cpu])
		return;

	cpu_blame[cpu]->wake_ups++;
	cpu_blame[cpu] = NULL;
	cpu_level[cpu] = 0;
	clear_wakeup_pending(cpu);
}


class perf_process_bundle: public perf_bundle
{
	virtual void handle_trace_point(void *trace, int cpu, uint64_t time);
};

static bool comm_is_xorg(char *comm)
{
	return strcmp(comm, "Xorg") == 0 || strcmp(comm, "X") == 0;
}

/* some processes shouldn't be blamed for the wakeup if they wake a process up... for now this is a hardcoded list */
int dont_blame_me(char *comm)
{
	if (comm_is_xorg(comm))
		return 1;
	if (strcmp(comm, "dbus-daemon") == 0)
		return 1;

	return 0;
}

static char * get_tep_field_str(void *trace, struct tep_event *event, struct tep_format_field *field)
{
	unsigned long long offset, len;
	if (field->flags & TEP_FIELD_IS_DYNAMIC) {
		offset = field->offset;
		len = field->size;
		offset = tep_read_number(event->tep, (char *)trace + offset, len);
		offset &= 0xffff;
		return (char *)trace + offset;
	}
	/** no __data_loc field type*/
	return (char *)trace + field->offset;
}

void perf_process_bundle::handle_trace_point(void *trace, int cpu, uint64_t time)
{
	struct tep_event *event;
	struct tep_record rec; /* holder */
	struct tep_format_field *field;
	unsigned long long val;
	int type;
	int ret;

	rec.data = trace;

	type = tep_data_type(perf_event::tep, &rec);
	event = tep_find_event(perf_event::tep, type);
	if (!event)
		return;

	if (time < first_stamp)
		first_stamp = time;

	if (time > last_stamp) {
		last_stamp = time;
		measurement_time = (0.0001 + last_stamp - first_stamp) / 1000000000 ;
	}

	if (strcmp(event->name, "sched_switch") == 0) {
		class process *old_proc = NULL;
		class process *new_proc  = NULL;
		const char *next_comm;
		int next_pid;
		int prev_pid;

		field = tep_find_any_field(event, "next_comm");
		if (!field || !(field->flags & TEP_FIELD_IS_STRING))
			return; /* ?? */

		next_comm = get_tep_field_str(trace, event, field);

		ret = tep_get_field_val(NULL, event, "next_pid", &rec, &val, 0);
		if (ret < 0)
			return;
		next_pid = (int)val;

		ret = tep_get_field_val(NULL, event, "prev_pid", &rec, &val, 0);
		if (ret < 0)
			return;
		prev_pid = (int)val;

		/* find new process pointer */
		new_proc = find_create_process(next_comm, next_pid);

		/* find the old process pointer */

		while  (consumer_depth(cpu) > 1) {
			pop_consumer(cpu);
		}

		if (consumer_depth(cpu) == 1)
			old_proc = (class process *)current_consumer(cpu);

		if (old_proc && strcmp(old_proc->name(), "process"))
			old_proc = NULL;

		/* retire the old process */

		if (old_proc) {
			old_proc->deschedule_thread(time, prev_pid);
			old_proc->waker = NULL;
		}

		if (consumer_depth(cpu))
			pop_consumer(cpu);

		push_consumer(cpu, new_proc);

		/* start new process */
		new_proc->schedule_thread(time, next_pid);

		if (strncmp(next_comm,"migration/", 10) && strncmp(next_comm,"kworker/", 8) && strncmp(next_comm, "kondemand/",10)) {
			if (next_pid) {
				/* If someone woke us up.. blame him instead */
				if (new_proc->waker) {
					change_blame(cpu, new_proc->waker, LEVEL_PROCESS);
				} else {
					change_blame(cpu, new_proc, LEVEL_PROCESS);
				}
			}

			consume_blame(cpu);
		}
		new_proc->waker = NULL;
	}
	else if (strcmp(event->name, "sched_wakeup") == 0) {
		class power_consumer *from = NULL;
		class process *dest_proc = NULL;
		class process *from_proc = NULL;
		const char *comm;
		int flags;
		int pid;

		ret = tep_get_common_field_val(NULL, event, "common_flags", &rec, &val, 0);
		if (ret < 0)
			return;
		flags = (int)val;

		if ( (flags & TRACE_FLAG_HARDIRQ) || (flags & TRACE_FLAG_SOFTIRQ)) {
			class timer *timer;
			timer = (class timer *) current_consumer(cpu);
			if (timer && strcmp(timer->name(), "timer")==0) {
				if (strcmp(timer->handler, "delayed_work_timer_fn") &&
				    strcmp(timer->handler, "hrtimer_wakeup") &&
				    strcmp(timer->handler, "it_real_fn"))
					from = timer;
			}
			/* woken from interrupt */
			/* TODO: find the current irq handler and set "from" to that */
		} else {
			from = current_consumer(cpu);
		}


		field = tep_find_any_field(event, "comm");

		if (!field || !(field->flags & TEP_FIELD_IS_STRING))
 			return;

		comm = get_tep_field_str(trace, event, field);

		ret = tep_get_field_val(NULL, event, "pid", &rec, &val, 0);
		if (ret < 0)
			return;
		pid = (int)val;

		dest_proc = find_create_process(comm, pid);

		if (from && strcmp(from->name(), "process")!=0){
			/* not a process doing the wakeup */
			from = NULL;
			from_proc = NULL;
		} else {
			from_proc = (class process *) from;
		}

		if (from_proc && (dest_proc->running == 0) && (dest_proc->waker == NULL) && (pid != 0) && !dont_blame_me(from_proc->comm))
			dest_proc->waker = from;
		if (from)
			dest_proc->last_waker = from;

		/* Account processes that wake up X specially */
		if (from && dest_proc && comm_is_xorg(dest_proc->comm))
			from->xwakes ++ ;

	}
	else if (strcmp(event->name, "irq_handler_entry") == 0) {
		class interrupt *irq = NULL;
		const char *handler;
		int nr;

		field = tep_find_any_field(event, "name");
		if (!field || !(field->flags & TEP_FIELD_IS_STRING))
			return; /* ?? */

		handler = get_tep_field_str(trace, event, field);

		ret = tep_get_field_val(NULL, event, "irq", &rec, &val, 0);
		if (ret < 0)
			return;
		nr = (int)val;

		irq = find_create_interrupt(handler, nr, cpu);


		push_consumer(cpu, irq);

		irq->start_interrupt(time);

		if (strstr(irq->handler, "timer") ==NULL)
			change_blame(cpu, irq, LEVEL_HARDIRQ);

	}

	else if (strcmp(event->name, "irq_handler_exit") == 0) {
		class interrupt *irq = NULL;
		uint64_t t;

		/* find interrupt (top of stack) */
		irq = (class interrupt *)current_consumer(cpu);
		if (!irq || strcmp(irq->name(), "interrupt"))
			return;
		pop_consumer(cpu);
		/* retire interrupt */
		t = irq->end_interrupt(time);
		consumer_child_time(cpu, t);
	}

	else if (strcmp(event->name, "softirq_entry") == 0) {
		class interrupt *irq = NULL;
		const char *handler = NULL;
		int vec;

		ret = tep_get_field_val(NULL, event, "vec", &rec, &val, 0);
                if (ret < 0) {
                        fprintf(stderr, "softirq_entry event returned no vector number?\n");
                        return;
                }
		vec = (int)val;

		if (vec <= 9)
			handler = softirqs[vec];

		if (!handler)
			return;

		irq = find_create_interrupt(handler, vec, cpu);

		push_consumer(cpu, irq);

		irq->start_interrupt(time);
		change_blame(cpu, irq, LEVEL_SOFTIRQ);
	}
	else if (strcmp(event->name, "softirq_exit") == 0) {
		class interrupt *irq = NULL;
		uint64_t t;

		irq = (class interrupt *) current_consumer(cpu);
		if (!irq  || strcmp(irq->name(), "interrupt"))
			return;
		pop_consumer(cpu);
		/* pop irq */
		t = irq->end_interrupt(time);
		consumer_child_time(cpu, t);
	}
	else if (strcmp(event->name, "timer_expire_entry") == 0) {
		class timer *timer = NULL;
		uint64_t function;
		uint64_t tmr;

		ret = tep_get_field_val(NULL, event, "function", &rec, &val, 0);
		if (ret < 0) {
			fprintf(stderr, "timer_expire_entry event returned no function value?\n");
			return;
		}
		function = (uint64_t)val;

		timer = find_create_timer(function);

		if (timer->is_deferred())
			return;

		ret = tep_get_field_val(NULL, event, "timer", &rec, &val, 0);
		if (ret < 0) {
			fprintf(stderr, "softirq_entry event returned no timer ?\n");
			return;
		}
		tmr = (uint64_t)val;

		push_consumer(cpu, timer);
		timer->fire(time, tmr);

		if (strcmp(timer->handler, "delayed_work_timer_fn"))
			change_blame(cpu, timer, LEVEL_TIMER);
	}
	else if (strcmp(event->name, "timer_expire_exit") == 0) {
		class timer *timer = NULL;
		uint64_t tmr;
		uint64_t t;

		ret = tep_get_field_val(NULL, event, "timer", &rec, &val, 0);
		if (ret < 0)
			return;
		tmr = (uint64_t)val;

		timer = (class timer *) current_consumer(cpu);
		if (!timer || strcmp(timer->name(), "timer")) {
			return;
		}
		pop_consumer(cpu);
		t = timer->done(time, tmr);
		if (t == ~0ULL) {
			timer->fire(first_stamp, tmr);
			t = timer->done(time, tmr);
		}
		consumer_child_time(cpu, t);
	}
	else if (strcmp(event->name, "hrtimer_expire_entry") == 0) {
		class timer *timer = NULL;
		uint64_t function;
		uint64_t tmr;

		ret = tep_get_field_val(NULL, event, "function", &rec, &val, 0);
		if (ret < 0)
			return;
		function = (uint64_t)val;

		timer = find_create_timer(function);

		ret = tep_get_field_val(NULL, event, "hrtimer", &rec, &val, 0);
		if (ret < 0)
			return;
		tmr = (uint64_t)val;

		push_consumer(cpu, timer);
		timer->fire(time, tmr);

		if (strcmp(timer->handler, "delayed_work_timer_fn"))
			change_blame(cpu, timer, LEVEL_TIMER);
	}
	else if (strcmp(event->name, "hrtimer_expire_exit") == 0) {
		class timer *timer = NULL;
		uint64_t tmr;
		uint64_t t;

		timer = (class timer *) current_consumer(cpu);
		if (!timer || strcmp(timer->name(), "timer")) {
			return;
		}

		ret = tep_get_field_val(NULL, event, "hrtimer", &rec, &val, 0);
		if (ret < 0)
			return;
		tmr = (uint64_t)val;

		pop_consumer(cpu);
		t = timer->done(time, tmr);
		if (t == ~0ULL) {
			timer->fire(first_stamp, tmr);
			t = timer->done(time, tmr);
		}
		consumer_child_time(cpu, t);
	}
	else if (strcmp(event->name, "workqueue_execute_start") == 0) {
		class work *work = NULL;
		uint64_t function;
		uint64_t wk;

		ret = tep_get_field_val(NULL, event, "function", &rec, &val, 0);
		if (ret < 0)
			return;
		function = (uint64_t)val;

		ret = tep_get_field_val(NULL, event, "work", &rec, &val, 0);
		if (ret < 0)
			return;
		wk = (uint64_t)val;

		work = find_create_work(function);


		push_consumer(cpu, work);
		work->fire(time, wk);


		if (strcmp(work->handler, "do_dbs_timer") != 0 && strcmp(work->handler, "vmstat_update") != 0)
			change_blame(cpu, work, LEVEL_WORK);
	}
	else if (strcmp(event->name, "workqueue_execute_end") == 0) {
		class work *work = NULL;
		uint64_t t;
		uint64_t wk;

		ret = tep_get_field_val(NULL, event, "work", &rec, &val, 0);
		if (ret < 0)
			return;
		wk = (uint64_t)val;

		work = (class work *) current_consumer(cpu);
		if (!work || strcmp(work->name(), "work")) {
			return;
		}
		pop_consumer(cpu);
		t = work->done(time, wk);
		if (t == ~0ULL) {
			work->fire(first_stamp, wk);
			t = work->done(time, wk);
		}
		consumer_child_time(cpu, t);
	}
	else if (strcmp(event->name, "cpu_idle") == 0) {
		tep_get_field_val(NULL, event, "state", &rec, &val, 0);
		if (val == (unsigned int)-1)
			consume_blame(cpu);
		else
			set_wakeup_pending(cpu);
	}
	else if (strcmp(event->name, "power_start") == 0) {
		set_wakeup_pending(cpu);
	}
	else if (strcmp(event->name, "power_end") == 0) {
		consume_blame(cpu);
	}
	else if (strcmp(event->name, "i915_gem_ring_dispatch") == 0
	 || strcmp(event->name, "i915_gem_request_submit") == 0) {
		/* any kernel contains only one of the these tracepoints,
		 * the latter one got replaced by the former one */
		class power_consumer *consumer = NULL;
		int flags;

		ret = tep_get_common_field_val(NULL, event, "common_flags", &rec, &val, 0);
		if (ret < 0)
			return;
		flags = (int)val;

		consumer = current_consumer(cpu);
		/* currently we don't count graphic requests submitted from irq contect */
		if ( (flags & TRACE_FLAG_HARDIRQ) || (flags & TRACE_FLAG_SOFTIRQ)) {
			consumer = NULL;
		}


		/* if we are X, and someone just woke us, account the GPU op to the guy waking us */
		if (consumer && strcmp(consumer->name(), "process")==0) {
			class process *proc = NULL;
			proc = (class process *) consumer;
			if (comm_is_xorg(proc->comm) && proc->last_waker) {
				consumer = proc->last_waker;
			}
		}



		if (consumer) {
			consumer->gpu_ops++;
		}
	}
	else if (strcmp(event->name, "writeback_inode_dirty") == 0) {
		static uint64_t prev_time;
		class power_consumer *consumer = NULL;
		int dev;

		consumer = current_consumer(cpu);

		ret = tep_get_field_val(NULL, event, "dev", &rec, &val, 0);
		if (ret < 0)

			return;
		dev = (int)val;

		if (consumer && strcmp(consumer->name(),
			"process")==0 && dev > 0) {

			consumer->disk_hits++;

			/* if the previous inode dirty was > 1 second ago, it becomes a hard hit */
			if ((time - prev_time) > 1000000000)
				consumer->hard_disk_hits++;

			prev_time = time;
		}
	}
}

void start_process_measurement(void)
{
	if (!perf_events) {
		perf_events = new perf_process_bundle();
		perf_events->add_event("sched","sched_switch");
		perf_events->add_event("sched","sched_wakeup");
		perf_events->add_event("irq","irq_handler_entry");
		perf_events->add_event("irq","irq_handler_exit");
		perf_events->add_event("irq","softirq_entry");
		perf_events->add_event("irq","softirq_exit");
		perf_events->add_event("timer","timer_expire_entry");
		perf_events->add_event("timer","timer_expire_exit");
		perf_events->add_event("timer","hrtimer_expire_entry");
		perf_events->add_event("timer","hrtimer_expire_exit");
		if (!perf_events->add_event("power","cpu_idle")){
			perf_events->add_event("power","power_start");
			perf_events->add_event("power","power_end");
		}
		perf_events->add_event("workqueue","workqueue_execute_start");
		perf_events->add_event("workqueue","workqueue_execute_end");
		perf_events->add_event("i915","i915_gem_ring_dispatch");
		perf_events->add_event("i915","i915_gem_request_submit");
		perf_events->add_event("writeback","writeback_inode_dirty");
	}

	first_stamp = ~0ULL;
	last_stamp = 0;
	perf_events->start();
}

void end_process_measurement(void)
{
	if (!perf_events)
		return;

	perf_events->stop();
}


static bool power_cpu_sort(class power_consumer * i, class power_consumer * j)
{
	double iW, jW;

	iW = i->Witts();
	jW = j->Witts();

	if (equals(iW, jW)) {
		double iR, jR;

		iR = i->accumulated_runtime - i->child_runtime;
		jR = j->accumulated_runtime - j->child_runtime;

		if (equals(iR, jR))
			return i->wake_ups > j->wake_ups;
		return (iR > jR);
	}

        return (iW > jW);
}

double total_wakeups(void)
{
	double total = 0;
	unsigned int i;
	for (i = 0; i < all_power.size() ; i++)
		total += all_power[i]->wake_ups;

	total = total / measurement_time;


	return total;
}

double total_gpu_ops(void)
{
	double total = 0;
	unsigned int i;
	for (i = 0; i < all_power.size() ; i++)
		total += all_power[i]->gpu_ops;


	total = total / measurement_time;


	return total;
}

double total_disk_hits(void)
{
	double total = 0;
	unsigned int i;
	for (i = 0; i < all_power.size() ; i++)
		total += all_power[i]->disk_hits;


	total = total / measurement_time;


	return total;
}


double total_hard_disk_hits(void)
{
	double total = 0;
	unsigned int i;
	for (i = 0; i < all_power.size() ; i++)
		total += all_power[i]->hard_disk_hits;


	total = total / measurement_time;


	return total;
}

double total_xwakes(void)
{
	double total = 0;
	unsigned int i;
	for (i = 0; i < all_power.size() ; i++)
		total += all_power[i]->xwakes;


	total = total / measurement_time;


	return total;
}

void process_update_display(void)
{
	unsigned int i;
	WINDOW *win;
	double pw;
	double joules;
	int tl;
	int tlt;
	int tlr;

	int show_power;
	int need_linebreak = 0;

	sort(all_power.begin(), all_power.end(), power_cpu_sort);

	show_power = global_power_valid();

	win = get_ncurses_win("Overview");
	if (!win)
		return;

	wclear(win);

	wmove(win, 1,0);

	wprintw(win, "%s\n","Overview - CPU에 웨이크업을 가장 자주 보내거나 시스템 전원을 가장 많이 사용하는 시스템 구성 요소 목록을 볼 수 있습니다.");
	wprintw(win, "%s\n", "Usage - 초당 전력 사용량 / Events/s - 초당 event(Wakeup) 발생량 / Category - 분류 / Description - 설명");

#if 0
	double sum;
	calculate_params();
	sum = 0.0;
	sum += get_parameter_value("base power");
	for (i = 0; i < all_power.size(); i++) {
		sum += all_power[i]->Witts();
	}

	wprintw(win, _("Estimated power: %5.1f    Measured power: %5.1f    Sum: %5.1f\n\n"),
				all_parameters.guessed_power, global_power(), sum);
#endif

	pw = global_power();
	joules = global_joules();
	tl = global_time_left() / 60;
	tlt = (tl /60);
	tlr = tl % 60;

	if (pw > 0.0001) {
		char buf[32];
		wprintw(win, _("The battery reports a discharge rate of %sW\n"),
				fmt_prefix(pw, buf));
		wprintw(win, _("The energy consumed was %sJ\n"),
				fmt_prefix(joules, buf));
		need_linebreak = 1;
	}
	if (tl > 0 && pw > 0.0001) {
		wprintw(win, _("The estimated remaining time is %i hours, %i minutes\n"), tlt, tlr);
		need_linebreak = 1;
	}

	if (need_linebreak)
		wprintw(win, "\n");


	wprintw(win, "%s: %3.1f %s,  %3.1f %s, %3.1f %s %3.1f%% %s\n\n",_("Summary"), total_wakeups(), _("wakeups/second"), total_gpu_ops(), _("GPU ops/seconds"), total_disk_hits(), _("VFS ops/sec and"), total_cpu_time()*100, _("CPU use"));


	if (show_power)
		wprintw(win, "%s              %s       %s    %s       %s\n", _("Power est."), _("Usage"), _("Events/s"), _("Category"), _("Description"));
	else
		wprintw(win, "                %s       %s    %s       %s\n", _("Usage"), _("Events/s"), _("Category"), _("Description"));

	for (i = 0; i < all_power.size(); i++) {
		char power[16];
		char name[20];
		char usage[20];
		char events[20];
		char descr[128];

		format_watts(all_power[i]->Witts(), power, 10);
		if (!show_power)
			strcpy(power, "          ");
		snprintf(name, sizeof(name), "%s", all_power[i]->type());

		align_string(name, 14, 20);

		if (all_power[i]->events() == 0 && all_power[i]->usage() == 0 && all_power[i]->Witts() == 0)
			break;

		usage[0] = 0;
		if (all_power[i]->usage_units()) {
			if (all_power[i]->usage() < 1000)
				snprintf(usage, sizeof(usage), "%5.1f%s", all_power[i]->usage(), all_power[i]->usage_units());
			else
				snprintf(usage, sizeof(usage), "%5i%s", (int)all_power[i]->usage(), all_power[i]->usage_units());
		}

		align_string(usage, 14, 20);

		snprintf(events, sizeof(events), "%5.1f", all_power[i]->events());
		if (!all_power[i]->show_events())
			events[0] = 0;
		else if (all_power[i]->events() <= 0.3)
			snprintf(events, sizeof(events), "%5.2f", all_power[i]->events());

		align_string(events, 12, 20);
		wprintw(win, "%s  %s %s %s %s\n", power, usage, events, name, pretty_print(all_power[i]->description(), descr, 128));
	}
}

void report_process_update_display(void)
{
	unsigned int i;
	unsigned int total;
	int show_power, cols, rows, idx;

	/* div attr css_class and css_id */
	tag_attr div_attr;
	init_div(&div_attr, "clear_block", "software");

	/* Set Table attributes, rows, and cols */
	cols=7;
	sort(all_power.begin(), all_power.end(), power_cpu_sort);
	show_power = global_power_valid();
	if (show_power)
		cols=8;

	idx=cols;

	total = all_power.size();
	if (total > 100)
		total = 100;

	rows=total+1;
	table_attributes std_table_css;
	init_nowarp_table_attr(&std_table_css, rows, cols);


	/* Set Title attributes */
	tag_attr title_attr;
	init_title_attr(&title_attr);

	/* Set array of data in row Major order */
	string *software_data = new string[cols * rows];
	software_data[0]=__("Usage");
	software_data[1]=__("Wakeups/s");
	software_data[2]=__("GPU ops/s");
	software_data[3]=__("Disk IO/s");
	software_data[4]=__("GFX Wakeups/s");
	software_data[5]=__("Category");
	software_data[6]=__("Description");

	if (show_power)
		software_data[7]=__("PW Estimate");


	for (i = 0; i < total; i++) {
		char power[16];
		char name[20];
		char usage[20];
		char wakes[20];
		char gpus[20];
		char disks[20];
		char xwakes[20];
		char descr[128];
		format_watts(all_power[i]->Witts(), power, 10);

		if (!show_power)
			strcpy(power, "          ");
		snprintf(name, sizeof(name), "%s", all_power[i]->type());

		if (strcmp(name, "Device") == 0)
			continue;

		if (all_power[i]->events() == 0 && all_power[i]->usage() == 0
				&& all_power[i]->Witts() == 0)
			break;

		usage[0] = 0;
		if (all_power[i]->usage_units()) {
			if (all_power[i]->usage() < 1000)
				snprintf(usage, sizeof(usage), "%5.1f%s", all_power[i]->usage(), all_power[i]->usage_units());
			else
				snprintf(usage, sizeof(usage), "%5i%s", (int)all_power[i]->usage(), all_power[i]->usage_units());
		}
		snprintf(wakes, sizeof(wakes), "%5.1f", all_power[i]->wake_ups / measurement_time);
		if (all_power[i]->wake_ups / measurement_time <= 0.3)
			snprintf(wakes, sizeof(wakes), "%5.2f", all_power[i]->wake_ups / measurement_time);
		snprintf(gpus, sizeof(gpus), "%5.1f", all_power[i]->gpu_ops / measurement_time);
		snprintf(disks, sizeof(disks), "%5.1f (%5.1f)", all_power[i]->hard_disk_hits / measurement_time,
				all_power[i]->disk_hits / measurement_time);
		snprintf(xwakes, sizeof(xwakes), "%5.1f", all_power[i]->xwakes / measurement_time);
		if (!all_power[i]->show_events()) {
			wakes[0] = 0;
			gpus[0] = 0;
			disks[0] = 0;
		}

		if (all_power[i]->gpu_ops == 0)
			gpus[0] = 0;
		if (all_power[i]->wake_ups == 0)
			wakes[0] = 0;
		if (all_power[i]->disk_hits == 0)
			disks[0] = 0;
		if (all_power[i]->xwakes == 0)
			xwakes[0] = 0;

		software_data[idx]=string(usage);
		idx+=1;

		software_data[idx]=string(wakes);
		idx+=1;

		software_data[idx]=string(gpus);
		idx+=1;

		software_data[idx]=string(disks);
		idx+=1;

		software_data[idx]=string(xwakes);
		idx+=1;

		software_data[idx]=string(name);
		idx+=1;

		software_data[idx]=string(pretty_print(all_power[i]->description(), descr, 128));
		idx+=1;
		if (show_power) {
			software_data[idx]=string(power);
			idx+=1;
		}
	}

	/* Report Output */
	report.add_div(&div_attr);
	report.add_title(&title_attr, __("Overview of Software Power Consumers"));
	report.add_table(software_data, &std_table_css);
        report.end_div();
	delete [] software_data;
}

void report_summary(void)
{
	unsigned int i;
	unsigned int total;
	int show_power;
	int rows, cols, idx;

	sort(all_power.begin(), all_power.end(), power_cpu_sort);
	show_power = global_power_valid();

	/* div attr css_class and css_id */
	tag_attr div_attr;
	init_div(&div_attr, "clear_block", "summary");


	/* Set table attributes, rows, and cols */
	cols=4;
	if (show_power)
		cols=5;
	idx=cols;
	total = all_power.size();
	if (total > 10)
		total = 10;
	rows=total+1;
	table_attributes std_table_css;
	init_std_table_attr(&std_table_css, rows, cols);

	/* Set title attributes */
	tag_attr title_attr;
	init_title_attr(&title_attr);

	/* Set array for summary */
	int summary_size =12;
	string *summary = new string [summary_size];
	summary[0]=__("Target:");
	summary[1]=__("1 units/s");
	summary[2]=__("System: ");
	summary[3]= double_to_string(total_wakeups());
	summary[3].append(__(" wakeup/s"));
	summary[4]=__("CPU: ");
	summary[5]= double_to_string(total_cpu_time()*100);
	summary[5].append(__("\% usage"));
	summary[6]=__("GPU:");
	summary[7]=double_to_string(total_gpu_ops());
	summary[7].append(__(" ops/s"));
	summary[8]=__("GFX:");
	summary[9]=double_to_string(total_xwakes());
	summary[9].append(__(" wakeups/s"));
	summary[10]=__("VFS:");
	summary[11]= double_to_string(total_disk_hits());
	summary[11].append(__(" ops/s"));

	/* Set array of data in row Major order */
	string *summary_data = new string[cols * (rows + 1)];
	summary_data[0]=__("Usage");
	summary_data[1]=__("Events/s");
	summary_data[2]=__("Category");
	summary_data[3]=__("Description");
	if (show_power)
		summary_data[4]=__("PW Estimate");

	for (i = 0; i < all_power.size(); i++) {
		char power[16];
		char name[20];
		char usage[20];
		char events[20];
		char descr[128];
		format_watts(all_power[i]->Witts(), power, 10);

		if (!show_power)
			strcpy(power, "          ");
		snprintf(name, sizeof(name), "%s", all_power[i]->type());

		if (i > total)
			break;

		if (all_power[i]->events() == 0 && all_power[i]->usage() == 0 &&
				all_power[i]->Witts() == 0)
			break;

		usage[0] = 0;
		if (all_power[i]->usage_units()) {
			if (all_power[i]->usage() < 1000)
				snprintf(usage, sizeof(usage), "%5.1f%s", all_power[i]->usage_summary(),
					all_power[i]->usage_units_summary());
			else
				snprintf(usage, sizeof(usage), "%5i%s", (int)all_power[i]->usage_summary(),
					all_power[i]->usage_units_summary());
		}
		snprintf(events, sizeof(events), "%5.1f", all_power[i]->events());
		if (!all_power[i]->show_events())
			events[0] = 0;
		else if (all_power[i]->events() <= 0.3)
			snprintf(events, sizeof(events), "%5.2f", all_power[i]->events());

		summary_data[idx]=string(usage);
		idx+=1;

		summary_data[idx]=string(events);
		idx+=1;

		summary_data[idx]=string(name);
		idx+=1;

		summary_data[idx]=string(pretty_print(all_power[i]->description(), descr, 128));
		idx+=1;

		if (show_power){
			summary_data[idx]=power;
			idx+=1;
		}
	}

	/* Report Summary for all */
	report.add_summary_list(summary, summary_size);
	report.add_div(&div_attr);
	report.add_title(&title_attr, __("Top 10 Power Consumers"));
	report.add_table(summary_data, &std_table_css);
	report.end_div();
	delete [] summary;
	delete [] summary_data;
}


void process_process_data(void)
{
	if (!perf_events)
		return;

	clear_processes();
	clear_interrupts();

	all_power.erase(all_power.begin(), all_power.end());
	clear_consumers();


	cpu_credit.resize(0, 0);
	cpu_credit.resize(get_max_cpu()+1, 0);
	cpu_level.resize(0, 0);
	cpu_level.resize(get_max_cpu()+1, 0);
	cpu_blame.resize(0, NULL);
	cpu_blame.resize(get_max_cpu()+1, NULL);



	/* process data */
	perf_events->process();
	perf_events->clear();

	run_devpower_list();

	merge_processes();

	all_processes_to_all_power();
	all_interrupts_to_all_power();
	all_timers_to_all_power();
	all_work_to_all_power();
	all_devices_to_all_power();

	sort(all_power.begin(), all_power.end(), power_cpu_sort);
}


double total_cpu_time(void)
{
	unsigned int i;
	double total = 0.0;
	for (i = 0; i < all_power.size() ; i++) {
		if (all_power[i]->child_runtime > all_power[i]->accumulated_runtime)
			all_power[i]->child_runtime = 0;
		total += all_power[i]->accumulated_runtime - all_power[i]->child_runtime;
	}

	total =  (total / (0.0001 + last_stamp - first_stamp));

	return total;
}



void end_process_data(void)
{
	report_utilization("cpu-consumption", total_cpu_time());
	report_utilization("cpu-wakeups", total_wakeups());
	report_utilization("gpu-operations", total_gpu_ops());
	report_utilization("disk-operations", total_disk_hits());
	report_utilization("disk-operations-hard", total_hard_disk_hits());
	report_utilization("xwakes", total_xwakes());

	all_power.erase(all_power.begin(), all_power.end());
	clear_processes();
	clear_proc_devices();
	clear_interrupts();
	clear_timers();
	clear_work();
	clear_consumers();

	perf_events->clear();

}

void clear_process_data(void)
{
	if (perf_events)
		perf_events->release();
	delete perf_events;
}

