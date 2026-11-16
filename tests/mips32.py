# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed MIPS32r2 test interpreter, not a PIC32 timing/peripheral emulator."""
MASK=0xffffffff
STOP=0xfeed0000

def signed(v):
    v &= MASK
    return v if v<0x80000000 else v-0x100000000

class CPU:
    def __init__(self, image, hooks=None):
        self.mem={a|0x80000000:v for a,v in image.items()}
        self.code=set(self.mem)
        self.r=[0]*32
        self.r[28],self.r[29]=0xa0007ff0,0xa000fff8
        self.hi=self.lo=0
        self.cp0={}
        self.hooks=hooks or {}
        self.calls=[]
        self.writes=[]
        self.steps=0
        self.read_hook=None

    def read(self,a,n=4):
        if n>1 and a%n: raise AssertionError(f'unaligned read {a:#x}')
        if self.read_hook:
            value=self.read_hook(a,n)
            if value is not None: return value
        return sum(self.mem.get(a+i,0)<<(8*i) for i in range(n))

    def poke(self,a,value,n=4):
        for i in range(n): self.mem[a+i]=(value>>(8*i))&255

    def write(self,a,value,n=4):
        if n>1 and a%n: raise AssertionError(f'unaligned write {a:#x}')
        if not (0xa0000000<=a<a000end or 0xbf800000<=a<0xbf900000):
            raise AssertionError(f'write outside RAM/MMIO {a:#x}')
        self.poke(a,value,n)
        if a<0xa000f000 or a>=0xbf800000: self.writes.append((a,value&((1<<(8*n))-1),n))
        # Only the CLR/SET/INV aliases used in these tests are modeled.
        bases=(0xbf881030,0xbf881040,0xbf881070,0xbf883120,0xbf883140)
        for base in bases:
            if n==4 and base+4<=a<=base+12:
                old=self.read(base)
                self.poke(base,old&~value if a==base+4 else old|value if a==base+8 else old^value)

    def snapshot(self):
        return {a:v for a,v in self.mem.items() if v and (0xa0000000<=a<0xa000f000 or 0xbf800000<=a<0xbf900000)}

    def run(self,entry,*args,budget=100000):
        for i,arg in enumerate(args): self.r[4+i]=arg&MASK
        self.r[31]=STOP
        pc,pending=entry,None
        for _ in range(budget):
            self.steps+=1
            if pc==STOP: return self.r[2]
            if pc in self.hooks:
                arguments=tuple(self.r[4:8])
                self.calls.append((pc,arguments))
                result=self.hooks[pc](self,*arguments)
                self.r[2]=(result or 0)&MASK
                pc=self.r[31]
                continue
            if any(pc+i not in self.code for i in range(4)):
                raise AssertionError(f'execute outside supplied bytes {pc:#x}')
            w=self.read(pc)
            op,rs,rt,rd,sa,fn=w>>26,(w>>21)&31,(w>>16)&31,(w>>11)&31,(w>>6)&31,w&63
            imm=w&65535
            simm=imm if imm<32768 else imm-65536
            branch=None
            skip=False
            if op==0:
                a,b=self.r[rs],self.r[rt]
                if fn==0: self.r[rd]=b<<sa
                elif fn==2:
                    if rs not in (0,1):raise AssertionError('reserved SRL/ROTR encoding')
                    self.r[rd]=(b>>sa)|(b<<(32-sa)) if rs else b>>sa
                elif fn==3: self.r[rd]=signed(b)>>sa
                elif fn==4: self.r[rd]=b<<(a&31)
                elif fn==6:
                    if sa not in (0,1):raise AssertionError('reserved SRLV/ROTRV encoding')
                    shift=a&31
                    self.r[rd]=(b>>shift)|(b<<(32-shift)) if sa else b>>shift
                elif fn==7: self.r[rd]=signed(b)>>(a&31)
                elif fn==8: branch=a
                elif fn==9: self.r[rd]=pc+8;branch=a
                elif fn==10:
                    if b==0:self.r[rd]=a
                elif fn==11:
                    if b!=0:self.r[rd]=a
                elif fn==15:pass # SYNC: sequential interpreter has no pending memory operations
                elif fn==16:self.r[rd]=self.hi
                elif fn==17:self.hi=a
                elif fn==18:self.r[rd]=self.lo
                elif fn==19:self.lo=a
                elif fn in (24,25):
                    product=(signed(a)*signed(b) if fn==24 else a*b)&0xffffffffffffffff
                    self.hi,self.lo=product>>32,product&MASK
                elif fn in (26,27):
                    if not b:raise AssertionError('division by zero')
                    if fn==27:self.lo,self.hi=a//b,a%b
                    else:
                        quotient=abs(signed(a))//abs(signed(b))
                        if (signed(a)<0)!=(signed(b)<0):quotient=-quotient
                        self.lo,self.hi=quotient&MASK,(signed(a)-quotient*signed(b))&MASK
                elif fn in (32,33):self.r[rd]=a+b
                elif fn in (34,35):self.r[rd]=a-b
                elif fn==36:self.r[rd]=a&b
                elif fn==37:self.r[rd]=a|b
                elif fn==38:self.r[rd]=a^b
                elif fn==39:self.r[rd]=~(a|b)
                elif fn==42:self.r[rd]=int(signed(a)<signed(b))
                elif fn==43:self.r[rd]=int(a<b)
                elif fn==52:
                    if a==b:raise AssertionError('TEQ trap')
                else:raise AssertionError(f'unsupported SPECIAL {w:#x} at {pc:#x}')
            elif op in (2,3):
                if op==3:self.r[31]=pc+8
                branch=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
            elif op in (1,4,5,6,7,20,21,22,23):
                if op==1:
                    if rt not in (0,1,2,3):raise AssertionError(f'unsupported REGIMM {w:#x}')
                    taken=(signed(self.r[rs])<0) if rt in (0,2) else (signed(self.r[rs])>=0)
                    likely=rt in (2,3)
                else:
                    normal=op-16 if op>=20 else op
                    taken={4:lambda:self.r[rs]==self.r[rt],5:lambda:self.r[rs]!=self.r[rt],
                           6:lambda:signed(self.r[rs])<=0,7:lambda:signed(self.r[rs])>0}[normal]()
                    likely=op>=20
                if taken:branch=(pc+4+simm*4)&MASK
                elif likely:skip=True
            elif op in (8,9):self.r[rt]=self.r[rs]+simm
            elif op==10:self.r[rt]=int(signed(self.r[rs])<simm)
            elif op==11:self.r[rt]=int(self.r[rs]<(simm&MASK))
            elif op==12:self.r[rt]=self.r[rs]&imm
            elif op==13:self.r[rt]=self.r[rs]|imm
            elif op==14:self.r[rt]=self.r[rs]^imm
            elif op==15:self.r[rt]=imm<<16
            elif op in (32,33,35,36,37):
                width={32:1,33:2,35:4,36:1,37:2}[op]
                value=self.read((self.r[rs]+simm)&MASK,width)
                if op in (32,33) and value&(1<<(width*8-1)):value-=1<<(width*8)
                self.r[rt]=value
            elif op in (40,41,43):self.write((self.r[rs]+simm)&MASK,self.r[rt],{40:1,41:2,43:4}[op])
            elif op==16:
                select=w&7
                if rs==0:self.r[rt]=self.steps if (rd,select)==(9,0) else self.cp0.get((rd,select),0)
                elif rs==4:self.cp0[(rd,select)]=self.r[rt]
                elif w==0x41606000:self.cp0[(12,0)]=self.cp0.get((12,0),0)&~1
                else:raise AssertionError(f'unsupported COP0 {w:#x} at {pc:#x}')
            elif op==28 and fn==2:self.r[rd]=self.r[rs]*self.r[rt]
            elif op==31 and fn==0:self.r[rt]=(self.r[rs]>>sa)&((1<<(rd+1))-1)
            elif op==31 and fn==4:
                bits=((1<<(rd-sa+1))-1)<<sa
                self.r[rt]=(self.r[rt]&~bits)|((self.r[rs]<<sa)&bits)
            else:raise AssertionError(f'unsupported instruction {w:#x} at {pc:#x}')
            self.r=[v&MASK for v in self.r]
            self.r[0]=0
            if pending is not None and skip:raise AssertionError('branch in delay slot')
            pc=pending if pending is not None else pc+(8 if skip else 4)
            pending=branch
        raise AssertionError(f'instruction budget exhausted at {pc:#x}')

a000end=0xa0010000
