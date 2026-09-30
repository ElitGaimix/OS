@echo off
REM Build Windows sans WSL : necessite nasm, clang (LLVM, avec ld.lld) et qemu dans le PATH
setlocal EnableDelayedExpansion

if not exist build mkdir build
if not exist build\obj mkdir build\obj
if exist build\kernel-objects.rsp del /q build\kernel-objects.rsp
if exist build\module-objects.rsp del /q build\module-objects.rsp
type nul > build\kernel-objects.rsp
type nul > build\module-objects.rsp

REM Compile all C sources, routing src\system files to the module.
for /r src %%F in (*.c) do (
	if /I "%%~fF"=="%CD%\src\boot.c" (
		rem boot.c needs ENTRY_SECTORS, known only after linking the module.
	) else (
		set "REL=%%~dpnF"
		set "REL=!REL:%CD%\src\=!"
		set "OBJ=build\obj\!REL!.o"
		for %%D in ("!OBJ!") do if not exist "%%~dpD" mkdir "%%~dpD"
		clang --target=i386-none-elf -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -O2 -Wall -c "%%~fF" -o "!OBJ!" || goto :err
		if /I "!REL:~0,7!"=="system\" (
			echo "!OBJ!">>build\module-objects.rsp
		) else (
			echo "!OBJ!">>build\kernel-objects.rsp
		)
	)
)

ld.lld -m elf_i386 --oformat binary -T module.ld @build\module-objects.rsp -o build\entry.bin || goto :err
for %%A in (build\entry.bin) do set /a ENTRY_SECTORS=(%%~zA+511)/512
clang --target=i386-none-elf -DENTRY_SECTORS=!ENTRY_SECTORS! -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -O2 -Wall -c src\boot.c -o build\obj\boot.o || goto :err
echo "build\obj\boot.o">>build\kernel-objects.rsp
ld.lld -m elf_i386 --oformat binary -T linker.ld @build\kernel-objects.rsp -o build\boot.bin || goto :err

REM nombre de secteurs de boot.bin = taille arrondie au multiple de 512 superieur
for %%A in (build\boot.bin) do set /a CS=(%%~zA+511)/512
if %CS% GEQ 16 goto :err

nasm -f bin -DC_SECTORS=%CS% src\asm\bootloader.asm -o build\bootloader.bin || goto :err
if exist build\os.img del /q build\os.img
if exist build\os.img goto :err
if exist build\os-base.img del /q build\os-base.img
if exist build\os-base.img goto :err
copy /b build\bootloader.bin+build\boot.bin build\os-base.img >nul || goto :err
powershell -NoProfile -Command "$f=[System.IO.File]::Open('build\os-base.img',[System.IO.FileMode]::Open,[System.IO.FileAccess]::Write); try { $module=[System.IO.File]::ReadAllBytes('build\entry.bin'); $offset=[long]16*512; $f.SetLength($offset+[long]!ENTRY_SECTORS!*512); $f.Position=$offset; $f.Write($module,0,$module.Length) } finally { $f.Dispose() }" || goto :err
pushd build
qemu-img create -f qcow2 -F raw -b os-base.img os.img 5000000000 || goto :err
popd

echo Build OK (%CS% secteurs pour boot.bin)
if /i "%1"=="run" qemu-system-i386 -m 8G -drive format=qcow2,file=build/os.img
exit /b 0

:err
echo Erreur pendant le build.
exit /b 1
