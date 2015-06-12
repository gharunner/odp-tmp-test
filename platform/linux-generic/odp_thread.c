/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

#include <odp/thread.h>
#include <odp/thrmask.h>
#include <odp_internal.h>
#include <odp/spinlock.h>
#include <odp/config.h>
#include <odp_debug_internal.h>
#include <odp/shared_memory.h>
#include <odp/align.h>
#include <odp/cpu.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	int thr;
	int cpu;
	odp_thread_type_t type;
} thread_state_t;


typedef struct {
	thread_state_t thr[ODP_CONFIG_MAX_THREADS];
	odp_thrmask_t  all;
	odp_thrmask_t  worker;
	odp_thrmask_t  control;
	uint32_t       num;
	odp_spinlock_t lock;
} thread_globals_t;


/* Globals */
static thread_globals_t *thread_globals;


/* Thread local */
static __thread thread_state_t *this_thread;


int odp_thread_init_global(void)
{
	odp_shm_t shm;

	shm = odp_shm_reserve("odp_thread_globals",
			      sizeof(thread_globals_t),
			      ODP_CACHE_LINE_SIZE, 0);

	thread_globals = odp_shm_addr(shm);

	if (thread_globals == NULL)
		return -1;

	memset(thread_globals, 0, sizeof(thread_globals_t));
	odp_spinlock_init(&thread_globals->lock);
	odp_thrmask_zero(&thread_globals->all);
	odp_thrmask_zero(&thread_globals->worker);
	odp_thrmask_zero(&thread_globals->control);

	return 0;
}

int odp_thread_term_global(void)
{
	int ret;

	ret = odp_shm_free(odp_shm_lookup("odp_thread_globals"));
	if (ret < 0)
		ODP_ERR("shm free failed for odp_thread_globals");

	return ret;
}

static int alloc_id(odp_thread_type_t type)
{
	int thr;
	odp_thrmask_t *all = &thread_globals->all;

	if (thread_globals->num >= ODP_CONFIG_MAX_THREADS)
		return -1;

	for (thr = 0; thr < ODP_CONFIG_MAX_THREADS; thr++) {
		if (odp_thrmask_isset(all, thr) == 0) {
			odp_thrmask_set(all, thr);

			if (type == ODP_THREAD_WORKER)
				odp_thrmask_set(&thread_globals->worker, thr);
			else
				odp_thrmask_set(&thread_globals->control, thr);

			thread_globals->num++;
			return thr;
		}
	}

	return -2;
}

static int free_id(int thr)
{
	odp_thrmask_t *all = &thread_globals->all;

	if (thr < 0 || thr >= ODP_CONFIG_MAX_THREADS)
		return -1;

	if (odp_thrmask_isset(all, thr) == 0)
		return -1;

	odp_thrmask_clr(all, thr);

	if (thread_globals->thr[thr].type == ODP_THREAD_WORKER)
		odp_thrmask_clr(&thread_globals->worker, thr);
	else
		odp_thrmask_clr(&thread_globals->control, thr);

	thread_globals->num--;
	return thread_globals->num;
}

int odp_thread_init_local(odp_thread_type_t type)
{
	int id;
	int cpu;

	odp_spinlock_lock(&thread_globals->lock);
	id = alloc_id(type);
	odp_spinlock_unlock(&thread_globals->lock);

	if (id < 0) {
		ODP_ERR("Too many threads\n");
		return -1;
	}

	cpu = sched_getcpu();

	if (cpu < 0) {
		ODP_ERR("getcpu failed\n");
		return -1;
	}

	thread_globals->thr[id].thr  = id;
	thread_globals->thr[id].cpu  = cpu;
	thread_globals->thr[id].type = type;

	this_thread = &thread_globals->thr[id];
	return 0;
}

int odp_thread_term_local(void)
{
	int num;
	int id = this_thread->thr;

	odp_spinlock_lock(&thread_globals->lock);
	num = free_id(id);
	odp_spinlock_unlock(&thread_globals->lock);

	if (num < 0) {
		ODP_ERR("failed to free thread id %i", id);
		return -1;
	}

	return num; /* return a number of threads left */
}

int odp_thread_id(void)
{
	return this_thread->thr;
}

int odp_thread_count(void)
{
	return thread_globals->num;
}

odp_thread_type_t odp_thread_type(void)
{
	return this_thread->type;
}

int odp_cpu_id(void)
{
	return this_thread->cpu;
}
