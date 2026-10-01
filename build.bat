@echo off
REM Build Windows sans WSL : necessite nasm, clang (LLVM, avec ld.lld) et qemu dans le PATH
setlocal EnableDelayedExpansion

if /I "%1"=="clean" (
	if exist build rmdir /s /q build
	if exist build exit /b 1
	echo Clean OK
	exit /b 0
)

if not exist build mkdir build
if not exist build\obj mkdir build\obj
if exist build\kernel-objects.rsp del /q build\kernel-objects.rsp
type nul > build\kernel-objects.rsp

REM Compile the kernel and system sources; boot.c is the preceding boot stage.
for /r src %%F in (*.c) do (
	if /I "%%~fF"=="%CD%\src\bootloader\boot.c" (
		rem boot.c needs KERNEL_SECTORS, known only after linking the kernel.
	) else (
		set "REL=%%~dpnF"
		set "REL=!REL:%CD%\src\=!"
		set "OBJ=build\obj\!REL!.o"
		for %%D in ("!OBJ!") do if not exist "%%~dpD" mkdir "%%~dpD"
		clang --target=x86_64-none-elf -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -mgeneral-regs-only -O2 -Wall -c "%%~fF" -o "!OBJ!" || goto :err
		echo "!OBJ!">>build\kernel-objects.rsp
	)
)

ld.lld -m elf_x86_64 --oformat binary -T module.ld @build\kernel-objects.rsp -o build\kernel.bin || goto :err
for %%A in (build\kernel.bin) do set /a KERNEL_SECTORS=(%%~zA+511)/512
if not exist build\obj\bootloader mkdir build\obj\bootloader
clang --target=i386-none-elf -DKERNEL_SECTORS=!KERNEL_SECTORS! -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -O2 -Wall -c src\bootloader\boot.c -o build\obj\bootloader\boot.o || goto :err
nasm -f elf32 src\bootloader\longmode.asm -o build\obj\bootloader\longmode.o || goto :err
ld.lld -m elf_i386 --oformat binary -T linker.ld build\obj\bootloader\boot.o build\obj\bootloader\longmode.o -o build\boot.bin || goto :err

REM nombre de secteurs de boot.bin = taille arrondie au multiple de 512 superieur
for %%A in (build\boot.bin) do set /a CS=(%%~zA+511)/512
if %CS% GEQ 16 goto :err

nasm -f bin -DC_SECTORS=%CS% src\bootloader\bootloader.asm -o build\bootloader.bin || goto :err
if exist build\os.img del /q build\os.img
if exist build\os.img goto :err
if exist build\os-base.img del /q build\os-base.img
if exist build\os-base.img goto :err
copy /b build\bootloader.bin+build\boot.bin build\os-base.img >nul || goto :err
powershell -NoProfile -Command "$f=[System.IO.File]::Open('build\os-base.img',[System.IO.FileMode]::Open,[System.IO.FileAccess]::Write); try { $kernel=[System.IO.File]::ReadAllBytes('build\kernel.bin'); $offset=[long]16*512; $f.SetLength($offset+[long]!KERNEL_SECTORS!*512); $f.Position=$offset; $f.Write($kernel,0,$kernel.Length) } finally { $f.Dispose() }" || goto :err
pushd build
qemu-img create -f qcow2 -F raw -b os-base.img os.img 5000000000 || goto :err
popd

echo Build OK (%CS% secteurs pour boot.bin)
if /i "%1"=="run" qemu-system-x86_64 -m 8G -drive format=qcow2,file=build/os.img
exit /b 0

:err
echo Erreur pendant le build.
exit /b 1
