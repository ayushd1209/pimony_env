# Code rules — read before writing code or working on the sequencer/instructions

- The sequencer and the instructions are Ayush's contribution. Explain the architecture;
  write the gem5 framework plumbing for him and label it "skip".
- Terse code comments: one-liners, not multi-line blocks.
- Faithful model only: no CPU-side magic-state shortcuts, even as throwaways.
