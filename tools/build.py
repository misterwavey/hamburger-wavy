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

def extract(path):
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
    if name!='.bss' or sec[3]!=0xa0000000 or sec[3]+sec[5]>0xa000e000:raise ValueError('invalid RAM layout')
   else:
    if name not in ('.exception','.startup','.debug_exception','.text','.reset_handler','.config1','.config2') and not name.startswith('.vector'):raise ValueError('unexpected allocated section '+name)
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
 ap=argparse.ArgumentParser();ap.add_argument('--cc');ap.add_argument('--ld');ap.add_argument('--out',type=Path,default=ROOT/'build');args=ap.parse_args()
 out=args.out;out.mkdir(parents=True,exist_ok=True)
 pitch_report=generate_pitch(out)
 cc=tool(args.cc,['/usr/local/opt/llvm/bin/clang','/opt/homebrew/opt/llvm/bin/clang','clang'])
 ld=tool(args.ld,['ld.lld',*Path.home().glob('.rustup/toolchains/*/lib/rustlib/*/bin/rust-lld')])
 flags=['-I'+str(out),'--target=mipsel-none-elf','-march=mips32r2','-mabi=32','-mno-abicalls','-fno-pic','-G0','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-jump-tables','-Os','-Wall','-Wextra','-Werror']
 sources=sorted(p for p in (ROOT/'firmware').iterdir() if p.suffix in ('.c','.S'));objects=[]
 for src in sources:
  obj=out/(src.stem+'.o');subprocess.run([cc,*flags,'-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
 elf=out/'hamburger-wavy.elf';subprocess.run([ld,'-flavor','gnu','-m','elf32ltsmip','-T',str(ROOT/'firmware/layout.ld'),*objects,'-o',str(elf)],check=True)
 payload,symbols,layout,calls=extract(elf);spec,memory=template(ROOT/'board/update-layout.json')
 if {a for a in memory if a>=0x1fc00000}!=set(range(0x1fc00bf4,0x1fc00bfc)):raise ValueError('unexpected boot-flash update coverage')
 if any(a not in memory for a in payload):raise ValueError('new address coverage')
 # No boot-debug payload is permitted; configuration words remain unchanged.
 if any(0x1fc00480<=a<0x1fc004ac for a in payload):raise ValueError('boot debug payload must be omitted')
 for a,w in ((0x1fc00bf4,0xfff9fffc),(0x1fc00bf8,0xff6bcedb)):
  if bytes(payload.get(a+i,0) for i in range(4))!=w.to_bytes(4,'little'):raise ValueError('hardware configuration changed')
 memory.update(payload);image=out/'image.hex';write_image(spec,memory,image)
 build_number=int(re.search(r'^#define ONE_BUILD_NUMBER (\d+)$',(ROOT/'firmware/build.h').read_text(),re.M)[1])
 report=dict(project='Hamburger Wavy',licence='GPL-3.0-only',build_number=build_number,status='EXPERIMENTAL — COMPLETE APPLICATION, NOT HARDWARE VALIDATED',source_built=True,
             features=dict(config_txt=True,playlist_txt=True,playback_modes=['Trigger','Gate','Fade','Loop','Sequence','Combo','Random Sequence','Random Loop'],cv_modes=['Free','Quantize','Select File'],cv_select_delays_ms=dict(Short=0,Medium=2,Long=10),front_panel_modes=True,last_manual_file_recall='ONESTATE.BIN: two alternating CRC32 sectors; stock status.bin read-only import',build_identification='ONEBUILD.TXT: best-effort on mount, written only if content changes',generated_chromatic_tables=True,fade_ramp_samples=256),
             pitch_quantization=pitch_report,
             boot_debug_handler='omitted: no debug-vector bytes distributed; resident flash preservation unverified',
             resident_bootloader='pre-existing, omitted from update; not replaced',image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),
             source_payload_bytes=len(payload),sections=layout,symbols={k:hex(v) for k,v in symbols.items() if k and not k.startswith('$')},
             external_application_calls=[],static_jumps_inside_source_payload=calls,filesystem='FAT16/FAT32: sample/config/playlist reads; reserved state/build-info allocation and updates',sd_write_commands=[24],sd_write_scope='ONESTATE.BIN and ONEBUILD.TXT data; FAT allocation, root directory and FSInfo on first creation; audio stopped during writes',
             unused_application_bytes='0xff',
             record_layout='address/record headers in board/update-layout.json; debug-vector bytes absent',
             source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted([*(ROOT/'firmware').iterdir(),ROOT/'tools/generate_pitch.py',ROOT/'board/update-layout.json',Path(__file__).resolve()]) if p.is_file()})
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k] for k in ('project','build_number','status','image_sha256','source_payload_bytes','sections')},indent=2))

if __name__=='__main__':main()
