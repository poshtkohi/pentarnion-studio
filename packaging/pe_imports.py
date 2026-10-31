import struct
from pathlib import Path
def pe_imports(path):
    data=Path(path).read_bytes()
    if data[:2]!=b'MZ':return []
    pe=struct.unpack_from('<I',data,0x3c)[0]
    if data[pe:pe+4]!=b'PE\0\0':return []
    number=struct.unpack_from('<H',data,pe+6)[0]
    optsize=struct.unpack_from('<H',data,pe+20)[0]
    opt=pe+24
    magic=struct.unpack_from('<H',data,opt)[0]
    directories=opt+(112 if magic==0x20b else 96)
    headers=struct.unpack_from('<I',data,opt+60)[0]
    sections=[]
    for i in range(number):
        vsize,va,rawsize,rawptr=struct.unpack_from('<IIII',data,opt+optsize+i*40+8)
        sections.append((va,max(vsize,rawsize),rawptr))
    def offset(rva):
        if rva<headers:return rva
        for va,size,raw in sections:
            if va<=rva<va+size:return raw+rva-va
        raise ValueError((str(path),hex(rva)))
    def string(rva):
        at=offset(rva);end=data.index(b'\0',at);return data[at:end].decode('ascii')
    names=[]
    rva,size=struct.unpack_from('<II',data,directories+8)
    if rva:
        at=offset(rva)
        while any(data[at:at+20]):
            names.append(string(struct.unpack_from('<I',data,at+12)[0]));at+=20
    return names
