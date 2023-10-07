# OS Lab 0.5 & Lab 1 

> Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)
>
> [GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## Lab 0.5 练习：使用GDB验证启动流程

> 为了熟悉使用qemu和gdb进行调试工作,使用gdb调试qemu模拟的RISC-V计算机加电开始运行到执行应用程序的第一条指令(即跳转到`0x80200000`)这个阶段的执行过程，说明RISC-V硬件加电后的几条指令在哪里？完成了哪些功能？要求在报告中简要写出练习过程和回答。

RISC-V硬件加电后，首先会执行存储在ROM中的复位代码，复位代码控制CPU跳转到BootLoader(OpenSBI)的加载区域，Bootloader将进行初始化加载操作系统内核并启动操作系统的执行。但实际上，我们的qemu启动过程与之有些不同，可以直接使用qemu提供的接口加载操作系统内核，之后具体分析。

### (一) 复位代码

使用qemu与gdb进行调试工作。在加电后，即将执行的10条汇编指令如下：

![aaac5164294532928ba13524803c9af.png](https://s2.loli.net/2023/09/23/nv4GakPRBVXLb9t.png)

可以看到，在上电后CPU会从`0x00001000`处获取第一条指令。

#### 1. 上电后的第一条指令为什么在0x00001000处？

通过阅读qemu源码来解决为什么上电后的第一条指令在`0x00001000`处这个问题。

在`qemu/target/riscv/cpu_bits.h`中，`DEFAULT_RSTVEC`被宏定义为`0x1000`。

```C
/* Default Reset Vector adress */
#define DEFAULT_RSTVEC      0x1000
```

在`riscv_any_cpu_init`函数中，CPU状态`env`中的复位向量`reset vector`(CPU启动时执行的第一条指令的地址)设置为`DEFAULT_RSTVEC`。

```C
static void riscv_any_cpu_init(Object *obj)
{
    CPURISCVState *env = &RISCV_CPU(obj)->env;
    set_misa(env, RVXLEN | RVI | RVM | RVA | RVF | RVD | RVC | RVU);
    set_priv_version(env, PRIV_VERSION_1_11_0);
    set_resetvec(env, DEFAULT_RSTVEC); // 复位向量设置为`DEFAULT_RSTVEC`
}
```

在`riscv_cpu_reset`函数中，将CPU状态中的程序计数器(`pc`)设置为复位向量(`resetvec`)的值，以指定CPU重新启动时将执行的第一条指令的地址。

```C
static void riscv_cpu_reset(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cpu);
    CPURISCVState *env = &cpu->env;

    mcc->parent_reset(cs);

#ifndef CONFIG_USER_ONLY
    env->priv = PRV_M;
    env->mstatus &= ~(MSTATUS_MIE | MSTATUS_MPRV);
    env->mcause = 0;
    env->pc = env->resetvec; // 将CPU状态中的程序计数器设置为复位向量的值
#endif
    cs->exception_index = EXCP_NONE;
    env->load_res = -1;
    set_default_nan_mode(1, &env->fp_status);
}
```

#### 2. 复位代码

在`qemu/hw/riscv/virt.c`中查看：0x1000 处的内存块为 `VIRT_MROM`，这是一个只读存储器(ROM)。

```c
static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
}
virt_memmap[] = {
    // 省略其他部分
    [VIRT_MROM] =        {     0x1000,       0x11000 }, // ROM，前面是地址，后面是大小
    // 省略其他部分
    [VIRT_DRAM] =        { 0x80000000,           0x0 },
    // 省略其他部分
};
```

由于RAM是易失性存储器，掉电后数据丢失，所以计算机刚加电的时候RAM中没有内容。此时计算机能够执行的只有存储在非易失性存储器(例如固化在主板上的ROM、Flash等)中的指令，所以第一条指令当然是在ROM里。

为了执行ROM中的代码，就需要将ROM中的内容映射到内存上，那么CPU就可以通过地址来访问ROM中的内容。通过下面的代码进行配置，首先是对ROM进行初始化，并这个 ROM 添加到系统内存的内存映射中，接下来向这个内存块中载入数据。

```c
// 初始化名为 mask_rom 的 ROM 内存区域，用于存储只读数据
// "riscv_virt_board.mrom" 是区域的名称，memmap[VIRT_MROM].size 指定了区域大小
// &error_fatal 是错误处理参数
memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                       memmap[VIRT_MROM].size, &error_fatal);

// 将 mask_rom 区域添加到系统内存区域 system_memory 中
// memmap[VIRT_MROM].base 指定了添加的位置
memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                            mask_rom);

// 向 ROM 区域中添加一个名为 "mrom.reset" 的固定二进制数据块
// reset_vec 包含了初始化代码，sizeof(reset_vec) 表示数据块大小
// memmap[VIRT_MROM].base 指定了数据块的存储位置
// &address_space_memory 表示地址空间的指针
rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                      memmap[VIRT_MROM].base, &address_space_memory);

```

从中我们可以分析得出，在这块初始的ROM中载入的内容为`reset_vec`，查看它的内容：

```c
	uint32_t reset_vec[8] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                  /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        0x00000000,
        memmap[VIRT_DRAM].base,      /* start: .dword memmap[VIRT_DRAM].base */
        0x00000000,
                                     /* dtb: */
    };
```

QEMU 会将这一段代码写入内存地址 `0x1000` 处，并在虚拟 CPU 启动时执行。

我们看到，代码的最后跳转到`0x80000`000。

我们看一下固件加载的代码，在`qemu/hw/riscv/virt.c`中：

    riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                 memmap[VIRT_DRAM].base);

` memmap[VIRT_DRAM].base`即为`0x80000000`；BIOS_FILENAME就是OpenSBI，所以OpenSBI被加载到了`0x8000`0000处。

```c
#if defined(TARGET_RISCV32)
# define BIOS_FILENAME "opensbi-riscv32-virt-fw_jump.bin"
#else
# define BIOS_FILENAME "opensbi-riscv64-virt-fw_jump.bin"
#endif
```

所以，通过以上步骤，我们跳转到了OpenSBI.bin的加载位置`0x80000000`，将控制权转移给了OpenSBI。

这与gdb调试过程中看到的启动时执行的代码一致：

![image.png](https://s2.loli.net/2023/09/23/xPB5qLJCwm4bcSO.png)

对这些代码进行详细解读：

```assembly
   0x1000:	auipc	t0,0x0
   # auipc 是 "add upper immediate to pc" 的缩写
   # auipc rd , imm 将立即数imm逻辑左移12位与当前 PC(程序计数器)相加，并将结果存储在目的寄存器中
   # 此处将立即数0x0<<15后与PC相加，并将结果存储在寄存器 t0 中
   0x1004:	addi	a2,t0,32
   # 将寄存器 t0 中的值与一个立即数(这里是32)相加，并将结果存储在寄存器 a2 中
   0x1008:	csrr	a0,mhartid
   # csrr 是 "CSR Read" 的缩写，用于从控制状态寄存器(CSR)中读取值
   # mhartid是 "Machine Hart ID" 的缩写，存储了当前RISC-V多核处理器中运行的每个核心的唯一标识符或编号。每个核心都有一个不同的 mhartid 值，通常从0开始递增，以唯一地标识核心。
   # 本程序中，该寄存器值为0；mhartid 为0的核心通常被认为是处理器的主核心(引导核心)，通常负责引导操作系统和初始化系统的各种资源
   # 这条代码读取mhartid控制状态寄存器的值并将结果存储在寄存器 a0 中
   0x100c:	ld	t0,24(t0)
   # ld 是 "load doubleword" 的缩写，用于从内存地址加载一个双字(64位)的数据
   # 从内存地址 (t0 + 24) 处加载一个双字的数据，并将结果存储在寄存器 t0 中
   # t0当前的地址是4096d=0X1000；偏移d=18h，查看(t0 + 32) 处即0X1018处的64位即8位数字，为0X80000000
   0x1010:	jr	t0
   # 将程序控制权转移到寄存器 t0 中存储的地址处执行
   0x1014:	unimp
   0x1016:	unimp
   0x1018:	unimp
   0x101a:	0x8000
   0x101c:	unimp
```

单步调试该代码记录如下，可以发现最终跳转到`0x80000000`处：

![image-20230920122131453.png](https://s2.loli.net/2023/09/23/FKo1l4cRCeZWhOG.png)

### (二) OpenSBI的工作

跳转到`0x80000000`之后，OpenSBI首先进行一些初始化工作。

代码中所加载的bios为`opensbi-riscv64-virt-fw_jump.bin`，说明OpenSBI加载使用的`fw_jump`固件：指定下一引导阶段的跳转地址。意思是，OpenSBI初始化完毕后，可直接跳转到指定的OS加载的地址。

我们在`qemu/roms/opensbi/platform/qemu/virt/config.mk`中查看到指定的跳转地址：

```mk
ifeq ($(PLATFORM_RISCV_XLEN), 32)
  # This needs to be 4MB alligned for 32-bit system
  FW_JUMP_ADDR=0x80400000
else
  # This needs to be 2MB alligned for 64-bit system
  FW_JUMP_ADDR=0x80200000
endif
```

因为我们是riscv64架构的系统，因此跳转地址`FW_JUMP_ADDR`为`0x80200000`。

接下来分析OpenSBI初始化过程的工作：

首先执行`qemu/roms/opensbi/firmware/fw_base.S`中的`_start`函数。

```asm
_start:
	csrr	a6, CSR_MHARTID
	blt	zero, a6, _wait_relocate_copy_done

	la	t0, _load_start
	la	t1, _start
	REG_S	t1, 0(t0)
```

该函数检查当前核心是否为主核心(`mhartid == 0`)，主核心会去做重定位，而其他核心会进入` _wait_relocate_copy_done`等待主核心重定位结束。

我们使用`gdb`调试查看`0x80000000`处的汇编指令，发现与`_start`函数逻辑相同。

![image.png](https://s2.loli.net/2023/09/23/vagVCAjid8NwRPy.png)

简单分析OpenSBI的`fw_base.S`文件源码，发现还进行了内存重定位、清除`BSS`段等操作。

OpenSBI还会进行硬件的初始化。首先调用`qemu/roms/opensbi/lib/sbi/sbi_init.c`中的`sbi_boot_prints`函数，打印启动页面。随后还进行了`timer`、控制台、核间中断等初始化工作。

```c
static void sbi_boot_prints(struct sbi_scratch *scratch, u32 hartid)
{
	char str[64];
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	misa_string(str, sizeof(str));
	sbi_printf("\nOpenSBI v%d.%d (%s %s)\n", OPENSBI_VERSION_MAJOR,
		   OPENSBI_VERSION_MINOR, __DATE__, __TIME__);

	sbi_printf(BANNER);

	/* Platform details */
	sbi_printf("Platform Name          : %s\n", sbi_platform_name(plat));
	sbi_printf("Platform HART Features : RV%d%s\n", misa_xlen(), str);
	sbi_printf("Platform Max HARTs     : %d\n",
		   sbi_platform_hart_count(plat));
	sbi_printf("Current Hart           : %u\n", hartid);
	/* Firmware details */
	sbi_printf("Firmware Base          : 0x%lx\n", scratch->fw_start);
	sbi_printf("Firmware Size          : %d KB\n",
		   (u32)(scratch->fw_size / 1024));
	/* Generic details */
	sbi_printf("Runtime SBI Version    : %d.%d\n",
		   sbi_ecall_version_major(), sbi_ecall_version_minor());
	sbi_printf("\n");

    /* PMP */
	sbi_hart_pmp_dump(scratch);
}
```

我们执行`make qemu`中可以验证启动页面的打印：

![image.png](https://s2.loli.net/2023/09/23/Gu9SR2n3zksUTqW.png)

通过前面的步骤，OpenSBI已经完成了底层和设备的初始化工作(实际上，我们阅读源码后对上述过程也是一知半解，希望通过之后的学习能够更加深入理解此过程)。它会跳转到`0x80200000`并将控制权移交给内核镜像。

### (二 plus) 生成和加载内核镜像

为了正确地和上一阶段的 OpenSBI 对接，我们需要保证内核的第一条指令位于物理地址` 0x80200000 `处。我们需要将内核镜像预先加载到 qemu 物理内存以地址 `0x80200000 `开头的区域上。在这里，我们使用qemu提供的接口，借助qemu而非OpenSBI帮助我们加载内核镜像。

因此，我们认为有必要分析内核生成和加载的过程。

查看`lab0/Makefile`，可以得到内核镜像生成步骤如下：

首先编译和链接操作系统内核，生成`elf`文件：

```makefile
# kernel
KINCLUDE	+= kern/debug/ \
			   kern/driver/ \
			   kern/trap/ \
			   kern/libs/\
			   kern/mm/ \
			   kern/arch/
KSRCDIR		+= kern/init \
			   kern/debug \
			   kern/libs \
			   kern/driver \
			   kern/trap \
			   kern/mm
KCFLAGS		+= $(addprefix -I,$(KINCLUDE))
# 将指定目录下的源文件编译成目标文件(.o 文件)，并将这些目标文件添加到 kernel 目标中
$(call add_files_cc,$(call listf_cc,$(KSRCDIR)),kernel,$(KCFLAGS))
# 包含了一些目标文件的列表，这些目标文件是通过编译内核源代码生成的。
KOBJS	= $(call read_packet,kernel libs)

# create kernel target
kernel = $(call totarget,kernel)

$(kernel): tools/kernel.ld
# 链接这些目标文件以生成最终的内核可执行文件
$(kernel): $(KOBJS)
	@echo + ld $@
	$(V)$(LD) $(LDFLAGS) -T tools/kernel.ld -o $@ $(KOBJS)
	@$(OBJDUMP) -S $@ > $(call asmfile,kernel)
	@$(OBJDUMP) -t $@ | $(SED) '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(call symfile,kernel)
# 创建 kernel 目标，以便在Makefile的其他部分可以使用这个目标来构建内核
$(call create_target,kernel)
```

在链接过程中使用了链接脚本`lab0/tools/kernel.ld`：主要工作就是定义了一个入口点，然后进行了`section`的规范布局，是从`0x80200000`开始布局的。

```c
OUTPUT_ARCH(riscv) 
ENTRY(kern_entry)  
/* 指定程序的入口点, 是一个叫做kern_entry的符号*/

BASE_ADDRESS = 0x80200000;/*定义了一个变量BASE_ADDRESS并初始化 */

/*链接脚本剩余的部分是一整条SECTIONS指令，用来指定输出文件的所有SECTION
 "." 是SECTIONS指令内的一个特殊变量/计数器，对应内存里的一个地址。*/
/* 下面省略 */
```

然后，由`elf`文件`kernel`生成一个二进制文件，然后将其保存为`ucore.img`，这就是操作系统内核镜像文件。

```makefile
# $(UCOREIMG): $(kernel)
#	cd ../../riscv-pk && rm -rf build && mkdir build && cd build && ../configure --prefix=$(RISCV) --host=riscv64-unknown-elf --with-payload=../../labcodes/$(PROJ)/$(kernel)  --disable-fp-emulation && make && cp bbl ../../labcodes/$(PROJ)/$(UCOREIMG)

$(UCOREIMG): $(kernel)
	$(OBJCOPY) $(kernel) --strip-all -O binary $@

$(call create_target,ucore.img)
```

之后，启动qemu时，就可以加载指定的操作系统镜像文件。`$(UCOREIMG)` 是操作系统镜像文件的路径，而 `addr=0x80200000`就是加载器要将镜像加载的地址。

```makefile
qemu: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
#	$(V)$(QEMU) -kernel $(UCOREIMG) -nographic
	$(V)$(QEMU) \
		-machine virt \
		-nographic \
		-bios default \
		-device loader,file=$(UCOREIMG),addr=0x80200000
```

具体加载的实现，是在`qemu/hw/riscv/virt.c`的`riscv_virt_board_init`函数中，调用`riscv_load_kernel`函数实现的，该函数尝试通过不同的方式加载指定的内核文件，并在成功加载时返回内核的入口地址。

```c
target_ulong riscv_load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf(kernel_filename, NULL, NULL, NULL,
                 &kernel_entry, NULL, &kernel_high, 0, EM_RISCV, 1, 0) > 0) {
        return kernel_entry;
    }

    if (load_uimage_as(kernel_filename, &kernel_entry, NULL, NULL,
                       NULL, NULL, NULL) > 0) {
        return kernel_entry;
    }

    if (load_image_targphys_as(kernel_filename, KERNEL_BOOT_ADDRESS,
                               ram_size, NULL) > 0) {
        return KERNEL_BOOT_ADDRESS;
    }
    error_report("could not load kernel '%s'", kernel_filename);
    exit(1);
}
```

### (三) 执行内核的指令

跳转至`0x80200000`后，将控制权转移给操作系统，执行内核指令。

在前面链接操作系统内核时，我们指定内核入口点为`ENTRY(kern_entry) `，它在`lab0\kern\init\entry.s`中定义：

```asm
#  从这里开始.text 这个section, "ax" 和 %progbits描述这个section的特征，可分配的("a")，可执行的("x")，并包含数据("@progbits")
.section .text,"ax",%progbits
    .globl kern_entry   # globl使得ld能够看到kern_entry这个符号所在的位置
kern_entry:
    la sp, bootstacktop
    tail kern_init

#开始data section
.section .data
    # .align 2^12
    # 按照2^PGSHIFT进行地址对齐, 也就是对齐到下一页 PGSHIFT在 mmu.h定义
    .align PGSHIFT
    # 内核栈
    .global bootstack
bootstack:
	# 留出KSTACKSIZE这么多个字节的内存
    .space KSTACKSIZE
    # 之后内核栈将要从高地址向低地址增长, 初始时的内核栈为空
    .global bootstacktop
bootstacktop:
```

简单来说，`kern_entry`的第一条指令把`bootstacktop`的地址放入寄存器`sp`中；第二条指令是把`kern_init`的地址放入`pc`，即执行`kern_init`函数。

进入`kern_init`函数：

```c
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    const char *message = "(NKU) os is loading ...\n";
    cprintf("%s\n\n", message);
    while (1)
        ;
}
```

我们通过`gdb`调试，来验证确实能执行内核指令：

在`0x80200000`处打断点，并执行至该断点处：

![image.png](https://s2.loli.net/2023/09/23/bNqcvBsPhUOW6H4.png)

可以看到，`0x8020000`处，执行的第一条指令，就是入口点`kern_entry`的指令。

继续执行，进入`kern_init`函数：

![image.png](https://s2.loli.net/2023/09/23/AFyI3n2P8c1ONBR.png)

查看`kern_init`函数汇编代码：

![image.png](https://s2.loli.net/2023/09/23/9ZrH6Dl8cLkaqsV.png)

继续执行，输出定义的`message`，并最终进入无限循环。

![image.png](https://s2.loli.net/2023/09/23/LsxaYlgVf74iUCt.png)

## Lab 1 练习 1：理解内核启动中的程序入口操作

> 阅读`kern/init/entry.S`内容代码，结合操作系统内核启动流程，说明指令 `la sp, bootstacktop `完成了什么操作，目的是什么？ `tail kern_init `完成了什么操作，目的是什么？

1. `la sp, bootstacktop`指令把`bootstacktop`的地址加载到寄存器`sp`中，从而将栈指针指向了内核的栈顶。该指令的目的是将栈指针初始化为内核栈的顶部，并为后续的函数调用和中断处理提供空间。
2. `tail kern_init` 指令完成了尾调用操作，它用于跳转到 `kern_init` 函数并执行该函数。`kern_init` 函数的调用被标记为尾调用是因为 `tail` 指令告诉编译器，在执行 `kern_init` 函数之后，不需要保留当前函数的调用帧。这是出于性能和栈空间的考虑，因为在这种情况下，不需要保留额外的函数调用信息，因为程序已经完成了，这对于嵌入式系统等资源受限的环境中尤其有用。

## Lab 1 练习 2：完善中断处理 

> 请编程完善`trap.c`中的中断处理函数`trap`，在对时钟中断进行处理的部分填写`kern/trap/trap.c`函数中处理时钟中断的部分，使操作系统每遇到100次时钟中断后，调用`print_ticks`子程序，向屏幕上打印一行文字”100 ticks”，在打印完10行后调用`sbi.h`中的`shut_down()`函数关机。
>
> 要求完成相关函数实现，提交改进后的源代码包(可以编译执行)，并在实验报告中简要说明实现过程和定时器中断处理的流程。实现要求的部分代码后，运行整个系统，大约每1秒会输出一次”100 ticks”，输出10行。

首先<u>**叙述定时器中断处理的流程**</u>。

我们需要每隔若干时间就发生一次时钟中断，但是OpenSBI提供的接口一次只能设置一个时钟中断事件。因此，我们必须：一开始在时钟中断初始化函数`clock_init()`中设置一个时钟中断，之后每次发生时钟中断的时候，在**相应的中断处理函数中设置下一次的时钟中断**。

### (一) 初始化时钟中断

`clock_init()`用于启用时钟中断并设置第一个时钟中断： 首先，启用了`SIE`寄存器中的时钟中断(`MIP_STIP`)，允许在S-Mode下时钟中断。然后，调用`clock_set_next_event(); `函数设置第一个时钟中断事件。

```c
void clock_init(void) {
    // 启用了SIE寄存器中的时钟中断(MIP_STIP)
    set_csr(sie, MIP_STIP);

    clock_set_next_event(); // 设置第一个时钟中断事件

    // 初始化计数器为0
    ticks = 0;

    cprintf("++ setup timer interrupts\n");
}
```

在`clock_set_next_event(); `函数中，调用`sbi_set_timer`函数，当`time`数值成为`timebase`加上当前时间时，触发一次时钟中断。

- 在QEMU上，时钟的频率是10MHz，每过1s，`rdtime`返回的结果增大10000000；
- 我们设定的`timebase`为100000，因此，每次时钟中断后设置10ms后触发下一次时钟中断。

```c
void clock_set_next_event(void) { 
    sbi_set_timer(get_cycles() + timebase); 
}
```

首先分析如何获取当前时间：

- `time`寄存器是一个同步计数器，从处理器上电开始运行，提供当前的实时时间。
- 32位架构下，把64位的`time`寄存器读到两个32位整数里，然后拼起来形成一个64位整数；
- 64位架构下，利用`rdtime`指令，读取`time`寄存器的值。

```c
static inline uint64_t get_cycles(void) {
#if __riscv_xlen == 64
    uint64_t n;
    __asm__ __volatile__("rdtime %0" : "=r"(n));
    return n;
#else
    uint32_t lo, hi, tmp;
    __asm__ __volatile__(
        "1:\n"
        "rdtimeh %0\n"
        "rdtime %1\n"
        "rdtimeh %2\n"
        "bne %0, %2, 1b"
        # 两次读取高位并比较，确保获取的64位时钟周期值在两次获取高32位之间没有发生变化
        : "=&r"(hi), "=&r"(lo), "=&r"(tmp));
    return ((uint64_t)hi << 32) | lo;
#endif
}
```

然后具体分析`sbi_set_timer`函数的实现：

我们查阅了相关资料，S-Mode并不直接时钟中断，而是通过`ecall`指令请求M-Mode设置定时器。也就是说，通过`ecall`触发一个` ecall-from-s-mode-exception`，从而请求M-Mode设置定时器。

```c
void sbi_set_timer(unsigned long long stime_value) {
    sbi_call(SBI_SET_TIMER, stime_value, 0, 0);
}
```

```c
uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t ret_val;
    __asm__ volatile (
        "mv x17, %[sbi_type]\n"
        "mv x10, %[arg0]\n"
        "mv x11, %[arg1]\n"
        "mv x12, %[arg2]\n"
        "ecall\n"
        "mv %[ret_val], x10"
        : [ret_val] "=r" (ret_val)
        : [sbi_type] "r" (sbi_type), [arg0] "r" (arg0), [arg1] "r" (arg1), [arg2] "r" (arg2)
        : "memory"
    );
    return ret_val;
}
```

进入M-Mode的中断后，利用它的`mcall_set_timer`函数来设置定时器。

```c
static uintptr_t mcall_set_timer(uint64_t when)
{
  *HLS()->timecmp = when;
  clear_csr(mip, MIP_STIP);
  set_csr(mie, MIP_MTIP);
  return 0;
}
```

在该函数中，首先设置了定时器的触发时间。

- 我们设定`timecmp` 寄存器为当前时间`time`的值+`timebase`。`timecmp` 的值会与 `time` 寄存器进行比较。当 `time` 值变得大于 `timecmp` 时，就会产生一个定时器中断。也就是，相隔`timebase`触发一次时钟中断。

接下来，清除`mip`中对应的`STIP`位，表示这个中断已经被处理了；并为`mie`中的`MTIP`置位，让下一次时钟中断能够被触发。

### (二) 时钟中断处理

在时钟中断初始化结束后，设置了第一个时钟中断，当到达触发时间时，会触发时钟中断，进入中断处理函数。

- 发生中断后，CPU会跳到`stvec`，我们在`idt_inti()`函数中将`stvec`置为中断入口点`__alltraps`)。
- 进入中断入口点后：
  - 保存所有寄存器到栈顶；
  - 调用中断处理函数`trap`；
  - 处理结束后恢复寄存器。
  - 这一过程会在[Challenge](###Lab-1-Challenge-1：描述与理解中断流程)中具体详细分析，我们此时关注的是在中断处理函数中如何处理时钟中断，因此此处不再赘述。

进入`trap`函数后，对中断处理和异常处理分别调用`interrupt_handler()`和`exception_handler()`。我们触发的时钟中断则是需要调用`interrupt_handler()`函数，属于中断中的`IRQ_S_TIMER`类型。

最后，完成处理，返回到`kern/trap/trapentry.S`。恢复原先的上下文，中断处理结束。

接下来，**<u>说明中断处理具体语句的实现过程</u>**：

- 我们前面提到，每次发生时钟中断的时候，在**相应的中断处理函数中设置下一次的时钟中断**。因此我们需要调用`clock_set_next_event();`函数，设置下一次时钟中断；

- 随后， 定义静态变量`num`，记录打印次数；
- 中断计数器加1；
- 当计数器为100的整数倍时，输出一个`100ticks`表示我们触发了100次时钟中断，同时打印次数`num`加一；
- 当打印次数`num`为10时，调用`sbi_shutdown()`关机。

下面是实现的代码：

```c
case IRQ_S_TIMER:
{
	clock_set_next_event();
	static int num=0;
	if (++ticks % TICK_NUM == 0) {
        print_ticks();
        num++;
    }
	if(num == 10){
        sbi_shutdown();
    }
    break;
}
```

终端运行`make qemu`验证，输出10行`100 ticks`后关机：

![image.png](https://s2.loli.net/2023/09/23/z9pCcjnb3hN1vit.png)

终端运行`make grade`，结果如下：

![image.png](https://s2.loli.net/2023/09/23/4rMDHaVlxWcihUb.png)

## Lab 1 Challenge 1：描述与理解中断流程

> 描述ucore中处理中断异常的流程(从异常的产生开始)，其中`mov a0，sp`的目的是什么？`SAVE_ALL`中寄寄存器保存在栈中的位置是什么确定的？对于任何中断，`_alltraps` 中都需要保存所有寄存器吗？请说明理由。

### (一) ucore中处理中断异常的流程

当触发一个**异常**后，CPU会跳转到中断向量表基址`stvec`。

- 在之前的初始化函数`idt_init`中，我们将`stvec`赋值为`&__alltraps`，也就是 `__alltraps` 函数的地址。

- `trapentry.s`中定义了 `__alltraps` ，这是异常的入口点。

因此，会进入中断入口点`trapentry.S`中执行 `_alltraps`函数。

```asm
 .globl __alltraps
.align(2) #中断入口点 __alltraps必须四字节对齐
__alltraps:
    SAVE_ALL

    move  a0, sp
    jal trap
    # sp should be the same as before "jal trap"
    
    
    #trap函数指向完之后，会回到这里向下继续执行__trapret里面的内容，RESTORE_ALL,sret
    .globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    sret
    # 执行sret从内核态中断返回。
```

`alltraps`函数先执行`SAVE_ALL`操作，将通用寄存器和控制状态寄存器保存至栈顶，实际上相当于把一个`trapframe`结构体放入栈顶。

- 首先将旧的`sp`保存到`sscratch`中；
- 然后保存通用寄存器；
- 在保存完所有通用寄存器后，这些寄存器已可被随意使用，此时读取 `sscratch` (旧的`sp`)到 `s0`，再借`s0`将旧的`sp`保存至内存，再赋 `sscratch = 0`(在内核态中，需要确保 `sscratch` 为 0，出现异常就能知道异常来自内核)。同样的，借 `s1` 至 `s4` 寄存器中转其他需要的 CSR 寄存器至内存中，方便后续读取。

然后，`sp`也就是结构体地址作为参数传递给`trap`函数进行异常处理。

- `trap`函数调用了`trap_dispatch`函数，而`trap_dispatch`函数又根据`tf->cause`将中断处理发给了` interrupt_handler()`，将异常处理的工作分给`exception_handler()`。

- 在这两个函数中，首先将`tf->cause`转化为无符号数，然后根据`tf->cause`的值对应不同的类型，分别进行处理。

`trap`函数的调用结束后，返回到` _alltraps`函数中执行`RESTORE_ALL`的操作，恢复上下文。

- 首先恢复`sstatus` 与 `sepc` 的值；
- 然后恢复除`sp`之外的其他通用寄存器；
- 最后恢复`sp`。

### (二) mov a0,sp的目的是什么？

函数`trap`的定义如下：

```C
void trap(struct trapframe*tf);
```

`a0~a7`寄存器用于存储函数参数。而`trap`函数只有一个参数，是一个`trapframe`类型的指针，如果调用需要将其参数保存到`a0`中。

`sp`是栈顶指针。调用`SAVE_ALL`指令后，`SAVE_ALL`函数将原先的栈顶指针向低地址空间延伸 36个寄存器的空间，即一个`trapFrame`结构体的空间。`sp`指针指向保存上下文后的`trapFrame`结构体，其栈顶地址即为`trapFrame`结构体的地址，可以作为`trap`函数的参数。

`mov a0,sp`后成功的将`trapFrame`结构体的地址`sp`作为参数传递给了 `trap`函数，传递了`trapframe`结构体。

### (三 ) SAVE_ALL中寄存器保存在栈中的位置是什么确定的

我们将通用寄存器和控制状态寄存器保存至栈顶，需要按照`trapFrame `结构体和`pushreg`结构体变量声明的顺序进行排列。这样做，就相当于把`trapframe`结构体放入栈顶，栈顶指针就相当于这个`trapFrame `结构体地址。`trap`函数的参数就可以直接为`sp`。

对于通用寄存器与控制状态寄存器内部的顺序，与`trapFrame` 结构体声明的顺序保持一致。

以控制寄存器为例，在`trapframe`声明中顺序为`sscratch`、`sstatus`、`sepc`、`sbadaddr`、`scause`，那么这五个控制寄存器在栈帧中自顶向下的排列顺序也应如此。

```c
//声明
struct trapframe {
    struct pushregs gpr;
    uintptr_t status; //sstatus
    uintptr_t epc; //sepc
    uintptr_t badvaddr; //sbadvaddr
    uintptr_t cause; //scause
};

//保存
csrrw s0, sscratch, x0
csrr s1, sstatus
csrr s2, sepc
csrr s3, sbadaddr
csrr s4, scause
```

### (四) _alltraps中都需要保存所有寄存器吗？

`_alltraps`需要保存所有的寄存器。

在进行上下文切换时，我们要保存所有当前用到的寄存器，在不同的中断处理中可能会使用到不同的寄存器，如果在保存之前先判断哪些寄存器需要保存，那么将在判断上浪费大量资源，从而减慢切换上下文的速度。

而且为了使`sp`能成为`trap`函数的参数，我们需要在栈中有一个完整的`trapFrame`结构体，该结构体定义中就包括所有寄存器，我们也应当在栈中保存所有寄存器。

## Lab 1 Challenge 2：理解上下文切换机制

> 在`trapentry.S`中汇编代码 `csrw sscratch, sp；csrrw s0, sscratch, x0`实现了什么操作，目的是什么？`save all`里面保存了`stval scause`这些`csr`，而在`restore all`里面却不还原它们？那这样`store`的意义何在呢？

`csrw sscratch, sp `保存旧的栈顶指针`sp`到`sscratch`中；

- 先把旧的`sp`保存起来，因为之后扩张栈帧会修改`sp`的值；
- 不能把`sp`存到通用寄存器里，是因为这些通用寄存器还没保存；
- 我们可以把`sp`保存到未保存的`sscratch`里，是因为我们知道的`sscratch`的值是0。当操作系统处于用户态时，` sscratch` 保存内核栈地址；处于内核态时，`sscratch `为0。因为我们处于S-Mode，在`idt_init()`函数里规定了`write_csr(sscratch, 0)`；
- 在栈扩张后，`sp`指向内核栈， `sscratch` 指向中断前的栈。

`csrrw s0, sscratch, x0`把旧的`sp`从`sscratch`中赋值给`s0`，然后将`sscratch`置为`0`(`x0`寄存器恒为`0`)

- RISCV不能直接从`csr`写入内存，因此需要将`sscratch`写入通用寄存器`s0`；
  - 这是可以的，因为我们之前已经保存完所有的通用寄存器了；
  - 在之后就可以将`s0`保存到`2*REGBYTES(sp)`，保存旧的`sp`；
- 然后将`sscratch`置为`0`，这是因为操作系统处于S-Mode。我们需要将`sscratch`恢复原样，以发挥它指示中断来自什么态的作用。

`save all`里面保存了四个控制状态寄存器，但是只恢复了`sstatus`和`sepc`，没有恢复`sbadaddr`和`scause`。

- 四个状态寄存器都要保存：是因为我们需要在栈中有一个完整的`trapFrame`结构体，该结构体定义中就包括四个状态寄存器，我们也应当在栈中保存四个状态寄存器。
  - `scause`虽然没有恢复，但是保存的意义是为了在中断处理函数中能够访问和修改它。
    - `csr`寄存器不可直接使用条件判断指令，将其保存到栈中，访问和使用更加方便。
    - `trap_dispatch`函数根据`tf->cause`将中断处理发给了` interrupt_handler()`，将异常处理的工作分给`exception_handler()`。
    - 在这两个函数中，首先将`tf->cause`转化为无符号数，然后根据`tf->cause`的值对应不同的类型，分别进行处理。
  - 虽然还未涉及，但是`sbadaddr` 寄存器保存产生异常的虚拟地址，中断处理函数(如缺页异常的处理)中也需要能够访问它。
- 为什么恢复`sepc`：
  - 对于大部分情况，中断处理完成后还回到这个指令继续执行。所以将原先保存的中断位置恢复给`sepc`，然后继续执行。
  - 对于用户主动触发的异常(例如`ebreak`用于触发断点，`ecall`用于系统调用)，中断处理函数需要调整 `sepc` 以跳过这条指令。在我们的中断处理函数中，我们在栈里改变了`tf->epc`的值。在恢复时，就应将改变了的值保存到`sepc`寄存器中。
- 为什么恢复`sstatus`：
  - `sstatus`：位于该寄存器中的`SIE`位，控制设备中断是否开启，如果`SIE`被清0，RISC-V会推迟期间的设备中断，直到`SIE`被再次置位；
  - 在中断处理过程中，为了防止处理中断时不会被其他中断打断，会将`SIE`被清0；
  - 而中断处理结束后，可以继续允许中断，将`SIE`恢复；
  - 因此，需要恢复系统状态为中断处理前的状态。
- 为什么不恢复`scause`和`sbadaddr`：
  - `scause`保存的是中断的原因；`sbadaddr`保存的是产生异常的虚拟地址；
  - 此次中断已经处理结束了，这两个寄存器不会继续发挥作用，因此不用恢复；
  - 如果产生下次中断或异常，新的值直接可以覆盖寄存器。

## Lab1 Challenge 3：完善异常中断

> 编程完善在触发一条非法指令异常` mret`和断点异常`ebreak`，在` kern/trap/trap.c`的异常处理函数中捕获，并对其进行处理，简单输出异常类型和异常指令触发地址，即`Illegal instruction caught at 0x(地址),ebreak caught at 0x(地址)`与`Exception type:Illegal instruction,Exception type: breakpoint`。

### (一) **异常处理代码**

在`kern/trap/trap.c`文件中补充以下代码：

```c#
    case CAUSE_ILLEGAL_INSTRUCTION:
         // 非法指令异常处理
        /*(1)输出指令异常类型( Illegal instruction)
         *(2)输出异常指令地址
         *(3)更新 tf->epc寄存器
        */
        cprintf("Exception type:Illegal instruction\n");
        cprintf("Illegal instruction caught at 0x%08x\n",tf->epc);
        tf->epc+=4;
        break;
    case CAUSE_BREAKPOINT:
        // 断点异常处理
        /*(1)输出指令异常类型( breakpoint)
         *(2)输出异常指令地址
         *(3)更新 tf->epc寄存器
        */
        cprintf("Exception type:breakpoint\n");
        cprintf("ebreak caught at 0x%08x\n",tf->epc);
        tf->epc+=2;            
        break;
```

`cprintf`函数用于输出异常提示信息，包括指令异常类型及异常指令地址。

在`RISC-V64`架构中，`epc`寄存器用于存储引发异常的指令地址，因此可以通过该寄存器输出异常指令地址。

此外，可以通过修改`tf->epc`的值实现跳过异常指令，从下一条指令继续执行，这样可以确保在异常处理结束后，程序能够恢复到正确的执行位置。

- 在非法指令异常的处理中，使用`tf->epc+=4`实现`tf->epc`寄存器的更新，是因为`asm("mret")`指令的长度为4个字节；

- 而在断点异常的处理中，使用`tf->epc+=2`实现`tf->epc`寄存器的更新，是因为`asm("ebreak")`指令的长度为2个字节。

### (二) 增加非法指令

可通过内联汇编的方式加入非法指令：

```c#
asm("mret");  
asm("ebreak");  
```

* 非法指令必须在`idt_init();`之后插入，因为在`idt_init`函数执行之前，处理器并不知道如何处理各种中断和异常。而在`idt_init`函数中，操作系统会为各种中断和异常设置相应的处理程序，并将这些处理程序的入口地址存储在异常向量表中。将 `stvec` 寄存器设置为` __alltraps`函数地址后，一旦发生异常，CPU 可以根据异常向量表中对应的位置所存储的跳转地址，找到相应的异常处理程序，并执行其指令，这样操作系统就可以对各种中断和异常进行自定义的处理。
* 非法指令的执行必须在执行`sbi_shutdown`之前。因为根据练习2的编程，在打印10次的时候，会调用`sbi.h`中的`sbi_shutdown`函数进行关机，若在关机之后执行非法指令，永远无法对异常进行捕获和处理。

### (三) 运行结果

在`kern_init`函数中加入非法指令后，输出以下内容：

```
sbi_emulate_csr_read: hartid0: invalid csr_num=0x302
Exception type:Illegal instruction
Illegal instruction caught at 0x80200048
Exception type:breakpoint
ebreak caught at 0x8020004c
```

![image.png](https://s2.loli.net/2023/09/23/WDYj2ToeLIHQ381.png)

## 实验总结

### **RISCV中的四种特权级**

| Level | Encoding | 全称                       | 简称 |
| ----- | -------- | -------------------------- | ---- |
| 0     | 00       | User/Application           | U    |
| 1     | 01       | Supervisor                 | S    |
| 2     | 10       | Reserved(目前未使用，保留) |      |
| 3     | 11       | Machine                    | M    |

粗略的分类：U-mode是用户程序、应用程序的特权级，S-mode是操作系统内核的特权级，M-mode是固件的特权级。

**对应的OS原理中知识点：x86和MIPS特权级**

x86特权级：

* Ring 0（内核态）：也称为最高特权级别或内核模式。操作系统内核在这个特权级别下执行，具有对系统资源的完全访问权限。内核态可以执行特权指令，并可以更改各种系统设置。

* Ring 1 和 Ring 2：这些特权级别通常未使用，在传统的操作系统中，它们被保留给设备驱动程序和扩展服务程序。相比于 Ring 0，这两个特权级别的权限较低，受到更多限制。

* Ring 3（用户态）：也称为最低特权级别或用户模式。大多数应用程序在这个特权级别下运行。用户态程序无法直接访问底层硬件资源，而是通过系统调用接口来请求操作系统提供服务和资源。

MIPS特权级：

* 内核态：操作系统内核在这个特权级别下执行，具有完全的系统访问权限。内核模式可以执行特权指令，并对所有系统资源具有完全控制权。
* 用户态：大多数应用程序在这个特权级别下运行。用户模式下的程序无法直接访问底层硬件资源，而是通过系统调用接口请求操作系统提供服务和资源。

**知识点之间关系：**

* 相同处
  * 特权级别的概念：RISCV、x86和MIPS都采用了特权级别的概念来区分操作系统内核和用户程序之间的权限和访问控制。
  * 用户程序的限制：在RISCV、x86和MIPS中，用户程序在低特权级别下运行，无法直接访问底层硬件资源，需要通过系统调用接口请求操作系统提供服务和资源。
* 不同处
  * 特权级别数量和命名有差异。
  * 权限划分：RISCV的特权级别划分更加灵活，允许在每个级别上进行自定义的扩展，而x86和MIPS没有类似的灵活性。
  * 多模式支持：RISCV的特权级别可以根据需求支持多种模式，例如支持虚拟化、安全扩展等。而x86和MIPS的特权级别一般用于区分用户态和内核态，并没有额外的模式支持。

### CPU加电后到加载操作系统的流程

RISC-V硬件加电后，首先会执行存储在ROM中的复位代码，复位代码控制CPU跳转到BootLoader的加载区域，然后控制权转移给OpenSBI将CPU加载到操作系统并转移控制权。

### 中断的分类

广义的中断有以下三种分类：

- 异常 (Exception)，指在执行一条指令的过程中发生了错误。例如访问无效内存地址。
- 陷入 (Trap)，指我们主动通过一条指令停下来，并跳转到处理函数。例如syscall和ebreak。
- 外部中断(Intertupt) , 简称中断，指的是 CPU 的执行过程被外设发来的信号打断。例如时钟中断。

### 中断处理的过程

执行流：产生中断或异常 → 跳转到`kern/trap/trapentry.S`的`__alltraps`标记 → 保存当前执行流的上下文，并通过函数调用，切换为`kern/trap/trap.c`的中断处理函数`trap()`的上下文，进入`trap()`的执行流。切换前的上下文作为一个结构体，传递给`trap()`作为函数参数 → `kern/trap/trap.c`按照中断类型进行分发(`trap_dispatch(), interrupt_handler()`) → 根据不同类型分别处理 → 完成处理，返回到`kern/trap/trapentry.S ` → 恢复原先的上下文，中断处理结束。

**对应的OS原理中知识点：进程的上下文切换**

在课堂中我们学习了进程之间的切换要依靠`switch_to`函数进行。`Switch_to`函数有两个参数：`from`是暂停的进程；`to`是要运行的进程，在执行进程切换之前，要将`from`指向的进程中的所有寄存器都保存在内存当中，之后再执行`to`指向的进程。当`to`指向的进程执行完毕后，就恢复上下文，将原本保存的寄存器的值从内存中转存到寄存器中。在存储和保存的过程中要注意对称。

**知识点之间关系：**

* 相同点

  在本实验中 中断处理函数_`alltraps`函数就类似于进程切换的`switch_to`函数。`SAVE_ALL`保存当前的上下文；而在`Switch_to`函数中也保存了当前的上下文 。 `_alltraps`函数调用`trap`函数的过程，从`swtich_to`函数的`from`进程视角来看，就类似于执行`to`进程的作用。在执行完毕后，`RESTORE_ALL`也和`switch_to`函数一样进行了恢复上下文操作。

* 不同点

  在进程切换时，保存的寄存器都可以直接存储在内存中。但是在本实验中，控制状态寄存器无法直接存储到内存中，需要先使用通用寄存器作为中转。因此在本实验中需要特别注意二者的顺序，存储时先存储通用寄存器，再存储控制状态寄存器；读取时先读取控制状态寄存器，再读取通用寄存器。
