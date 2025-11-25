This is tough.

# Store
I guess when we do the switch to a different task, the CPU automatically pushes the 5 most important things (I called the _task scene_) to the Kernel Stack:
- SS
- RSP       (user stack)
- RFLAGS
- CS
- RIP       ('bout-to-execute instr)

Then, I need an assembly code to push the other registers to the stack:
- RAX
- RBX
- RCX
...
- R15

After we've done everything, we know for sure that the current RSP value is where our previous running program was stored, save it somewhere safe. Let's call one item of this whole thing is `struct Task`.

# Restore
We want to restore a program, so we look for the task item in our store, find the RSP value. Update the current CPU's RSP register with that value, and pop all the registers we saved by Assembly code. The remaining are SS, old RSP, RFLAGS, CS, and RIP, but no worries as CPU automatically retrieves these for us.

---

That's how we switch between 2 running programs. How about switching to a brand new program/task?
We've done that before, it's the transition from Ring 0 to Ring 3 (kern to user), in the subroutine `enter_user_mode` in the file `gdt.asm`. We have to create a _fake task scene_, then jump to it iwht `IRETQ` instruction.
