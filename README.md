# Clonixin Event Dispatcher

This header-only library contains two flavor of an event dispatcher: Sync and Async.

## Features
##### Data oriented
The data that will be contains in the event **IS** the event. Callbacks are registered using said type, which ensure
proper typing, and easy usage.

##### Thread Safety
The Async flavor of the dispatcher is thread-safe. Internally, it uses two task queue, one active (being processed) and a passive queue (being filled).
This allow for processing to happens at the same time the next batch is being prepared, without locking your whole software.

## Planned Features
- auto unregistration -> when implemented, a callback will be automatically unregistered upon it's handle destruction.
This should ensure data consistency when the callback is an object. It also won't change current functionalities.

- Being able to disable a single callback.
- Allow a callback to be fired at most x time.
