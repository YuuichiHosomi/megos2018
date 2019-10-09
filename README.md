# MEG-OS MOE

- Minimal Operating Environment

## THIS VERSION

- THIS IS JUNK

## Supported Hardware

- 64bit UEFI system
- Up to 32 logical processor cores
- XX MB of system memory
- 800x600 pixels graphics display

## Requirements

- clang + lld (llvm)
- rake (ruby)
- nasm
- qemu (optional)
- mkisofs (optional)

## How to Build

### build

```
$ rake
```

### build boot loader for 32bit UEFI

```
$ rake ARCH=i386
```

### Install to mnt/

```
$ rake install
```

### Run with qemu

```
$ rake run
```

### Make an iso image

```
$ rake iso
```
