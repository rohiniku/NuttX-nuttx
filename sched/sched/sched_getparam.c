/****************************************************************************
 * sched/sched/sched_getparam.c
 *
 *   Copyright (C) 2007, 2009, 2015 Gregory Nutt. All rights reserved.
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

#include <sys/types.h>
#include <sched.h>

#include "clock/clock.h"
#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sched_getparam
 *
 * Description:
 *   This function gets the scheduling priority of the task
 *  specified by pid.
 *
 * Inputs:
 *   pid - the task ID of the task.  If pid is zero, the priority
 *     of the calling task is returned.
 *   param - A structure whose member sched_priority is the integer
 *     priority.  The task's priority is copied to the sched_priority
 *     element of this structure.
 *
 * Return Value:
 *    0 (OK) if successful, otherwise -1 (ERROR).
 *
 *    This function can fail if param is null or if pid does
 *    not correspond to any task.
 *
 * Assumptions:
 *
 ****************************************************************************/

int sched_getparam (pid_t pid, FAR struct sched_param *param)
{
  FAR struct tcb_s *rtcb;
  FAR struct tcb_s *tcb;
  int ret = OK;

  if (!param)
    {
      return ERROR;
    }

  /* Check if the task to restart is the calling task */

  rtcb = this_task();
  if ((pid == 0) || (pid == rtcb->pid))
    {
      /* Return the priority if the calling task. */

      param->sched_priority = (int)rtcb->sched_priority;
    }

  /* Ths pid is not for the calling task, we will have to look it up */

  else
    {
      /* Get the TCB associated with this pid */

      sched_lock();
      tcb = sched_gettcb(pid);
      if (!tcb)
        {
          /* This pid does not correspond to any known task */

          ret = ERROR;
        }
      else
        {
#ifdef CONFIG_SCHED_SPORADIC
#endif
          /* Return the priority of the task */

          param->sched_priority = (int)tcb->sched_priority;

#ifdef CONFIG_SCHED_SPORADIC
          if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_SPORADIC)
            {
              FAR struct sporadic_s *sporadic = tcb->sporadic;
              DEBUGASSERT(sporadic != NULL);

              /* Return parameters associated with SCHED_SPORADIC */

              param->sched_ss_low_priority = (int)sporadic->low_priority;
              param->sched_ss_max_repl     = (int)sporadic->max_repl;

              clock_ticks2time((int)sporadic->repl_period, &param->sched_ss_repl_period);
              clock_ticks2time((int)sporadic->budget, &param->sched_ss_init_budget);
            }
          else
            {
              param->sched_ss_low_priority        = 0;
              param->sched_ss_max_repl            = 0;
              param->sched_ss_repl_period.tv_sec  = 0;
              param->sched_ss_repl_period.tv_nsec = 0;
              param->sched_ss_init_budget.tv_sec  = 0;
              param->sched_ss_init_budget.tv_nsec = 0;
            }
#endif
        }

      sched_unlock();
    }

  return ret;
}

