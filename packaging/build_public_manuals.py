"""Render the reviewed public Markdown manuals to offline HTML using stdlib only.

No old research-preview DOCX/PDF is read. This renders the documented authoring
directives without running examples, invoking a renderer, or starting a browser.
"""
from pathlib import Path
import argparse,hashlib,html,json,re,shutil

NAMES={'quick-start':'Pentarnion-Studio-Quick-Start','fundamentals':'Pentarnion-Theory-and-Programming-Language-Fundamentals'}
EQUATIONS={
    'element':'p = p₀ + p₁i + p₂j + p₃k + p₄ℓ',
    'switching':'κ = 1/√(1+γ²)     σ = γ/√(1+γ²)     h = 1/√2',
    'conjugation':'p̄ = p₀ − p₁i − p₂j − p₃k − p₄ℓ',
    'norm':'N(p) = p₀² + p₁² + p₂² + p₃² + p₄²',
    'quadratic':'p² − 2p₀p + N(p)1 = 0',
    'bracketing':'(ij)j = (k − ℓ)/2     i(jj) = −i     (γ = 1)',
    'associator':'[p,q,r] = (pq)r − p(qr)     [p,q]c = pq − qp',
    'inverse':'p⁻¹ = p̄/N(p)     p ≠ 0',
    'operators':'Lq c(x) = c(qx)     Rq c(x) = c(xq)',
    'exponential':'exp(a+u) = exp(a) (cos r + (u/r) sin r)',
    'trigonometric':'sin(a+u) = sin(a) cosh(r) + (u/r) cos(a) sinh(r)',
    'cosine':'cos(a+u) = cos(a) cosh(r) − (u/r) sin(a) sinh(r)',
    'hyperbolicsine':'sinh(a+u) = sinh(a) cos(r) + (u/r) cosh(a) sin(r)',
    'hyperboliccosine':'cosh(a+u) = cosh(a) cos(r) + (u/r) sinh(a) sin(r)',
    'sedenion':'(e₁ + e₁₀)(e₄ + e₁₅) = 0',
    'paperode':'p′ = Ap + (1−η) B(pp) + η (Bp)p',
    'pde':'∂u/∂t = D ∂²u/∂x² + F(u,t,x)',
    'fem':'M u′ = M F(u,t) − D K u',
}
STYLE='''body{font:17px/1.6 system-ui,Segoe UI,sans-serif;color:#182b39;background:#f3f6f9;margin:0}main{max-width:1050px;margin:auto;padding:42px;background:white}h1,h2,h3{line-height:1.25;color:#0d596d}h1{font-size:2.1em}h2{margin-top:2.3em;border-bottom:1px solid #d6e4e9;padding-bottom:12px}h3{margin-top:1.6em}a{color:#0865b3}pre{padding:20px;background:#f3f6f9;overflow:auto;border-left:4px solid #15849a;font:14px/1.6 Consolas,monospace}code{font-family:Consolas,monospace;overflow-wrap:anywhere}p code,td code{background:#f0f4f7;padding:2px 4px}.kw{color:#0644c0}.str{color:#a22654}.comment{color:#228032}table{border-collapse:collapse;width:100%;font-size:.93em}td,th{border:1px solid #cddce5;padding:10px;vertical-align:top}th{background:#e8f2f5;text-align:left}.caption{font-weight:600;color:#3e5968;margin-top:25px}.equation{font-size:17px;text-align:center;border:0;background:#f8fafb;white-space:pre-wrap}.authors{color:#465e6d;font-size:.92em}.logo{width:78px;height:78px}.footer{border-top:1px solid #cddce5;margin-top:45px;padding-top:20px;font-size:.9em}.math{font-family:Cambria,Georgia,serif}nav li{margin-bottom:5px}@media(max-width:700px){main{padding:20px}body{font-size:16px}table{display:block;overflow:auto}}@media print{body{background:white}main{max-width:none;padding:0}h2{break-before:page}pre,table,.equation{break-inside:avoid}nav{break-after:page}}'''

def inline(text):
    parts=[]
    for n,segment in enumerate(re.split(r'(`[^`]*`)',text)):
        if segment.startswith('`') and segment.endswith('`'):
            parts.append('<code>'+html.escape(segment[1:-1])+'</code>');continue
        segment=html.escape(segment)
        segment=re.sub(r'\*\*(.+?)\*\*',r'<strong>\1</strong>',segment)
        segment=re.sub(r'\$([^$]+)\$',lambda m:'<span class="math">'+m[1].replace('\\ell','ℓ').replace('\\gamma','γ').replace('\\star','⋆')+'</span>',segment)
        parts.append(segment)
    return ''.join(parts)

def code(text,language):
    if language!='p':return html.escape(text)
    pattern=r'("(?:\\.|[^"\\])*"|%[^\n]*|\b(?:use_algebra|pentarnion|real|complex|vector|matrix|tensor|solution|ode|function|return|if|else|for|while|callback|options)\b)'
    output=[];last=0
    for match in re.finditer(pattern,text):
        output.append(html.escape(text[last:match.start()]));token=match[0]
        kind='str' if token.startswith('"') else 'comment' if token.startswith('%') else 'kw'
        output.append('<span class="'+kind+'">'+html.escape(token)+'</span>');last=match.end()
    output.append(html.escape(text[last:]));return ''.join(output)

def render(text):
    lines=text.splitlines();headings=[line[3:] for line in lines if line.startswith('## ')]
    output=[];i=0;chapter=0;examples=0;equations=0;tables=0;title='Pentarnion Studio'
    while i<len(lines):
        line=lines[i].strip()
        if not line:i+=1;continue
        if line.startswith('# '):
            title=line[2:];output+=['<img class="logo" src="pentarnion-studio-256.png" alt="Pentarnion Studio logo">','<h1>'+inline(title)+'</h1>'];i+=1;continue
        if line=='@authors':
            output.append('<div class="authors"><p><strong>Dr Alireza Poshtkohi</strong><br>University of Hertfordshire<br><a href="mailto:a.poshtkohi@herts.ac.uk">a.poshtkohi@herts.ac.uk</a></p><p><strong>Dr Susanne Pumplün</strong><br>University of Nottingham<br><a href="mailto:Susanne.Pumpluen@nottingham.ac.uk">Susanne.Pumpluen@nottingham.ac.uk</a></p></div>');i+=1;continue
        if line=='@contents':
            output.append('<nav aria-label="Contents"><h2>Contents</h2><ol>'+''.join('<li><a href="#chapter-'+str(n)+'">'+inline(h)+'</a></li>' for n,h in enumerate(headings,1))+'</ol></nav>');i+=1;continue
        if line.startswith('## '):
            chapter+=1;output.append('<h2 id="chapter-'+str(chapter)+'">'+inline(line[3:])+'</h2>');i+=1;continue
        if line.startswith('### '):output.append('<h3>'+inline(line[4:])+'</h3>');i+=1;continue
        if line.startswith('```'):
            language=line[3:].strip();block=[];i+=1
            while i<len(lines) and not lines[i].startswith('```'):block.append(lines[i]);i+=1
            if i==len(lines):raise ValueError('Unclosed code block')
            output.append('<pre><code>'+code('\n'.join(block),language)+'</code></pre>');i+=1;continue
        if line.startswith('@example '):
            examples+=1;output.append('<p class="caption">Example '+str(examples)+'. '+inline(line[9:])+'</p>');i+=1;continue
        if line.startswith('@equation '):
            key=line.split()[1]
            if key not in EQUATIONS:raise ValueError('Unknown public manual equation: '+key)
            equations+=1;output.append('<pre class="equation">'+html.escape(EQUATIONS[key])+'     ('+str(equations)+')</pre>');i+=1;continue
        if line.startswith('@table '):
            caption=line[7:];identifier=''
            if caption.startswith('id='):identifier,caption=caption.split(' ',1);identifier=identifier[3:]
            rows=[];i+=1
            while i<len(lines) and '|' in lines[i]:rows.append([cell.strip() for cell in lines[i].strip().strip('|').split('|')]);i+=1
            if not rows:raise ValueError('Empty manual table')
            tables+=1;output.append('<p class="caption"'+(' id="'+html.escape(identifier,quote=True)+'"' if identifier else '')+'>Table '+str(tables)+'. '+inline(caption)+'</p><table>')
            for row,n in zip(rows,range(len(rows))):
                tag='th' if n==0 else 'td';output.append('<tr>'+''.join('<'+tag+'>'+inline(cell)+'</'+tag+'>' for cell in row)+'</tr>')
            output.append('</table>');continue
        if line.startswith('@'):raise ValueError('Unknown public manual directive: '+line)
        if line.startswith('- '):
            output.append('<ul>')
            while i<len(lines) and lines[i].strip().startswith('- '):output.append('<li>'+inline(lines[i].strip()[2:])+'</li>');i+=1
            output.append('</ul>');continue
        paragraph=[line];i+=1
        while i<len(lines) and lines[i].strip() and not lines[i].startswith(('#','@','```','- ')):
            paragraph.append(lines[i].strip());i+=1
        output.append('<p>'+inline(' '.join(paragraph))+'</p>')
    document='<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+html.escape(title)+'</title><style>'+STYLE+'</style></head><body><main>'+''.join(output)+'<footer class="footer">Pentarnion Studio · Apache License 2.0 · <a href="../licenses/Pentarnion/LICENSE">Licence</a> · <a href="../source/README.txt">Complete source</a><br>Offline HTML edition generated from this release\'s public Markdown. Formula text preserves the supplied mathematical notation; these are not the earlier DOCX/PDF files.</footer></main></body></html>'
    return document,{'chapters':chapter,'examples':examples,'equations':equations,'tables':tables}

def build(source,out):
    source=Path(source);out=Path(out)
    if out.exists():raise ValueError('Public manual output must be new')
    out.mkdir(parents=True);counts=[]
    for key,name in NAMES.items():
        text=(source/(key+'.md')).read_text(encoding='utf-8');document,count=render(text)
        (out/(name+'.html')).write_text(document,encoding='utf-8',newline='\n')
        (out/(name+'.md')).write_text(text,encoding='utf-8',newline='\n');counts.append({'manual':key,**count})
    shutil.copyfile(source/'assets/pentarnion-studio-256.png',out/'pentarnion-studio-256.png')
    (out/'README.txt').write_text('PUBLIC DOCUMENTATION\n\nOpen Pentarnion-Studio-Quick-Start.html or Pentarnion-Theory-and-Programming-Language-Fundamentals.html in your browser. All content and styling are local; no external script, font or service is required. The Markdown copies are editable public authoring sources. No earlier research-preview DOCX/PDF is included. See PUBLIC-RELEASE-GUIDE.txt for licence/source/export guidance.\n',encoding='utf-8')
    files=[{'name':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in sorted(out.iterdir()) if p.is_file()]
    (out/'document-manifest.json').write_text(json.dumps({'format':'Pentarnion public offline HTML/Markdown manuals','files':files,'counts':counts,'old_DOCX_or_PDF_used':False,'examples_executed':False,'browser_started':False},indent=2),encoding='utf-8')
    return counts

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    print(json.dumps(build(a.source,a.out),indent=2))
if __name__=='__main__':main()
