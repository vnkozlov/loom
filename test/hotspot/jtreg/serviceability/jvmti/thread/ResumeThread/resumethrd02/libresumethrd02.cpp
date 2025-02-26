/*
 * Copyright (c) 2003, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <string.h>
#include "jvmti.h"
#include "jvmti_common.h"
#include "jvmti_thread.h"

extern "C" {

/* ============================================================================= */

/* scaffold objects */
static jlong timeout = 0;

/* constant names */
#define THREAD_NAME     "TestedThread"

/* constants */
#define EVENTS_COUNT    1

/* events list */
static jvmtiEvent eventsList[EVENTS_COUNT] = {
    JVMTI_EVENT_THREAD_END
};

static volatile int eventsReceived = 0;
static jthread testedThread = NULL;

/* ============================================================================= */

/** Agent algorithm. */
static void JNICALL
agentProc(jvmtiEnv* jvmti, JNIEnv* jni, void* arg) {

    LOG("Wait for thread to start\n");
    if (!agent_wait_for_sync(timeout))
        return;

    /* perform testing */
    {
        LOG("Find thread: %s\n", THREAD_NAME);
        testedThread = find_thread_by_name(jvmti, jni,THREAD_NAME);
        if (testedThread == NULL) {
          return;
        }
        LOG("  ... found thread: %p\n", (void*)testedThread);

        eventsReceived = 0;
        LOG("Enable event: %s\n", "THREAD_END");
        enable_events_notifications(jvmti, jni, JVMTI_ENABLE, EVENTS_COUNT, eventsList, NULL);

        LOG("Suspend thread: %p\n", (void*)testedThread);
        jvmtiError err = jvmti->SuspendThread(testedThread);
        if (err != JVMTI_ERROR_NONE) {
          set_agent_fail_status();
          return;
        }

        LOG("Let thread to run and finish\n");
        if (!agent_resume_sync())
            return;

        LOG("Resume thread: %p\n", (void*)testedThread);
        err = jvmti->ResumeThread(testedThread);
        if (err != JVMTI_ERROR_NONE) {
          set_agent_fail_status();
          return;
        }

        LOG("Check that THREAD_END event received for timeout: %ld ms\n", (long)timeout);
        {
            jlong delta = 1000;
            jlong time;
            for (time = 0; time < timeout; time += delta) {
                if (eventsReceived > 0)
                    break;
                sleep_sec(delta);
            }
            if (eventsReceived <= 0) {
                COMPLAIN("Thread has not run and finished after resuming\n");
                set_agent_fail_status();
            }
        }

        LOG("Disable event: %s\n", "THREAD_END");
        enable_events_notifications(jvmti, jni,JVMTI_DISABLE, EVENTS_COUNT, eventsList, NULL);

        LOG("Wait for thread to finish\n");
        if (!agent_wait_for_sync(timeout))
            return;

        LOG("Delete thread reference\n");
        jni->DeleteGlobalRef(testedThread);
    }

    LOG("Let debugee to finish\n");
    if (!agent_resume_sync())
        return;
}

/* ============================================================================= */

/** THREAD_END callback. */
JNIEXPORT void JNICALL
callbackThreadEnd(jvmtiEnv* jvmti, JNIEnv* jni, jthread thread) {
    /* check if event is for tested thread */
    if (thread != NULL && jni->IsSameObject(testedThread, thread)) {
        LOG("  ... received THREAD_END event for tested thread: %p\n", (void*)thread);
        eventsReceived++;
    } else {
        LOG("  ... received THREAD_END event for unknown thread: %p\n", (void*)thread);
    }
}

/* ============================================================================= */

/** Agent library initialization. */
jint Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
  jvmtiEnv* jvmti = NULL;

  timeout =  60 * 1000;

  jint res = jvm->GetEnv((void **) &jvmti, JVMTI_VERSION_9);
  if (res != JNI_OK || jvmti == NULL) {
    LOG("Wrong result of a valid call to GetEnv!\n");
    return JNI_ERR;
  }

  /* add specific capabilities for suspending thread */
  {
    jvmtiCapabilities suspendCaps;
    memset(&suspendCaps, 0, sizeof(suspendCaps));
    suspendCaps.can_suspend = 1;
    if (jvmti->AddCapabilities(&suspendCaps) != JVMTI_ERROR_NONE) {
      return JNI_ERR;
    }
  }

  /* set callbacks for THREAD_END event */
  {
    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ThreadEnd = callbackThreadEnd;
    jvmtiError err = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if (err != JVMTI_ERROR_NONE) {
      LOG("(SetEventCallbacks) unexpected error: %s (%d)\n", TranslateError(err), err);
      return JNI_ERR;
    }
  }

  if (init_agent_data(jvmti, &agent_data) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }

  /* register agent proc and arg */
  if (!set_agent_proc(agentProc, NULL)) {
    return JNI_ERR;
  }

  return JNI_OK;
}

/* ============================================================================= */

}
