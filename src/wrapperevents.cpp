#include "wrapperevents.h"

#include "dmtcp.h"
#include "jassert.h"

namespace dmtcp
{
struct WrapperSubscriber {
  BuiltinPluginId owner;
  WrapperHook hook;
};

static const int MAX_SUBSCRIBERS_PER_EVENT = 8;

static WrapperSubscriber subscribers[WRAPPER_EVENT_COUNT]
                                    [MAX_SUBSCRIBERS_PER_EVENT];
static int subscriberCounts[WRAPPER_EVENT_COUNT];
static DmtcpMutex subscriberLock = DMTCP_MUTEX_INITIALIZER_LLL;

static void
assertValidWrapperEvent(WrapperEvent event)
{
  JASSERT(event >= 0 && event < WRAPPER_EVENT_COUNT) (event)
  .Text("Invalid wrapper event.");
}

void
registerBuiltinWrapperHook(BuiltinPluginId owner,
                           WrapperEvent event,
                           WrapperHook hook)
{
  assertValidWrapperEvent(event);
  JASSERT(hook != NULL).Text("Invalid wrapper hook.");

  int rc = DmtcpMutexLock(&subscriberLock);
  JASSERT(rc == 0) (rc).Text("Failed to lock wrapper event subscribers.");

  int count = subscriberCounts[event];
  JASSERT(count < MAX_SUBSCRIBERS_PER_EVENT) (event) (count)
  .Text("Too many wrapper event subscribers.");

  subscribers[event][count].owner = owner;
  subscribers[event][count].hook = hook;
  subscriberCounts[event] = count + 1;

  rc = DmtcpMutexUnlock(&subscriberLock);
  JASSERT(rc == 0) (rc).Text("Failed to unlock wrapper event subscribers.");
}

static int
snapshotSubscribers(WrapperEvent event, WrapperSubscriber *snapshot)
{
  int rc = DmtcpMutexLock(&subscriberLock);
  JASSERT(rc == 0) (rc).Text("Failed to lock wrapper event subscribers.");

  int count = subscriberCounts[event];
  for (int i = 0; i < count; i++) {
    snapshot[i] = subscribers[event][i];
  }

  rc = DmtcpMutexUnlock(&subscriberLock);
  JASSERT(rc == 0) (rc).Text("Failed to unlock wrapper event subscribers.");

  return count;
}

void
dispatchWrapperPre(WrapperEvent event, void *ctx)
{
  assertValidWrapperEvent(event);

  WrapperSubscriber snapshot[MAX_SUBSCRIBERS_PER_EVENT];
  int count = snapshotSubscribers(event, snapshot);
  for (int i = 0; i < count; i++) {
    if (builtinPluginEnabled(snapshot[i].owner)) {
      snapshot[i].hook(event, ctx);
    }
  }
}

void
dispatchWrapperPost(WrapperEvent event, void *ctx)
{
  assertValidWrapperEvent(event);

  WrapperSubscriber snapshot[MAX_SUBSCRIBERS_PER_EVENT];
  int count = snapshotSubscribers(event, snapshot);
  for (int i = count - 1; i >= 0; i--) {
    if (builtinPluginEnabled(snapshot[i].owner)) {
      snapshot[i].hook(event, ctx);
    }
  }
}
}
