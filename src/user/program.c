// Programme utilisateur minimal pour tester l'execution en ring 3.
// L'objectif est de faire quelque chose de simple et stable : ne rien faire, sauf hlt.

__attribute__((section(".text.entry")))
void user_main(void)
{
    volatile int a = 1;
    volatile int b = 0;
    volatile int x = a / b;
    (void)x;

    for (;;) {}
}
