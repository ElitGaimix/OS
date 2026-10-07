# Carte actuelle de la RAM et du disque

Ce document decrit l'organisation visible dans le code actuel. Les adresses
fixes sont celles du code ; les zones dependantes du BIOS ou de la taille des
binaires sont indiquees comme telles. A maintenir lorsque le boot ou la gestion
memoire change.

## RAM physique

| Adresse physique | Taille / limite | Usage actuel |
| --- | --- | --- |
| `0x00000` - `0x004FF` | 1.25 Kio | Zone basse reservee au BIOS / IVT. |
| `0x005000` - `0x00517F` | jusqu'a 384 octets | Tampon de la carte E820 (maximum 16 entrees de 24 octets). |
| `0x07C00` | 512 octets | Secteur de boot charge par le BIOS. La pile real-mode descend depuis `0x7C00`. |
| `0x10000` | taille variable | `boot.bin`, charge par le bootloader. Le build exige moins de 16 secteurs. |
| `0x70000` - `0x75FFF` | 24 Kio | Tables de pagination initiales : PML4, PDPT et quatre page directories, pour mapper 4 Gio. |
| autour de `0x90000` | pile descendante | Pile temporaire du bootloader / de l'entree en mode long. |
| `0x100000` | taille variable | Kernel charge depuis le disque ; le linker place son image a `0x100000`. |
| `0x400000` - `0x5FFFFF` | 2 Mio | Chargement utilisateur temporaire du boot, puis cette adresse virtuelle est remappee par processus. |
| `0xB8000` | zone VGA texte | Memoire video utilisee par la console. |
| pages libres E820 sous `0x100000000` | 4 Kio/page | Pages physiques attribuees dynamiquement aux programmes et a leur pile. |

Les limites de la RAM reellement presente sont determinees par la carte E820.
Le kernel l'utilise pour initialiser un allocateur de pages, en excluant les
zones reservees et les plages au-dessus de 4 Gio que le mapping courant ne
couvre pas. Le nombre de pages libres est affiche au demarrage et consultable
avec la commande `memory`.

### Adresse virtuelle d'un processus

Chaque processus utilisateur voit son programme a l'adresse virtuelle
`0x400000`. Cette adresse pointe maintenant vers des pages physiques de 4 Kio
allouees depuis les regions E820 disponibles. Le binaire occupe les pages du
debut de cette zone ; les quatre pages de pile (16 Kio) sont placees en haut,
pres de `0x600000`. Les pages intermediaires restent non mappees. Les pages de
code sont en lecture seule pour le ring 3 ; les pages de pile et du tas sont
lisibles et inscriptibles mais non executables lorsque le CPU prend en charge
NX. Le syscall d'affichage verifie que chaque page demandee est effectivement
mappee avant de copier les donnees.

L'allocateur gere maintenant les adresses physiques sous 4 Gio, limitee par le
mapping identity du boot. Les tables de
pagination de chaque processus et leurs piles kernel de 16 Kio restent
stockees statiquement dans le kernel. La zone physique `0x400000` - `0x5FFFFF`
est reservee, car le bootloader y charge le programme utilisateur temporaire.

## Disque (image BIOS/ATA)

Les secteurs sont des secteurs de 512 octets. Les numeros de LBA ci-dessous
viennent du bootloader et des scripts de build.

| LBA | Adresse dans l'image | Usage actuel |
| --- | --- | --- |
| `0` | octets `0` - `511` | Secteur de boot BIOS. |
| `1` a `15` | octets `512` - `8191` | `boot.bin`, charge depuis le LBA 1 ; le build verifie qu'il tient avant le LBA 16. |
| `16` a `16 + KERNEL_SECTORS - 1` | a partir de l'octet `8192` | `kernel.bin`, nombre de secteurs calcule au build. Il doit finir avant le LBA 2048. |
| `2048` a `2051` au maximum | a partir de l'octet `1 048 576` | `user.bin` (`hello` et `crash`), maximum quatre secteurs. |
| `2052` a `2055` | a partir de l'octet `1 050 624` | Espace d'ecart entre les emplacements actuels. |
| `2056` a `2059` au maximum | a partir de l'octet `1 052 672` | `ticker.bin`, maximum quatre secteurs. |

Le kernel lit `hello` / `crash` depuis le LBA 2048 et `ticker` depuis le LBA
2056. Le script Windows ecrit les binaires a ces emplacements ; le Makefile
utilise les memes LBA. Les tailles maximales sont des limites de build, pas
necessairement le nombre exact de secteurs ecrits. `process_exec` lit quatre
secteurs par programme, ce qui correspond a la limite imposee par les scripts
de build et a une page de code de 4 Kio.

## Systeme de fichiers TinyFS

L'image TinyFS est placee a partir du LBA 512 et contient
`program.elf` et `ticker.elf`. Le noyau cherche les fichiers dans le petit
repertoire du systeme de fichiers puis charge leurs segments ELF64. Les anciens
binaires raw aux LBA 2048 et 2056 restent presents pour le chemin de boot BIOS
intermediaire ; ils ne sont plus utilises par `process_exec`.

TinyFS accepte les ecritures, remplacements et suppressions depuis la console.
Les donnees d'un remplacement sont ecrites dans un extent libre avant la mise a
jour du repertoire, qui est ecrite en dernier. Les secteurs sont bornes avant
les anciens programmes BIOS a partir du LBA 2048. Le mapping physique au-dela
de 4 Gio n'est pas encore gere.

Le chemin UEFI ajoute une image FAT16 distincte, sans modifier les LBA utilises
par le chemin BIOS.

## Image UEFI experimentale

`build/uefi.img` est une image brute distincte de l'image BIOS. Elle conserve
les donnees BIOS au debut, puis ajoute une partition FAT16 de 8 Mio a partir
du LBA 4096. Le secteur de partition est de type EFI (0xEF) et le fichier
`EFI/BOOT/BOOTX64.EFI` charge `KERNEL.BIN` depuis cette partition. Un
`startup.nsh` lance le chargeur si le firmware ne propose pas directement
l'application comme option de demarrage.

Le chargeur UEFI reserve 2 Mio a partir de `0x100000` pour le noyau et sa BSS,
une pile, ainsi que des tables de pagination identite couvrant 4 Gio. Le
linker refuse un noyau dont la fin depasse cette reservation. Le chargeur
convertit les descripteurs UEFI en entrees E820 simplifiees avant de quitter
les services de demarrage. Le demarrage et les tests d'integration passent sous
QEMU avec EDK2/OVMF ; d'autres firmwares et le materiel reel restent a tester.
