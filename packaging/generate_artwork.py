"""Compose wizard artwork from existing transparent Studio branding."""
from pathlib import Path
import argparse, math, shutil
from PIL import Image,ImageDraw,ImageFont

def main():
    p=argparse.ArgumentParser();p.add_argument('--branding',type=Path,required=True);p.add_argument('--out',type=Path,default=Path(__file__).parent/'assets');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    logo=Image.open(a.branding/'pentarnion-studio-512.png').convert('RGBA')
    shutil.copy2(a.branding/'pentarnion-studio.ico',a.out/'pentarnion-studio.ico')
    shutil.copy2(a.branding/'pentarnion-studio.svg',a.out/'pentarnion-studio.svg')
    logo.save(a.out/'pentarnion-logo-transparent.png')
    def font(size,bold=False):
        return ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf' if bold else 'C:/Windows/Fonts/segoeui.ttf',size)
    width,height=328,628
    art=Image.new('RGBA',(width,height));pixels=art.load()
    for y in range(height):
        for x in range(width):
            t=.65*y/height+.35*x/width;pixels[x,y]=(int(13+6*t),int(39+72*t),int(60+58*t),255)
    draw=ImageDraw.Draw(art)
    cx,cy=180,465
    for angle in (-55,0,55):
        points=[]
        for i in range(181):
            t=i*2*math.pi/180;px=118*math.cos(t);py=40*math.sin(t);r=math.radians(angle)
            points.append((cx+px*math.cos(r)-py*math.sin(r),cy+px*math.sin(r)+py*math.cos(r)))
        draw.line(points,fill=(90,205,220,90),width=1)
    small=logo.copy();small.thumbnail((130,130),Image.Resampling.LANCZOS);art.alpha_composite(small,((width-small.width)//2,54))
    draw.text((31,223),'Pentarnion',font=font(30,True),fill='white');draw.text((31,263),'Studio',font=font(30,True),fill='white')
    draw.text((31,318),'Algebra. Models.',font=font(17),fill=(190,234,242));draw.text((31,344),'Scientific computing.',font=font(17),fill=(190,234,242))
    draw.text((31,565),'pentarnion.com',font=font(17),fill='white')
    art.save(a.out/'wizard-wallpaper.png');art.convert('RGB').save(a.out/'wizard-wallpaper.bmp')
    small=Image.new('RGBA',(110,110),'white');icon=logo.copy();icon.thumbnail((88,88),Image.Resampling.LANCZOS);small.alpha_composite(icon,((110-icon.width)//2,(110-icon.height)//2));small.convert('RGB').save(a.out/'wizard-small.bmp')
    (a.out/'ARTWORK-PROVENANCE.txt').write_text('Wizard artwork is composed locally from the established Pentarnion Studio logo.\nThe source SVG and transparent PNG are retained. Alpha compositing preserves\ntransparency and all placements preserve the original aspect ratio. No GTG or\nuniversity branding is used. Segoe UI is used only when rasterising the artwork;\nno system font is installed or redistributed by this script.\n',encoding='utf-8')

if __name__=='__main__':main()
