#include "dab_remote_control.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

static dab_remote_command_t  s_pending_command;
static dab_remote_result_t   s_completed_result;
static dab_remote_snapshot_t s_snapshot;
static uint32_t              s_next_ticket = 1U;
static bool                  s_command_pending;
static bool                  s_result_ready;

static void dab_remote_copy_text(char * p_destination,
                                 size_t destination_size,
                                 char const * p_source)
{
    if ((NULL == p_destination) || (0U == destination_size))
    {
        return;
    }

    if (NULL == p_source)
    {
        p_destination[0] = '\0';
        return;
    }

    (void) strncpy(p_destination, p_source, destination_size - 1U);
    p_destination[destination_size - 1U] = '\0';
}

bool dab_remote_command_submit(dab_remote_command_t const * p_command,
                               uint32_t * p_ticket)
{
    if ((NULL == p_command) || (NULL == p_ticket))
    {
        return false;
    }

    bool accepted = false;
    taskENTER_CRITICAL();
    if (!s_command_pending)
    {
        /* A timed-out TCP request can leave a late result behind.  A new
         * single-client request supersedes that stale result so the mailbox
         * cannot remain permanently busy after a disconnect. */
        s_result_ready = false;
        s_pending_command = *p_command;
        s_pending_command.ticket = s_next_ticket++;
        if (0U == s_next_ticket)
        {
            s_next_ticket = 1U;
        }
        *p_ticket = s_pending_command.ticket;
        s_command_pending = true;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

bool dab_remote_command_take(dab_remote_command_t * p_command)
{
    if (NULL == p_command)
    {
        return false;
    }

    bool available = false;
    taskENTER_CRITICAL();
    if (s_command_pending)
    {
        *p_command = s_pending_command;
        s_command_pending = false;
        available = true;
    }
    taskEXIT_CRITICAL();
    return available;
}

void dab_remote_command_complete(uint32_t ticket, bool ok, char const * p_error)
{
    dab_remote_result_t result = {0};
    result.ticket = ticket;
    result.ok = ok;
    dab_remote_copy_text(result.error, sizeof(result.error), p_error);

    taskENTER_CRITICAL();
    s_completed_result = result;
    s_result_ready = true;
    taskEXIT_CRITICAL();
}

bool dab_remote_result_get(uint32_t ticket, dab_remote_result_t * p_result)
{
    if (NULL == p_result)
    {
        return false;
    }

    bool available = false;
    taskENTER_CRITICAL();
    if (s_result_ready && (s_completed_result.ticket == ticket))
    {
        *p_result = s_completed_result;
        s_result_ready = false;
        available = true;
    }
    taskEXIT_CRITICAL();
    return available;
}

void dab_remote_snapshot_publish(dab_remote_snapshot_t const * p_snapshot)
{
    if (NULL == p_snapshot)
    {
        return;
    }

    taskENTER_CRITICAL();
    s_snapshot = *p_snapshot;
    taskEXIT_CRITICAL();
}

void dab_remote_snapshot_get(dab_remote_snapshot_t * p_snapshot)
{
    if (NULL == p_snapshot)
    {
        return;
    }

    taskENTER_CRITICAL();
    *p_snapshot = s_snapshot;
    taskEXIT_CRITICAL();
}
