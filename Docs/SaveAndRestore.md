# Save and Restore State

**Historical design sketch; superseded for this fork (2026-10-16).** The text
below predates the implemented binary snapshots and is not a description of
their format or coverage. Use `save`/`restore` and `diff` in
[A2GSPU_RAILS.md](A2GSPU_RAILS.md), the current
[M5 status](INSTRUMENTATION_SHAPE.md#m5--snapshot-manifests-then-snapshot-classes--p5--s-then-l),
and each file's included/excluded manifest. CPU/MMU/RAM and supported device,
scanner and media blocks exist; session clocks and host connections are not
complete replay state. Expanded observational context snapshots are a separate
artifact described in [MACHINE_CONTEXT.md](MACHINE_CONTEXT.md).

Here is the state we need:

* the platform / machine state (the configuration of the computer)
* cpu state: registers, cycle count
* Each motherboard device state
* Each slot card state
* Contents of memory
* current floppy disk image(s) nib state and file names.

Now, the device and slot card states don't have to be every bit in them. It only needs to be enough to get things going again. This may be a long, difficult journey!

What is NOT saved:
* the memory map - but we do need the state of the soft switches that determine the memory map.

The devices should essentially write instructions to themselves that will cause the state to be restored.

Probably emit the data as json or something like that. Apple2ts basically just dumps the above. What does not seem to be included: mockingboard state?

like what about the memory expansion card.
