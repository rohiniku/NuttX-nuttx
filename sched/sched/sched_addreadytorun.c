/****************************************************************************
 * sched/sched/sched_addreadytorun.c
 *
 *   Copyright (C) 2007-2009, 2014, 2016 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <queue.h>
#include <assert.h>

#include "sched/sched.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  sched_addreadytorun
 *
 * Description:
 *   This function adds a TCB to the ready to run list.  If the currently
 *   active task has preemption disabled and the new TCB would cause this
 *   task to be pre-empted, the new task is added to the g_pendingtasks list
 *   instead.  The pending tasks will be made ready-to-run when preemption is
 *   unlocked.
 *
 * Inputs:
 *   btcb - Points to the blocked TCB that is ready-to-run
 *
 * Return Value:
 *   true if the currently active task (the head of the ready-to-run list)
 *   has changed.
 *
 * Assumptions:
 * - The caller has established a critical section before calling this
 *   function (calling sched_lock() first is NOT a good idea -- use
 *   irqsave()).
 * - The caller has already removed the input rtcb from whatever list it
 *   was in.
 * - The caller handles the condition that occurs if the head of the
 *   ready-to-run list is changed.
 *
 ****************************************************************************/

#ifndef CONFIG_SMP
bool sched_addreadytorun(FAR struct tcb_s *btcb)
{
  FAR struct tcb_s *rtcb = this_task();
  bool ret;

  /* Check if pre-emption is disabled for the current running task and if
   * the new ready-to-run task would cause the current running task to be
   * pre-empted.
   */

  if (rtcb->lockcount && rtcb->sched_priority < btcb->sched_priority)
    {
      /* Yes.  Preemption would occur!  Add the new ready-to-run task to the
       * g_pendingtasks task list for now.
       */

      sched_addprioritized(btcb, (FAR dq_queue_t *)&g_pendingtasks);
      btcb->task_state = TSTATE_TASK_PENDING;
      ret = false;
    }

  /* Otherwise, add the new task to the ready-to-run task list */

  else if (sched_addprioritized(btcb, (FAR dq_queue_t *)&g_readytorun))
    {
      /* Inform the instrumentation logic that we are switching tasks */

      sched_note_switch(rtcb, btcb);

      /* The new btcb was added at the head of the ready-to-run list.  It
       * is now the new active task!
       */

      ASSERT(!rtcb->lockcount && btcb->flink != NULL);

      btcb->task_state = TSTATE_TASK_RUNNING;
      btcb->flink->task_state = TSTATE_TASK_READYTORUN;
      ret = true;
    }
  else
    {
      /* The new btcb was added in the middle of the ready-to-run list */

      btcb->task_state = TSTATE_TASK_READYTORUN;
      ret = false;
    }

  return ret;
}
#endif /* !CONFIG_SMP */

/****************************************************************************
 * Name:  sched_addreadytorun
 *
 * Description:
 *   This function adds a TCB to one of the ready to run lists.  That might
 *   be:
 *
 *   1. The g_readytorun list if the task is ready-to-run but not running
 *      and not assigned to a CPU.
 *   2. The g_assignedtask[cpu] list if the task is running or if has been
 *      assigned to a CPU.
 *
 *   If the currently active task has preemption disabled and the new TCB
 *   would cause this task to be pre-empted, the new task is added to the
 *   g_pendingtasks list instead.  Thepending tasks will be made
 *   ready-to-run when preemption isunlocked.
 *
 * Inputs:
 *   btcb - Points to the blocked TCB that is ready-to-run
 *
 * Return Value:
 *   true if the currently active task (the head of the ready-to-run list)
 *   has changed.
 *
 * Assumptions:
 * - The caller has established a critical section before calling this
 *   function (calling sched_lock() first is NOT a good idea -- use
 *   irqsave()).
 * - The caller has already removed the input rtcb from whatever list it
 *   was in.
 * - The caller handles the condition that occurs if the head of the
 *   ready-to-run list is changed.
 *
 ****************************************************************************/

#ifdef CONFIG_SMP
bool sched_addreadytorun(FAR struct tcb_s *btcb)
{
  FAR struct tcb_s *rtcb;
  FAR struct tcb_s *next;
  FAR dq_queue_t *tasklist;
  int task_state;
  int cpu;
  bool switched;
  bool doswitch;

  /* Check if the blocked TCB is already assigned to a CPU */

  if ((btcb->flags & TCB_FLAG_CPU_ASSIGNED) != 0)
    {
      /* Yes.. that that is the CPU we must use */

      cpu  = btcb->cpu;
      rtcb = (FAR struct tcb_s *)g_assignedtasks[cpu].head;
    }
  else
    {
      uint8_t minprio;
      int i;

      /* Otherwise, find the CPU that is executing the lowest priority task
       * (possibly its IDLE task).
       */

      rtcb     = NULL;
      minprio  = SCHED_PRIORITY_MAX;
      cpu      = 0;

      for (i = 0; i < CONFIG_SMP_NCPUS; i++)
        {
          FAR struct tcb_s *candidate =
            (FAR struct tcb_s *)g_assignedtasks[i].head;

          /* If this thread is executing its IDLE task, the use it.  The
           * IDLE task is always the last task in the assigned task list.
           */

          if (candidate->flink == NULL)
            {
              /* The IDLE task should always be assigned to this CPU and
               * have a priority of zero.
               */

              DEBUGASSERT(candidate->sched_priority == 0);

              rtcb = candidate;
              cpu  = i;
              break;
            }
          else if (candidate->sched_priority < minprio)
            {
              DEBUGASSERT(candidate->sched_priority > 0);

              rtcb = candidate;
              cpu  = i;
            }
        }
    }

  /* Determine the desired new task state.  First, if the new task priority
   * is higher then the priority of the lowest priority, running task, then
   * the new task will be running and a context switch switch will be required.
   */

  if (rtcb->sched_priority < btcb->sched_priority)
    {
      task_state = TSTATE_TASK_RUNNING;
    }

  /* If it will not be running, but is assigned to a CPU, then it will be in
   * the asssigned state.
   */

  else if ((btcb->flags & TCB_FLAG_CPU_ASSIGNED) != 0)
    {
      task_state = TSTATE_TASK_ASSIGNED;
      cpu = btcb->cpu;
    }

  /* Otherwise, it will be ready-to-run, but not not yet running */

  else
    {
      task_state = TSTATE_TASK_READYTORUN;
      cpu = 0;  /* CPU does not matter */
    }

  /* If the selected state is TSTATE_TASK_RUNNING, then we would like to
   * start running the task.  Be we cannot do that if pre-emption is disable.
   */

  if (spin_islocked(g_cpu_schedlock) && task_state == TSTATE_TASK_RUNNING)
    {
      /* Preemption would occur!  Add the new ready-to-run task to the
       * g_pendingtasks task list for now.
       */

      sched_addprioritized(btcb, (FAR dq_queue_t *)&g_pendingtasks);
      btcb->task_state = TSTATE_TASK_PENDING;
      doswitch = false;
    }
  else if (task_state == TSTATE_TASK_READYTORUN)
    {
      /* The new btcb was added either (1) in the middle of the assigned
       * task list (the btcb->cpu field is already valid) or (2) was
       * added to the ready-to-run list (the btcb->cpu field does not
       * matter).  Either way, it won't be running.
       *
       * Add the task to the ready-to-run (but not running) task list
       */

      (void)sched_addprioritized(btcb, (FAR dq_queue_t *)&g_readytorun);

       btcb->task_state = TSTATE_TASK_READYTORUN;
       doswitch         = false;
    }
  else /* (task_state == TSTATE_TASK_ASSIGNED || task_state == TSTATE_TASK_RUNNING) */
    {
      int me = this_cpu();

      /* If we are modifying some assigned task list other than our own, we will
       * need to stop that CPU.
       */

      if (cpu != me)
        {
          DEBUGVERIFY(up_cpustop(cpu));
        }

      /* Add the task to the list corresponding to the selected state
       * and check if a context switch will occur
       */

      tasklist = (FAR dq_queue_t *)g_assignedtasks[cpu].head;
      switched = sched_addprioritized(btcb, tasklist);

      /* If the selected task was the g_assignedtasks[] list, then a context
       * swith will occur.
       */

      if (switched)
        {
          /* The new btcb was added at the head of the ready-to-run list.  It
           * is now the new active task!
           *
           * Inform the instrumentation logic that we are switching tasks.
           */

          sched_note_switch(rtcb, btcb);

          /* Assign the CPU and set the running state */

          DEBUGASSERT(task_state == TSTATE_TASK_RUNNING);

          btcb->cpu        = cpu;
          btcb->task_state = TSTATE_TASK_RUNNING;

          /* Adjust global pre-emption controls. */

          if (btcb->lockcount > 0)
            {
              g_cpu_lockset  |= (1 << cpu);
              g_cpu_schedlock = SP_LOCKED;
            }
          else
            {
              g_cpu_lockset &= ~(1 << cpu);
              if (g_cpu_lockset == 0)
                {
                  g_cpu_schedlock = SP_UNLOCKED;
                }
            }

          /* If the following task is not assigned to this CPU, then it must
           * be moved to the g_readytorun list.  Since it cannot be at the
           * head of the list, we can do this without invoking any heavy
           * lifting machinery.
           */

          next = (FAR struct tcb_s *)btcb->flink;
          ASSERT(!rtcb->lockcount && next != NULL);

          if ((btcb->flags & TCB_FLAG_CPU_ASSIGNED) != 0)
            {
              next->task_state = TSTATE_TASK_ASSIGNED;
            }
          else
            {
              /* Remove the task from the assigned task list */

              dq_rem((FAR dq_entry_t *)next, tasklist);

              /* Add the task to the g_readytorun list.  It may be
               * assigned to a different CPU the next time that it runs.
               */

              next->task_state = TSTATE_TASK_READYTORUN;
              (void)sched_addprioritized(btcb,
                                         (FAR dq_queue_t *)&g_readytorun);
            }

          doswitch = true;
        }
      else
        {
          /* No context switch.  Assign the CPU and set the assigned state */

          DEBUGASSERT(task_state == TSTATE_TASK_ASSIGNED);

          btcb->cpu        = cpu;
          btcb->task_state = TSTATE_TASK_ASSIGNED;
        }

      /* All done, restart the other that CPU. */

      if (cpu != me)
        {
          DEBUGVERIFY(up_cpurestart(cpu));
          doswitch = false;
        }
    }

  return doswitch;
}

#endif /* CONFIG_SMP */
