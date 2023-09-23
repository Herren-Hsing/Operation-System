
bin/kernel:     file format elf64-littleriscv


Disassembly of section .text:

0000000080200000 <kern_entry>:
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    la sp, bootstacktop
    80200000:	00004117          	auipc	sp,0x4
    80200004:	00010113          	mv	sp,sp

    tail kern_init
    80200008:	0040006f          	j	8020000c <kern_init>

000000008020000c <kern_init>:
void grade_backtrace(void);

// init the kern
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    8020000c:	00004517          	auipc	a0,0x4
    80200010:	00450513          	addi	a0,a0,4 # 80204010 <edata>
    80200014:	00004617          	auipc	a2,0x4
    80200018:	01460613          	addi	a2,a2,20 # 80204028 <end>
int kern_init(void) {
    8020001c:	1141                	addi	sp,sp,-16
    memset(edata, 0, end - edata);
    8020001e:	8e09                	sub	a2,a2,a0
    80200020:	4581                	li	a1,0
int kern_init(void) {
    80200022:	e406                	sd	ra,8(sp)
    memset(edata, 0, end - edata);
    80200024:	245000ef          	jal	ra,80200a68 <memset>

    cons_init();  // init the console
    80200028:	152000ef          	jal	ra,8020017a <cons_init>

    const char *message = "(NKU) os is loading ...\n";
    cprintf("%s\n\n", message);
    8020002c:	00001597          	auipc	a1,0x1
    80200030:	a5458593          	addi	a1,a1,-1452 # 80200a80 <etext+0x6>
    80200034:	00001517          	auipc	a0,0x1
    80200038:	a6c50513          	addi	a0,a0,-1428 # 80200aa0 <etext+0x26>
    8020003c:	036000ef          	jal	ra,80200072 <cprintf>

    print_kerninfo();
    80200040:	066000ef          	jal	ra,802000a6 <print_kerninfo>

    // grade_backtrace();

    idt_init();  // init interrupt descriptor table
    80200044:	146000ef          	jal	ra,8020018a <idt_init>

    // rdtime in mbare mode crashes
    clock_init();  // init clock interrupt
    80200048:	0ee000ef          	jal	ra,80200136 <clock_init>

    intr_enable();  // enable irq interrupt
    8020004c:	138000ef          	jal	ra,80200184 <intr_enable>
    
    asm("mret");  
    80200050:	30200073          	mret
    asm("ebreak");  
    80200054:	9002                	ebreak
    
    while (1)
        ;
    80200056:	a001                	j	80200056 <kern_init+0x4a>

0000000080200058 <cputch>:

/* *
 * cputch - writes a single character @c to stdout, and it will
 * increace the value of counter pointed by @cnt.
 * */
static void cputch(int c, int *cnt) {
    80200058:	1141                	addi	sp,sp,-16
    8020005a:	e022                	sd	s0,0(sp)
    8020005c:	e406                	sd	ra,8(sp)
    8020005e:	842e                	mv	s0,a1
    cons_putc(c);
    80200060:	11c000ef          	jal	ra,8020017c <cons_putc>
    (*cnt)++;
    80200064:	401c                	lw	a5,0(s0)
}
    80200066:	60a2                	ld	ra,8(sp)
    (*cnt)++;
    80200068:	2785                	addiw	a5,a5,1
    8020006a:	c01c                	sw	a5,0(s0)
}
    8020006c:	6402                	ld	s0,0(sp)
    8020006e:	0141                	addi	sp,sp,16
    80200070:	8082                	ret

0000000080200072 <cprintf>:
 * cprintf - formats a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 * */
int cprintf(const char *fmt, ...) {
    80200072:	711d                	addi	sp,sp,-96
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    80200074:	02810313          	addi	t1,sp,40 # 80204028 <end>
int cprintf(const char *fmt, ...) {
    80200078:	f42e                	sd	a1,40(sp)
    8020007a:	f832                	sd	a2,48(sp)
    8020007c:	fc36                	sd	a3,56(sp)
    vprintfmt((void *)cputch, &cnt, fmt, ap);
    8020007e:	862a                	mv	a2,a0
    80200080:	004c                	addi	a1,sp,4
    80200082:	00000517          	auipc	a0,0x0
    80200086:	fd650513          	addi	a0,a0,-42 # 80200058 <cputch>
    8020008a:	869a                	mv	a3,t1
int cprintf(const char *fmt, ...) {
    8020008c:	ec06                	sd	ra,24(sp)
    8020008e:	e0ba                	sd	a4,64(sp)
    80200090:	e4be                	sd	a5,72(sp)
    80200092:	e8c2                	sd	a6,80(sp)
    80200094:	ecc6                	sd	a7,88(sp)
    va_start(ap, fmt);
    80200096:	e41a                	sd	t1,8(sp)
    int cnt = 0;
    80200098:	c202                	sw	zero,4(sp)
    vprintfmt((void *)cputch, &cnt, fmt, ap);
    8020009a:	5c8000ef          	jal	ra,80200662 <vprintfmt>
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}
    8020009e:	60e2                	ld	ra,24(sp)
    802000a0:	4512                	lw	a0,4(sp)
    802000a2:	6125                	addi	sp,sp,96
    802000a4:	8082                	ret

00000000802000a6 <print_kerninfo>:
/* *
 * print_kerninfo - print the information about kernel, including the location
 * of kernel entry, the start addresses of data and text segements, the start
 * address of free memory and how many memory that kernel has used.
 * */
void print_kerninfo(void) {
    802000a6:	1141                	addi	sp,sp,-16
    extern char etext[], edata[], end[], kern_init[];
    cprintf("Special kernel symbols:\n");
    802000a8:	00001517          	auipc	a0,0x1
    802000ac:	a0050513          	addi	a0,a0,-1536 # 80200aa8 <etext+0x2e>
void print_kerninfo(void) {
    802000b0:	e406                	sd	ra,8(sp)
    cprintf("Special kernel symbols:\n");
    802000b2:	fc1ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  entry  0x%016x (virtual)\n", kern_init);
    802000b6:	00000597          	auipc	a1,0x0
    802000ba:	f5658593          	addi	a1,a1,-170 # 8020000c <kern_init>
    802000be:	00001517          	auipc	a0,0x1
    802000c2:	a0a50513          	addi	a0,a0,-1526 # 80200ac8 <etext+0x4e>
    802000c6:	fadff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  etext  0x%016x (virtual)\n", etext);
    802000ca:	00001597          	auipc	a1,0x1
    802000ce:	9b058593          	addi	a1,a1,-1616 # 80200a7a <etext>
    802000d2:	00001517          	auipc	a0,0x1
    802000d6:	a1650513          	addi	a0,a0,-1514 # 80200ae8 <etext+0x6e>
    802000da:	f99ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  edata  0x%016x (virtual)\n", edata);
    802000de:	00004597          	auipc	a1,0x4
    802000e2:	f3258593          	addi	a1,a1,-206 # 80204010 <edata>
    802000e6:	00001517          	auipc	a0,0x1
    802000ea:	a2250513          	addi	a0,a0,-1502 # 80200b08 <etext+0x8e>
    802000ee:	f85ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  end    0x%016x (virtual)\n", end);
    802000f2:	00004597          	auipc	a1,0x4
    802000f6:	f3658593          	addi	a1,a1,-202 # 80204028 <end>
    802000fa:	00001517          	auipc	a0,0x1
    802000fe:	a2e50513          	addi	a0,a0,-1490 # 80200b28 <etext+0xae>
    80200102:	f71ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("Kernel executable memory footprint: %dKB\n",
            (end - kern_init + 1023) / 1024);
    80200106:	00004597          	auipc	a1,0x4
    8020010a:	32158593          	addi	a1,a1,801 # 80204427 <end+0x3ff>
    8020010e:	00000797          	auipc	a5,0x0
    80200112:	efe78793          	addi	a5,a5,-258 # 8020000c <kern_init>
    80200116:	40f587b3          	sub	a5,a1,a5
    cprintf("Kernel executable memory footprint: %dKB\n",
    8020011a:	43f7d593          	srai	a1,a5,0x3f
}
    8020011e:	60a2                	ld	ra,8(sp)
    cprintf("Kernel executable memory footprint: %dKB\n",
    80200120:	3ff5f593          	andi	a1,a1,1023
    80200124:	95be                	add	a1,a1,a5
    80200126:	85a9                	srai	a1,a1,0xa
    80200128:	00001517          	auipc	a0,0x1
    8020012c:	a2050513          	addi	a0,a0,-1504 # 80200b48 <etext+0xce>
}
    80200130:	0141                	addi	sp,sp,16
    cprintf("Kernel executable memory footprint: %dKB\n",
    80200132:	f41ff06f          	j	80200072 <cprintf>

0000000080200136 <clock_init>:

/* *
 * clock_init - initialize 8253 clock to interrupt 100 times per second,
 * and then enable IRQ_TIMER.
 * */
void clock_init(void) {
    80200136:	1141                	addi	sp,sp,-16
    80200138:	e406                	sd	ra,8(sp)
    // enable timer interrupt in sie
    // 使能时钟中断
    set_csr(sie, MIP_STIP);
    8020013a:	02000793          	li	a5,32
    8020013e:	1047a7f3          	csrrs	a5,sie,a5
    __asm__ __volatile__("rdtime %0" : "=r"(n));
    80200142:	c0102573          	rdtime	a0
    ticks = 0;

    cprintf("++ setup timer interrupts\n");
}

void clock_set_next_event(void) { sbi_set_timer(get_cycles() + timebase); }
    80200146:	67e1                	lui	a5,0x18
    80200148:	6a078793          	addi	a5,a5,1696 # 186a0 <BASE_ADDRESS-0x801e7960>
    8020014c:	953e                	add	a0,a0,a5
    8020014e:	0bd000ef          	jal	ra,80200a0a <sbi_set_timer>
}
    80200152:	60a2                	ld	ra,8(sp)
    ticks = 0;
    80200154:	00004797          	auipc	a5,0x4
    80200158:	ec07b623          	sd	zero,-308(a5) # 80204020 <ticks>
    cprintf("++ setup timer interrupts\n");
    8020015c:	00001517          	auipc	a0,0x1
    80200160:	a1c50513          	addi	a0,a0,-1508 # 80200b78 <etext+0xfe>
}
    80200164:	0141                	addi	sp,sp,16
    cprintf("++ setup timer interrupts\n");
    80200166:	f0dff06f          	j	80200072 <cprintf>

000000008020016a <clock_set_next_event>:
    __asm__ __volatile__("rdtime %0" : "=r"(n));
    8020016a:	c0102573          	rdtime	a0
void clock_set_next_event(void) { sbi_set_timer(get_cycles() + timebase); }
    8020016e:	67e1                	lui	a5,0x18
    80200170:	6a078793          	addi	a5,a5,1696 # 186a0 <BASE_ADDRESS-0x801e7960>
    80200174:	953e                	add	a0,a0,a5
    80200176:	0950006f          	j	80200a0a <sbi_set_timer>

000000008020017a <cons_init>:

/* serial_intr - try to feed input characters from serial port */
void serial_intr(void) {}

/* cons_init - initializes the console devices */
void cons_init(void) {}
    8020017a:	8082                	ret

000000008020017c <cons_putc>:

/* cons_putc - print a single character @c to console devices */
void cons_putc(int c) { sbi_console_putchar((unsigned char)c); }
    8020017c:	0ff57513          	andi	a0,a0,255
    80200180:	06f0006f          	j	802009ee <sbi_console_putchar>

0000000080200184 <intr_enable>:
#include <intr.h>
#include <riscv.h>

/* intr_enable - enable irq interrupt */
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }
    80200184:	100167f3          	csrrsi	a5,sstatus,2
    80200188:	8082                	ret

000000008020018a <idt_init>:
void idt_init(void) {
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
       presently executing in the kernel */
    // 如果中断发生在内核态，不需要保存用户态的栈指针信息
    write_csr(sscratch, 0);
    8020018a:	14005073          	csrwi	sscratch,0
    /* Set the exception vector address */
    // 保证__alltraps的地址是四字节对齐的，将__alltraps这个符号的地址直接写到stvec寄存器
    write_csr(stvec, &__alltraps);
    8020018e:	00000797          	auipc	a5,0x0
    80200192:	3b278793          	addi	a5,a5,946 # 80200540 <__alltraps>
    80200196:	10579073          	csrw	stvec,a5
}
    8020019a:	8082                	ret

000000008020019c <print_regs>:
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    8020019c:	610c                	ld	a1,0(a0)
void print_regs(struct pushregs *gpr) {
    8020019e:	1141                	addi	sp,sp,-16
    802001a0:	e022                	sd	s0,0(sp)
    802001a2:	842a                	mv	s0,a0
    cprintf("  zero     0x%08x\n", gpr->zero);
    802001a4:	00001517          	auipc	a0,0x1
    802001a8:	b6450513          	addi	a0,a0,-1180 # 80200d08 <etext+0x28e>
void print_regs(struct pushregs *gpr) {
    802001ac:	e406                	sd	ra,8(sp)
    cprintf("  zero     0x%08x\n", gpr->zero);
    802001ae:	ec5ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  ra       0x%08x\n", gpr->ra);
    802001b2:	640c                	ld	a1,8(s0)
    802001b4:	00001517          	auipc	a0,0x1
    802001b8:	b6c50513          	addi	a0,a0,-1172 # 80200d20 <etext+0x2a6>
    802001bc:	eb7ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  sp       0x%08x\n", gpr->sp);
    802001c0:	680c                	ld	a1,16(s0)
    802001c2:	00001517          	auipc	a0,0x1
    802001c6:	b7650513          	addi	a0,a0,-1162 # 80200d38 <etext+0x2be>
    802001ca:	ea9ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  gp       0x%08x\n", gpr->gp);
    802001ce:	6c0c                	ld	a1,24(s0)
    802001d0:	00001517          	auipc	a0,0x1
    802001d4:	b8050513          	addi	a0,a0,-1152 # 80200d50 <etext+0x2d6>
    802001d8:	e9bff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  tp       0x%08x\n", gpr->tp);
    802001dc:	700c                	ld	a1,32(s0)
    802001de:	00001517          	auipc	a0,0x1
    802001e2:	b8a50513          	addi	a0,a0,-1142 # 80200d68 <etext+0x2ee>
    802001e6:	e8dff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t0       0x%08x\n", gpr->t0);
    802001ea:	740c                	ld	a1,40(s0)
    802001ec:	00001517          	auipc	a0,0x1
    802001f0:	b9450513          	addi	a0,a0,-1132 # 80200d80 <etext+0x306>
    802001f4:	e7fff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t1       0x%08x\n", gpr->t1);
    802001f8:	780c                	ld	a1,48(s0)
    802001fa:	00001517          	auipc	a0,0x1
    802001fe:	b9e50513          	addi	a0,a0,-1122 # 80200d98 <etext+0x31e>
    80200202:	e71ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t2       0x%08x\n", gpr->t2);
    80200206:	7c0c                	ld	a1,56(s0)
    80200208:	00001517          	auipc	a0,0x1
    8020020c:	ba850513          	addi	a0,a0,-1112 # 80200db0 <etext+0x336>
    80200210:	e63ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s0       0x%08x\n", gpr->s0);
    80200214:	602c                	ld	a1,64(s0)
    80200216:	00001517          	auipc	a0,0x1
    8020021a:	bb250513          	addi	a0,a0,-1102 # 80200dc8 <etext+0x34e>
    8020021e:	e55ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s1       0x%08x\n", gpr->s1);
    80200222:	642c                	ld	a1,72(s0)
    80200224:	00001517          	auipc	a0,0x1
    80200228:	bbc50513          	addi	a0,a0,-1092 # 80200de0 <etext+0x366>
    8020022c:	e47ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a0       0x%08x\n", gpr->a0);
    80200230:	682c                	ld	a1,80(s0)
    80200232:	00001517          	auipc	a0,0x1
    80200236:	bc650513          	addi	a0,a0,-1082 # 80200df8 <etext+0x37e>
    8020023a:	e39ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a1       0x%08x\n", gpr->a1);
    8020023e:	6c2c                	ld	a1,88(s0)
    80200240:	00001517          	auipc	a0,0x1
    80200244:	bd050513          	addi	a0,a0,-1072 # 80200e10 <etext+0x396>
    80200248:	e2bff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a2       0x%08x\n", gpr->a2);
    8020024c:	702c                	ld	a1,96(s0)
    8020024e:	00001517          	auipc	a0,0x1
    80200252:	bda50513          	addi	a0,a0,-1062 # 80200e28 <etext+0x3ae>
    80200256:	e1dff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a3       0x%08x\n", gpr->a3);
    8020025a:	742c                	ld	a1,104(s0)
    8020025c:	00001517          	auipc	a0,0x1
    80200260:	be450513          	addi	a0,a0,-1052 # 80200e40 <etext+0x3c6>
    80200264:	e0fff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a4       0x%08x\n", gpr->a4);
    80200268:	782c                	ld	a1,112(s0)
    8020026a:	00001517          	auipc	a0,0x1
    8020026e:	bee50513          	addi	a0,a0,-1042 # 80200e58 <etext+0x3de>
    80200272:	e01ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a5       0x%08x\n", gpr->a5);
    80200276:	7c2c                	ld	a1,120(s0)
    80200278:	00001517          	auipc	a0,0x1
    8020027c:	bf850513          	addi	a0,a0,-1032 # 80200e70 <etext+0x3f6>
    80200280:	df3ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a6       0x%08x\n", gpr->a6);
    80200284:	604c                	ld	a1,128(s0)
    80200286:	00001517          	auipc	a0,0x1
    8020028a:	c0250513          	addi	a0,a0,-1022 # 80200e88 <etext+0x40e>
    8020028e:	de5ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  a7       0x%08x\n", gpr->a7);
    80200292:	644c                	ld	a1,136(s0)
    80200294:	00001517          	auipc	a0,0x1
    80200298:	c0c50513          	addi	a0,a0,-1012 # 80200ea0 <etext+0x426>
    8020029c:	dd7ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s2       0x%08x\n", gpr->s2);
    802002a0:	684c                	ld	a1,144(s0)
    802002a2:	00001517          	auipc	a0,0x1
    802002a6:	c1650513          	addi	a0,a0,-1002 # 80200eb8 <etext+0x43e>
    802002aa:	dc9ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s3       0x%08x\n", gpr->s3);
    802002ae:	6c4c                	ld	a1,152(s0)
    802002b0:	00001517          	auipc	a0,0x1
    802002b4:	c2050513          	addi	a0,a0,-992 # 80200ed0 <etext+0x456>
    802002b8:	dbbff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s4       0x%08x\n", gpr->s4);
    802002bc:	704c                	ld	a1,160(s0)
    802002be:	00001517          	auipc	a0,0x1
    802002c2:	c2a50513          	addi	a0,a0,-982 # 80200ee8 <etext+0x46e>
    802002c6:	dadff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s5       0x%08x\n", gpr->s5);
    802002ca:	744c                	ld	a1,168(s0)
    802002cc:	00001517          	auipc	a0,0x1
    802002d0:	c3450513          	addi	a0,a0,-972 # 80200f00 <etext+0x486>
    802002d4:	d9fff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s6       0x%08x\n", gpr->s6);
    802002d8:	784c                	ld	a1,176(s0)
    802002da:	00001517          	auipc	a0,0x1
    802002de:	c3e50513          	addi	a0,a0,-962 # 80200f18 <etext+0x49e>
    802002e2:	d91ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s7       0x%08x\n", gpr->s7);
    802002e6:	7c4c                	ld	a1,184(s0)
    802002e8:	00001517          	auipc	a0,0x1
    802002ec:	c4850513          	addi	a0,a0,-952 # 80200f30 <etext+0x4b6>
    802002f0:	d83ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s8       0x%08x\n", gpr->s8);
    802002f4:	606c                	ld	a1,192(s0)
    802002f6:	00001517          	auipc	a0,0x1
    802002fa:	c5250513          	addi	a0,a0,-942 # 80200f48 <etext+0x4ce>
    802002fe:	d75ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s9       0x%08x\n", gpr->s9);
    80200302:	646c                	ld	a1,200(s0)
    80200304:	00001517          	auipc	a0,0x1
    80200308:	c5c50513          	addi	a0,a0,-932 # 80200f60 <etext+0x4e6>
    8020030c:	d67ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s10      0x%08x\n", gpr->s10);
    80200310:	686c                	ld	a1,208(s0)
    80200312:	00001517          	auipc	a0,0x1
    80200316:	c6650513          	addi	a0,a0,-922 # 80200f78 <etext+0x4fe>
    8020031a:	d59ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  s11      0x%08x\n", gpr->s11);
    8020031e:	6c6c                	ld	a1,216(s0)
    80200320:	00001517          	auipc	a0,0x1
    80200324:	c7050513          	addi	a0,a0,-912 # 80200f90 <etext+0x516>
    80200328:	d4bff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t3       0x%08x\n", gpr->t3);
    8020032c:	706c                	ld	a1,224(s0)
    8020032e:	00001517          	auipc	a0,0x1
    80200332:	c7a50513          	addi	a0,a0,-902 # 80200fa8 <etext+0x52e>
    80200336:	d3dff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t4       0x%08x\n", gpr->t4);
    8020033a:	746c                	ld	a1,232(s0)
    8020033c:	00001517          	auipc	a0,0x1
    80200340:	c8450513          	addi	a0,a0,-892 # 80200fc0 <etext+0x546>
    80200344:	d2fff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t5       0x%08x\n", gpr->t5);
    80200348:	786c                	ld	a1,240(s0)
    8020034a:	00001517          	auipc	a0,0x1
    8020034e:	c8e50513          	addi	a0,a0,-882 # 80200fd8 <etext+0x55e>
    80200352:	d21ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  t6       0x%08x\n", gpr->t6);
    80200356:	7c6c                	ld	a1,248(s0)
}
    80200358:	6402                	ld	s0,0(sp)
    8020035a:	60a2                	ld	ra,8(sp)
    cprintf("  t6       0x%08x\n", gpr->t6);
    8020035c:	00001517          	auipc	a0,0x1
    80200360:	c9450513          	addi	a0,a0,-876 # 80200ff0 <etext+0x576>
}
    80200364:	0141                	addi	sp,sp,16
    cprintf("  t6       0x%08x\n", gpr->t6);
    80200366:	d0dff06f          	j	80200072 <cprintf>

000000008020036a <print_trapframe>:
void print_trapframe(struct trapframe *tf) {
    8020036a:	1141                	addi	sp,sp,-16
    8020036c:	e022                	sd	s0,0(sp)
    cprintf("trapframe at %p\n", tf);
    8020036e:	85aa                	mv	a1,a0
void print_trapframe(struct trapframe *tf) {
    80200370:	842a                	mv	s0,a0
    cprintf("trapframe at %p\n", tf);
    80200372:	00001517          	auipc	a0,0x1
    80200376:	c9650513          	addi	a0,a0,-874 # 80201008 <etext+0x58e>
void print_trapframe(struct trapframe *tf) {
    8020037a:	e406                	sd	ra,8(sp)
    cprintf("trapframe at %p\n", tf);
    8020037c:	cf7ff0ef          	jal	ra,80200072 <cprintf>
    print_regs(&tf->gpr);
    80200380:	8522                	mv	a0,s0
    80200382:	e1bff0ef          	jal	ra,8020019c <print_regs>
    cprintf("  status   0x%08x\n", tf->status);
    80200386:	10043583          	ld	a1,256(s0)
    8020038a:	00001517          	auipc	a0,0x1
    8020038e:	c9650513          	addi	a0,a0,-874 # 80201020 <etext+0x5a6>
    80200392:	ce1ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  epc      0x%08x\n", tf->epc);
    80200396:	10843583          	ld	a1,264(s0)
    8020039a:	00001517          	auipc	a0,0x1
    8020039e:	c9e50513          	addi	a0,a0,-866 # 80201038 <etext+0x5be>
    802003a2:	cd1ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    802003a6:	11043583          	ld	a1,272(s0)
    802003aa:	00001517          	auipc	a0,0x1
    802003ae:	ca650513          	addi	a0,a0,-858 # 80201050 <etext+0x5d6>
    802003b2:	cc1ff0ef          	jal	ra,80200072 <cprintf>
    cprintf("  cause    0x%08x\n", tf->cause);
    802003b6:	11843583          	ld	a1,280(s0)
}
    802003ba:	6402                	ld	s0,0(sp)
    802003bc:	60a2                	ld	ra,8(sp)
    cprintf("  cause    0x%08x\n", tf->cause);
    802003be:	00001517          	auipc	a0,0x1
    802003c2:	caa50513          	addi	a0,a0,-854 # 80201068 <etext+0x5ee>
}
    802003c6:	0141                	addi	sp,sp,16
    cprintf("  cause    0x%08x\n", tf->cause);
    802003c8:	cabff06f          	j	80200072 <cprintf>

00000000802003cc <interrupt_handler>:

void interrupt_handler(struct trapframe *tf) {
    intptr_t cause = (tf->cause << 1) >> 1;
    802003cc:	11853783          	ld	a5,280(a0)
    802003d0:	577d                	li	a4,-1
    802003d2:	8305                	srli	a4,a4,0x1
    802003d4:	8ff9                	and	a5,a5,a4
    switch (cause) {
    802003d6:	472d                	li	a4,11
    802003d8:	08f76763          	bltu	a4,a5,80200466 <interrupt_handler+0x9a>
    802003dc:	00000717          	auipc	a4,0x0
    802003e0:	7b870713          	addi	a4,a4,1976 # 80200b94 <etext+0x11a>
    802003e4:	078a                	slli	a5,a5,0x2
    802003e6:	97ba                	add	a5,a5,a4
    802003e8:	439c                	lw	a5,0(a5)
    802003ea:	97ba                	add	a5,a5,a4
    802003ec:	8782                	jr	a5
            break;
        case IRQ_H_SOFT:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_SOFT:
            cprintf("Machine software interrupt\n");
    802003ee:	00001517          	auipc	a0,0x1
    802003f2:	8ca50513          	addi	a0,a0,-1846 # 80200cb8 <etext+0x23e>
    802003f6:	c7dff06f          	j	80200072 <cprintf>
            cprintf("Hypervisor software interrupt\n");
    802003fa:	00001517          	auipc	a0,0x1
    802003fe:	89e50513          	addi	a0,a0,-1890 # 80200c98 <etext+0x21e>
    80200402:	c71ff06f          	j	80200072 <cprintf>
            cprintf("User software interrupt\n");
    80200406:	00001517          	auipc	a0,0x1
    8020040a:	85250513          	addi	a0,a0,-1966 # 80200c58 <etext+0x1de>
    8020040e:	c65ff06f          	j	80200072 <cprintf>
            cprintf("Supervisor software interrupt\n");
    80200412:	00001517          	auipc	a0,0x1
    80200416:	86650513          	addi	a0,a0,-1946 # 80200c78 <etext+0x1fe>
    8020041a:	c59ff06f          	j	80200072 <cprintf>
            break;
        case IRQ_U_EXT:
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_EXT:
            cprintf("Supervisor external interrupt\n");
    8020041e:	00001517          	auipc	a0,0x1
    80200422:	8ca50513          	addi	a0,a0,-1846 # 80200ce8 <etext+0x26e>
    80200426:	c4dff06f          	j	80200072 <cprintf>
void interrupt_handler(struct trapframe *tf) {
    8020042a:	1141                	addi	sp,sp,-16
    8020042c:	e406                	sd	ra,8(sp)
            clock_set_next_event();
    8020042e:	d3dff0ef          	jal	ra,8020016a <clock_set_next_event>
            if (++ticks % TICK_NUM == 0) {
    80200432:	00004797          	auipc	a5,0x4
    80200436:	bee78793          	addi	a5,a5,-1042 # 80204020 <ticks>
    8020043a:	639c                	ld	a5,0(a5)
    8020043c:	06400713          	li	a4,100
    80200440:	0785                	addi	a5,a5,1
    80200442:	02e7f733          	remu	a4,a5,a4
    80200446:	00004697          	auipc	a3,0x4
    8020044a:	bcf6bd23          	sd	a5,-1062(a3) # 80204020 <ticks>
    8020044e:	cf11                	beqz	a4,8020046a <interrupt_handler+0x9e>
    80200450:	00004797          	auipc	a5,0x4
    80200454:	bc078793          	addi	a5,a5,-1088 # 80204010 <edata>
    80200458:	4398                	lw	a4,0(a5)
            if(num == 10){
    8020045a:	47a9                	li	a5,10
    8020045c:	02f70b63          	beq	a4,a5,80200492 <interrupt_handler+0xc6>
            break;
        default:
            print_trapframe(tf);
            break;
    }
}
    80200460:	60a2                	ld	ra,8(sp)
    80200462:	0141                	addi	sp,sp,16
    80200464:	8082                	ret
            print_trapframe(tf);
    80200466:	f05ff06f          	j	8020036a <print_trapframe>
    cprintf("%d ticks\n", TICK_NUM);
    8020046a:	06400593          	li	a1,100
    8020046e:	00001517          	auipc	a0,0x1
    80200472:	86a50513          	addi	a0,a0,-1942 # 80200cd8 <etext+0x25e>
    80200476:	bfdff0ef          	jal	ra,80200072 <cprintf>
                num++;
    8020047a:	00004797          	auipc	a5,0x4
    8020047e:	b9678793          	addi	a5,a5,-1130 # 80204010 <edata>
    80200482:	439c                	lw	a5,0(a5)
    80200484:	0017871b          	addiw	a4,a5,1
    80200488:	00004697          	auipc	a3,0x4
    8020048c:	b8e6a423          	sw	a4,-1144(a3) # 80204010 <edata>
    80200490:	b7e9                	j	8020045a <interrupt_handler+0x8e>
}
    80200492:	60a2                	ld	ra,8(sp)
    80200494:	0141                	addi	sp,sp,16
                sbi_shutdown();
    80200496:	5900006f          	j	80200a26 <sbi_shutdown>

000000008020049a <exception_handler>:

void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
    8020049a:	11853783          	ld	a5,280(a0)
    8020049e:	472d                	li	a4,11
    802004a0:	02f76863          	bltu	a4,a5,802004d0 <exception_handler+0x36>
    802004a4:	4705                	li	a4,1
    802004a6:	00f71733          	sll	a4,a4,a5
    802004aa:	6785                	lui	a5,0x1
    802004ac:	17cd                	addi	a5,a5,-13
    802004ae:	8ff9                	and	a5,a5,a4
    802004b0:	ef99                	bnez	a5,802004ce <exception_handler+0x34>
void exception_handler(struct trapframe *tf) {
    802004b2:	1141                	addi	sp,sp,-16
    802004b4:	e022                	sd	s0,0(sp)
    802004b6:	e406                	sd	ra,8(sp)
    802004b8:	00877793          	andi	a5,a4,8
    802004bc:	842a                	mv	s0,a0
    802004be:	e3b1                	bnez	a5,80200502 <exception_handler+0x68>
    802004c0:	8b11                	andi	a4,a4,4
    802004c2:	eb09                	bnez	a4,802004d4 <exception_handler+0x3a>
            break;
        default:
            print_trapframe(tf);
            break;
    }
}
    802004c4:	6402                	ld	s0,0(sp)
    802004c6:	60a2                	ld	ra,8(sp)
    802004c8:	0141                	addi	sp,sp,16
            print_trapframe(tf);
    802004ca:	ea1ff06f          	j	8020036a <print_trapframe>
    802004ce:	8082                	ret
    802004d0:	e9bff06f          	j	8020036a <print_trapframe>
            cprintf("Exception type:Illegal instruction\n");
    802004d4:	00000517          	auipc	a0,0x0
    802004d8:	6f450513          	addi	a0,a0,1780 # 80200bc8 <etext+0x14e>
    802004dc:	b97ff0ef          	jal	ra,80200072 <cprintf>
            cprintf("Illegal instruction caught at 0x%08x\n",tf->epc);
    802004e0:	10843583          	ld	a1,264(s0)
    802004e4:	00000517          	auipc	a0,0x0
    802004e8:	70c50513          	addi	a0,a0,1804 # 80200bf0 <etext+0x176>
    802004ec:	b87ff0ef          	jal	ra,80200072 <cprintf>
            tf->epc+=4;
    802004f0:	10843783          	ld	a5,264(s0)
}
    802004f4:	60a2                	ld	ra,8(sp)
            tf->epc+=4;
    802004f6:	0791                	addi	a5,a5,4
    802004f8:	10f43423          	sd	a5,264(s0)
}
    802004fc:	6402                	ld	s0,0(sp)
    802004fe:	0141                	addi	sp,sp,16
    80200500:	8082                	ret
            cprintf("Exception type:breakpoint\n");
    80200502:	00000517          	auipc	a0,0x0
    80200506:	71650513          	addi	a0,a0,1814 # 80200c18 <etext+0x19e>
    8020050a:	b69ff0ef          	jal	ra,80200072 <cprintf>
            cprintf("ebreak caught at 0x%08x\n",tf->epc);
    8020050e:	10843583          	ld	a1,264(s0)
    80200512:	00000517          	auipc	a0,0x0
    80200516:	72650513          	addi	a0,a0,1830 # 80200c38 <etext+0x1be>
    8020051a:	b59ff0ef          	jal	ra,80200072 <cprintf>
            tf->epc+=2;            
    8020051e:	10843783          	ld	a5,264(s0)
}
    80200522:	60a2                	ld	ra,8(sp)
            tf->epc+=2;            
    80200524:	0789                	addi	a5,a5,2
    80200526:	10f43423          	sd	a5,264(s0)
}
    8020052a:	6402                	ld	s0,0(sp)
    8020052c:	0141                	addi	sp,sp,16
    8020052e:	8082                	ret

0000000080200530 <trap>:

/* trap_dispatch - dispatch based on what type of trap occurred */
static inline void trap_dispatch(struct trapframe *tf) {
    // scause 最高位为1，trap是由中断引起的
    if ((intptr_t)tf->cause < 0) {
    80200530:	11853783          	ld	a5,280(a0)
    80200534:	0007c463          	bltz	a5,8020053c <trap+0xc>
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    80200538:	f63ff06f          	j	8020049a <exception_handler>
        interrupt_handler(tf);
    8020053c:	e91ff06f          	j	802003cc <interrupt_handler>

0000000080200540 <__alltraps>:
    .endm

    .globl __alltraps
.align(2)
__alltraps:
    SAVE_ALL
    80200540:	14011073          	csrw	sscratch,sp
    80200544:	712d                	addi	sp,sp,-288
    80200546:	e002                	sd	zero,0(sp)
    80200548:	e406                	sd	ra,8(sp)
    8020054a:	ec0e                	sd	gp,24(sp)
    8020054c:	f012                	sd	tp,32(sp)
    8020054e:	f416                	sd	t0,40(sp)
    80200550:	f81a                	sd	t1,48(sp)
    80200552:	fc1e                	sd	t2,56(sp)
    80200554:	e0a2                	sd	s0,64(sp)
    80200556:	e4a6                	sd	s1,72(sp)
    80200558:	e8aa                	sd	a0,80(sp)
    8020055a:	ecae                	sd	a1,88(sp)
    8020055c:	f0b2                	sd	a2,96(sp)
    8020055e:	f4b6                	sd	a3,104(sp)
    80200560:	f8ba                	sd	a4,112(sp)
    80200562:	fcbe                	sd	a5,120(sp)
    80200564:	e142                	sd	a6,128(sp)
    80200566:	e546                	sd	a7,136(sp)
    80200568:	e94a                	sd	s2,144(sp)
    8020056a:	ed4e                	sd	s3,152(sp)
    8020056c:	f152                	sd	s4,160(sp)
    8020056e:	f556                	sd	s5,168(sp)
    80200570:	f95a                	sd	s6,176(sp)
    80200572:	fd5e                	sd	s7,184(sp)
    80200574:	e1e2                	sd	s8,192(sp)
    80200576:	e5e6                	sd	s9,200(sp)
    80200578:	e9ea                	sd	s10,208(sp)
    8020057a:	edee                	sd	s11,216(sp)
    8020057c:	f1f2                	sd	t3,224(sp)
    8020057e:	f5f6                	sd	t4,232(sp)
    80200580:	f9fa                	sd	t5,240(sp)
    80200582:	fdfe                	sd	t6,248(sp)
    80200584:	14001473          	csrrw	s0,sscratch,zero
    80200588:	100024f3          	csrr	s1,sstatus
    8020058c:	14102973          	csrr	s2,sepc
    80200590:	143029f3          	csrr	s3,stval
    80200594:	14202a73          	csrr	s4,scause
    80200598:	e822                	sd	s0,16(sp)
    8020059a:	e226                	sd	s1,256(sp)
    8020059c:	e64a                	sd	s2,264(sp)
    8020059e:	ea4e                	sd	s3,272(sp)
    802005a0:	ee52                	sd	s4,280(sp)

    move  a0, sp
    802005a2:	850a                	mv	a0,sp
    jal trap
    802005a4:	f8dff0ef          	jal	ra,80200530 <trap>

00000000802005a8 <__trapret>:
    # sp should be the same as before "jal trap"

    .globl __trapret
__trapret:
    RESTORE_ALL
    802005a8:	6492                	ld	s1,256(sp)
    802005aa:	6932                	ld	s2,264(sp)
    802005ac:	10049073          	csrw	sstatus,s1
    802005b0:	14191073          	csrw	sepc,s2
    802005b4:	60a2                	ld	ra,8(sp)
    802005b6:	61e2                	ld	gp,24(sp)
    802005b8:	7202                	ld	tp,32(sp)
    802005ba:	72a2                	ld	t0,40(sp)
    802005bc:	7342                	ld	t1,48(sp)
    802005be:	73e2                	ld	t2,56(sp)
    802005c0:	6406                	ld	s0,64(sp)
    802005c2:	64a6                	ld	s1,72(sp)
    802005c4:	6546                	ld	a0,80(sp)
    802005c6:	65e6                	ld	a1,88(sp)
    802005c8:	7606                	ld	a2,96(sp)
    802005ca:	76a6                	ld	a3,104(sp)
    802005cc:	7746                	ld	a4,112(sp)
    802005ce:	77e6                	ld	a5,120(sp)
    802005d0:	680a                	ld	a6,128(sp)
    802005d2:	68aa                	ld	a7,136(sp)
    802005d4:	694a                	ld	s2,144(sp)
    802005d6:	69ea                	ld	s3,152(sp)
    802005d8:	7a0a                	ld	s4,160(sp)
    802005da:	7aaa                	ld	s5,168(sp)
    802005dc:	7b4a                	ld	s6,176(sp)
    802005de:	7bea                	ld	s7,184(sp)
    802005e0:	6c0e                	ld	s8,192(sp)
    802005e2:	6cae                	ld	s9,200(sp)
    802005e4:	6d4e                	ld	s10,208(sp)
    802005e6:	6dee                	ld	s11,216(sp)
    802005e8:	7e0e                	ld	t3,224(sp)
    802005ea:	7eae                	ld	t4,232(sp)
    802005ec:	7f4e                	ld	t5,240(sp)
    802005ee:	7fee                	ld	t6,248(sp)
    802005f0:	6142                	ld	sp,16(sp)
    # return from supervisor call
    sret
    802005f2:	10200073          	sret

00000000802005f6 <printnum>:
 * */
static void
printnum(void (*putch)(int, void*), void *putdat,
        unsigned long long num, unsigned base, int width, int padc) {
    unsigned long long result = num;
    unsigned mod = do_div(result, base);
    802005f6:	02069813          	slli	a6,a3,0x20
        unsigned long long num, unsigned base, int width, int padc) {
    802005fa:	7179                	addi	sp,sp,-48
    unsigned mod = do_div(result, base);
    802005fc:	02085813          	srli	a6,a6,0x20
        unsigned long long num, unsigned base, int width, int padc) {
    80200600:	e052                	sd	s4,0(sp)
    unsigned mod = do_div(result, base);
    80200602:	03067a33          	remu	s4,a2,a6
        unsigned long long num, unsigned base, int width, int padc) {
    80200606:	f022                	sd	s0,32(sp)
    80200608:	ec26                	sd	s1,24(sp)
    8020060a:	e84a                	sd	s2,16(sp)
    8020060c:	f406                	sd	ra,40(sp)
    8020060e:	e44e                	sd	s3,8(sp)
    80200610:	84aa                	mv	s1,a0
    80200612:	892e                	mv	s2,a1
    80200614:	fff7041b          	addiw	s0,a4,-1
    unsigned mod = do_div(result, base);
    80200618:	2a01                	sext.w	s4,s4

    // first recursively print all preceding (more significant) digits
    if (num >= base) {
    8020061a:	03067e63          	bleu	a6,a2,80200656 <printnum+0x60>
    8020061e:	89be                	mv	s3,a5
        printnum(putch, putdat, result, base, width - 1, padc);
    } else {
        // print any needed pad characters before first digit
        while (-- width > 0)
    80200620:	00805763          	blez	s0,8020062e <printnum+0x38>
    80200624:	347d                	addiw	s0,s0,-1
            putch(padc, putdat);
    80200626:	85ca                	mv	a1,s2
    80200628:	854e                	mv	a0,s3
    8020062a:	9482                	jalr	s1
        while (-- width > 0)
    8020062c:	fc65                	bnez	s0,80200624 <printnum+0x2e>
    }
    // then print this (the least significant) digit
    putch("0123456789abcdef"[mod], putdat);
    8020062e:	1a02                	slli	s4,s4,0x20
    80200630:	020a5a13          	srli	s4,s4,0x20
    80200634:	00001797          	auipc	a5,0x1
    80200638:	bdc78793          	addi	a5,a5,-1060 # 80201210 <error_string+0x38>
    8020063c:	9a3e                	add	s4,s4,a5
}
    8020063e:	7402                	ld	s0,32(sp)
    putch("0123456789abcdef"[mod], putdat);
    80200640:	000a4503          	lbu	a0,0(s4)
}
    80200644:	70a2                	ld	ra,40(sp)
    80200646:	69a2                	ld	s3,8(sp)
    80200648:	6a02                	ld	s4,0(sp)
    putch("0123456789abcdef"[mod], putdat);
    8020064a:	85ca                	mv	a1,s2
    8020064c:	8326                	mv	t1,s1
}
    8020064e:	6942                	ld	s2,16(sp)
    80200650:	64e2                	ld	s1,24(sp)
    80200652:	6145                	addi	sp,sp,48
    putch("0123456789abcdef"[mod], putdat);
    80200654:	8302                	jr	t1
        printnum(putch, putdat, result, base, width - 1, padc);
    80200656:	03065633          	divu	a2,a2,a6
    8020065a:	8722                	mv	a4,s0
    8020065c:	f9bff0ef          	jal	ra,802005f6 <printnum>
    80200660:	b7f9                	j	8020062e <printnum+0x38>

0000000080200662 <vprintfmt>:
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want printfmt() instead.
 * */
void
vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap) {
    80200662:	7119                	addi	sp,sp,-128
    80200664:	f4a6                	sd	s1,104(sp)
    80200666:	f0ca                	sd	s2,96(sp)
    80200668:	e8d2                	sd	s4,80(sp)
    8020066a:	e4d6                	sd	s5,72(sp)
    8020066c:	e0da                	sd	s6,64(sp)
    8020066e:	fc5e                	sd	s7,56(sp)
    80200670:	f862                	sd	s8,48(sp)
    80200672:	f06a                	sd	s10,32(sp)
    80200674:	fc86                	sd	ra,120(sp)
    80200676:	f8a2                	sd	s0,112(sp)
    80200678:	ecce                	sd	s3,88(sp)
    8020067a:	f466                	sd	s9,40(sp)
    8020067c:	ec6e                	sd	s11,24(sp)
    8020067e:	892a                	mv	s2,a0
    80200680:	84ae                	mv	s1,a1
    80200682:	8d32                	mv	s10,a2
    80200684:	8ab6                	mv	s5,a3
            putch(ch, putdat);
        }

        // Process a %-escape sequence
        char padc = ' ';
        width = precision = -1;
    80200686:	5b7d                	li	s6,-1
        lflag = altflag = 0;

    reswitch:
        switch (ch = *(unsigned char *)fmt ++) {
    80200688:	00001a17          	auipc	s4,0x1
    8020068c:	9f4a0a13          	addi	s4,s4,-1548 # 8020107c <etext+0x602>
                for (width -= strnlen(p, precision); width > 0; width --) {
                    putch(padc, putdat);
                }
            }
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
                if (altflag && (ch < ' ' || ch > '~')) {
    80200690:	05e00b93          	li	s7,94
            if (err > MAXERROR || (p = error_string[err]) == NULL) {
    80200694:	00001c17          	auipc	s8,0x1
    80200698:	b44c0c13          	addi	s8,s8,-1212 # 802011d8 <error_string>
        while ((ch = *(unsigned char *)fmt ++) != '%') {
    8020069c:	000d4503          	lbu	a0,0(s10)
    802006a0:	02500793          	li	a5,37
    802006a4:	001d0413          	addi	s0,s10,1
    802006a8:	00f50e63          	beq	a0,a5,802006c4 <vprintfmt+0x62>
            if (ch == '\0') {
    802006ac:	c521                	beqz	a0,802006f4 <vprintfmt+0x92>
        while ((ch = *(unsigned char *)fmt ++) != '%') {
    802006ae:	02500993          	li	s3,37
    802006b2:	a011                	j	802006b6 <vprintfmt+0x54>
            if (ch == '\0') {
    802006b4:	c121                	beqz	a0,802006f4 <vprintfmt+0x92>
            putch(ch, putdat);
    802006b6:	85a6                	mv	a1,s1
        while ((ch = *(unsigned char *)fmt ++) != '%') {
    802006b8:	0405                	addi	s0,s0,1
            putch(ch, putdat);
    802006ba:	9902                	jalr	s2
        while ((ch = *(unsigned char *)fmt ++) != '%') {
    802006bc:	fff44503          	lbu	a0,-1(s0)
    802006c0:	ff351ae3          	bne	a0,s3,802006b4 <vprintfmt+0x52>
    802006c4:	00044603          	lbu	a2,0(s0)
        char padc = ' ';
    802006c8:	02000793          	li	a5,32
        lflag = altflag = 0;
    802006cc:	4981                	li	s3,0
    802006ce:	4801                	li	a6,0
        width = precision = -1;
    802006d0:	5cfd                	li	s9,-1
    802006d2:	5dfd                	li	s11,-1
        switch (ch = *(unsigned char *)fmt ++) {
    802006d4:	05500593          	li	a1,85
                if (ch < '0' || ch > '9') {
    802006d8:	4525                	li	a0,9
        switch (ch = *(unsigned char *)fmt ++) {
    802006da:	fdd6069b          	addiw	a3,a2,-35
    802006de:	0ff6f693          	andi	a3,a3,255
    802006e2:	00140d13          	addi	s10,s0,1
    802006e6:	20d5e563          	bltu	a1,a3,802008f0 <vprintfmt+0x28e>
    802006ea:	068a                	slli	a3,a3,0x2
    802006ec:	96d2                	add	a3,a3,s4
    802006ee:	4294                	lw	a3,0(a3)
    802006f0:	96d2                	add	a3,a3,s4
    802006f2:	8682                	jr	a3
            for (fmt --; fmt[-1] != '%'; fmt --)
                /* do nothing */;
            break;
        }
    }
}
    802006f4:	70e6                	ld	ra,120(sp)
    802006f6:	7446                	ld	s0,112(sp)
    802006f8:	74a6                	ld	s1,104(sp)
    802006fa:	7906                	ld	s2,96(sp)
    802006fc:	69e6                	ld	s3,88(sp)
    802006fe:	6a46                	ld	s4,80(sp)
    80200700:	6aa6                	ld	s5,72(sp)
    80200702:	6b06                	ld	s6,64(sp)
    80200704:	7be2                	ld	s7,56(sp)
    80200706:	7c42                	ld	s8,48(sp)
    80200708:	7ca2                	ld	s9,40(sp)
    8020070a:	7d02                	ld	s10,32(sp)
    8020070c:	6de2                	ld	s11,24(sp)
    8020070e:	6109                	addi	sp,sp,128
    80200710:	8082                	ret
    if (lflag >= 2) {
    80200712:	4705                	li	a4,1
    80200714:	008a8593          	addi	a1,s5,8
    80200718:	01074463          	blt	a4,a6,80200720 <vprintfmt+0xbe>
    else if (lflag) {
    8020071c:	26080363          	beqz	a6,80200982 <vprintfmt+0x320>
        return va_arg(*ap, unsigned long);
    80200720:	000ab603          	ld	a2,0(s5)
    80200724:	46c1                	li	a3,16
    80200726:	8aae                	mv	s5,a1
    80200728:	a06d                	j	802007d2 <vprintfmt+0x170>
            goto reswitch;
    8020072a:	00144603          	lbu	a2,1(s0)
            altflag = 1;
    8020072e:	4985                	li	s3,1
        switch (ch = *(unsigned char *)fmt ++) {
    80200730:	846a                	mv	s0,s10
            goto reswitch;
    80200732:	b765                	j	802006da <vprintfmt+0x78>
            putch(va_arg(ap, int), putdat);
    80200734:	000aa503          	lw	a0,0(s5)
    80200738:	85a6                	mv	a1,s1
    8020073a:	0aa1                	addi	s5,s5,8
    8020073c:	9902                	jalr	s2
            break;
    8020073e:	bfb9                	j	8020069c <vprintfmt+0x3a>
    if (lflag >= 2) {
    80200740:	4705                	li	a4,1
    80200742:	008a8993          	addi	s3,s5,8
    80200746:	01074463          	blt	a4,a6,8020074e <vprintfmt+0xec>
    else if (lflag) {
    8020074a:	22080463          	beqz	a6,80200972 <vprintfmt+0x310>
        return va_arg(*ap, long);
    8020074e:	000ab403          	ld	s0,0(s5)
            if ((long long)num < 0) {
    80200752:	24044463          	bltz	s0,8020099a <vprintfmt+0x338>
            num = getint(&ap, lflag);
    80200756:	8622                	mv	a2,s0
    80200758:	8ace                	mv	s5,s3
    8020075a:	46a9                	li	a3,10
    8020075c:	a89d                	j	802007d2 <vprintfmt+0x170>
            err = va_arg(ap, int);
    8020075e:	000aa783          	lw	a5,0(s5)
            if (err > MAXERROR || (p = error_string[err]) == NULL) {
    80200762:	4719                	li	a4,6
            err = va_arg(ap, int);
    80200764:	0aa1                	addi	s5,s5,8
            if (err < 0) {
    80200766:	41f7d69b          	sraiw	a3,a5,0x1f
    8020076a:	8fb5                	xor	a5,a5,a3
    8020076c:	40d786bb          	subw	a3,a5,a3
            if (err > MAXERROR || (p = error_string[err]) == NULL) {
    80200770:	1ad74363          	blt	a4,a3,80200916 <vprintfmt+0x2b4>
    80200774:	00369793          	slli	a5,a3,0x3
    80200778:	97e2                	add	a5,a5,s8
    8020077a:	639c                	ld	a5,0(a5)
    8020077c:	18078d63          	beqz	a5,80200916 <vprintfmt+0x2b4>
                printfmt(putch, putdat, "%s", p);
    80200780:	86be                	mv	a3,a5
    80200782:	00001617          	auipc	a2,0x1
    80200786:	b3e60613          	addi	a2,a2,-1218 # 802012c0 <error_string+0xe8>
    8020078a:	85a6                	mv	a1,s1
    8020078c:	854a                	mv	a0,s2
    8020078e:	240000ef          	jal	ra,802009ce <printfmt>
    80200792:	b729                	j	8020069c <vprintfmt+0x3a>
            lflag ++;
    80200794:	00144603          	lbu	a2,1(s0)
    80200798:	2805                	addiw	a6,a6,1
        switch (ch = *(unsigned char *)fmt ++) {
    8020079a:	846a                	mv	s0,s10
            goto reswitch;
    8020079c:	bf3d                	j	802006da <vprintfmt+0x78>
    if (lflag >= 2) {
    8020079e:	4705                	li	a4,1
    802007a0:	008a8593          	addi	a1,s5,8
    802007a4:	01074463          	blt	a4,a6,802007ac <vprintfmt+0x14a>
    else if (lflag) {
    802007a8:	1e080263          	beqz	a6,8020098c <vprintfmt+0x32a>
        return va_arg(*ap, unsigned long);
    802007ac:	000ab603          	ld	a2,0(s5)
    802007b0:	46a1                	li	a3,8
    802007b2:	8aae                	mv	s5,a1
    802007b4:	a839                	j	802007d2 <vprintfmt+0x170>
            putch('0', putdat);
    802007b6:	03000513          	li	a0,48
    802007ba:	85a6                	mv	a1,s1
    802007bc:	e03e                	sd	a5,0(sp)
    802007be:	9902                	jalr	s2
            putch('x', putdat);
    802007c0:	85a6                	mv	a1,s1
    802007c2:	07800513          	li	a0,120
    802007c6:	9902                	jalr	s2
            num = (unsigned long long)va_arg(ap, void *);
    802007c8:	0aa1                	addi	s5,s5,8
    802007ca:	ff8ab603          	ld	a2,-8(s5)
            goto number;
    802007ce:	6782                	ld	a5,0(sp)
    802007d0:	46c1                	li	a3,16
            printnum(putch, putdat, num, base, width, padc);
    802007d2:	876e                	mv	a4,s11
    802007d4:	85a6                	mv	a1,s1
    802007d6:	854a                	mv	a0,s2
    802007d8:	e1fff0ef          	jal	ra,802005f6 <printnum>
            break;
    802007dc:	b5c1                	j	8020069c <vprintfmt+0x3a>
            if ((p = va_arg(ap, char *)) == NULL) {
    802007de:	000ab603          	ld	a2,0(s5)
    802007e2:	0aa1                	addi	s5,s5,8
    802007e4:	1c060663          	beqz	a2,802009b0 <vprintfmt+0x34e>
            if (width > 0 && padc != '-') {
    802007e8:	00160413          	addi	s0,a2,1
    802007ec:	17b05c63          	blez	s11,80200964 <vprintfmt+0x302>
    802007f0:	02d00593          	li	a1,45
    802007f4:	14b79263          	bne	a5,a1,80200938 <vprintfmt+0x2d6>
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
    802007f8:	00064783          	lbu	a5,0(a2)
    802007fc:	0007851b          	sext.w	a0,a5
    80200800:	c905                	beqz	a0,80200830 <vprintfmt+0x1ce>
    80200802:	000cc563          	bltz	s9,8020080c <vprintfmt+0x1aa>
    80200806:	3cfd                	addiw	s9,s9,-1
    80200808:	036c8263          	beq	s9,s6,8020082c <vprintfmt+0x1ca>
                    putch('?', putdat);
    8020080c:	85a6                	mv	a1,s1
                if (altflag && (ch < ' ' || ch > '~')) {
    8020080e:	18098463          	beqz	s3,80200996 <vprintfmt+0x334>
    80200812:	3781                	addiw	a5,a5,-32
    80200814:	18fbf163          	bleu	a5,s7,80200996 <vprintfmt+0x334>
                    putch('?', putdat);
    80200818:	03f00513          	li	a0,63
    8020081c:	9902                	jalr	s2
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
    8020081e:	0405                	addi	s0,s0,1
    80200820:	fff44783          	lbu	a5,-1(s0)
    80200824:	3dfd                	addiw	s11,s11,-1
    80200826:	0007851b          	sext.w	a0,a5
    8020082a:	fd61                	bnez	a0,80200802 <vprintfmt+0x1a0>
            for (; width > 0; width --) {
    8020082c:	e7b058e3          	blez	s11,8020069c <vprintfmt+0x3a>
    80200830:	3dfd                	addiw	s11,s11,-1
                putch(' ', putdat);
    80200832:	85a6                	mv	a1,s1
    80200834:	02000513          	li	a0,32
    80200838:	9902                	jalr	s2
            for (; width > 0; width --) {
    8020083a:	e60d81e3          	beqz	s11,8020069c <vprintfmt+0x3a>
    8020083e:	3dfd                	addiw	s11,s11,-1
                putch(' ', putdat);
    80200840:	85a6                	mv	a1,s1
    80200842:	02000513          	li	a0,32
    80200846:	9902                	jalr	s2
            for (; width > 0; width --) {
    80200848:	fe0d94e3          	bnez	s11,80200830 <vprintfmt+0x1ce>
    8020084c:	bd81                	j	8020069c <vprintfmt+0x3a>
    if (lflag >= 2) {
    8020084e:	4705                	li	a4,1
    80200850:	008a8593          	addi	a1,s5,8
    80200854:	01074463          	blt	a4,a6,8020085c <vprintfmt+0x1fa>
    else if (lflag) {
    80200858:	12080063          	beqz	a6,80200978 <vprintfmt+0x316>
        return va_arg(*ap, unsigned long);
    8020085c:	000ab603          	ld	a2,0(s5)
    80200860:	46a9                	li	a3,10
    80200862:	8aae                	mv	s5,a1
    80200864:	b7bd                	j	802007d2 <vprintfmt+0x170>
    80200866:	00144603          	lbu	a2,1(s0)
            padc = '-';
    8020086a:	02d00793          	li	a5,45
        switch (ch = *(unsigned char *)fmt ++) {
    8020086e:	846a                	mv	s0,s10
    80200870:	b5ad                	j	802006da <vprintfmt+0x78>
            putch(ch, putdat);
    80200872:	85a6                	mv	a1,s1
    80200874:	02500513          	li	a0,37
    80200878:	9902                	jalr	s2
            break;
    8020087a:	b50d                	j	8020069c <vprintfmt+0x3a>
            precision = va_arg(ap, int);
    8020087c:	000aac83          	lw	s9,0(s5)
            goto process_precision;
    80200880:	00144603          	lbu	a2,1(s0)
            precision = va_arg(ap, int);
    80200884:	0aa1                	addi	s5,s5,8
        switch (ch = *(unsigned char *)fmt ++) {
    80200886:	846a                	mv	s0,s10
            if (width < 0)
    80200888:	e40dd9e3          	bgez	s11,802006da <vprintfmt+0x78>
                width = precision, precision = -1;
    8020088c:	8de6                	mv	s11,s9
    8020088e:	5cfd                	li	s9,-1
    80200890:	b5a9                	j	802006da <vprintfmt+0x78>
            goto reswitch;
    80200892:	00144603          	lbu	a2,1(s0)
            padc = '0';
    80200896:	03000793          	li	a5,48
        switch (ch = *(unsigned char *)fmt ++) {
    8020089a:	846a                	mv	s0,s10
            goto reswitch;
    8020089c:	bd3d                	j	802006da <vprintfmt+0x78>
                precision = precision * 10 + ch - '0';
    8020089e:	fd060c9b          	addiw	s9,a2,-48
                ch = *fmt;
    802008a2:	00144603          	lbu	a2,1(s0)
        switch (ch = *(unsigned char *)fmt ++) {
    802008a6:	846a                	mv	s0,s10
                if (ch < '0' || ch > '9') {
    802008a8:	fd06069b          	addiw	a3,a2,-48
                ch = *fmt;
    802008ac:	0006089b          	sext.w	a7,a2
                if (ch < '0' || ch > '9') {
    802008b0:	fcd56ce3          	bltu	a0,a3,80200888 <vprintfmt+0x226>
            for (precision = 0; ; ++ fmt) {
    802008b4:	0405                	addi	s0,s0,1
                precision = precision * 10 + ch - '0';
    802008b6:	002c969b          	slliw	a3,s9,0x2
                ch = *fmt;
    802008ba:	00044603          	lbu	a2,0(s0)
                precision = precision * 10 + ch - '0';
    802008be:	0196873b          	addw	a4,a3,s9
    802008c2:	0017171b          	slliw	a4,a4,0x1
    802008c6:	0117073b          	addw	a4,a4,a7
                if (ch < '0' || ch > '9') {
    802008ca:	fd06069b          	addiw	a3,a2,-48
                precision = precision * 10 + ch - '0';
    802008ce:	fd070c9b          	addiw	s9,a4,-48
                ch = *fmt;
    802008d2:	0006089b          	sext.w	a7,a2
                if (ch < '0' || ch > '9') {
    802008d6:	fcd57fe3          	bleu	a3,a0,802008b4 <vprintfmt+0x252>
    802008da:	b77d                	j	80200888 <vprintfmt+0x226>
            if (width < 0)
    802008dc:	fffdc693          	not	a3,s11
    802008e0:	96fd                	srai	a3,a3,0x3f
    802008e2:	00ddfdb3          	and	s11,s11,a3
    802008e6:	00144603          	lbu	a2,1(s0)
    802008ea:	2d81                	sext.w	s11,s11
        switch (ch = *(unsigned char *)fmt ++) {
    802008ec:	846a                	mv	s0,s10
    802008ee:	b3f5                	j	802006da <vprintfmt+0x78>
            putch('%', putdat);
    802008f0:	85a6                	mv	a1,s1
    802008f2:	02500513          	li	a0,37
    802008f6:	9902                	jalr	s2
            for (fmt --; fmt[-1] != '%'; fmt --)
    802008f8:	fff44703          	lbu	a4,-1(s0)
    802008fc:	02500793          	li	a5,37
    80200900:	8d22                	mv	s10,s0
    80200902:	d8f70de3          	beq	a4,a5,8020069c <vprintfmt+0x3a>
    80200906:	02500713          	li	a4,37
    8020090a:	1d7d                	addi	s10,s10,-1
    8020090c:	fffd4783          	lbu	a5,-1(s10)
    80200910:	fee79de3          	bne	a5,a4,8020090a <vprintfmt+0x2a8>
    80200914:	b361                	j	8020069c <vprintfmt+0x3a>
                printfmt(putch, putdat, "error %d", err);
    80200916:	00001617          	auipc	a2,0x1
    8020091a:	99a60613          	addi	a2,a2,-1638 # 802012b0 <error_string+0xd8>
    8020091e:	85a6                	mv	a1,s1
    80200920:	854a                	mv	a0,s2
    80200922:	0ac000ef          	jal	ra,802009ce <printfmt>
    80200926:	bb9d                	j	8020069c <vprintfmt+0x3a>
                p = "(null)";
    80200928:	00001617          	auipc	a2,0x1
    8020092c:	98060613          	addi	a2,a2,-1664 # 802012a8 <error_string+0xd0>
            if (width > 0 && padc != '-') {
    80200930:	00001417          	auipc	s0,0x1
    80200934:	97940413          	addi	s0,s0,-1671 # 802012a9 <error_string+0xd1>
                for (width -= strnlen(p, precision); width > 0; width --) {
    80200938:	8532                	mv	a0,a2
    8020093a:	85e6                	mv	a1,s9
    8020093c:	e032                	sd	a2,0(sp)
    8020093e:	e43e                	sd	a5,8(sp)
    80200940:	102000ef          	jal	ra,80200a42 <strnlen>
    80200944:	40ad8dbb          	subw	s11,s11,a0
    80200948:	6602                	ld	a2,0(sp)
    8020094a:	01b05d63          	blez	s11,80200964 <vprintfmt+0x302>
    8020094e:	67a2                	ld	a5,8(sp)
    80200950:	2781                	sext.w	a5,a5
    80200952:	e43e                	sd	a5,8(sp)
                    putch(padc, putdat);
    80200954:	6522                	ld	a0,8(sp)
    80200956:	85a6                	mv	a1,s1
    80200958:	e032                	sd	a2,0(sp)
                for (width -= strnlen(p, precision); width > 0; width --) {
    8020095a:	3dfd                	addiw	s11,s11,-1
                    putch(padc, putdat);
    8020095c:	9902                	jalr	s2
                for (width -= strnlen(p, precision); width > 0; width --) {
    8020095e:	6602                	ld	a2,0(sp)
    80200960:	fe0d9ae3          	bnez	s11,80200954 <vprintfmt+0x2f2>
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
    80200964:	00064783          	lbu	a5,0(a2)
    80200968:	0007851b          	sext.w	a0,a5
    8020096c:	e8051be3          	bnez	a0,80200802 <vprintfmt+0x1a0>
    80200970:	b335                	j	8020069c <vprintfmt+0x3a>
        return va_arg(*ap, int);
    80200972:	000aa403          	lw	s0,0(s5)
    80200976:	bbf1                	j	80200752 <vprintfmt+0xf0>
        return va_arg(*ap, unsigned int);
    80200978:	000ae603          	lwu	a2,0(s5)
    8020097c:	46a9                	li	a3,10
    8020097e:	8aae                	mv	s5,a1
    80200980:	bd89                	j	802007d2 <vprintfmt+0x170>
    80200982:	000ae603          	lwu	a2,0(s5)
    80200986:	46c1                	li	a3,16
    80200988:	8aae                	mv	s5,a1
    8020098a:	b5a1                	j	802007d2 <vprintfmt+0x170>
    8020098c:	000ae603          	lwu	a2,0(s5)
    80200990:	46a1                	li	a3,8
    80200992:	8aae                	mv	s5,a1
    80200994:	bd3d                	j	802007d2 <vprintfmt+0x170>
                    putch(ch, putdat);
    80200996:	9902                	jalr	s2
    80200998:	b559                	j	8020081e <vprintfmt+0x1bc>
                putch('-', putdat);
    8020099a:	85a6                	mv	a1,s1
    8020099c:	02d00513          	li	a0,45
    802009a0:	e03e                	sd	a5,0(sp)
    802009a2:	9902                	jalr	s2
                num = -(long long)num;
    802009a4:	8ace                	mv	s5,s3
    802009a6:	40800633          	neg	a2,s0
    802009aa:	46a9                	li	a3,10
    802009ac:	6782                	ld	a5,0(sp)
    802009ae:	b515                	j	802007d2 <vprintfmt+0x170>
            if (width > 0 && padc != '-') {
    802009b0:	01b05663          	blez	s11,802009bc <vprintfmt+0x35a>
    802009b4:	02d00693          	li	a3,45
    802009b8:	f6d798e3          	bne	a5,a3,80200928 <vprintfmt+0x2c6>
    802009bc:	00001417          	auipc	s0,0x1
    802009c0:	8ed40413          	addi	s0,s0,-1811 # 802012a9 <error_string+0xd1>
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
    802009c4:	02800513          	li	a0,40
    802009c8:	02800793          	li	a5,40
    802009cc:	bd1d                	j	80200802 <vprintfmt+0x1a0>

00000000802009ce <printfmt>:
printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...) {
    802009ce:	715d                	addi	sp,sp,-80
    va_start(ap, fmt);
    802009d0:	02810313          	addi	t1,sp,40
printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...) {
    802009d4:	f436                	sd	a3,40(sp)
    vprintfmt(putch, putdat, fmt, ap);
    802009d6:	869a                	mv	a3,t1
printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...) {
    802009d8:	ec06                	sd	ra,24(sp)
    802009da:	f83a                	sd	a4,48(sp)
    802009dc:	fc3e                	sd	a5,56(sp)
    802009de:	e0c2                	sd	a6,64(sp)
    802009e0:	e4c6                	sd	a7,72(sp)
    va_start(ap, fmt);
    802009e2:	e41a                	sd	t1,8(sp)
    vprintfmt(putch, putdat, fmt, ap);
    802009e4:	c7fff0ef          	jal	ra,80200662 <vprintfmt>
}
    802009e8:	60e2                	ld	ra,24(sp)
    802009ea:	6161                	addi	sp,sp,80
    802009ec:	8082                	ret

00000000802009ee <sbi_console_putchar>:

int sbi_console_getchar(void) {
    return sbi_call(SBI_CONSOLE_GETCHAR, 0, 0, 0);
}
void sbi_console_putchar(unsigned char ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
    802009ee:	00003797          	auipc	a5,0x3
    802009f2:	61278793          	addi	a5,a5,1554 # 80204000 <bootstacktop>
    __asm__ volatile (
    802009f6:	6398                	ld	a4,0(a5)
    802009f8:	4781                	li	a5,0
    802009fa:	88ba                	mv	a7,a4
    802009fc:	852a                	mv	a0,a0
    802009fe:	85be                	mv	a1,a5
    80200a00:	863e                	mv	a2,a5
    80200a02:	00000073          	ecall
    80200a06:	87aa                	mv	a5,a0
}
    80200a08:	8082                	ret

0000000080200a0a <sbi_set_timer>:

void sbi_set_timer(unsigned long long stime_value) {
    sbi_call(SBI_SET_TIMER, stime_value, 0, 0);
    80200a0a:	00003797          	auipc	a5,0x3
    80200a0e:	60e78793          	addi	a5,a5,1550 # 80204018 <SBI_SET_TIMER>
    __asm__ volatile (
    80200a12:	6398                	ld	a4,0(a5)
    80200a14:	4781                	li	a5,0
    80200a16:	88ba                	mv	a7,a4
    80200a18:	852a                	mv	a0,a0
    80200a1a:	85be                	mv	a1,a5
    80200a1c:	863e                	mv	a2,a5
    80200a1e:	00000073          	ecall
    80200a22:	87aa                	mv	a5,a0
}
    80200a24:	8082                	ret

0000000080200a26 <sbi_shutdown>:


void sbi_shutdown(void)
{
    sbi_call(SBI_SHUTDOWN,0,0,0);
    80200a26:	00003797          	auipc	a5,0x3
    80200a2a:	5e278793          	addi	a5,a5,1506 # 80204008 <SBI_SHUTDOWN>
    __asm__ volatile (
    80200a2e:	6398                	ld	a4,0(a5)
    80200a30:	4781                	li	a5,0
    80200a32:	88ba                	mv	a7,a4
    80200a34:	853e                	mv	a0,a5
    80200a36:	85be                	mv	a1,a5
    80200a38:	863e                	mv	a2,a5
    80200a3a:	00000073          	ecall
    80200a3e:	87aa                	mv	a5,a0
    80200a40:	8082                	ret

0000000080200a42 <strnlen>:
 * pointed by @s.
 * */
size_t
strnlen(const char *s, size_t len) {
    size_t cnt = 0;
    while (cnt < len && *s ++ != '\0') {
    80200a42:	c185                	beqz	a1,80200a62 <strnlen+0x20>
    80200a44:	00054783          	lbu	a5,0(a0)
    80200a48:	cf89                	beqz	a5,80200a62 <strnlen+0x20>
    size_t cnt = 0;
    80200a4a:	4781                	li	a5,0
    80200a4c:	a021                	j	80200a54 <strnlen+0x12>
    while (cnt < len && *s ++ != '\0') {
    80200a4e:	00074703          	lbu	a4,0(a4)
    80200a52:	c711                	beqz	a4,80200a5e <strnlen+0x1c>
        cnt ++;
    80200a54:	0785                	addi	a5,a5,1
    while (cnt < len && *s ++ != '\0') {
    80200a56:	00f50733          	add	a4,a0,a5
    80200a5a:	fef59ae3          	bne	a1,a5,80200a4e <strnlen+0xc>
    }
    return cnt;
}
    80200a5e:	853e                	mv	a0,a5
    80200a60:	8082                	ret
    size_t cnt = 0;
    80200a62:	4781                	li	a5,0
}
    80200a64:	853e                	mv	a0,a5
    80200a66:	8082                	ret

0000000080200a68 <memset>:
memset(void *s, char c, size_t n) {
#ifdef __HAVE_ARCH_MEMSET
    return __memset(s, c, n);
#else
    char *p = s;
    while (n -- > 0) {
    80200a68:	ca01                	beqz	a2,80200a78 <memset+0x10>
    80200a6a:	962a                	add	a2,a2,a0
    char *p = s;
    80200a6c:	87aa                	mv	a5,a0
        *p ++ = c;
    80200a6e:	0785                	addi	a5,a5,1
    80200a70:	feb78fa3          	sb	a1,-1(a5)
    while (n -- > 0) {
    80200a74:	fec79de3          	bne	a5,a2,80200a6e <memset+0x6>
    }
    return s;
#endif /* __HAVE_ARCH_MEMSET */
}
    80200a78:	8082                	ret
