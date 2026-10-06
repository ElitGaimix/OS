#define MAX_PROCESSES 4
#define USER_BASE   0x400000ULL
#define USER_STACK  (USER_BASE + 0x200000 - 8)   /* haut du bloc de 2 Mio */
#define FRAMES_BASE 0x2000000ULL                 /* 32 Mio : un bloc de 2 Mio par slot */


#include <kernel/process/process.h>

struct program { const char *name; u32 lba; u32 sectors; };

/* Remplace un système de fichiers pour l'instant : nom -> position sur le disque */
static const struct program programs[] = {
    { "crash", 2048, 4 },
    { "hello", 2112, 4 },
};

struct process {
    int   used;
    int   exit_code;
    u64   frame;          /* mémoire physique du processus */
    u64   cr3;
    void *kctx[5];        /* contexte du kernel à restaurer à la sortie */
};

static u64 tables[MAX_PROCESSES][3][512] __attribute__((aligned(4096)));
static struct process procs[MAX_PROCESSES];
static struct process *current;
static u64 kernel_cr3;

void process_init(void)
{
    __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_cr3));
}

static const struct program *find_program(const char *name)
{
    for (unsigned i = 0; i < sizeof(programs) / sizeof(programs[0]); i++)
        if (strcmp(programs[i].name, name) == 0) return &programs[i];
    return 0;
}

static void enter_user(u64 rip, u64 rsp)   /* ton iretq, inchangé */
{
    __asm__ volatile(
        "pushq %0\n pushq %1\n pushq $0x202\n pushq %2\n pushq %3\n iretq\n"
        : : "r"((u64)GDT_USER_DATA), "r"(rsp), "r"((u64)GDT_USER_CODE), "r"(rip)
        : "memory");
    __builtin_unreachable();
}

int process_exec(const char *name)
{
    const struct program *prog = find_program(name);
    if (!prog) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (!procs[i].used) { slot = i; break; }
    if (slot < 0) return -2;

    struct process *p = &procs[slot];
    p->used = 1;
    p->exit_code = 0;
    p->frame = FRAMES_BASE + (u64)slot * 0x200000;

    /* 1. Mémoire du processus : tout à zéro, puis l'image du programme depuis le disque */
    u8 *mem = (u8 *)p->frame;
    for (u64 i = 0; i < 0x200000; i++) mem[i] = 0;
    if (ata_read(prog->lba, prog->sectors, mem) != 0) { p->used = 0; return -3; }

    /* 2. Tables de pages propres au processus */
    u64 *pml4 = tables[slot][0], *pdpt = tables[slot][1], *pd = tables[slot][2];
    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i] = ((u64)i << 21) | 0x83;              /* kernel : identité, sans User */
    }
    pml4[0] = (u64)pdpt | 0x07;
    pdpt[0] = (u64)pd   | 0x07;
    pd[USER_BASE >> 21] = p->frame | 0x87;          /* 0x400000 -> bloc du processus, avec User */
    p->cr3 = (u64)pml4;

    /* 3. On y va ; on revient ici quand process_kill() est appelée */
    current = p;
    if (__builtin_setjmp(p->kctx) == 0) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(p->cr3) : "memory");
        enter_user(USER_BASE, USER_STACK);
    }

    __asm__ volatile("sti");                        /* l'exception avait coupé IF */
    p = current;                                    /* relire : les locals ne sont pas garantis */
    int code = p->exit_code;
    p->used = 0;
    current = 0;
    return code;
}

/* Appelée quand le programme meurt (exception) ou, plus tard, via un appel système exit */
void process_kill(int code)
{
    struct process *p = current;
    p->exit_code = code;
    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_cr3) : "memory");
    __builtin_longjmp(p->kctx, 1);
}