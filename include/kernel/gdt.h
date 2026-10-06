#ifndef KERNEL_GDT_H
#define KERNEL_GDT_H

/* Sélecteurs de segment (index dans la GDT * 8, plus le ring dans les 2 bits de poids faible) */
#define GDT_KERNEL_CODE  0x18          /* index 3, ring 0 */
#define GDT_KERNEL_DATA  0x20          /* index 4, ring 0 */
#define GDT_USER_DATA    (0x28 | 3)    /* index 5, ring 3 -> 0x2B */
#define GDT_USER_CODE    (0x30 | 3)    /* index 6, ring 3 -> 0x33 */
#define GDT_TSS          0x38          /* index 7-8 (16 octets en long mode) */

typedef unsigned long long u64;


/* Construit la GDT du kernel (segments noyau + user + TSS), la charge,
 * recharge les registres de segment et le TSS. A appeler une seule fois au demarrage. */
void gdt_init(void);
void gdt_set_kernel_stack(u64 stack_top);

#endif