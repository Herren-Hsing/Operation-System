# Operation-System
This GitHub repository contains the coursework for the **Operating System** course at Nankai University during the autumn semester of 2023. 

## Background
The overarching objective of this project is to port the `ucore`  to the 64-bit `RISC-V` instruction set architecture. This task is being undertaken incrementally, with each module constructed from scratch, ultimately resulting in the development of an operating system capable of running basic command-line functions. 
## Collaborators
This collaborative effort is being carried out by a team composed of 焦心雨 [@ChanceuxJiao](https://github.com/ChanceuxJiao) , 李艺楠 [@y99nnn](https://github.com/y99nnn), and 辛浩然 [@Herren-Hsing](https://github.com/Herren-Hsing).

## Lab 0.5

In Lab 0.5, we used gdb to debug the startup process of a RISC-V computer simulated by QEMU, exploring the execution flow of the minimal executable kernel.

Main explorations include: 

- Describing memory layout using linker scripts.
- Cross-compiling to generate executable files and kernel images.
- Using OpenSBI as a bootloader to load the kernel image and simulating with QEMU. 
- Formatting and printing strings on the screen for future debugging using services provided by OpenSBI.

## Lab 1

In Lab 1, we extended the minimal executable kernel to support interrupt mechanism and used a timer interrupt to test our interrupt handling system.

Main explorations include: 

- Understanding the interrupt-related concepts in RISC-V. 
- How to save and restore the context before and after an interrupt. 
- The process of interrupt handling.

In [the relevant folder](https://github.com/Herren-Hsing/Operation-System/tree/main/Lab0.5%26Lab1), we have uploaded the code and experimental report for both Lab 0.5 and Lab 1.

## Lab 2

In Lab2, we gained an understanding of the process of physical memory management and attempted to create a simple physical memory management system ourselves. 

Main explorations include: 

- Understanding the first-fit continuous physical memory allocation algorithm.
- Implementing the Best-Fit continuous physical memory allocation algorithm.
- Implementing the Buddy System allocation algorithm.
- Implementing the Slub allocation algorithm for memory units of arbitrary sizes.

In [the relevant folder](https://github.com/Herren-Hsing/Operation-System/tree/main/Lab2), we have uploaded the code and experimental report for Lab 2.

## Lab 3

In Lab3, we utilized the page table mechanism and the interrupt exception handling mechanism discussed in Lab 1 to complete the implementation of Page Fault exception handling and a partial page replacement algorithm. This, combined with the cache space provided by the disk, allows us to support virtual memory management.

Main explorations include: 

- Understanding the implementation of Page Fault exception handling for virtual memory.
- Understanding the implementation of page replacement algorithms in the operating system.
- Learning how to use multi-level page tables to handle page faults and implement page replacement algorithms.

In [the relevant folder](https://github.com/Herren-Hsing/Operation-System/tree/main/Lab3), we have uploaded the code and experimental report for Lab 3.
