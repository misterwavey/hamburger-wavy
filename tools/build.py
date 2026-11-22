#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build Hamburger Wavy from the included source and hardware layout contract."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import zlib
from generate_pitch import generate as generate_pitch
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def tool(explicit,choices):
 for name in ([explicit] if explicit else choices):
  found=shutil.which(str(name))
  if found:return found
 raise SystemExit('missing tool; tried '+str(choices))

def record(addr,kind,data):
 raw=bytes([len(data)])+addr.to_bytes(2,'big')+bytes([kind])+data
 return ':'+(raw+bytes([-sum(raw)&255])).hex()

def template(path):
 spec=json.loads(path.read_text());memory={};base=0
 for line in spec['records']:
  header=bytes.fromhex(line[1:9]);count=header[0];addr=int.from_bytes(header[1:3],'big');kind=header[3]
  if kind==0:
   if len(line)!=9 or addr+count>65536:raise ValueError('invalid data record template')
   for i in range(count):memory[base+addr+i]=255
  else:
   full=bytes.fromhex(line[1:])
   if len(full)!=count+5 or sum(full)&255:raise ValueError('invalid metadata record')
   if kind==4:base=int.from_bytes(full[4:-1],'big')<<16
   elif kind!=1:raise ValueError('unsupported template record')
 return spec,memory

def extract(path, flash_probe=False, flash_bank=False, extended=False):
 raw=path.read_bytes()
 if raw[:7]!=b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H',raw,18)[0]!=8:raise ValueError('not ELF32 MIPS little endian')
 off=struct.unpack_from('<I',raw,32)[0];size,count,index=struct.unpack_from('<HHH',raw,46)
 sections=[struct.unpack_from('<10I',raw,off+i*size) for i in range(count)]
 n=sections[index];names=raw[n[4]:n[4]+n[5]];payload={};symbols={};layout={};functions=[];undefined=[]
 for sec in sections:
  name=names[sec[0]:].split(b'\0')[0].decode()
  if sec[1] in (4,9) and sec[5]:raise ValueError('unresolved relocations')
  if sec[1]==2:
   ss=sections[sec[6]];strings=raw[ss[4]:ss[4]+ss[5]]
   for p in range(sec[4],sec[4]+sec[5],sec[9]):
    idx,address,nbytes,info,_,which=struct.unpack_from('<IIIBBH',raw,p)
    symbol=strings[idx:].split(b'\0')[0].decode()
    if symbol and which==0:undefined.append(symbol)
    symbols[symbol]=address
    if info&15==2 and nbytes:functions.append((address,address+nbytes,symbol))
  if sec[2]&2 and sec[5]:
   layout[name]={'address':hex(sec[3]),'bytes':sec[5],'zero_fill':sec[1]==8}
   if sec[1]==8:
    if name!='.bss' or sec[3]!=0xa0000000 or sec[3]+sec[5]>(0xa000f000 if extended else 0xa000e000):raise ValueError('invalid RAM layout')
   else:
    if name=='.app_extension':
     if not extended or not (sec[3]==0x9d014000<sec[3]+sec[5]<=0x9d020000):raise ValueError('invalid application extension')
    elif name in ('.flash_probe','.flash_probe_tail','.flash_probe_high'):
     ranges={'.flash_probe':(0x9d014000,0x9d0143f0),'.flash_probe_tail':(0x9d0143f0,0x9d014400)}
     if flash_bank:ranges={'.flash_probe':(0x9d014000,0x9d0143f0),'.flash_probe_high':(0x9d01ff00,0x9d01fff0),'.flash_probe_tail':(0x9d01fff0,0x9d020000)}
     bounds=ranges.get(name)
     if not flash_probe or not bounds or not (bounds[0]==sec[3]<sec[3]+sec[5]<=bounds[1]):raise ValueError('invalid flash probe section')
    elif name not in ('.exception','.startup','.debug_exception','.text','.reset_handler','.config1','.config2') and not name.startswith('.vector'):raise ValueError('unexpected allocated section '+name)
    if sec[1]!=1:raise ValueError('unexpected section type')
    address=sec[3]&0x1fffffff
    for i,v in enumerate(raw[sec[4]:sec[4]+sec[5]]):
     if address+i in payload:raise ValueError('overlapping sections')
     payload[address+i]=v
 if undefined:raise ValueError('undefined symbols: '+str(undefined))
 if symbols.get('standalone_start')!=0x9d00b970 or symbols.get('standalone_reset')!=0x9d013a44:raise ValueError('entry contract changed')
 # Every statically encoded J/JAL inside a function must remain in source code.
 calls=[]
 for low,high,symbol in functions:
  for pc in range(low,high,4):
   a=pc&0x1fffffff
   if not all(a+i in payload for i in range(4)):raise ValueError('function outside payload')
   w=sum(payload[a+i]<<(i*8) for i in range(4))
   if w>>26 in (2,3):
    target=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
    if not all((target&0x1fffffff)+i in payload for i in range(4)):raise ValueError('jump outside source payload')
    calls.append([hex(pc),hex(target)])
 return payload,symbols,layout,calls

def write_image(spec,memory,path):
 output=[];base=0
 for line in spec['records']:
  header=bytes.fromhex(line[1:9]);count=header[0];addr=int.from_bytes(header[1:3],'big');kind=header[3]
  if kind==0:line=record(addr,kind,bytes(memory[base+addr+i] for i in range(count)))
  elif kind==4:base=int.from_bytes(bytes.fromhex(line[1:])[4:-1],'big')<<16
  output.append(line)
 path.write_bytes((spec['line_ending'].join(output)+spec['line_ending']).encode('ascii'))

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--cc');ap.add_argument('--ld');ap.add_argument('--out',type=Path);modes=ap.add_mutually_exclusive_group();modes.add_argument('--profile',action='store_true');modes.add_argument('--flash-probe',action='store_true');modes.add_argument('--flash-bank',action='store_true');modes.add_argument('--cv-diag',action='store_true');ap.add_argument('--extended',action='store_true');args=ap.parse_args()
 if args.extended and (args.flash_probe or args.flash_bank or args.cv_diag):ap.error("--extended cannot be combined with probe/CV diagnostic modes")
 probe=args.flash_probe or args.flash_bank
 extension_size=0xc000 if args.flash_bank or args.extended else 1024
 out=args.out or (ROOT/'build/cv-diag' if args.cv_diag else ROOT/'build/extended/profile' if args.extended and args.profile else ROOT/'build/extended' if args.extended else ROOT/'build/flash-bank' if args.flash_bank else ROOT/'build/flash-probe' if args.flash_probe else ROOT/'build/profile' if args.profile else ROOT/'build');out.mkdir(parents=True,exist_ok=True)
 pitch_report=generate_pitch(out)
 cc=tool(args.cc,['/usr/local/opt/llvm/bin/clang','/opt/homebrew/opt/llvm/bin/clang','clang'])
 ld=tool(args.ld,['ld.lld',*Path.home().glob('.rustup/toolchains/*/lib/rustlib/*/bin/rust-lld')])
 flags=['-I'+str(out),'--target=mipsel-none-elf','-march=mips32r2','-mabi=32','-mno-abicalls','-fno-pic','-G0','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-jump-tables','-Os','-fstack-usage','-Wall','-Wextra','-Werror']
 if args.profile:flags+=['-DONE_PROFILE=1']
 if args.cv_diag:flags+=['-DONE_CV_DIAG=1','-DONE_BUILD_NUMBER=29']
 if args.extended:flags+=['-DONE_EXTENDED_APP=1','-DONE_FLIP=1','-DONE_COLOUR=1','-DONE_POLL_DECODE=1','-DONE_TOLERANT_ENCODER=1','-DONE_SD_RETRY=1','-DONE_BUILD_NUMBER=53']
 if probe:flags+=['-DONE_FLASH_PROBE=1','-DONE_BUILD_NUMBER='+('23' if args.flash_bank else '22')]
 if args.flash_bank:flags+=['-DONE_FLASH_BANK=1']
 linker=ROOT/'firmware/layout.ld'
 def extension_header(bank):
  crc=[zlib.crc32(bank[i:i+1024]) for i in range(0,0xc000,1024)]
  (out/'extension_expected.h').write_text('/* Generated CRC32 of each 1 KiB bank page. */\n'+
    'static const uint32_t extension_crc[48]={'+','.join(hex(n)+'u' for n in crc)+'};\n')
 if args.extended:
  extension_header(bytes(0xc000))
  modules=('fat','wave','policy','settings','recall','profile')
  selectors=' '.join('*'+name+'.o(.text .text.* .rodata .rodata.*)' for name in modules)
  text=linker.read_text().replace('.text 0x9d00be00',
    '.app_extension 0x9d014000 : { '+selectors+' }\n'+
    'ASSERT(SIZEOF(.app_extension)<=0xc000,"Application extension overflow")\n'+
    '.text 0x9d00be00')
  # Observed stack use is about 1.7 KiB; the extended build reserves 4 KiB.
  stack='ASSERT(__bss_end<=0xa000e000,"Leave at least 8 KiB for stack")'
  if stack not in text:raise SystemExit('stack reserve assertion not found')
  text=text.replace(stack,'ASSERT(__bss_end<=0xa000f000,"Leave at least 4 KiB for stack")')
  linker=out/'extended.ld';linker.write_text(text)
 def expected_header(page,regions=None):
  def mix(value):
   for _ in range(32):value=(value*1664525+1013904223)&0xffffffff
   return value^(0x48574157 if args.flash_bank else 0x48574156)
  results='#define FLASH_RESULT_A '+hex(mix(0x13579bdf))+'u\n#define FLASH_RESULT_B '+hex(mix(0x2468ace0))+'u\n'
  if args.flash_bank:
   def high(value):
    for i in range(16):value=(((value<<5)|(value>>27))&0xffffffff)^(0x9e3779b9+i)
    return (value+0x17000023)&0xffffffff
   def array(name,offset,length):
    words=struct.unpack('<'+str(length//4)+'I',page[offset:offset+length])
    return 'static const uint32_t '+name+'[]={'+','.join(hex(w)+'u' for w in words)+'};\n'
   regions=regions or {'.flash_probe':{'bytes':4},'.flash_probe_high':{'bytes':4}}
   low_size=regions['.flash_probe']['bytes'];high_size=regions['.flash_probe_high']['bytes']
   text='#define FLASH_BANK_WORDS 12288u\n#define FLASH_LOW_WORDS '+str(low_size//4)+'u\n'+\
    '#define FLASH_HIGH_FIRST 12224u\n#define FLASH_HIGH_WORDS '+str(high_size//4)+'u\n#define FLASH_TAIL_FIRST 12284u\n'
   text+=array('flash_low_expected',0,low_size)+array('flash_high_expected',0xbf00,high_size)+array('flash_tail_expected',0xbff0,16)
   text+='#define FLASH_HIGH_RESULT_A '+hex(high(0x13579bdf))+'u\n#define FLASH_HIGH_RESULT_B '+hex(high(0x2468ace0))+'u\n'
  else:
   words=struct.unpack('<256I',page)
   text='static const uint32_t flash_expected[256]={'+','.join(hex(w)+'u' for w in words)+'};\n'
  (out/'flash_probe_expected.h').write_text('/* Generated expected extension contents. */\n'+text+results)

 if probe:
  expected_header(bytes(extension_size))
  text=linker.read_text().replace('.reset_handler 0x9d013a44',
    '.flash_probe 0x9d014000 : { *(.flash_probe) }\n'+
    '.flash_probe_tail 0x9d0143f0 : { *(.flash_probe_tail) }\n'+
    'ASSERT(SIZEOF(.flash_probe)<=0x3f0,"Flash probe code overflow")\n'+
    'ASSERT(SIZEOF(.flash_probe_tail)==16,"Flash probe tail size")\n'+
    '.reset_handler 0x9d013a44')
  if args.flash_bank:
   text=text.replace('.flash_probe_tail 0x9d0143f0',
     '.flash_probe_high 0x9d01ff00 : { *(.flash_probe_high) }\n'+
     'ASSERT(SIZEOF(.flash_probe_high)<=0xf0,"High flash probe overflow")\n'+
     '.flash_probe_tail 0x9d01fff0')
  linker=out/'flash-probe.ld';linker.write_text(text)
 sources=sorted(p for p in (ROOT/'firmware').iterdir() if p.suffix in ('.c','.S'))
 if args.cv_diag:sources=[p for p in sources if p.stem in ('advertise','cv_capture','cv_diag','fat','memory','platform','policy','sd','start')]
 objects=[]
 for src in sources:
  obj=out/(src.stem+'.o');subprocess.run([cc,*flags,'-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
 elf=out/'hamburger-wavy.elf';subprocess.run([ld,'-flavor','gnu','-m','elf32ltsmip','-T',str(linker),*objects,'-o',str(elf)],check=True)
 payload,symbols,layout,calls=extract(elf, probe, args.flash_bank, args.extended)
 if probe:
  fill={}
  if args.flash_bank:
   for i in range(extension_size//4):
    word=((i*0x9e3779b9)^0x48570017)&0xffffffff
    for j,v in enumerate(word.to_bytes(4,'little')):fill[0x1d014000+i*4+j]=v
  probe_page=bytes(payload.get(0x1d014000+i,fill.get(0x1d014000+i,255)) for i in range(extension_size))
  expected_header(probe_page,layout)
  subprocess.run([cc,*flags,'-c',str(ROOT/'firmware/flash_probe.c'),'-o',str(out/'flash_probe.o')],check=True)
  subprocess.run([ld,'-flavor','gnu','-m','elf32ltsmip','-T',str(linker),*objects,'-o',str(elf)],check=True)
  payload,symbols,layout,calls=extract(elf, True, args.flash_bank)
  if probe_page!=bytes(payload.get(0x1d014000+i,fill.get(0x1d014000+i,255)) for i in range(extension_size)):raise ValueError('flash probe changed between link passes')
 if args.extended:
  extension=bytes(payload.get(0x1d014000+i,255) for i in range(0xc000))
  extension_header(extension)
  subprocess.run([cc,*flags,'-c',str(ROOT/'firmware/extension_check.c'),'-o',str(out/'extension_check.o')],check=True)
  subprocess.run([ld,'-flavor','gnu','-m','elf32ltsmip','-T',str(linker),*objects,'-o',str(elf)],check=True)
  payload,symbols,layout,calls=extract(elf,extended=True)
  if extension!=bytes(payload.get(0x1d014000+i,255) for i in range(0xc000)):raise ValueError('application extension changed between link passes')
 spec,memory=template(ROOT/'board/update-layout.json')
 if args.extended:fill={}
 if probe or args.extended:
  if spec['records'][-1].lower()!=':00000001ff':raise ValueError('missing final EOF')
  spec['records'][-1:]=[record(0,4,bytes.fromhex('1d01'))]+[':10'+format(a,'04x')+'00' for a in range(0x4000,0x4000+extension_size,16)]+[record(0,1,b'')]
  for a in range(0x1d014000,0x1d014000+extension_size):
   if a in memory:raise ValueError('flash probe overlaps established coverage')
   memory[a]=fill.get(a,255)
 if {a for a in memory if a>=0x1fc00000}!=set(range(0x1fc00bf4,0x1fc00bfc)):raise ValueError('unexpected boot-flash update coverage')
 if any(a not in memory for a in payload):raise ValueError('new address coverage')
 # No boot-debug payload is permitted; configuration words remain unchanged.
 if any(0x1fc00480<=a<0x1fc004ac for a in payload):raise ValueError('boot debug payload must be omitted')
 for a,w in ((0x1fc00bf4,0xfff9fffc),(0x1fc00bf8,0xff6bcedb)):
  if bytes(payload.get(a+i,0) for i in range(4))!=w.to_bytes(4,'little'):raise ValueError('hardware configuration changed')
 memory.update(payload);image=out/'image.hex';write_image(spec,memory,image)
 build_number=int(re.search(r'^#define ONE_BUILD_NUMBER (\d+)$',(ROOT/'firmware/build.h').read_text(),re.M)[1])
 if probe:build_number=23 if args.flash_bank else 22
 if args.extended:build_number=53
 if args.cv_diag:build_number=29
 report=dict(profiling=args.profile,project='Hamburger Wavy',licence='GPL-3.0-only',build_number=build_number,status='EXPERIMENTAL — COMPLETE APPLICATION, NOT HARDWARE VALIDATED',source_built=True,
             features=dict(config_txt=True,playlist_txt=True,playback_modes=['Trigger','Gate','Fade','Loop','Sequence','Combo','Random Sequence','Random Loop']+(['Flip','CV Flip','Tape','Chop'] if args.extended else []),cv_modes=['Free','Quantize','Select File']+(['Colour (config.txt only)','Filter (config.txt only)','Rate (config.txt only)'] if args.extended else []),cv_select_delays_ms=dict(Short=0,Medium=2,Long=10),front_panel_modes=True,last_manual_file_recall='ONESTATE.BIN: two alternating CRC32 sectors; stock status.bin read-only import',build_identification='ONEBUILD.TXT: best-effort on mount, written only if content changes',generated_chromatic_tables=True,fade_ramp_samples=256),
             pitch_quantization=pitch_report,
             boot_debug_handler='omitted: no debug-vector bytes distributed; resident flash preservation unverified',
             resident_bootloader='pre-existing, omitted from update; not replaced',image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
             source_payload_bytes=len(payload),sections=layout,symbols={k:hex(v) for k,v in symbols.items() if k and not k.startswith('$')},
             external_application_calls=[],static_jumps_inside_source_payload=calls,filesystem='FAT16/FAT32: sample/config/playlist reads; reserved state/build-info allocation and updates',sd_write_commands=[24],sd_write_scope='ONESTATE.BIN and ONEBUILD.TXT data; FAT allocation, root directory and FSInfo on first creation; audio stopped during writes',
             unused_application_bytes='0xff',
             record_layout='address/record headers in board/update-layout.json; debug-vector bytes absent',
             source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted([*(ROOT/'firmware').iterdir(),ROOT/'tools/generate_pitch.py',ROOT/'board/update-layout.json',Path(__file__).resolve()]) if p.is_file()})
 half_frames=int(re.search(r'^#define ONE_HALF_FRAMES (\d+)u$',(ROOT/'firmware/core.h').read_text(),re.M)[1])
 report['audio_buffers']=dict(half_frames=half_frames,dma_frames=half_frames*2,
                              dma_bytes=half_frames*16,commit_guard_frames=128)
 report['resources']=dict(text_bytes=layout['.text']['bytes'],text_limit=0x7c44,
                          text_free=0x7c44-layout['.text']['bytes'],
                          static_ram_bytes=layout['.bss']['bytes'],ram_total=65536,
                          minimum_stack_reserve=4096 if args.extended else 8192,
                          static_ram_growth_budget=(0xf000 if args.extended else 0xe000)-layout['.bss']['bytes'])
 if probe:
  report['flash_probe']=dict(physical_start='0x1d014000',physical_end_exclusive=hex(0x1d014000+extension_size),
    additional_addressed_bytes=extension_size,page_sha256=hashlib.sha256(probe_page).hexdigest(),
    validation=('all 12288 words compared through uncached alias, then two checked calls at each end' if args.flash_bank else 'all 256 words compared through uncached alias, then two calls into new page'),
    failure_signal='alternating blue/red; application and build-record writer not entered',
    updater_acceptance='not hardware validated',maximum_device_flash_end_exclusive='0x1d040000')
  report['resources']['extension_capacity_bytes']=extension_size
  report['resources']['extension_payload_bytes']=sum(v['bytes'] for k,v in layout.items() if k.startswith('.flash_probe'))
  report['record_layout']+='; explicit experimental extension appended before EOF'
  if args.flash_bank:
   report['unused_application_bytes']='0xff in legacy coverage; deterministic word pattern in extension bank'
   report['flash_probe']['pattern']='((word_index * 0x9e3779b9) ^ 0x48570017) modulo 2^32, overridden by linked code/marker'
 if args.extended:
  size=layout['.app_extension']['bytes']
  report['extended_application']=dict(physical_start='0x1d014000',physical_end_exclusive='0x1d020000',
    modules=list(modules),additional_addressed_bytes=49152,bank_sha256=hashlib.sha256(extension).hexdigest(),
    validation='per-page CRC32 via uncached alias before any application-bank calls',hardware_validation='pending')
  report['resources'].update(extension_text_bytes=size,extension_capacity_bytes=49152,
    extension_free=49152-size,combined_text_bytes=layout['.text']['bytes']+size,
    combined_text_limit=80964,combined_text_free=80964-layout['.text']['bytes']-size)
  report['record_layout']+='; verified 48 KiB bank appended before EOF, unused bank bytes 0xff'
 if args.extended:
  report['cv_flip']=dict(zero_default_adc=327,hysteresis_counts=5,zero_calibration='hold PLAY without moving controls, release',speed='native',gate='ignored',calibration_persistence='RAM only')
 if args.extended:
  report['tape']=dict(zero_enter_counts=4,zero_exit_counts=8,counts_per_native_speed=100,maximum_speed=1.5,calibration_persistence='RAM only')
  report['chop']=dict(position='gate-edge ADC mapped across the file',speed='native',retrigger='fade then jump; latest target wins')
 if args.cv_diag:
  report['cv_diagnostic']=dict(rows=12,settle_ms=500,window_ms=5000,poll_ms=1,
    report_file='ONECV.TXT',adc_bits=10,audio_playback=False,hardware_validation='pending')
  report['features']={'cv_diagnostic':True}
  report['sd_write_scope']='ONEBUILD.TXT and ONECV.TXT plus their reserved FAT/root/FSInfo allocation; audio remains stopped'
 if args.profile:
  report['sd_write_scope']+='; ONEPROF.TXT (two sectors) and its allocation, only when playback is stopped'
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k] for k in ('project','build_number','status','image_sha256','source_payload_bytes','sections')},indent=2))

if __name__=='__main__':main()
