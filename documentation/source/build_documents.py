"""Reproducible manual builder. Requires Python with python-docx and lxml."""
from pathlib import Path
import re,json,hashlib,zipfile,shutil
from copy import deepcopy
from docx import Document
from docx.shared import Inches,Pt,RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT,WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.opc.constants import RELATIONSHIP_TYPE as RT

ROOT=Path(__file__).resolve().parent
OUTPUT=ROOT/'output';OUTPUT.mkdir(exist_ok=True)
(ROOT/'qa').mkdir(exist_ok=True)
LOGO=ROOT/'assets/pentarnion-studio-256.png'
M='http://schemas.openxmlformats.org/officeDocument/2006/math'
BLACK=RGBColor(0,0,0)

def node(tag,text=None):
    e=OxmlElement('m:'+tag)
    if text is not None:e.text=text
    return e
def mr(text):
    r=node('r');pr=node('rPr');sty=node('sty');sty.set(qn('m:val'),'p');pr.append(sty);r.append(pr)
    r.append(node('t',text));return r
def mathseq(*items):
    out=[]
    for x in items:out.extend(mathseq(*x) if isinstance(x,list) else [mr(x) if isinstance(x,str) else x])
    return out
def attach(parent,items):
    for item in mathseq(items):parent.append(deepcopy(item))
    return parent
def sub(base,index):
    out=node('sSub')
    out.append(attach(node('e'),[base]));out.append(attach(node('sub'),[index]));return out
def sup(base,index):
    out=node('sSup');out.append(attach(node('e'),[base]));out.append(attach(node('sup'),[index]));return out
def frac(a,b):
    out=node('f');out.append(attach(node('num'),a));out.append(attach(node('den'),b));return out
def bar(a):
    out=node('bar');pr=node('barPr');pos=node('pos');pos.set(qn('m:val'),'top');pr.append(pos);out.append(pr);out.append(attach(node('e'),[a]));return out
def radical(items):
    out=node('rad');pr=node('radPr');hide=node('degHide');hide.set(qn('m:val'),'1');pr.append(hide);out.append(pr);out.append(node('deg'));out.append(attach(node('e'),items));return out
def omath(items):return attach(node('oMath'),items)
def inline_math(s):
    replacements={'\\ell':'ℓ','\\gamma':'γ','\\star':'⋆','\\eta':'η','\\kappa':'κ','\\sigma':'σ','\\partial':'∂'}
    for a,b in replacements.items():s=s.replace(a,b)
    result=[];last=0
    for match in re.finditer(r'([A-Za-z])_([0-9A-Za-z]+)|([A-Za-z])\^([0-9]+)',s):
        if match.start()>last:result.append(mr(s[last:match.start()]))
        result.append(sub(match[1],match[2]) if match[1] else sup(match[3],match[4]));last=match.end()
    if last<len(s):result.append(mr(s[last:]))
    return omath(result)

E={
'element':mathseq('p = ',sub('p','0'),' + ',sub('p','1'),'i + ',sub('p','2'),'j + ',sub('p','3'),'k + ',sub('p','4'),'ℓ'),
'switching':mathseq('κ = ',frac(['1'],[radical(mathseq('1 + ',sup('γ','2')))]),'     σ = ',frac(['γ'],[radical(mathseq('1 + ',sup('γ','2')))]),'     h = ',frac(['1'],[radical(['2'])])),
'conjugation':mathseq(bar('p'),' = ',sub('p','0'),' − ',sub('p','1'),'i − ',sub('p','2'),'j − ',sub('p','3'),'k − ',sub('p','4'),'ℓ'),
'norm':mathseq('N(p) = ',sup(sub('p','0'),'2'),' + ',sup(sub('p','1'),'2'),' + ',sup(sub('p','2'),'2'),' + ',sup(sub('p','3'),'2'),' + ',sup(sub('p','4'),'2')),
'quadratic':mathseq(sup('p','2'),' − 2',sub('p','0'),'p + N(p)1 = 0'),
'bracketing':mathseq('(ij)j = ',frac(['k − ℓ'],['2']),'     i(jj) = −i     (γ = 1)'),
'associator':mathseq('[p,q,r] = (pq)r − p(qr)     ',sub('[p,q]','c'),' = pq − qp'),
'inverse':mathseq(sup('p','−1'),' = ',frac([bar('p')],['N(p)']),'     p ≠ 0'),
'operators':mathseq(sub('L','q'),' c(x) = c(qx)     ',sub('R','q'),' c(x) = c(xq)'),
'exponential':mathseq('exp(a+u) = exp(a)', ' (cos r + ',frac(['u'],['r']),' sin r)'),
'trigonometric':mathseq('sin(a+u) = sin(a) cosh(r) + ',frac(['u'],['r']),' cos(a) sinh(r)'),
'cosine':mathseq('cos(a+u) = cos(a) cosh(r) − ',frac(['u'],['r']),' sin(a) sinh(r)'),
'hyperbolicsine':mathseq('sinh(a+u) = sinh(a) cos(r) + ',frac(['u'],['r']),' cosh(a) sin(r)'),
'hyperboliccosine':mathseq('cosh(a+u) = cosh(a) cos(r) + ',frac(['u'],['r']),' sinh(a) sin(r)'),
'sedenion':mathseq('(',sub('e','1'),' + ',sub('e','10'),')(',sub('e','4'),' + ',sub('e','15'),') = 0'),
'paperode':mathseq('p′ = Ap + (1−η) B(pp) + η (Bp)p'),
'pde':mathseq(frac(['∂u'],['∂t']),' = D ',frac([sup('∂','2'),'u'],['∂',sup('x','2')]),' + F(u,t,x)'),
'fem':mathseq('M',sup('u','′'),' = M F(u,t) − D K u'),
}

def field(p,code,value):
    r=OxmlElement('w:r');b=OxmlElement('w:fldChar');b.set(qn('w:fldCharType'),'begin');r.append(b);p._p.append(r)
    r=OxmlElement('w:r');ins=OxmlElement('w:instrText');ins.set(qn('xml:space'),'preserve');ins.text=' '+code+' ';r.append(ins);p._p.append(r)
    r=OxmlElement('w:r');sep=OxmlElement('w:fldChar');sep.set(qn('w:fldCharType'),'separate');r.append(sep);p._p.append(r)
    p.add_run(str(value))
    r=OxmlElement('w:r');end=OxmlElement('w:fldChar');end.set(qn('w:fldCharType'),'end');r.append(end);p._p.append(r)
def toc_boundary(p,opening):
    r=OxmlElement('w:r');f=OxmlElement('w:fldChar');f.set(qn('w:fldCharType'),'begin' if opening else 'end');r.append(f);p._p.append(r)
    if opening:
        r=OxmlElement('w:r');text=OxmlElement('w:instrText');text.set(qn('xml:space'),'preserve');text.text=' TOC \\o "1-1" \\h \\z \\u ';r.append(text);p._p.append(r)
        r=OxmlElement('w:r');f=OxmlElement('w:fldChar');f.set(qn('w:fldCharType'),'separate');r.append(f);p._p.append(r)
def bookmark(p,name,counter):
    a=OxmlElement('w:bookmarkStart');a.set(qn('w:id'),str(counter));a.set(qn('w:name'),name);p._p.insert(0,a)
    b=OxmlElement('w:bookmarkEnd');b.set(qn('w:id'),str(counter));p._p.append(b)
def hyperlink(p,label,target=None,anchor=None):
    link=OxmlElement('w:hyperlink')
    if anchor:link.set(qn('w:anchor'),anchor)
    else:link.set(qn('r:id'),p.part.relate_to(target,RT.HYPERLINK,is_external=True))
    run=OxmlElement('w:r');pr=OxmlElement('w:rPr');color=OxmlElement('w:color');color.set(qn('w:val'),'176CA3');pr.append(color);run.append(pr)
    t=OxmlElement('w:t');t.text=label;run.append(t);link.append(run);p._p.append(link)
def rich(p,text):
    # Markdown is only a local authoring format. Math remains editable OMML.
    for part in re.split(r'(\$[^$]+\$|`[^`]+`|\*\*.+?\*\*|@ref [A-Za-z0-9_]+)',text):
        if not part:continue
        if part.startswith('$'):p._p.append(inline_math(part[1:-1]))
        elif part.startswith('`'):
            value=part[1:-1]
            if value.startswith('https://'):hyperlink(p,value,value)
            elif '@' in value and ' ' not in value:hyperlink(p,value,'mailto:'+value)
            else:r=p.add_run(value);r.font.name='Consolas';r.font.size=Pt(9.5)
        elif part.startswith('**'):p.add_run(part[2:-2]).bold=True
        elif part.startswith('@ref '):field(p,'REF '+part.split()[1]+' \\h',{'table_pure':2}.get(part.split()[1],1))
        else:p.add_run(part)
    return p

def setup(doc,short):
    sec=doc.sections[0];sec.page_height=Inches(11.69);sec.page_width=Inches(8.27)
    sec.top_margin=Inches(.77);sec.bottom_margin=Inches(.77);sec.left_margin=Inches(.86);sec.right_margin=Inches(.86)
    sec.header_distance=Inches(.32);sec.footer_distance=Inches(.35)
    for name in ['Normal','Title','Subtitle','Heading 1','Heading 2','Heading 3','Caption','TOC 1','TOC 2']:
        if name not in doc.styles:continue
        s=doc.styles[name];s.font.name='Calibri';s.font.color.rgb=BLACK
        if s.element.rPr is not None:
            for node_ in s.element.rPr.findall(qn('w:color')):
                for attr in ['themeColor','themeTint','themeShade']:node_.attrib.pop(qn('w:'+attr),None)
    n=doc.styles['Normal'];n.font.size=Pt(11);n.paragraph_format.space_after=Pt(7);n.paragraph_format.line_spacing=1.08;n.paragraph_format.widow_control=True
    # The bundled default template can contain a decorative Title border.
    # Manuals use black typography only; tables retain their own cell borders.
    for s in doc.styles:
        for border in list(s.element.iter(qn('w:pBdr'))):border.getparent().remove(border)
    for name,size,before,after in [('Title',26,0,12),('Heading 1',19,0,12),('Heading 2',13.5,13,7),('Heading 3',11.5,10,5)]:
        s=doc.styles[name];s.font.size=Pt(size);s.font.bold=True;s.paragraph_format.space_before=Pt(before);s.paragraph_format.space_after=Pt(after)
    doc.styles['Caption'].font.size=Pt(10);doc.styles['Caption'].font.italic=False;doc.styles['Caption'].paragraph_format.space_after=Pt(6)
    header=sec.header.paragraphs[0];header.text='Pentarnion Studio   '+('Quick Start' if short else 'Theory and Programming Language Fundamentals');header.runs[0].font.size=Pt(8);header.runs[0].font.color.rgb=BLACK
    footer=sec.footer.paragraphs[0];footer.paragraph_format.tab_stops.add_tab_stop(Inches(6.4),WD_ALIGN_PARAGRAPH.RIGHT);footer.add_run('Language 0.2   31 October 2026\t');field(footer,'PAGE','1')
    for r in footer.runs:r.font.size=Pt(8)
    settings=doc.settings.element;update=OxmlElement('w:updateFields');update.set(qn('w:val'),'true');settings.append(update)
    math=OxmlElement('m:mathPr');font=OxmlElement('m:mathFont');font.set(qn('m:val'),'Cambria Math');math.append(font);settings.append(math)
    doc.core_properties.author='Dr Alireza Poshtkohi and Dr Susanne Pumplün';doc.core_properties.subject='Pentarnion Studio 0.2';doc.core_properties.keywords='Pentarnion; language 0.2; scientific computing'

def authors(doc):
    p=doc.add_paragraph();p.paragraph_format.space_before=Pt(11);p.add_run('Authors').bold=True
    for name,aff,email in [
      ('Dr Alireza Poshtkohi','School of Physics, Engineering and Computer Science, University of Hertfordshire, Hatfield, Hertfordshire, United Kingdom','a.poshtkohi@herts.ac.uk'),
      ('Dr Susanne Pumplün','School of Mathematical Sciences, University of Nottingham, Nottingham NG7 2RD, United Kingdom','Susanne.Pumpluen@nottingham.ac.uk')]:
        p=doc.add_paragraph();p.paragraph_format.space_after=Pt(3);p.add_run(name).bold=True
        p=doc.add_paragraph(aff);p.paragraph_format.space_after=Pt(3)
        p=doc.add_paragraph();hyperlink(p,email,'mailto:'+email)
    p=doc.add_paragraph('Correspondence   Dr Alireza Poshtkohi');p.paragraph_format.space_after=Pt(3)
    p=doc.add_paragraph();hyperlink(p,'pentarnion.com','https://pentarnion.com');p.add_run('   © 2026 Pentranion Team')

def table(doc,caption,lines,number,key):
    p=doc.add_paragraph(style='Caption');p.paragraph_format.keep_with_next=True;p.add_run('Table ')
    if key:
        begin=OxmlElement('w:bookmarkStart');begin.set(qn('w:id'),str(1000+number));begin.set(qn('w:name'),key);p._p.append(begin)
    field(p,'SEQ Table \\* ARABIC',number)
    if key:
        end=OxmlElement('w:bookmarkEnd');end.set(qn('w:id'),str(1000+number));p._p.append(end)
    p.add_run('   '+caption)
    parsed=[[x.strip() for x in line.split('|')] for line in lines];cols=len(parsed[0])
    t=doc.add_table(rows=1,cols=cols);t.alignment=WD_TABLE_ALIGNMENT.CENTER;t.autofit=False
    widths=([1.05,1.2,1.4,1.4,1.4] if key=='table_pure' else ([1.5,2.0,3.0] if cols==3 else [2.0,4.5]))
    for column,width in zip(t.columns,widths):column.width=Inches(width)
    for row_index,data in enumerate(parsed):
        row=t.rows[0] if row_index==0 else t.add_row()
        for column,text in enumerate(data):
            cell=row.cells[column];cell.width=Inches(widths[column]);cell.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.CENTER
            tcpr=cell._tc.get_or_add_tcPr();margins=OxmlElement('w:tcMar')
            for side in ['top','left','bottom','right']:
                e=OxmlElement('w:'+side);e.set(qn('w:w'),'85');e.set(qn('w:type'),'dxa');margins.append(e)
            tcpr.append(margins);borders=OxmlElement('w:tcBorders')
            for side in ['top','left','bottom','right']:
                b=OxmlElement('w:'+side);b.set(qn('w:val'),'single');b.set(qn('w:sz'),'4');b.set(qn('w:color'),'D9D9D9');borders.append(b)
            tcpr.append(borders);shade=OxmlElement('w:shd');shade.set(qn('w:fill'),'DCE8EF' if row_index==0 else ('F5F7F8' if row_index%2==0 else 'FFFFFF'));tcpr.append(shade)
            p=cell.paragraphs[0];p.paragraph_format.space_after=Pt(2);p.paragraph_format.space_before=Pt(2);p.paragraph_format.line_spacing=1.0
            if key=='table_pure' and not(row_index==0 and column==0):
                text=text.replace('kappa','κ').replace('sigma','σ').replace('ell','ℓ').replace('-','−');p._p.append(inline_math(text));p.alignment=WD_ALIGN_PARAGRAPH.CENTER
            else:rich(p,text)
            for r in p.runs:r.font.size=Pt(9.5);r.font.bold=row_index==0
        trpr=row._tr.get_or_add_trPr();cant=OxmlElement('w:cantSplit');trpr.append(cant)
        if row_index==0:repeat=OxmlElement('w:tblHeader');trpr.append(repeat)
    doc.add_paragraph().paragraph_format.space_after=Pt(1)

def code(doc,lines):
    pattern=re.compile(r'(%.*$|"(?:\\.|[^"\\])*"|\b(?:use_algebra|gamma|integer|real|pentarnion|matrix|vector|tensor|algebra|element|function|native|ode|solution|record|options|assert|display|return|for|if|else|clear|clc|figure|subplot|plot2|plot3|plot|xlabel|ylabel|title|close|threads)\b|\b\d+(?:\.\d+)?(?:e[-+]?\d+)?\b)')
    for index,line in enumerate(lines):
        p=doc.add_paragraph();p.paragraph_format.left_indent=Inches(.12);p.paragraph_format.space_after=Pt(0 if index+1<len(lines) else 8);p.paragraph_format.line_spacing=1.0;p.paragraph_format.keep_with_next=index+1<len(lines)
        last=0
        for m in pattern.finditer(line):
            chunks=[(line[last:m.start()],BLACK),(m[0],RGBColor.from_string('2A7534' if m[0].startswith('%') else 'A12C37' if m[0].startswith('"') else '6D3290' if m[0][0].isdigit() else '003FB5'))]
            for text,color in chunks:
                r=p.add_run(text);r.font.name='Consolas';r.font.size=Pt(9.2);r.font.color.rgb=color
            last=m.end()
        r=p.add_run(line[last:]);r.font.name='Consolas';r.font.size=Pt(9.2)

def build(source,name,short=False,pages=None):
    doc=Document();setup(doc,short);lines=(ROOT/source).read_text(encoding='utf-8').splitlines()
    headings=[x[3:] for x in lines if x.startswith('## ')];page_map=pages or {}
    count={'Table':0,'Example':0,'Equation':0};i=0;chapter=0;body_started=False
    while i<len(lines):
        line=lines[i].strip()
        if not line:i+=1;continue
        if line.startswith('# '):
            if LOGO.exists():p=doc.add_paragraph();p.add_run().add_picture(str(LOGO),width=Inches(.87))
            doc.add_paragraph(line[2:],style='Title');doc.core_properties.title=line[2:];i+=1;continue
        if line=='@authors':authors(doc);i+=1;continue
        if line=='@contents':
            doc.add_page_break();p=doc.add_paragraph('Contents');p.runs[0].font.size=Pt(19);p.runs[0].bold=True;p.paragraph_format.space_after=Pt(12)
            for j,title in enumerate(headings):
                p=doc.add_paragraph();p.paragraph_format.space_after=Pt(8);p.paragraph_format.tab_stops.add_tab_stop(Inches(6.3),WD_ALIGN_PARAGRAPH.RIGHT)
                if j==0:toc_boundary(p,True)
                hyperlink(p,title,anchor='chapter_'+str(j+1));p.add_run('\t');field(p,'PAGEREF chapter_'+str(j+1)+' \\h',page_map.get(title,' '))
                if j==len(headings)-1:toc_boundary(p,False)
            p=doc.add_paragraph('Select a chapter title or page number to navigate. In Word, update fields after editing to refresh page references.');p.runs[0].font.size=Pt(9)
            doc.add_page_break();body_started=True;i+=1;continue
        if line.startswith('## '):
            continuous=(not short and chapter+1 in {2,5,7,11,13,14})
            if chapter and ((not short and not continuous) or (short and line.startswith('## 6 '))):doc.add_page_break()
            chapter+=1;p=doc.add_paragraph(line[3:],style='Heading 1')
            if continuous:p.paragraph_format.space_before=Pt(18)
            bookmark(p,'chapter_'+str(chapter),chapter);i+=1;continue
        if line.startswith('### '):doc.add_paragraph(line[4:],style='Heading 2');i+=1;continue
        if line.startswith('@example '):
            count['Example']+=1;p=doc.add_paragraph(style='Caption');p.paragraph_format.keep_with_next=True;p.add_run('Example ');field(p,'SEQ Example \\* ARABIC',count['Example']);p.add_run('   '+line[9:]);i+=1;continue
        if line.startswith('```'):
            content=[];i+=1
            while i<len(lines) and not lines[i].startswith('```'):content.append(lines[i]);i+=1
            code(doc,content);i+=1;continue
        if line.startswith('@equation '):
            count['Equation']+=1;key=line.split()[1];p=doc.add_paragraph();p.paragraph_format.space_before=Pt(3);p.paragraph_format.space_after=Pt(6);p.paragraph_format.keep_together=True;p.paragraph_format.tab_stops.add_tab_stop(Inches(6.2),WD_ALIGN_PARAGRAPH.RIGHT)
            p._p.append(omath(E[key]));p.add_run('\t(');field(p,'SEQ Equation \\* ARABIC',count['Equation']);p.add_run(')');i+=1;continue
        if line.startswith('@table '):
            caption=line[7:];key=None
            if caption.startswith('id='):key,caption=caption.split(' ',1);key=key[3:]
            rows=[];i+=1
            while i<len(lines) and '|' in lines[i]:rows.append(lines[i]);i+=1
            count['Table']+=1;table(doc,caption,rows,count['Table'],key);continue
        paragraph=[line];i+=1
        while i<len(lines) and lines[i].strip() and not lines[i].startswith(('#','@','```')):paragraph.append(lines[i].strip());i+=1
        rich(doc.add_paragraph(),' '.join(paragraph))
    path=OUTPUT/(name+'.docx');doc.save(path)
    with zipfile.ZipFile(path) as z:
        xml=z.read('word/document.xml').decode();omml=xml.count('<m:oMath>');images=xml.count('<w:drawing>')
    return {'filename':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'equations_and_inline_math':omml,'drawings':images,'caption_counts':count}

if __name__=='__main__':
    page_maps=json.loads((ROOT/'qa/page-maps.json').read_text()) if (ROOT/'qa/page-maps.json').exists() else {}
    results=[build('quick-start.md','Pentarnion-Studio-Quick-Start',True,page_maps.get('quick',{})),build('fundamentals.md','Pentarnion-Theory-and-Programming-Language-Fundamentals',False,page_maps.get('manual',{}))]
    (ROOT/'qa/document-build.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
