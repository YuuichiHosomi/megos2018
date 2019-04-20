#
# Rakefile for EFI Hello world
#
require 'rake/clean'
require 'rake/packagetask'
require 'json'
require 'base64'
require 'digest/sha2'

ARCH  = ENV['ARCH'] || case `uname -m`
when /i[3456789]86/
  :i386
when /x86.64/
  :x64
when /aarch64/
  :aa64
else
  :unknown
end

VENDOR_NAME     = "MOE"
PATH_BIN        = "bin/"
PATH_SRC        = "src/"
PATH_SRC_FONTS  = "#{PATH_SRC}fonts/include/"
PATH_OBJ        = "obj/"
PATH_MNT        = "mnt/"
PATH_EFI_BOOT   = "#{PATH_MNT}EFI/BOOT/"
PATH_EFI_VENDOR = "#{PATH_MNT}EFI/#{VENDOR_NAME}/"
PATH_INC        = "#{PATH_SRC}include/"
CP932_BIN       = "#{PATH_EFI_VENDOR}cp932.bin"
TARGET_ISO      = "var/moe.iso"

case ARCH.to_sym
when :x64
  PATH_OVMF     = "var/bios64.bin"
  QEMU_ARCH     = "x86_64"
  QEMU_OPTS     = "-smp 4 -rtc base=localtime -device nec-usb-xhci,id=xhci "
  # -device usb-kbd -device usb-mouse -device usb-tablet"
when :i386
  PATH_OVMF     = "var/bios32.bin"
  QEMU_ARCH     = "x86_64"
  QEMU_OPTS     = ""
when :arm
  PATH_OVMF     = "var/ovmfarm.fd"
  QEMU_ARCH     = "aarch64"
  QEMU_OPTS     = "-M virt -cpu cortex-a15"
when :aa64
  PATH_OVMF     = "var/ovmfaa64.fd"
  QEMU_ARCH     = "aarch64"
  QEMU_OPTS     = "-M virt -cpu cortex-a57"
else
  raise "UNKNOWN ARCH #{ARCH}"
end

if RUBY_PLATFORM =~ /darwin/ then
  LLVM_PREFIX     = `brew --prefix llvm`.gsub(/\n/, '')
  CC      = ENV['CC'] || "#{LLVM_PREFIX}/bin/clang"
  LD      = ENV['LD'] || "#{LLVM_PREFIX}/bin/lld-link"
else
  CC      = ENV['CC'] || "clang"
  LD      = ENV['LD'] || "lld-link-7.0"
end
CFLAGS  = "-Os -std=c11 -fno-stack-protector -fshort-wchar -mno-red-zone -nostdlibinc -I #{PATH_INC} -I #{PATH_SRC} -I #{PATH_SRC_FONTS} -Wall -Wpedantic -fno-exceptions"
AS      = ENV['AS'] || "nasm"
AFLAGS  = "-s -I #{ PATH_SRC }"
LFLAGS  = "-subsystem:efi_application -nodefaultlib -entry:efi_main"

INCS  = [FileList["#{PATH_SRC}*.h"], FileList["#{PATH_INC}*.h"]]

CLEAN.include(FileList["#{PATH_BIN}**/*"])
CLEAN.include(FileList["#{PATH_OBJ}**/*"])
CLEAN.include(CP932_BIN)

directory PATH_MNT
directory PATH_OBJ
directory PATH_BIN
directory PATH_EFI_BOOT
directory PATH_EFI_VENDOR

TASKS = [ :main ]

TASKS.each do |t|
  task t => [t.to_s + ":build"]
end

desc "Defaults"
task :default => [PATH_OBJ, PATH_BIN, TASKS].flatten

desc "Install to #{PATH_MNT}"
task :install => [:default, PATH_MNT, PATH_EFI_BOOT, PATH_EFI_VENDOR, PATH_OVMF, CP932_BIN] do
  (target, efi_suffix) = convert_arch(ARCH)
  FileUtils.cp("#{PATH_BIN}menu#{efi_suffix}.efi", "#{PATH_EFI_BOOT}boot#{efi_suffix}.efi")
  FileUtils.cp("#{PATH_BIN}boot#{efi_suffix}.efi", "#{PATH_EFI_VENDOR}boot#{efi_suffix}.efi")
  FileUtils.cp("#{PATH_BIN}kernel.efi", "#{PATH_EFI_VENDOR}kernel.efi")
end

desc "Make ISO image"
task :iso => [PATH_MNT, :install] do
  sh "mkisofs -r -J -o #{TARGET_ISO} #{PATH_MNT}"
end

desc "Run with QEMU"
task :run => :install do
  sh "qemu-system-#{QEMU_ARCH} #{QEMU_OPTS} -bios #{PATH_OVMF} -monitor stdio -drive format=raw,file=fat:rw:mnt"
end

desc "Format"
task :format do
  sh "clang-format -i #{ FileList["#{PATH_SRC}**/*.c"] } #{ FileList["#{PATH_SRC}**/*.h"] }"
end


file CP932_BIN => [ PATH_EFI_VENDOR, "#{PATH_SRC}cp932.txt"] do |t|
  bin = []
  File.open(t.prerequisites[1]) do |file|
    file.each_line do |line|
      bin << Base64.decode64(line)
    end
  end
  File.binwrite(t.name, bin.join(''))
  raise unless File.exist?(t.name)
end


def convert_arch(s)
  case s.to_sym
  when :x64
    ['x86_64-pc-win32-coff', 'x64']
  when :i386
    ['i386-pc-win32-coff', 'ia32']
  when :arm
    ['arm-pc-win32-coff', 'arm']
  when :aa64
    ['aarch64-pc-win32-coff', 'aa64']
  end
end

def make_efi(cputype, target, src_tokens, options = {})

  (cf_target, efi_suffix) = convert_arch(cputype)

  case cputype.to_sym
  when :x64
    af_target = "-f win64"
  when :i386
    af_target = "-f win32"
  end

  path_obj      = "#{PATH_OBJ}#{efi_suffix}/"
  directory path_obj

  local_incs = []

  if options['base_dir']
    path_src_p    = "#{PATH_SRC}#{options['base_dir']}/"
    local_incs  << FileList["#{path_src_p}*.h"]
  else
    path_src_p    = "#{PATH_SRC}"
  end

  if options['no_suffix']
    efi_output    = "#{PATH_BIN}#{target}.efi"
  else
    efi_output    = "#{PATH_BIN}#{target}#{efi_suffix}.efi"
  end

  rsrc_json = "#{path_src_p}rsrc.json"
  if File.exist?(rsrc_json)
    rsrc_inc = "#{path_src_p}rsrc.h"
    INCS << rsrc_inc
    CLEAN.include(rsrc_inc)

    file rsrc_inc => [rsrc_json] do |t|
      json = File.open(t.prerequisites[0]) do |file|
        JSON.load(file)
      end

      prefix = json['prefix']
      strings = json['strings']
      keys = strings['default'].keys

      File.open(t.name, 'w') do |file|
        file.puts '// AUTO GENERATED rsrc.h'
        file.puts "typedef enum {"
        keys.each do |key|
          file.puts "\t#{prefix}_#{key},"
        end
        file.puts "\t#{prefix}_max"
        file.puts "} #{prefix};"

        %w(default ja).each do |lang|
          if strings[lang]
            file.puts "const char* #{prefix}_#{lang}[] = {"
            keys.each do |key|
              value = strings[lang][key]
               file.puts "\t#{value ? value.inspect : 'NULL'},"
            end
            file.puts "};"
          end
        end

      end
    end
  end

  srcs = {}
  src_tokens.each do |s|
    t = nil
    if s !~ /\.\w+/
      s += '.c'
    end
    base = File.basename(s, '.*')
    ext = File.extname(s)
    if s !~ /\//
      t = [
        "#{path_src_p}#{s}",
        "#{path_src_p}#{base}-#{efi_suffix}#{ext}",
        "#{PATH_SRC}#{s}",
        "#{PATH_SRC}#{base}-#{efi_suffix}#{ext}",
      ].find do |p|
        if File.exist?(p)
          p
        end
      end
    end
    srcs[s] = t || s
  end

  objs = srcs.map do |token, src|
    mod_name = File.basename(token, '.*')
    hash = Digest::SHA256.hexdigest(File.dirname(src)).slice(0, 16)
    obj = "#{path_obj}#{mod_name}-#{hash}.o"

    case File.extname(src)
    when '.c'
      file obj => [ src, INCS, local_incs, path_obj ].flatten do |t|
        sh "#{ CC } -target #{ cf_target } #{ CFLAGS} -DEFI_VENDOR_NAME=\\\"#{VENDOR_NAME}\\\" -c -o #{ t.name } #{ src }"
      end
    when '.asm'
      file obj => [ src, path_obj ] do | t |
        sh "#{ AS } #{ af_target } #{ AFLAGS } -o #{ t.name } #{ src }"
      end
    end

    obj
  end

  file efi_output => objs do |t|
    sh "#{LD} #{ LFLAGS} #{ t.prerequisites.join(' ') } -out:#{ t.name }"
  end

  efi_output
end


namespace :main do

  targets = []

  json = File.open("make.json") do |file|
    JSON.load(file)
  end

  json['targets'].keys.each do |target|
    sources = json['targets'][target]['sources']
    allow = json['targets'][target]['valid_arch']
    if allow == 'all' || allow.include?(ARCH.to_s) then
      targets << make_efi(ARCH, target, sources, json['targets'][target])
    end
  end

  desc "Build Main"
  task :build => targets

end
