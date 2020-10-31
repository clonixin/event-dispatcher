# Clonixin Generic Event Dispatcher

This project is a first version of an event dispatcher that will be usable in Clonixin Framework.

## WARNING
This project is in an early phase of development. It should not be used in a threaded context, and as such does not
support any async function. This is a planned feature, I'd suggest not to use this if you do need multi-threading 
for the time being.

## Features
##### Data oriented
The data that will be contains in the event **IS** the event. Callbacks are registered using said type, which ensure
proper typing, and easy usage.

## Planned Features
- async support. At the time of writing no step have been take to prevent issue while using this tool in a threaded context.
This will be fixed eventually.
- auto unregistration -> when implemented, a callback will be automatically unregistered upon it's handle destruction.
This shoul ensure data consistency.
