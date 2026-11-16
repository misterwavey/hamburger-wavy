# SPDX-License-Identifier: GPL-3.0-only
"""Checks that require only this source tree and ordinary build tools."""
import json
import binascii
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build import extract, template
from analyze_profile import parse_report, attach_build_info
from mips32 import CPU


def read_hex(path):
    memory = {}
    base = 0
    ended = False
    for line in path.read_text().splitlines():
        if ended or not line.startswith(':'):
            raise ValueError('unexpected HEX content')
        raw = bytes.fromhex(line[1:])
        if len(raw) != raw[0] + 5 or sum(raw) & 255:
            raise ValueError('invalid HEX record')
        count, kind = raw[0], raw[3]
        address = int.from_bytes(raw[1:3], 'big')
        if kind == 0:
            for i, byte in enumerate(raw[4:-1]):
                target = base + address + i
                if target in memory:
                    raise ValueError('overlapping HEX data')
                memory[target] = byte
        elif kind == 4 and count == 2:
            base = int.from_bytes(raw[4:-1], 'big') << 16
        elif kind == 1 and count == 0:
            ended = True
        else:
            raise ValueError('unexpected record type')
    if not ended:
        raise ValueError('missing EOF')
    return memory


class ReleaseTests(unittest.TestCase):
    def _compiled_artifact(self):
        directory=getattr(self,'compiled_dir',ROOT/'build')
        manifest=json.loads((directory/'manifest.json').read_text())
        _,symbols,_,_=extract(directory/'hamburger-wavy.elf',extended='extended_application' in manifest)
        return read_hex(directory/'image.hex'),symbols

    def test_image_contains_source_and_fill_only(self):
        payload, _, layout, _ = extract(ROOT / 'build/hamburger-wavy.elf')
        image = read_hex(ROOT / 'build/image.hex')
        _, contract = template(ROOT / 'board/update-layout.json')
        self.assertEqual(set(image), set(contract))
        for address, byte in image.items():
            self.assertEqual(byte, payload.get(address, 255), hex(address))
        self.assertNotIn('.boot_debug', layout)
        self.assertEqual({a for a in image if a >= 0x1fc00000},
                         set(range(0x1fc00bf4, 0x1fc00bfc)))
        for address, word in ((0x1fc00bf4, 0xfff9fffc), (0x1fc00bf8, 0xff6bcedb)):
            self.assertEqual(bytes(image[address+i] for i in range(4)),
                             word.to_bytes(4, 'little'))

    def test_generated_pitch_tables_against_math(self):
        report = json.loads((ROOT / 'build/pitch_tables.json').read_text())
        self.assertEqual(report['semitones'], list(range(7, -42, -1)))
        for base, row in zip(report['native_dividers'], report['dividers']):
            self.assertEqual(len(row), 49)
            self.assertEqual(row, sorted(row))
            for note, value in zip(report['semitones'], row):
                ideal = base * 2**(-note/12)
                self.assertEqual(value, math.floor(ideal + 0.5))
                self.assertGreaterEqual(value, 512)
                self.assertLessEqual(value, 65535)
                self.assertLess(abs(1200*math.log2(ideal/value)), 1.1)
            self.assertEqual(row[7], base)
            self.assertEqual(row[19], base*2)
            self.assertEqual(row[31], base*4)
            self.assertEqual(row[43], base*8)

    def test_rebuild_from_isolated_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ('firmware', 'tools', 'board'):
                shutil.copytree(ROOT/name, root/name,
                                ignore=shutil.ignore_patterns('__pycache__'))
            result = subprocess.run([sys.executable, str(root/'tools/build.py')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual((root/'build/image.hex').read_bytes(),
                             (ROOT/'build/image.hex').read_bytes())

    def test_host_core_under_sanitizers(self):
        cc = os.environ.get('CC') or shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        sources = [ROOT/'tests/core.c', *[ROOT/'firmware'/f'{name}.c' for name in
                   ('fat', 'wave', 'sd', 'ring', 'memory', 'app', 'advertise',
                    'settings', 'policy', 'ui', 'recall')]]
        binary = ROOT/'build/core-test'
        command = [cc, '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-fno-builtin', '-fsanitize=address,undefined',
                   '-I'+str(ROOT/'build'), *map(str, sources), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn('checks passed', result.stdout)
        (ROOT/'build/host-test-log.txt').write_text(result.stdout)

    def test_profile_report_and_instrumented_core(self):
        cc = os.environ.get('CC') or shutil.which('clang') or shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        flags = [cc, '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                 '-fno-builtin', '-fsanitize=address,undefined', '-DONE_PROFILE=1',
                 '-I'+str(ROOT/'build')]
        binary = ROOT/'build/profile-unit-test'
        result = subprocess.run([*flags, str(ROOT/'tests/profile.c'),
                                 str(ROOT/'firmware/profile.c'),
                                 str(ROOT/'firmware/memory.c'), '-o', str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        report_path = ROOT/'build/test-profile.txt'
        result = subprocess.run([str(binary), str(report_path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        raw = report_path.read_bytes()
        report = parse_report(raw)
        self.assertEqual(report['plays'], 3)
        self.assertEqual(report['metrics']['sd']['samples'], 2)
        self.assertEqual(report['metrics']['sd']['mean_us'], 1200)
        self.assertEqual(report['metrics']['refill_work']['maximum_us'], 2560)
        self.assertEqual(report['errors']['late_commit'], 1)
        self.assertEqual(report['errors']['copy_overlap'], 1)
        self.assertEqual(report['previous_report_failures'], 1)
        self.assertEqual(report['minimum_slack_frames'], 0)
        self.assertGreater(report['playback_seconds'], 0)
        self.assertLess(report['playback_seconds'], 1)  # Count wrap is not 2^32 ticks.
        corrupt = bytearray(raw); corrupt[200] ^= 1
        with self.assertRaises(ValueError): parse_report(corrupt)
        with self.assertRaises(ValueError): parse_report(raw[:512])
        sources = [ROOT/'tests/core.c', *[ROOT/'firmware'/f'{name}.c' for name in
                   ('fat','wave','sd','ring','memory','app','advertise','settings',
                    'policy','ui','recall','profile')]]
        binary = ROOT/'build/profile-core-test'
        result = subprocess.run([*flags, *map(str,sources), '-o', str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        (ROOT/'build/profile-host-test-log.txt').write_text(result.stdout)

    def test_profile_image_contract_and_rebuild(self):
        result = subprocess.run([sys.executable, str(ROOT/'tools/build.py'), '--profile'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        out = ROOT/'build/profile'
        payload, _, layout, _ = extract(out/'hamburger-wavy.elf')
        image = read_hex(out/'image.hex')
        self.assertEqual(set(image), set(read_hex(ROOT/'build/image.hex')))
        self.assertNotIn('.boot_debug', layout)
        for address, byte in image.items():
            self.assertEqual(byte, payload.get(address,255))
        self.assertLessEqual(layout['.text']['bytes'], 0x7c44)
        self.assertLessEqual(layout['.bss']['bytes'], 0xe000)
        self.assertTrue(json.loads((out/'manifest.json').read_text())['profiling'])
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ('firmware','tools','board'):
                shutil.copytree(ROOT/name,root/name,ignore=shutil.ignore_patterns('__pycache__'))
            result = subprocess.run([sys.executable,str(root/'tools/build.py'),'--profile'],
                                    capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual((root/'build/profile/image.hex').read_bytes(),
                             (out/'image.hex').read_bytes())

    @unittest.skipUnless(Path('/sbin/fsck_msdos').exists(),'optional macOS filesystem checker')
    def test_profile_allocation_passes_independent_filesystem_checker(self):
        import struct
        cc='/usr/local/opt/llvm/bin/clang'
        if not Path(cc).exists():cc=shutil.which('clang')
        binary=ROOT/'build/fs-image-test'
        sources=[ROOT/'tests/fs_image.c',*[ROOT/'firmware'/f'{n}.c' for n in ('fat','recall','memory')]]
        subprocess.run([cc,'-DONE_PROFILE=1','-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-builtin',*[str(p) for p in sources],'-o',str(binary)],check=True,capture_output=True,text=True)
        with tempfile.TemporaryDirectory() as tmp:
            for kind in (16,32):
                total=131072;spc=4 if kind==16 else 1
                reserved=1 if kind==16 else 32;fatsz=128 if kind==16 else 1024
                root_entries=512 if kind==16 else 0
                data=reserved+2*fatsz+root_entries//16
                p=bytearray(512);p[:3]=b'\xeb\x3c\x90';p[3:11]=b'ONETEST '
                def put16(off,v):struct.pack_into('<H',p,off,v)
                def put32(off,v):struct.pack_into('<I',p,off,v)
                put16(11,512);p[13]=spc;put16(14,reserved);p[16]=2
                put16(17,root_entries);p[21]=248;put16(24,63);put16(26,255);put32(32,total)
                if kind==16:
                    put16(22,fatsz);p[36]=128;p[38]=41;put32(39,123456)
                    p[43:54]=b'ONE TEST   ';p[54:62]=b'FAT16   '
                else:
                    put32(36,fatsz);put32(44,2);put16(48,1);put16(50,6)
                    p[64]=128;p[66]=41;put32(67,123456)
                    p[71:82]=b'ONE TEST   ';p[82:90]=b'FAT32   '
                p[510:512]=b'\x55\xaa'
                path=Path(tmp)/f'fat{kind}.img'
                with path.open('w+b') as f:
                    f.truncate(total*512)
                    def sector(lba,buf):f.seek(lba*512);f.write(buf)
                    sector(0,p)
                    fat=bytearray(512)
                    if kind==16:struct.pack_into('<4H',fat,0,0xfff8,0xffff,0,0xffff)
                    else:
                        struct.pack_into('<4I',fat,0,0x0ffffff8,0xffffffff,0x0fffffff,0x0fffffff)
                        sector(6,p)
                        info=bytearray(512)
                        struct.pack_into('<I',info,0,0x41615252)
                        struct.pack_into('<I',info,484,0x61417272)
                        struct.pack_into('<I',info,488,(total-data)//spc-2)
                        struct.pack_into('<I',info,492,4)
                        struct.pack_into('<I',info,508,0xaa550000)
                        sector(1,info);sector(7,info)
                    sector(reserved,fat);sector(reserved+fatsz,fat)
                    directory=bytearray(512);directory[:11]=b'A       WAV';directory[11]=32
                    struct.pack_into('<H',directory,26,3);struct.pack_into('<I',directory,28,512)
                    sector(reserved+2*fatsz if kind==16 else data,directory)
                    sample=bytes((i*13)&255 for i in range(512))
                    sector(data+spc,sample)
                before=subprocess.run(['/sbin/fsck_msdos','-n',str(path)],capture_output=True,text=True)
                self.assertEqual(before.returncode,0,before.stdout+before.stderr)
                subprocess.run([str(binary),str(path)],check=True,capture_output=True,text=True)
                after=subprocess.run(['/sbin/fsck_msdos','-n',str(path)],capture_output=True,text=True)
                self.assertEqual(after.returncode,0,after.stdout+after.stderr)
                with path.open('rb') as f:f.seek((data+spc)*512);self.assertEqual(f.read(512),sample)

    def test_compiled_bulk_spi_and_audio_copy(self):
        image,symbols=self._compiled_artifact()
        class SpiCPU(CPU):
            def __init__(self, stuck_after=None, delay=0):
                super().__init__(image)
                self.pending=False; self.clocks=0; self.received=0
                self.polls=[]; self.stuck_after=stuck_after
                self.delay=delay;self.ready_at=0
                self.hooks[symbols['app_background']]=self.background
            def background(self, cpu, *args):
                self.polls.append((self.received,self.pending))
            def write(self,address,value,n=4):
                if address==0xbf805a20:
                    self.assert_clock(value)
                super().write(address,value,n)
            def assert_clock(self,value):
                if self.pending or value!=255:
                    raise AssertionError('extra outstanding byte or non-dummy transmit')
                self.pending=True; self.clocks+=1
                self.ready_at=self.steps+self.delay
            def read(self,address,n=4):
                if address==0xbf805a10:
                    return int(self.pending and self.steps>=self.ready_at and (self.stuck_after is None or
                                                self.received<self.stuck_after))
                if address==0xbf805a20:
                    if not self.pending: raise AssertionError('receive without clock')
                    self.pending=False
                    result=(self.received*7+3)&255
                    self.received+=1
                    return result
                return super().read(address,n)
        for length in (0,1,63,64,65,512):
            for seed in (0,7):
                m=SpiCPU(); destination=0xa0001000;checksum=0xa0000802
                m.poke(checksum-1,0xa5,1);m.poke(checksum+2,0x5a,1)
                m.poke(symbols['spi_poll'],seed)
                m.poke(destination-1,0xa5,1);m.poke(destination+length,0x5a,1)
                self.assertEqual(m.run(symbols['spi_read_bytes'],0,destination,length,checksum),0)
                self.assertEqual((m.clocks,m.received,m.pending),(length,length,False))
                self.assertEqual([m.read(destination+i,1) for i in range(length)],
                                 [(i*7+3)&255 for i in range(length)])
                self.assertEqual(m.read(destination-1,1),0xa5)
                self.assertEqual(m.read(destination+length,1),0x5a)
                data=bytes((i*7+3)&255 for i in range(length))
                self.assertEqual(m.read(checksum,2),binascii.crc_hqx(data,0))
                self.assertEqual(m.read(checksum-1,1),0xa5)
                self.assertEqual(m.read(checksum+2,1),0x5a)
                expected=[i for i in range(1,length+1) if (seed+i)%64==0]
                self.assertEqual([n for n,_ in m.polls],expected)
                self.assertEqual([pending for n,pending in m.polls],
                                 [n<length for n in expected])
                self.assertEqual(m.read(symbols['spi_poll']),seed+length)
        # Exercise actual receive waiting as well as immediately-ready bytes.
        m=SpiCPU(delay=40)
        self.assertEqual(m.run(symbols['spi_read_bytes'],0,0xa0001000,512,0xa0000802),0)
        self.assertEqual((m.clocks,m.received,m.pending),(512,512,False))
        data=bytes((i*7+3)&255 for i in range(512))
        self.assertEqual(m.read(0xa0000802,2),binascii.crc_hqx(data,0))
        m=SpiCPU(stuck_after=17);m.steps=0xfffffff0
        m.poke(0xa0000802,0x5aa5,2)
        self.assertEqual(m.run(symbols['spi_read_bytes'],0,0xa0001000,512,0xa0000802,
                               budget=200000),0xffffffff)
        self.assertEqual((m.clocks,m.received),(18,17))
        self.assertEqual(m.read(0xa0000802,2),0x5aa5)
        for words in (0,1,3,4,5,1024,2048,4096):
            m=CPU(image);source=0xa0001004;destination=0xa0006004
            for i in range(words):m.poke(source+i*4,(i*0x1020305)&0xffffffff)
            m.poke(destination-1,0xa5,1);m.poke(destination+words*4,0x5a,1)
            m.run(symbols['audio_copy'],destination,source,words)
            self.assertEqual([m.read(destination+i*4) for i in range(words)],
                             [m.read(source+i*4) for i in range(words)])
            self.assertEqual(m.read(destination-1,1),0xa5)
            self.assertEqual(m.read(destination+words*4,1),0x5a)

    def test_compiled_memcpy_alignment_and_bounds(self):
        image,symbols=self._compiled_artifact()
        for a in range(4):
            for b in range(4):
                for length in (0,1,3,4,5,31,32,33,511,512,513,1536):
                    m=CPU(image);src=0xa0001004+b;dst=0xa0006004+a
                    for i in range(length):m.poke(src+i,(i*7+3)&255,1)
                    m.poke(dst-1,0xa5,1);m.poke(dst+length,0x5a,1)
                    self.assertEqual(m.run(symbols['memcpy'],dst,src,length),dst)
                    self.assertEqual([m.read(dst+i,1) for i in range(length)],
                                     [(i*7+3)&255 for i in range(length)])
                    self.assertEqual(m.read(dst-1,1),0xa5)
                    self.assertEqual(m.read(dst+length,1),0x5a)

    def test_compiled_pcm_decode_formats_and_bounds(self):
        image,symbols=self._compiled_artifact()
        for bits in (16,24):
            for frames in (0,1,37,1024,2048):
                m=CPU(image);src=0xa0001001;dst=0xa0006004
                width=bits//8
                data=bytes((i*173+91)&255 for i in range(frames*width))
                for i,b in enumerate(data):m.poke(src+i,b,1)
                m.poke(dst-1,0xa5,1);m.poke(dst+frames*8,0x5a,1)
                self.assertEqual(m.run(symbols['pcm_decode'],src,bits,dst,frames),0)
                for i in range(frames):
                    expected=int.from_bytes(data[i*width:(i+1)*width],'little')<<(32-bits)
                    self.assertEqual(m.read(dst+i*8),expected)
                    self.assertEqual(m.read(dst+i*8+4),expected)
                self.assertEqual(m.read(dst-1,1),0xa5)
                self.assertEqual(m.read(dst+frames*8,1),0x5a)
        if 'pcm_decode_reverse' in symbols:
            for bits in (16,24):
                for frames in (0,1,37,1024,2048):
                    m=CPU(image);src=0xa0001001;dst=0xa0006004;width=bits//8
                    data=bytes((i*173+91)&255 for i in range(frames*width))
                    for i,b in enumerate(data):m.poke(src+i,b,1)
                    m.poke(dst-1,0xa5,1);m.poke(dst+frames*8,0x5a,1)
                    self.assertEqual(m.run(symbols['pcm_decode_reverse'],src,bits,dst,frames),0)
                    for i in range(frames):
                        first=(frames-1-i)*width
                        expected=int.from_bytes(data[first:first+width],'little')<<(32-bits)
                        self.assertEqual(m.read(dst+i*8),expected)
                        self.assertEqual(m.read(dst+i*8+4),expected)
                    self.assertEqual(m.read(dst-1,1),0xa5)
                    self.assertEqual(m.read(dst+frames*8,1),0x5a)
        m=CPU(image)
        self.assertEqual(m.run(symbols['pcm_decode'],0xa0001000,8,0xa0006000,1),0xffffffff)

    def test_compiled_audio_deadline_guard(self):
        image,symbols=self._compiled_artifact()
        buffers=json.loads((getattr(self,'compiled_dir',ROOT/'build')/'manifest.json').read_text())['audio_buffers']
        half_bytes=buffers['half_frames']*8
        self.assertEqual(buffers['dma_bytes'],half_bytes*2)
        self.assertLessEqual(buffers['dma_bytes'],65535)
        for half in (0,1):
            for active in (0,1):
                for left in (8,1016,1024,1032,half_bytes):
                    m=CPU(image)
                    m.poke(0xbf883190,active*half_bytes+half_bytes-left)
                    expected=int(active!=half and left>=1024)
                    self.assertEqual(m.run(symbols['board_audio_commit_safe'],half),expected)

    def test_flash_extension_image_and_execution(self):
        result=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),'--flash-probe'],
                              capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        out=ROOT/'build/flash-probe'
        image=read_hex(out/'image.hex')
        _,contract=template(ROOT/'board/update-layout.json')
        extra=set(range(0x1d014000,0x1d014400))
        self.assertEqual(set(image)-set(contract),extra)
        self.assertFalse(set(contract)-set(image))
        self.assertEqual({a for a in image if a>=0x1fc00000},
                         set(range(0x1fc00bf4,0x1fc00bfc)))
        normal=read_hex(ROOT/'build/image.hex')
        for a in image:
            if a>=0x1fc00000:self.assertEqual(image[a],normal[a])
        with self.assertRaises(ValueError):extract(out/'hamburger-wavy.elf')
        payload,symbols,layout,calls=extract(out/'hamburger-wavy.elf',True)
        self.assertEqual(symbols['standalone_start'],0x9d00b970)
        self.assertEqual(symbols['standalone_reset'],0x9d013a44)
        self.assertEqual(symbols['flash_extension_mix'],0x9d014000)
        self.assertEqual(symbols['flash_extension_tail'],0x9d0143f0)
        self.assertTrue(any(0x9d014000<=int(target,16)<0x9d014400 for _,target in calls))
        for address,byte in image.items():self.assertEqual(byte,payload.get(address,255))
        for corrupt in (None,0,31,127,255,'missing'):
            m=CPU(image)
            for address in extra:m.poke(address|0xa0000000,image[address],1)
            if corrupt=='missing':
                for address in extra:m.poke(address|0xa0000000,255,1)
            elif corrupt is not None:
                address=0xbd014000+corrupt*4
                m.poke(address,m.read(address)^1)
            self.assertEqual(m.run(symbols['flash_probe_check']),int(corrupt is None))
        # A distribution built without private inputs produces the same probe.
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for name in ('firmware','tools','board'):
                shutil.copytree(ROOT/name,root/name,ignore=shutil.ignore_patterns('__pycache__'))
            result=subprocess.run([sys.executable,str(root/'tools/build.py'),'--flash-probe'],
                                  capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual((root/'build/flash-probe/image.hex').read_bytes(),(out/'image.hex').read_bytes())
        invalid=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),'--flash-probe','--profile'],
                               capture_output=True,text=True)
        self.assertNotEqual(invalid.returncode,0)

    def test_mips_rotate_and_shift_encodings(self):
        def run(word,value,amount=0):
            code=word.to_bytes(4,'little')+bytes.fromhex('0800e00300000000')
            m=CPU({0x1d000000+i:b for i,b in enumerate(code)})
            return m.run(0x9d000000,value,amount)
        for value in (0,1,0x80000000,0x12345678,0xffffffff):
            for amount in range(32):
                rotate=((value>>amount)|(value<<(32-amount)))&0xffffffff
                self.assertEqual(run((1<<21)|(4<<16)|(2<<11)|(amount<<6)|2,value),rotate)
                self.assertEqual(run((5<<21)|(4<<16)|(2<<11)|(1<<6)|6,value,amount),rotate)
                self.assertEqual(run((4<<16)|(2<<11)|(amount<<6)|2,value),value>>amount)
                self.assertEqual(run((5<<21)|(4<<16)|(2<<11)|6,value,amount),value>>amount)
        with self.assertRaises(AssertionError):run((2<<21)|(4<<16)|(2<<11)|2,1)
        with self.assertRaises(AssertionError):run((5<<21)|(4<<16)|(2<<11)|(2<<6)|6,1,1)

    def test_flash_bank_image_and_execution(self):
        result=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),'--flash-bank'],
                              capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        out=ROOT/'build/flash-bank';image=read_hex(out/'image.hex')
        spec,contract=template(ROOT/'board/update-layout.json')
        extra=set(range(0x1d014000,0x1d020000))
        self.assertEqual(set(image)-set(contract),extra)
        self.assertFalse(set(contract)-set(image))
        normal=read_hex(ROOT/'build/image.hex')
        self.assertEqual({a for a in image if a>=0x1fc00000},
                         {a for a in normal if a>=0x1fc00000})
        for a in image:
            if a>=0x1fc00000:self.assertEqual(image[a],normal[a])
        lines=(out/'image.hex').read_text().splitlines()
        for expected,actual in zip(spec['records'][:-1],lines):
            self.assertEqual(expected.lower(),actual[:9].lower() if expected[7:9]=='00' else actual.lower())
        with self.assertRaises(ValueError):extract(out/'hamburger-wavy.elf')
        with self.assertRaises(ValueError):extract(out/'hamburger-wavy.elf',True)
        payload,symbols,layout,calls=extract(out/'hamburger-wavy.elf',True,True)
        self.assertEqual(symbols['standalone_start'],0x9d00b970)
        self.assertEqual(symbols['standalone_reset'],0x9d013a44)
        self.assertEqual(symbols['flash_extension_mix'],0x9d014000)
        self.assertEqual(symbols['flash_extension_high'],0x9d01ff00)
        self.assertEqual(symbols['flash_extension_tail'],0x9d01fff0)
        for address,byte in image.items():
            expected=255
            if address in extra:
                word=(((address-0x1d014000)//4)*0x9e3779b9)^0x48570017
                expected=(word>>((address&3)*8))&255
            self.assertEqual(byte,payload.get(address,expected))
        for target in (0x9d014000,0x9d01ff00):
            self.assertTrue(any(int(dst,16)==target for _,dst in calls))
        for corrupt in (None,0,255,256,6144,12224,12287,'last_page_missing'):
            m=CPU(image)
            m.hooks[symbols['board_watchdog']]=lambda cpu,*args:0
            for address in extra:m.poke(address|0xa0000000,image[address],1)
            if corrupt=='last_page_missing':
                for address in range(0xbd01fc00,0xbd020000):m.poke(address,255,1)
            elif corrupt is not None:
                address=0xbd014000+corrupt*4;m.poke(address,m.read(address)^1)
            self.assertEqual(m.run(symbols['flash_probe_check'],budget=2000000),int(corrupt is None))
            if corrupt is None:self.assertEqual(len(m.calls),48)
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for name in ('firmware','tools','board'):
                shutil.copytree(ROOT/name,root/name,ignore=shutil.ignore_patterns('__pycache__'))
            result=subprocess.run([sys.executable,str(root/'tools/build.py'),'--flash-bank'],
                                  capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual((root/'build/flash-bank/image.hex').read_bytes(),(out/'image.hex').read_bytes())
        invalid=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),'--flash-bank','--profile'],
                               capture_output=True,text=True)
        self.assertNotEqual(invalid.returncode,0)

    def test_extended_application_layout_and_execution(self):
        for profiling in (False,True):
            flags=['--extended']+(['--profile'] if profiling else [])
            result=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),*flags],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            out=ROOT/('build/extended/profile' if profiling else 'build/extended')
            image=read_hex(out/'image.hex');_,contract=template(ROOT/'board/update-layout.json')
            extra=set(range(0x1d014000,0x1d020000))
            self.assertEqual(set(image),set(contract)|extra)
            payload,symbols,layout,calls=extract(out/'hamburger-wavy.elf',extended=True)
            with self.assertRaises(ValueError):extract(out/'hamburger-wavy.elf')
            for address,byte in image.items():self.assertEqual(byte,payload.get(address,255))
            for name in ('fat_mount','file_read','wave_open','pcm_decode','pitch_divider','recall_save'):
                self.assertTrue(0x9d014000<=symbols[name]<0x9d020000,name)
            if profiling:self.assertTrue(0x9d014000<=symbols['profile_idle']<0x9d020000)
            for name in ('standalone_main','extension_check','extension_page_crc','board_init','board_watchdog','sd_read'):
                self.assertLess(symbols[name],0x9d013a44,name)
            for name,address in [('standalone_start',0x9d00b970),('standalone_reset',0x9d013a44)]:
                self.assertEqual(symbols[name],address)
            normal=read_hex(ROOT/'build/image.hex')
            self.assertEqual({a for a in image if a>=0x1fc00000},{a for a in normal if a>=0x1fc00000})
            for address in normal:
                if address>=0x1fc00000:self.assertEqual(image[address],normal[address])
            manifest=json.loads((out/'manifest.json').read_text())
            self.assertEqual(manifest['resources']['combined_text_bytes'],layout['.text']['bytes']+layout['.app_extension']['bytes'])
            self.assertEqual(manifest['resources']['combined_text_free'],80964-manifest['resources']['combined_text_bytes'])
            if profiling:
                report={'build':25}
                attach_build_info(report,out/'manifest.json',ROOT/'build/extended/manifest.json')
                self.assertEqual(report['profiling_overhead_bytes']['combined_text'],
                                 report['profiling_overhead_bytes']['main_text']+report['profiling_overhead_bytes']['extension_text'])
            # Run the actual compiled CRC body against an independent implementation.
            for data in (bytes(1024),bytes([255])*1024,bytes((i*173+91)&255 for i in range(1024))):
                m=CPU(image)
                for i,b in enumerate(data):m.poke(0xa0001000+i,b,1)
                self.assertEqual(m.run(symbols['extension_page_crc'],0xa0001000,budget=200000),zlib.crc32(data))
            # Check the actual bank-validation control flow using independently computed page CRCs.
            for corrupt in (None,0,24576,49151):
                m=CPU(image)
                for address in extra:m.poke(address|0xa0000000,image[address],1)
                if corrupt is not None:
                    address=0xbd014000+corrupt;m.poke(address,m.read(address,1)^1,1)
                m.hooks[symbols['board_watchdog']]=lambda cpu,*args:0
                m.hooks[symbols['extension_page_crc']]=lambda cpu,address,*args:zlib.crc32(bytes(cpu.read(address+i,1) for i in range(1024)))
                self.assertEqual(m.run(symbols['extension_check']),int(corrupt is None))
                if corrupt is None:self.assertEqual(len(m.calls),96)
            # Both layouts execute actual relocated decoding/copies/deadline and SD paths.
            self.compiled_dir=out
            try:
                self.test_compiled_bulk_spi_and_audio_copy()
                self.test_compiled_memcpy_alignment_and_bounds()
                self.test_compiled_pcm_decode_formats_and_bounds()
                self.test_compiled_audio_deadline_guard()
            finally:del self.compiled_dir
            with tempfile.TemporaryDirectory() as temp:
                root=Path(temp)
                for name in ('firmware','tools','board'):
                    shutil.copytree(ROOT/name,root/name,ignore=shutil.ignore_patterns('__pycache__'))
                result=subprocess.run([sys.executable,str(root/'tools/build.py'),*flags],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual((root/('build/extended/profile' if profiling else 'build/extended')/'image.hex').read_bytes(),(out/'image.hex').read_bytes())
        invalid=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),'--extended','--flash-bank'],capture_output=True,text=True)
        self.assertNotEqual(invalid.returncode,0)

    def test_flip_core_under_sanitizers(self):
        cc=os.environ.get('CC') or shutil.which('clang') or shutil.which('cc')
        if not cc:self.skipTest('no host C compiler')
        sources=[ROOT/'tests/core.c',*[ROOT/'firmware'/f'{name}.c' for name in
                 ('fat','wave','sd','ring','memory','app','advertise','settings','policy','ui','recall')]]
        for profile in (False,True):
            binary=ROOT/('build/flip-profile-test' if profile else 'build/flip-core-test')
            flags=['-DONE_FLIP=1']+(['-DONE_PROFILE=1',str(ROOT/'firmware/profile.c')] if profile else [])
            result=subprocess.run([cc,'-std=c11','-g','-O1','-Wall','-Wextra','-Werror','-fno-builtin',
                '-fsanitize=address,undefined','-I'+str(ROOT/'build'),*flags,*map(str,sources),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,
                env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_crc_update_exhaustively(self):
        cc=os.environ.get('CC') or shutil.which('clang') or shutil.which('cc')
        if not cc: self.skipTest('no host C compiler')
        binary=ROOT/'build/crc-test'
        result=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                               str(ROOT/'tests/crc.c'),'-o',str(binary)],
                              capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([str(binary)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('16777216',result.stdout)

    def test_distribution_has_notices_and_no_private_inputs(self):
        self.assertIn('Version 3, 29 June 2007', (ROOT/'LICENSE').read_text())
        self.assertIn('Copyright 2011 Ben Buxton', (ROOT/'NOTICE').read_text())
        for name in ('vendor', 'backups', 'releases', '.source-history'):
            self.assertFalse((ROOT/name).exists(), name)
        for path in (ROOT/'firmware').iterdir():
            self.assertIn('SPDX-License-Identifier: GPL-3.0-only', path.read_text())


if __name__ == '__main__':
    unittest.main()
