# OS

OS est un projet personnel de système d'exploitation expérimental pour PC x86.
Il est écrit principalement en C, avec de l'assembleur NASM pour les premières
étapes du démarrage. Le projet est encore en développement : il sert à explorer
le démarrage d'une machine, le fonctionnement du noyau et les interactions de
bas niveau avec le matériel. Il ne s'agit pas d'un système d'exploitation
complet ou prêt à être utilisé sur une machine réelle.

## État du projet

Le chemin de démarrage actuel utilise le BIOS et charge un petit chargeur
assembleur, puis un chargeur intermédiaire en C. Celui-ci récupère la carte
mémoire E820, accède au disque avec ATA PIO, charge le noyau et passe le
processeur en mode 64 bits.

Le noyau initialise notamment la GDT, l'IDT et la gestion des exceptions. Il
affiche une console texte VGA, reçoit les entrées d'un clavier PS/2 et propose
quelques commandes de test. Le kshell et les programmes utilisateur partagent
maintenant un ordonnanceur round-robin simple, déclenché par le PIT. Le shell
reste une tâche noyau en ring 0 ; le programme `hello` s'exécute en ring 3,
revient au shell lorsqu'il se termine et peut être lancé depuis la console.
Cette gestion de tâches reste expérimentale et ne constitue pas encore un
environnement utilisateur complet.

Le projet est susceptible de changer et peut contenir des fonctionnalités
incomplètes ou temporaires. Il n'est pas destiné à être démarré sur du matériel
réel ni à contenir des données importantes. Les tests décrits ici s'exécutent
dans un émulateur.

## Console du noyau

Une fois le noyau démarré dans QEMU, les commandes actuellement disponibles
sont :

- `test` : affiche un résultat de test simple ;
- `shutdown` : tente d'arrêter l'émulateur ;
- `panic` : affiche les types d'exceptions que l'on peut déclencher avec
  `panic div0`, `panic ud`, `panic bp`, `panic page` ou `panic gpf` ;
- `hello` : ajoute le programme utilisateur à la file d'exécution. Le kshell
  reste actif pendant son exécution et le programme se termine via un appel
  système minimal ;
- `ticker` : affiche huit messages séparés par une attente active. Le syscall
  d'affichage copie une chaîne utilisateur courte et vérifie qu'elle reste
  dans la mémoire mappée du programme ;
- `crash` : lance le même binaire en mode test, qui provoque une exception
  utilisateur. Le noyau arrête uniquement cette tâche et affiche l'exception.

Les commandes `panic` déclenchent volontairement une exception noyau et peuvent
laisser la machine virtuelle arrêtée sur l'écran de panique. Elles sont
destinées aux essais dans QEMU.

## Prérequis

Pour compiler sous Linux, il faut :

- GNU Make ;
- Clang et LLD (notamment `clang` et `ld.lld`) ;
- NASM ;
- GNU `objcopy` ;
- `qemu-img`, utilisé pour créer l'image disque QCOW2.

Pour lancer le système, il faut également `qemu-system-x86_64`. L'émulateur est
configuré pour disposer de 8 Gio de mémoire vive ; prévoyez une machine hôte
ayant suffisamment de mémoire disponible.

Sur Ubuntu ou Debian, installez les outils avec :

```sh
sudo apt update
sudo apt install build-essential clang lld nasm qemu-system-x86 qemu-utils
```

## Compiler et lancer sous Linux

Depuis la racine du dépôt :

```sh
./build.sh
```

Cette commande compile les sources et crée l'image disque
`build/os.img`. Pour compiler puis démarrer cette image dans QEMU :

```sh
./build.sh run
```

Pour supprimer les fichiers de compilation et l'image générée :

```sh
./build.sh clean
```

Le script `build.sh` vérifie la présence des outils requis et indique les
paquets à installer si certains manquent. Il s'appuie sur le `Makefile` du
projet ; les mêmes cibles sont accessibles directement avec `make`, `make run`
et `make clean`.

## Compiler sous Windows

Le dépôt contient également `build.bat`, qui compile avec Clang/LLD et NASM,
puis peut lancer l'image dans QEMU. Les outils nécessaires doivent être
installés et accessibles depuis `PATH` : Clang, `ld.lld`, NASM, `objcopy`,
`qemu-img` et `qemu-system-x86_64`. Le script utilise aussi PowerShell, fourni
avec les versions récentes de Windows.

Dans une invite de commandes ouverte à la racine du dépôt :

```bat
build.bat
build.bat run
build.bat clean
```

## Organisation des sources

- `src/bootloader/` : chargeur BIOS en assembleur et chargeur intermédiaire en
  C, qui prépare le passage en mode 64 bits.
- `src/kernel/` : noyau, console, clavier, gestion des interruptions et
  exceptions, ainsi que du code expérimental pour le disque et les processus.
- `src/user/` : programme utilisateur minimal de test.
- `include/` : interfaces et structures partagées entre les composants.
- `linker.ld`, `module.ld` et `user.ld` : placement en mémoire du chargeur, du
  noyau et du programme utilisateur.
- `Makefile`, `build.sh` et `build.bat` : compilation et lancement sous Linux
  ou Windows.

## Contributions

Ce dépôt est avant tout un projet personnel d'apprentissage et
d'expérimentation ; il n'a pas pour objectif d'accueillir un développement
communautaire soutenu. Les contributions sont donc limitées et ne sont pas
garanties d'être acceptées ou suivies.

Si vous souhaitez proposer une modification importante, ouvrez d'abord une
discussion afin de vérifier qu'elle correspond à l'orientation du projet. Les
retours, signalements de problèmes et petites corrections restent les
bienvenus, dans la mesure du temps disponible.

## Tests

Aucune suite de tests automatisés n'est actuellement fournie. La vérification
se fait principalement en compilant le projet et en l'exécutant dans QEMU.